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
#ifdef DEBUGLEAKHELPER
#define _CRTDBG_MAP_ALLOC
static _CrtMemState g_msStart;
static _CrtMemState g_msAfterInit;
#endif
#include <locale.h>
#include <io.h>
#include <share.h>
#include <Mmsystem.h>
#include <atlimage.h>
#include <comdef.h>
#include <netfw.h>
#include "emule.h"
#include "opcodes.h"
#include "mdump.h"
#include "Scheduler.h"
#include "SearchList.h"
#include "kademlia/kademlia/Error.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/utils/KadUDPKey.h"
#include "kademlia/kademlia/Prefs.h"
#include "kademlia/utils/UInt128.h"
#include "eMuleAI/SafeKad.h"
#include "eMuleAI/FastKad.h"
#include "PerfLog.h"
#include <sockimpl.h> //for *m_pfnSockTerm()
#include <set>
#include "LastCommonRouteFinder.h"
#include "UploadBandwidthThrottler.h"
#include "ClientList.h"
#include "FriendList.h"
#include "ClientUDPSocket.h"
#include "UpDownClient.h"
#include "DownloadQueue.h"
#include "IPFilter.h"
#include "Statistics.h"
#include "WebServer.h"
#include "UploadQueue.h"
#include "SharedFileList.h"
#include "AICHSyncThread.h"
#include "SharedFilesCtrl.h"
#include "MuleListCtrl.h"
#include "ServerList.h"
#include "ServerConnect.h"
#include "ListenSocket.h"
#include "ClientCredits.h"
#include "KnownFileList.h"
#include "Server.h"
#include "ED2KLink.h"
#include "PartFile.h"
#include "Preferences.h"
#include "Preview.h"
#include "secrunasuser.h"
#include "SafeFile.h"
#include "emuleDlg.h"
#include "SearchDlg.h"
#include "SearchResultsWnd.h"
#include "TransferDlg.h"
#include "enbitmap.h"
#include "StringConversion.h"
#include "Log.h"
#include "Collection.h"
#include "HelpIDs.h"
#include "MenuCmds.h"
#include "UPnPImplWrapper.h"
#include "UploadDiskIOThread.h"
#include "PartFileWriteThread.h"
#include "eMuleAI/Shield.h"
#include "eMuleAI/AntiNick.h"
#include "eMuleAI/NetBind.h"
#include <iphlpapi.h>
#include <filesystem>
#include <regex>
#include "eMuleAI/DarkMode.h"
#include "UserMsgs.h"
#include <vector>
#include "eMuleAI/ThreadpoolWrapper.h"
// Kademlia cleanup helpers
#include "kademlia/kademlia/Entry.h"

void FreeEncryptedDatagramSocketRandomPool();
void FreeEncryptedStreamSocketRandomPool();
#include "kademlia/routing/RoutingBin.h"
#include "kademlia/routing/RoutingZone.h"
#include "eMuleAI/ThreadpoolWrapper.h"
#include "SHAHashSet.h"
#include "OtherFunctions.h"
#include "MediaInfo.h"
#include "aichsyncthread.h"
#include "MediaInfo/MediaInfo_Config.h"
#include "cryptopp/nbtheory.h"
#include "cryptopp/pkcspad.h"
#include "eMuleAI\LanguageSelectDlg.h"
#include "eMuleAI\MigrationWizardDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

const UINT CemuleApp::m_nVersionMjr = VERSION_MJR;
const UINT CemuleApp::m_nVersionMin = VERSION_MIN;
const UINT CemuleApp::m_nVersionUpd = VERSION_UPDATE;
const UINT CemuleApp::m_nVersionBld = VERSION_BUILD;
const TCHAR *CemuleApp::m_sPlatform = VERSION_PLATFORM;

namespace
{
	static const LPCTSTR EMULE_AI_FIREWALL_APPLY_ARG = _T("emuleai-firewall-apply-listen-ports");
	static const LPCTSTR EMULE_AI_FIREWALL_REMOVE_ARG = _T("emuleai-firewall-remove-listen-ports");
	static const LPCTSTR EMULE_AI_FIREWALL_TCP_ARG = _T("tcpport");
	static const LPCTSTR EMULE_AI_FIREWALL_UDP_ARG = _T("udpport");
	static const LPCTSTR EMULE_AI_FIREWALL_RULE_TCP_BASE = _T("eMule AI Listen TCP");
	static const LPCTSTR EMULE_AI_FIREWALL_RULE_UDP_BASE = _T("eMule AI Listen UDP");

	DWORD HashCaseInsensitiveString(const CString& rstrValue)
	{
		DWORD dwHash = 2166136261u;
		for (int i = 0; i < rstrValue.GetLength(); ++i) {
			const DWORD dwChar = static_cast<DWORD>(_totlower(static_cast<unsigned short>(rstrValue[i])));
			dwHash ^= (dwChar & 0xFFu);
			dwHash *= 16777619u;
			dwHash ^= ((dwChar >> 8) & 0xFFu);
			dwHash *= 16777619u;
		}
		return dwHash;
	}

	CString GetWindowsFirewallRuleName(LPCTSTR pszBaseName, const CString& rstrProgramPath)
	{
		CString strRuleName;
		strRuleName.Format(_T("%s %08lX"), pszBaseName, static_cast<unsigned long>(HashCaseInsensitiveString(rstrProgramPath)));
		return strRuleName;
	}

	CString GetSingleInstanceMutexName(UINT uTcpPort, bool bUseProfileScope)
	{
		CString strMutexName;
		if (bUseProfileScope) {
			const CString strProfileScope(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
			strMutexName.Format(_T("%s:profile:%08lX"), EMULE_GUID, static_cast<unsigned long>(HashCaseInsensitiveString(strProfileScope)));
		}
		else
			strMutexName.Format(_T("%s:%u"), EMULE_GUID, uTcpPort);
		return strMutexName;
	}

	bool IsCommandLineSwitch(LPCTSTR pszParam, LPCTSTR pszName)
	{
		if (pszParam == NULL)
			return false;
		if (*pszParam == _T('-') || *pszParam == _T('/'))
			++pszParam;
		return _tcsicmp(pszParam, pszName) == 0;
	}

	bool TryParsePortValue(LPCTSTR pszValue, uint16& rnPort)
	{
		if (pszValue == NULL || *pszValue == _T('\0'))
			return false;

		LPTSTR pszEnd = NULL;
		const unsigned long ulPort = _tcstoul(pszValue, &pszEnd, 10);
		if (pszEnd == pszValue || *pszEnd != _T('\0') || ulPort > _UI16_MAX)
			return false;
		rnPort = static_cast<uint16>(ulPort);
		return true;
	}

	CString GetCurrentExecutablePathForFirewall()
	{
		DWORD dwBufferChars = MAX_PATH;
		for (int iAttempt = 0; iAttempt < 4; ++iAttempt) {
			CString strPath;
			LPTSTR pszPath = strPath.GetBuffer(static_cast<int>(dwBufferChars));
			const DWORD dwLength = ::GetModuleFileName(NULL, pszPath, dwBufferChars);
			if (dwLength != 0 && dwLength < dwBufferChars) {
				strPath.ReleaseBuffer(static_cast<int>(dwLength));
				return strPath;
			}
			strPath.ReleaseBuffer(0);
			dwBufferChars *= 2;
		}
		return CString();
	}

	CString FormatFirewallHResult(HRESULT hr)
	{
		_com_error error(hr);
		CString strError(error.ErrorMessage());
		if (strError.IsEmpty())
			strError.Format(_T("0x%08lX"), static_cast<unsigned long>(hr));
		return strError;
	}

	bool IsFirewallAccessDenied(HRESULT hr)
	{
		return hr == E_ACCESSDENIED || hr == HRESULT_FROM_WIN32(ERROR_ACCESS_DENIED);
	}

	bool IsProcessElevated()
	{
		HANDLE hToken = NULL;
		if (::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken) == FALSE)
			return false;

		TOKEN_ELEVATION elevation = {};
		DWORD dwLength = 0;
		const BOOL bResult = ::GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwLength);
		VERIFY(::CloseHandle(hToken));
		return bResult != FALSE && elevation.TokenIsElevated != 0;
	}

	HRESULT GetFirewallRules(CComPtr<INetFwRules>& rpRules)
	{
		rpRules.Release();

		CComPtr<INetFwPolicy2> pPolicy;
		HRESULT hr = ::CoCreateInstance(__uuidof(NetFwPolicy2), NULL, CLSCTX_INPROC_SERVER, __uuidof(INetFwPolicy2), reinterpret_cast<void**>(&pPolicy));
		if (FAILED(hr))
			return hr;
		return pPolicy->get_Rules(&rpRules);
	}

	bool IsFirewallRuleNotFound(HRESULT hr)
	{
		return hr == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || hr == HRESULT_FROM_WIN32(ERROR_NOT_FOUND) || hr == DISP_E_BADINDEX;
	}

	HRESULT RemoveWindowsFirewallRule(INetFwRules* pRules, LPCTSTR pszRuleName)
	{
		if (pRules == NULL)
			return E_POINTER;

		static const int MAX_FIREWALL_RULE_REMOVE_ATTEMPTS = 64;
		CComBSTR bstrName(pszRuleName);
		for (int iAttempt = 0; iAttempt < MAX_FIREWALL_RULE_REMOVE_ATTEMPTS; ++iAttempt) {
			CComPtr<INetFwRule> pExistingRule;
			HRESULT hr = pRules->Item(bstrName, &pExistingRule);
			if (FAILED(hr))
				return IsFirewallRuleNotFound(hr) ? S_OK : hr;

			hr = pRules->Remove(bstrName);
			if (FAILED(hr))
				return IsFirewallRuleNotFound(hr) ? S_OK : hr;
		}
		return S_OK;
	}

	bool IsSameFirewallString(const CString& strLeft, BSTR bstrRight)
	{
		CString strRight;
		if (bstrRight != NULL)
			strRight = bstrRight;
		return strLeft.CompareNoCase(strRight) == 0;
	}

	bool IsMatchingWindowsFirewallPortRule(INetFwRule* pRule, LPCTSTR pszRuleName, long lProtocol, uint16 nPort, const CString& rstrProgramPath)
	{
		if (pRule == NULL)
			return false;

		CComBSTR bstrName;
		if (FAILED(pRule->get_Name(&bstrName)) || !IsSameFirewallString(CString(pszRuleName), bstrName))
			return false;

		VARIANT_BOOL bEnabled = VARIANT_FALSE;
		if (FAILED(pRule->get_Enabled(&bEnabled)) || bEnabled != VARIANT_TRUE)
			return false;

		NET_FW_RULE_DIRECTION eDirection = NET_FW_RULE_DIR_MAX;
		if (FAILED(pRule->get_Direction(&eDirection)) || eDirection != NET_FW_RULE_DIR_IN)
			return false;

		NET_FW_ACTION eAction = NET_FW_ACTION_BLOCK;
		if (FAILED(pRule->get_Action(&eAction)) || eAction != NET_FW_ACTION_ALLOW)
			return false;

		long lExistingProtocol = 0;
		if (FAILED(pRule->get_Protocol(&lExistingProtocol)) || lExistingProtocol != lProtocol)
			return false;

		long lProfiles = 0;
		if (FAILED(pRule->get_Profiles(&lProfiles)))
			return false;
		const long lRequiredProfiles = NET_FW_PROFILE2_DOMAIN | NET_FW_PROFILE2_PRIVATE | NET_FW_PROFILE2_PUBLIC;
		if ((lProfiles & lRequiredProfiles) != lRequiredProfiles)
			return false;

		CString strPort;
		strPort.Format(_T("%u"), nPort);
		CComBSTR bstrLocalPorts;
		if (FAILED(pRule->get_LocalPorts(&bstrLocalPorts)) || !IsSameFirewallString(strPort, bstrLocalPorts))
			return false;

		CComBSTR bstrApplicationName;
		if (FAILED(pRule->get_ApplicationName(&bstrApplicationName)) || !IsSameFirewallString(rstrProgramPath, bstrApplicationName))
			return false;

		return true;
	}

	bool IsNamedWindowsFirewallRule(INetFwRule* pRule, LPCTSTR pszRuleName)
	{
		if (pRule == NULL)
			return false;

		CComBSTR bstrName;
		if (FAILED(pRule->get_Name(&bstrName)) || !IsSameFirewallString(CString(pszRuleName), bstrName))
			return false;

		return true;
	}

	HRESULT CountNamedWindowsFirewallRules(INetFwRules* pRules, LPCTSTR pszRuleName, UINT& ruNamedRuleCount)
	{
		ruNamedRuleCount = 0;
		if (pRules == NULL)
			return E_POINTER;

		CComPtr<IUnknown> pEnumUnknown;
		HRESULT hr = pRules->get__NewEnum(&pEnumUnknown);
		if (FAILED(hr))
			return hr;

		CComPtr<IEnumVARIANT> pEnum;
		hr = pEnumUnknown->QueryInterface(__uuidof(IEnumVARIANT), reinterpret_cast<void**>(&pEnum));
		if (FAILED(hr))
			return hr;

		for (;;) {
			CComVariant varRule;
			ULONG uFetched = 0;
			hr = pEnum->Next(1, &varRule, &uFetched);
			if (hr == S_FALSE)
				break;
			if (FAILED(hr))
				return hr;
			if (uFetched == 0)
				break;

			CComPtr<INetFwRule> pRule;
			if (varRule.vt == VT_DISPATCH && varRule.pdispVal != NULL)
				varRule.pdispVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void**>(&pRule));
			else if (varRule.vt == VT_UNKNOWN && varRule.punkVal != NULL)
				varRule.punkVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void**>(&pRule));

			if (IsNamedWindowsFirewallRule(pRule, pszRuleName))
				++ruNamedRuleCount;
		}
		return S_OK;
	}

	HRESULT IsWindowsFirewallPortRuleSetCurrent(INetFwRules* pRules, LPCTSTR pszRuleName, long lProtocol, uint16 nPort, const CString& rstrProgramPath, bool& rbCurrent)
	{
		rbCurrent = false;
		if (pRules == NULL)
			return E_POINTER;

		CComPtr<IUnknown> pEnumUnknown;
		HRESULT hr = pRules->get__NewEnum(&pEnumUnknown);
		if (FAILED(hr))
			return hr;

		CComPtr<IEnumVARIANT> pEnum;
		hr = pEnumUnknown->QueryInterface(__uuidof(IEnumVARIANT), reinterpret_cast<void**>(&pEnum));
		if (FAILED(hr))
			return hr;

		UINT uNamedRuleCount = 0;
		UINT uMatchingRuleCount = 0;
		for (;;) {
			CComVariant varRule;
			ULONG uFetched = 0;
			hr = pEnum->Next(1, &varRule, &uFetched);
			if (hr == S_FALSE)
				break;
			if (FAILED(hr))
				return hr;
			if (uFetched == 0)
				break;

			CComPtr<INetFwRule> pRule;
			if (varRule.vt == VT_DISPATCH && varRule.pdispVal != NULL)
				varRule.pdispVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void**>(&pRule));
			else if (varRule.vt == VT_UNKNOWN && varRule.punkVal != NULL)
				varRule.punkVal->QueryInterface(__uuidof(INetFwRule), reinterpret_cast<void**>(&pRule));

			if (!IsNamedWindowsFirewallRule(pRule, pszRuleName))
				continue;
			++uNamedRuleCount;
			if (nPort != 0 && IsMatchingWindowsFirewallPortRule(pRule, pszRuleName, lProtocol, nPort, rstrProgramPath))
				++uMatchingRuleCount;
		}
		rbCurrent = nPort == 0 ? uNamedRuleCount == 0 : uNamedRuleCount == 1 && uMatchingRuleCount == 1;
		return S_OK;
	}

	HRESULT AreWindowsFirewallListenPortRulesCurrent(uint16 nTcpPort, uint16 nUdpPort, bool& rbCurrent)
	{
		rbCurrent = false;
		if (nTcpPort == 0)
			return E_INVALIDARG;

		CComPtr<INetFwRules> pRules;
		HRESULT hr = GetFirewallRules(pRules);
		if (FAILED(hr))
			return hr;

		const CString strProgramPath(GetCurrentExecutablePathForFirewall());
		if (strProgramPath.IsEmpty())
			return HRESULT_FROM_WIN32(::GetLastError() != ERROR_SUCCESS ? ::GetLastError() : ERROR_PATH_NOT_FOUND);

		const CString strTcpRuleName(GetWindowsFirewallRuleName(EMULE_AI_FIREWALL_RULE_TCP_BASE, strProgramPath));
		const CString strUdpRuleName(GetWindowsFirewallRuleName(EMULE_AI_FIREWALL_RULE_UDP_BASE, strProgramPath));

		bool bTcpRuleCurrent = false;
		hr = IsWindowsFirewallPortRuleSetCurrent(pRules, strTcpRuleName, NET_FW_IP_PROTOCOL_TCP, nTcpPort, strProgramPath, bTcpRuleCurrent);
		if (FAILED(hr) || !bTcpRuleCurrent)
			return hr;

		bool bUdpRuleCurrent = false;
		hr = IsWindowsFirewallPortRuleSetCurrent(pRules, strUdpRuleName, NET_FW_IP_PROTOCOL_UDP, nUdpPort, strProgramPath, bUdpRuleCurrent);
		if (FAILED(hr))
			return hr;

		rbCurrent = bUdpRuleCurrent;
		return S_OK;
	}

	HRESULT SetWindowsFirewallPortRuleProperties(INetFwRule* pRule, LPCTSTR pszRuleName, long lProtocol, uint16 nPort, const CString& rstrProgramPath)
	{
		if (pRule == NULL)
			return E_POINTER;

		CString strLocalPort;
		strLocalPort.Format(_T("%u"), nPort);

		HRESULT hr = pRule->put_Name(CComBSTR(pszRuleName));
		if (FAILED(hr)) return hr;
		hr = pRule->put_Description(CComBSTR(_T("Allows eMule AI listen port traffic.")));
		if (FAILED(hr)) return hr;
		hr = pRule->put_ApplicationName(CComBSTR(rstrProgramPath));
		if (FAILED(hr)) return hr;
		hr = pRule->put_Protocol(lProtocol);
		if (FAILED(hr)) return hr;
		hr = pRule->put_LocalPorts(CComBSTR(strLocalPort));
		if (FAILED(hr)) return hr;
		hr = pRule->put_Direction(NET_FW_RULE_DIR_IN);
		if (FAILED(hr)) return hr;
		hr = pRule->put_Profiles(NET_FW_PROFILE2_ALL);
		if (FAILED(hr)) return hr;
		hr = pRule->put_Action(NET_FW_ACTION_ALLOW);
		if (FAILED(hr)) return hr;
		hr = pRule->put_Enabled(VARIANT_TRUE);
		if (FAILED(hr)) return hr;

		return S_OK;
	}

	HRESULT AddWindowsFirewallPortRule(INetFwRules* pRules, LPCTSTR pszRuleName, long lProtocol, uint16 nPort, const CString& rstrProgramPath)
	{
		if (pRules == NULL)
			return E_POINTER;

		CComPtr<INetFwRule> pRule;
		HRESULT hr = ::CoCreateInstance(__uuidof(NetFwRule), NULL, CLSCTX_INPROC_SERVER, __uuidof(INetFwRule), reinterpret_cast<void**>(&pRule));
		if (FAILED(hr))
			return hr;
		hr = SetWindowsFirewallPortRuleProperties(pRule, pszRuleName, lProtocol, nPort, rstrProgramPath);
		if (FAILED(hr))
			return hr;
		return pRules->Add(pRule);
	}

	HRESULT AddOrUpdateWindowsFirewallPortRule(INetFwRules* pRules, LPCTSTR pszRuleName, long lProtocol, uint16 nPort, const CString& rstrProgramPath)
	{
		UINT uNamedRuleCount = 0;
		HRESULT hr = CountNamedWindowsFirewallRules(pRules, pszRuleName, uNamedRuleCount);
		if (FAILED(hr))
			return hr;
		if (uNamedRuleCount > 1) {
			hr = RemoveWindowsFirewallRule(pRules, pszRuleName);
			if (FAILED(hr))
				return hr;
			return AddWindowsFirewallPortRule(pRules, pszRuleName, lProtocol, nPort, rstrProgramPath);
		}

		CComPtr<INetFwRule> pRule;
		hr = pRules->Item(CComBSTR(pszRuleName), &pRule);
		if (SUCCEEDED(hr))
			return pRule != NULL ? SetWindowsFirewallPortRuleProperties(pRule, pszRuleName, lProtocol, nPort, rstrProgramPath) : E_POINTER;
		if (!IsFirewallRuleNotFound(hr))
			return hr;
		return AddWindowsFirewallPortRule(pRules, pszRuleName, lProtocol, nPort, rstrProgramPath);
	}

	HRESULT ConfigureWindowsFirewallListenPortRules(bool bEnable, uint16 nTcpPort, uint16 nUdpPort)
	{
		CComPtr<INetFwRules> pRules;
		HRESULT hr = GetFirewallRules(pRules);
		if (FAILED(hr))
			return hr;

		const CString strProgramPath(GetCurrentExecutablePathForFirewall());
		if (strProgramPath.IsEmpty())
			return HRESULT_FROM_WIN32(::GetLastError() != ERROR_SUCCESS ? ::GetLastError() : ERROR_PATH_NOT_FOUND);

		const CString strTcpRuleName(GetWindowsFirewallRuleName(EMULE_AI_FIREWALL_RULE_TCP_BASE, strProgramPath));
		const CString strUdpRuleName(GetWindowsFirewallRuleName(EMULE_AI_FIREWALL_RULE_UDP_BASE, strProgramPath));
		if (!bEnable) {
			hr = RemoveWindowsFirewallRule(pRules, strTcpRuleName);
			if (FAILED(hr))
				return hr;
			return RemoveWindowsFirewallRule(pRules, strUdpRuleName);
		}

		if (nTcpPort == 0)
			return E_INVALIDARG;

		hr = AddOrUpdateWindowsFirewallPortRule(pRules, strTcpRuleName, NET_FW_IP_PROTOCOL_TCP, nTcpPort, strProgramPath);
		if (FAILED(hr))
			return hr;

		if (nUdpPort == 0)
			return RemoveWindowsFirewallRule(pRules, strUdpRuleName);
		hr = AddOrUpdateWindowsFirewallPortRule(pRules, strUdpRuleName, NET_FW_IP_PROTOCOL_UDP, nUdpPort, strProgramPath);
		if (FAILED(hr))
			return hr;
		return S_OK;
	}

	bool RunElevatedFirewallMaintenance(bool bEnable, uint16 nTcpPort, uint16 nUdpPort, DWORD& rdwExitCode, bool& rbCancelled)
	{
		rdwExitCode = ERROR_SUCCESS;
		rbCancelled = false;

		const CString strExePath(GetCurrentExecutablePathForFirewall());
		if (strExePath.IsEmpty()) {
			rdwExitCode = ::GetLastError() != ERROR_SUCCESS ? ::GetLastError() : ERROR_PATH_NOT_FOUND;
			return false;
		}

		CString strParameters;
		if (bEnable)
			strParameters.Format(_T("/%s /%s %u /%s %u"), EMULE_AI_FIREWALL_APPLY_ARG, EMULE_AI_FIREWALL_TCP_ARG, nTcpPort, EMULE_AI_FIREWALL_UDP_ARG, nUdpPort);
		else
			strParameters.Format(_T("/%s"), EMULE_AI_FIREWALL_REMOVE_ARG);

		CString strDirectory(strExePath);
		const int iSlash = strDirectory.ReverseFind(_T('\\'));
		if (iSlash >= 0)
			strDirectory = strDirectory.Left(iSlash);

		SHELLEXECUTEINFO sei = {};
		sei.cbSize = sizeof(sei);
		sei.fMask = SEE_MASK_NOCLOSEPROCESS;
		sei.lpVerb = _T("runas");
		sei.lpFile = strExePath;
		sei.lpParameters = strParameters;
		sei.lpDirectory = strDirectory;
		sei.nShow = SW_HIDE;
		if (::ShellExecuteEx(&sei) == FALSE) {
			rdwExitCode = ::GetLastError();
			rbCancelled = rdwExitCode == ERROR_CANCELLED;
			return false;
		}

		if (sei.hProcess == NULL) {
			rdwExitCode = ERROR_INVALID_HANDLE;
			return false;
		}

		const DWORD dwWait = ::WaitForSingleObject(sei.hProcess, 60 * 1000);
		if (dwWait == WAIT_OBJECT_0) {
			DWORD dwProcessExitCode = ERROR_GEN_FAILURE;
			if (::GetExitCodeProcess(sei.hProcess, &dwProcessExitCode) != FALSE)
				rdwExitCode = dwProcessExitCode;
			else
				rdwExitCode = ::GetLastError();
		} else if (dwWait == WAIT_TIMEOUT) {
			rdwExitCode = WAIT_TIMEOUT;
		} else
			rdwExitCode = ::GetLastError();
		VERIFY(::CloseHandle(sei.hProcess));
		return rdwExitCode == ERROR_SUCCESS;
	}

	bool RunWindowsFirewallListenPortRules(bool bEnable, bool bEnsureOnly, bool bAllowElevation, uint16 nTcpPort, uint16 nUdpPort)
	{
		if (bEnable && bEnsureOnly) {
			bool bCurrent = false;
			HRESULT hrCurrent = AreWindowsFirewallListenPortRulesCurrent(nTcpPort, nUdpPort, bCurrent);
			if (SUCCEEDED(hrCurrent) && bCurrent)
				return true;
			if (SUCCEEDED(hrCurrent) && bAllowElevation && !IsProcessElevated()) {
				DWORD dwExitCode = ERROR_SUCCESS;
				bool bCancelled = false;
				if (RunElevatedFirewallMaintenance(true, nTcpPort, nUdpPort, dwExitCode, bCancelled)) {
					theApp.QueueLogLine(true, GetResString(_T("WINDOWS_FIREWALL_LISTEN_PORTS_OPENED")));
					return true;
				}
				if (bCancelled) {
					theApp.QueueLogLine(true, GetResString(_T("WINDOWS_FIREWALL_LISTEN_PORTS_CANCELLED")));
					return false;
				}
				theApp.QueueLogLine(true, GetResString(_T("WINDOWS_FIREWALL_LISTEN_PORTS_OPEN_FAILED")), (LPCTSTR)FormatFirewallHResult(static_cast<HRESULT>(dwExitCode)));
				return false;
			}
		}

		HRESULT hr = ConfigureWindowsFirewallListenPortRules(bEnable, nTcpPort, nUdpPort);
		if (SUCCEEDED(hr)) {
			theApp.QueueLogLine(true, GetResString(bEnable ? _T("WINDOWS_FIREWALL_LISTEN_PORTS_OPENED") : _T("WINDOWS_FIREWALL_LISTEN_PORTS_REMOVED")));
			return true;
		}

		DWORD dwExitCode = ERROR_SUCCESS;
		bool bCancelled = false;
		if (bAllowElevation && IsFirewallAccessDenied(hr)) {
			if (RunElevatedFirewallMaintenance(bEnable, nTcpPort, nUdpPort, dwExitCode, bCancelled)) {
				theApp.QueueLogLine(true, GetResString(bEnable ? _T("WINDOWS_FIREWALL_LISTEN_PORTS_OPENED") : _T("WINDOWS_FIREWALL_LISTEN_PORTS_REMOVED")));
				return true;
			}
			if (bCancelled) {
				theApp.QueueLogLine(true, GetResString(_T("WINDOWS_FIREWALL_LISTEN_PORTS_CANCELLED")));
				return false;
			}
			hr = static_cast<HRESULT>(dwExitCode);
		}

		theApp.QueueLogLine(true, GetResString(bEnable ? _T("WINDOWS_FIREWALL_LISTEN_PORTS_OPEN_FAILED") : _T("WINDOWS_FIREWALL_LISTEN_PORTS_REMOVE_FAILED")), (LPCTSTR)FormatFirewallHResult(hr));
		return false;
	}

	struct SWindowsFirewallMaintenanceThreadData
	{
		bool bEnable;
		bool bEnsureOnly;
		uint16 nTcpPort;
		uint16 nUdpPort;
	};

	UINT AFX_CDECL WindowsFirewallMaintenanceThreadProc(LPVOID pParam)
	{
		SWindowsFirewallMaintenanceThreadData* pData = static_cast<SWindowsFirewallMaintenanceThreadData*>(pParam);
		if (pData == NULL)
			return 1;

		const bool bEnable = pData->bEnable;
		const bool bEnsureOnly = pData->bEnsureOnly;
		const uint16 nTcpPort = pData->nTcpPort;
		const uint16 nUdpPort = pData->nUdpPort;
		delete pData;

		const HRESULT hrCoInit = ::CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
		RunWindowsFirewallListenPortRules(bEnable, bEnsureOnly, true, nTcpPort, nUdpPort);
		if (SUCCEEDED(hrCoInit))
			::CoUninitialize();
		return 0;
	}

	bool QueueWindowsFirewallListenPortRules(bool bEnable, bool bEnsureOnly, uint16 nTcpPort, uint16 nUdpPort)
	{
		SWindowsFirewallMaintenanceThreadData* pData = new SWindowsFirewallMaintenanceThreadData;
		pData->bEnable = bEnable;
		pData->bEnsureOnly = bEnsureOnly;
		pData->nTcpPort = nTcpPort;
		pData->nUdpPort = nUdpPort;

		CWinThread* pThread = AfxBeginThread(WindowsFirewallMaintenanceThreadProc, pData, THREAD_PRIORITY_BELOW_NORMAL);
		if (pThread != NULL)
			return true;

		delete pData;
		theApp.QueueLogLine(true, GetResString(bEnable ? _T("WINDOWS_FIREWALL_LISTEN_PORTS_OPEN_FAILED") : _T("WINDOWS_FIREWALL_LISTEN_PORTS_REMOVE_FAILED")), (LPCTSTR)FormatFirewallHResult(HRESULT_FROM_WIN32(ERROR_NOT_ENOUGH_MEMORY)));
		return false;
	}

	bool TryProcessFirewallMaintenanceCommandLine()
	{
		bool bApply = false;
		bool bRemove = false;
		uint16 nTcpPort = 0;
		uint16 nUdpPort = 0;

		for (int i = 1; i < __argc; ++i) {
			LPCTSTR pszParam = __targv[i];
			if (IsCommandLineSwitch(pszParam, EMULE_AI_FIREWALL_APPLY_ARG))
				bApply = true;
			else if (IsCommandLineSwitch(pszParam, EMULE_AI_FIREWALL_REMOVE_ARG))
				bRemove = true;
			else if (IsCommandLineSwitch(pszParam, EMULE_AI_FIREWALL_TCP_ARG) && i + 1 < __argc)
				TryParsePortValue(__targv[++i], nTcpPort);
			else if (IsCommandLineSwitch(pszParam, EMULE_AI_FIREWALL_UDP_ARG) && i + 1 < __argc)
				TryParsePortValue(__targv[++i], nUdpPort);
		}

		if (!bApply && !bRemove)
			return false;

		const HRESULT hr = ConfigureWindowsFirewallListenPortRules(bApply && !bRemove, nTcpPort, nUdpPort);
		::ExitProcess(SUCCEEDED(hr) ? ERROR_SUCCESS : static_cast<DWORD>(hr));
	}
}

static UINT ClampStartupTelemetryToUInt(size_t uValue)
{
	const size_t uMax = static_cast<size_t>(static_cast<UINT>(-1));
	return static_cast<UINT>(min(uValue, uMax));
}

#ifdef DEBUGLEAKHELPER
#include "eMuleAI/DebugLeakHelper.h"

static bool IsExitCommandInvocationForLeakDump()
{
	int argc = 0;
	LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
	if (argv == NULL)
		return false;

	const bool isExitCommand = (argc >= 2 && _wcsicmp(argv[1], L"exit") == 0);
	::LocalFree(argv);
	return isExitCommand;
}
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern UINT g_uMainThreadId;

namespace
{
	volatile LONG g_lOnlineSigSaveGeneration = 0;

	struct SWorkerTopologySpec
	{
		CemuleApp::EWorkerTopologyRole m_eRole;
		LPCTSTR m_pszName;
		UINT m_uQueueLimit;
		bool m_bDedicatedThread;
		bool m_bCoalesceByKey;
		bool m_bDropOldestOnPressure;
		bool m_bDrainOnShutdown;
		bool m_bCancelOnShutdown;
	};

	static const SWorkerTopologySpec s_workerTopologySpecs[] =
	{
		{ CemuleApp::WorkerTopologyBackendCommand, _T("backend-command"), 0, false, false, false, true, false },
		{ CemuleApp::WorkerTopologyNetworkParseCpu, _T("network-parse-cpu"), 64, true, true, true, true, true },
		{ CemuleApp::WorkerTopologyNetworkUtility, _T("network-utility"), 32, true, false, true, false, true },
		{ CemuleApp::WorkerTopologyPartFileDiskIo, _T("part-file-disk-io"), 0, false, false, false, true, false },
		{ CemuleApp::WorkerTopologyUploadDiskIo, _T("upload-disk-io"), 0, false, false, false, true, false },
		{ CemuleApp::WorkerTopologyPersistence, _T("persistence"), 32, true, true, false, true, false },
		{ CemuleApp::WorkerTopologyStartupLoadPrimary, _T("startup-load-primary"), 4, true, true, false, false, true },
		{ CemuleApp::WorkerTopologyStartupLoadSecondary, _T("startup-load-secondary"), 4, true, true, false, false, true },
		{ CemuleApp::WorkerTopologyStartupLoadSearches, _T("startup-load-searches"), 2, true, true, false, false, true },
		{ CemuleApp::WorkerTopologyStartupLoadDownloads, _T("startup-load-downloads"), 2, true, true, false, false, true },
		{ CemuleApp::WorkerTopologyStartupLoadKnown2, _T("startup-load-known2"), 2, true, true, false, false, true }
	};


	const SWorkerTopologySpec* GetWorkerTopologySpec(CemuleApp::EWorkerTopologyRole eRole)
	{
		for (int i = 0; i < static_cast<int>(_countof(s_workerTopologySpecs)); ++i) {
			if (s_workerTopologySpecs[i].m_eRole == eRole)
				return &s_workerTopologySpecs[i];
		}
		return NULL;
	}

	LPCSTR GetWorkerTopologyRoleThreadName(CemuleApp::EWorkerTopologyRole eRole)
	{
		switch (eRole) {
		case CemuleApp::WorkerTopologyNetworkParseCpu:
			return "NetworkParseCpuWorker";
		case CemuleApp::WorkerTopologyNetworkUtility:
			return "NetworkUtilityWorker";
		case CemuleApp::WorkerTopologyPersistence:
			return "PersistenceWorker";
		case CemuleApp::WorkerTopologyStartupLoadPrimary:
			return "StartupLoadWorker1";
		case CemuleApp::WorkerTopologyStartupLoadSecondary:
			return "StartupLoadWorker2";
		case CemuleApp::WorkerTopologyStartupLoadSearches:
			return "StartupLoadSearchesWorker";
		case CemuleApp::WorkerTopologyStartupLoadDownloads:
			return "StartupLoadDownloadsWorker";
		case CemuleApp::WorkerTopologyStartupLoadKnown2:
			return "StartupLoadKnown2Worker";
		default:
			return "WorkerTopology";
		}
	}
}

#if _MSC_VER>=1400 && defined(_UNICODE)
#if defined _M_IX86
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='x86' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_IA64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='ia64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_X64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='amd64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#elif defined _M_ARM64
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='arm64' publicKeyToken='6595b64144ccf1df' language='*'\"")
#else
#pragma comment(linker,"/manifestdependency:\"type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")
#endif
#endif

CLogFile theLog;
CLogFile theVerboseLog;
bool g_bLowColorDesktop = false;

namespace
{
	bool IsServerMetAsyncDiskWriteFailure(const CemuleApp::SApplicationEvent &event)
	{
		return event.m_strAsyncResult.CompareNoCase(_T("failed")) == 0 && event.m_strAsyncName.CompareNoCase(_T("server.met")) == 0;
	}

	CString BuildAsyncDiskWriteFailureDetail(const CemuleApp::SApplicationEvent &event)
	{
		if (event.m_dwLastError == 0)
			return CString();
		return GetErrorMessage(event.m_dwLastError);
	}
}


///////////////////////////////////////////////////////////////////////////////
// C-RTL Memory Debug Support
//
#ifdef _DEBUG
static CMemoryState oldMemState, newMemState, diffMemState;

_CRT_ALLOC_HOOK g_pfnPrevCrtAllocHook = NULL;
CMap<const unsigned char*, const unsigned char*, UINT, UINT> g_allocations;
static __declspec(thread) bool g_bAllocHookReentry = false;
int eMuleAllocHook(int mode, void *pUserData, size_t nSize, int nBlockUse, long lRequest, const unsigned char *pszFileName, int nLine) noexcept;

#ifdef DEBUGLEAKHELPER
// Apply breakalloc as early as possible to catch pre-init allocations.
#pragma section(".CRT$XIA", read)
static void ApplyCrtBreakAllocsFromEnv();
static void __cdecl EarlyApplyCrtBreakAlloc();
extern "C" __declspec(allocate(".CRT$XIA")) void (__cdecl *g_pEarlyApplyBreakAlloc)(void) = EarlyApplyCrtBreakAlloc;

static bool IsNonZeroEnvironmentFlag(LPCTSTR pszName)
{
	TCHAR szValue[16] = {};
	const DWORD dwLen = ::GetEnvironmentVariable(pszName, szValue, _countof(szValue));
	return dwLen > 0 && szValue[0] != _T('\0') && !(szValue[0] == _T('0') && szValue[1] == _T('\0'));
}

static bool IsCrtAllocHookRequested()
{
	return IsNonZeroEnvironmentFlag(_T("EMULE_CRT_ENABLE_ALLOC_HOOK"));
}

static void InstallCrtAllocHookIfRequested()
{
	if (!IsCrtAllocHookRequested())
		return;
	if (g_pfnPrevCrtAllocHook == NULL)
		g_pfnPrevCrtAllocHook = _CrtSetAllocHook(&eMuleAllocHook);
}

// Run during earliest C initialization to catch allocations before other initializers.
#pragma section(".CRT$XAA", read)
static void __cdecl EarlyInitLeakHook();
extern "C" __declspec(allocate(".CRT$XAA")) void (__cdecl *g_pEarlyInitLeakHook)(void) = EarlyInitLeakHook;

static void __cdecl EarlyApplyCrtBreakAlloc()
{
	ApplyCrtBreakAllocsFromEnv();
}

static void __cdecl EarlyInitLeakHook()
{
	ApplyCrtBreakAllocsFromEnv();
	if (IsNonZeroEnvironmentFlag(_T("EMULE_CRT_EARLY_ALLOC_HOOK"))) {
		DebugLeakHelper::EarlyInit();
		InstallCrtAllocHookIfRequested();
		DebugLeakHelper::Init();
	}
}
#endif

// Cannot use a CString for that memory - it will be unavailable on application termination!
#define APP_CRT_DEBUG_LOG_FILE _T("eMule CRT Debug Log.log")
static TCHAR s_szCrtDebugReportFilePath[MAX_PATH] = APP_CRT_DEBUG_LOG_FILE;

// Parse EMULE_CRT_BREAKALLOCS="12952,12949;15950 106 107" and set _CrtSetBreakAlloc for each ID (Debug only).
static void ApplyCrtBreakAllocsFromEnv()
{
	TCHAR buf[1024] = {0};
	const DWORD n = GetEnvironmentVariable(_T("EMULE_CRT_BREAKALLOCS"), buf, _countof(buf));
	if (n > 0 && n < _countof(buf)) {
		TCHAR *ctx = NULL;
		for (TCHAR *tok = _tcstok_s(buf, _T(",; \t"), &ctx); tok != NULL; tok = _tcstok_s(NULL, _T(",; \t"), &ctx)) {
			const __int64 id = _tstoi64(tok);
			if (id > 0 && id <= LONG_MAX)
				_CrtSetBreakAlloc((long)id);
		}
	}
	TCHAR singleBuf[64] = {0};
	const DWORD nSingle = GetEnvironmentVariable(_T("EMULE_CRT_BREAKALLOC"), singleBuf, _countof(singleBuf));
	if (nSingle > 0 && nSingle < _countof(singleBuf)) {
		const __int64 id = _tstoi64(singleBuf);
		if (id > 0 && id <= LONG_MAX)
			_CrtSetBreakAlloc((long)id);
	}
#ifdef FORCE_CRT_BREAKALLOCS
	// Optional compile-time list, e.g. #define FORCE_CRT_BREAKALLOCS {12952,12949,15950,106,107}
	static const long kForced[] = FORCE_CRT_BREAKALLOCS;
	for (size_t i = 0; i < _countof(kForced); ++i)
		_CrtSetBreakAlloc(kForced[i]);
#endif
}
#endif //_DEBUG

#ifdef _M_IX86
///////////////////////////////////////////////////////////////////////////////
// SafeSEH - Safe Exception Handlers
//
// This security feature must be enabled at compile time, due to using the
// linker command line option "/SafeSEH". Depending on the used libraries and
// object files which are used to link eMuleAI.exe, the linker may or may not
// throw some errors about 'safeseh'. Those errors have to get resolved until
// the linker is capable of linking eMuleAI.exe *with* "/SafeSEH".
//
// At runtime, we just can check if the linker created an according SafeSEH
// exception table in the '__safe_se_handler_table' object. If SafeSEH was not
// specified at all during link time, the address of '__safe_se_handler_table'
// is NULL -> hence, no SafeSEH is enabled.
///////////////////////////////////////////////////////////////////////////////
extern "C" PVOID __safe_se_handler_table[];
extern "C" BYTE  __safe_se_handler_count;

void InitSafeSEH()
{
	// Need to workaround the optimizer of the C-compiler...
	volatile PVOID safe_se_handler_table = __safe_se_handler_table;
	if (safe_se_handler_table == NULL)
		CDarkMode::MessageBox(_T("eMuleAI.exe was not linked with /SafeSEH!"), MB_ICONSTOP);
}
#endif //_M_IX86

///////////////////////////////////////////////////////////////////////////////
// DEP - Data Execution Prevention
//
// VS2003:	DEP must be enabled dynamically because the linker does not support
//			the "/NXCOMPAT" command line option.
// VS2005:	DEP can get enabled at link time by using the "/NXCOMPAT" command
//			line option.
// VS2008:	DEP can get enabled at link time by using the "DEP" option within
//			'Visual Studio Linker Advanced Options'.
//
#ifndef PROCESS_DEP_ENABLE
#define	PROCESS_DEP_ENABLE						0x00000001
#define	PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION	0x00000002
#endif//!PROCESS_DEP_ENABLE

void InitDEP()
{
	BOOL(WINAPI *pfnGetProcessDEPPolicy)(HANDLE hProcess, LPDWORD lpFlags, PBOOL lpPermanent);
	BOOL(WINAPI *pfnSetProcessDEPPolicy)(DWORD dwFlags);
	(FARPROC&)pfnGetProcessDEPPolicy = GetProcAddress(GetModuleHandle(_T("kernel32")), "GetProcessDEPPolicy");
	(FARPROC&)pfnSetProcessDEPPolicy = GetProcAddress(GetModuleHandle(_T("kernel32")), "SetProcessDEPPolicy");
	if (pfnGetProcessDEPPolicy && pfnSetProcessDEPPolicy) {
		DWORD dwFlags;
		BOOL bPermanent;
		if ((*pfnGetProcessDEPPolicy)(GetCurrentProcess(), &dwFlags, &bPermanent)) {
			// Vista SP1
			// ===============================================================
			//
			// BOOT.INI nx=OptIn,  VS2003/VS2005
			// ---------------------------------
			// DEP flags: 00000000
			// Permanent: 0
			//
			// BOOT.INI nx=OptOut, VS2003/VS2005
			// ---------------------------------
			// DEP flags: 00000001 (PROCESS_DEP_ENABLE)
			// Permanent: 0
			//
			// BOOT.INI nx=OptIn/OptOut, VS2003 + EditBinX/NXCOMPAT
			// ----------------------------------------------------
			// DEP flags: 00000003 (PROCESS_DEP_ENABLE | *PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION*)
			// Permanent: *1*
			// ---
			// There is no way to remove the PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION flag at runtime,
			// because the DEP policy is already permanent due to the NXCOMPAT flag.
			//
			// BOOT.INI nx=OptIn/OptOut, VS2005 + /NXCOMPAT
			// --------------------------------------------
			// DEP flags: 00000003 (PROCESS_DEP_ENABLE | PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION)
			// Permanent: *1*
			//
			// NOTE: It is ultimately important to explicitly enable the DEP policy even if the
			// process' DEP policy is already enabled. If the DEP policy is already enabled due
			// to an OptOut system policy, the DEP policy is though not yet permanent. As long as
			// the DEP policy is not permanent it could get changed during runtime...
			//
			// So, if the DEP policy for the current process is already enabled but not permanent,
			// it has to be explicitly enabled by calling 'SetProcessDEPPolicy' to make it permanent.
			//
			if (((dwFlags & PROCESS_DEP_ENABLE) == 0 || !bPermanent)
#if _ATL_VER>0x0710
				|| (dwFlags & PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION) == 0
#endif
				)
			{
				 // VS2003:	Enable DEP (with ATL-thunk emulation) if not already set by system policy
				 //			or if the policy is not yet permanent.
				 //
				 // VS2005:	Enable DEP (without ATL-thunk emulation) if not already set by system policy
				 //			or linker "/NXCOMPAT" option or if the policy is not yet permanent. We should
				 //			not reach this code path at all because the "/NXCOMPAT" option is specified.
				 //			However, the code path is here for safety reasons.
				dwFlags = PROCESS_DEP_ENABLE;
				// VS2005: Disable ATL thunks.
				dwFlags |= PROCESS_DEP_DISABLE_ATL_THUNK_EMULATION;
				(*pfnSetProcessDEPPolicy)(dwFlags);
			}
		}
	}
}


///////////////////////////////////////////////////////////////////////////////
// Heap Corruption Detection
//
// For Windows XP SP3 and later. Does *not* have any performance impact!
//
#ifndef HeapEnableTerminationOnCorruption
#define HeapEnableTerminationOnCorruption (HEAP_INFORMATION_CLASS)1
#endif//!HeapEnableTerminationOnCorruption

void InitHeapCorruptionDetection()
{
	BOOL(WINAPI *pfnHeapSetInformation)(HANDLE HeapHandle, HEAP_INFORMATION_CLASS HeapInformationClass, PVOID HeapInformation, SIZE_T HeapInformationLength);
	(FARPROC &)pfnHeapSetInformation = GetProcAddress(GetModuleHandle(_T("kernel32")), "HeapSetInformation");
	if (pfnHeapSetInformation)
		(*pfnHeapSetInformation)(NULL, HeapEnableTerminationOnCorruption, NULL, 0);
}


struct SLogItem
{
	UINT uFlags;
	CString line;
};

void CALLBACK myErrHandler(Kademlia::CKademliaError *error)
{
	CString msg;
	msg.Format(_T("\r\nError 0x%08X : %hs\r\n"), error->m_iErrorCode, error->m_szErrorDescription);
	if (!theApp.IsClosing())
		theApp.QueueDebugLogLine(false, _T("%s"), (LPCTSTR)msg);
}

void CALLBACK myDebugAndLogHandler(LPCSTR lpMsg)
{
	if (!theApp.IsClosing())
		theApp.QueueDebugLogLine(false, _T("%hs"), lpMsg);
}

void CALLBACK myLogHandler(LPCSTR lpMsg)
{
	if (!theApp.IsClosing())
		theApp.QueueLogLine(false, _T("%hs"), lpMsg);
}

static const UINT UWM_ARE_YOU_EMULE = RegisterWindowMessage(EMULE_GUID);

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) noexcept;

namespace
{
	enum EPartMetDiskSpaceState : BYTE
	{
		PARTMET_DISKSPACE_UNKNOWN = 0,
		PARTMET_DISKSPACE_ALLOWED = 1,
		PARTMET_DISKSPACE_BLOCKED = 2
	};

	BYTE QueryPartMetDiskSpaceState(const CString& strRootPath)
	{
		ULARGE_INTEGER nFreeDiskSpace;
		if (!::GetDiskFreeSpaceEx(strRootPath, &nFreeDiskSpace, NULL, NULL)) {
			DebugLogWarning(_T("Part.met disk-space guard could not query \"%s\" (%s), keeping metadata writes enabled."), (LPCTSTR)EscPercent(strRootPath), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
			return PARTMET_DISKSPACE_ALLOWED;
		}

		if (nFreeDiskSpace.QuadPart < MAXMETSIZE) {
			AddDebugLogLine(DLP_LOW, false, _T("Part.met disk-space guard blocked \"%s\" (%I64u bytes free, need %I64u)."), (LPCTSTR)EscPercent(strRootPath), nFreeDiskSpace.QuadPart, (ULONGLONG)MAXMETSIZE);
			return PARTMET_DISKSPACE_BLOCKED;
		}

		return PARTMET_DISKSPACE_ALLOWED;
	}

	BYTE QueryShutdownPartFlushDiskSpaceState(const CString& strRootPath)
	{
		if (!thePrefs.IsCheckDiskspaceEnabled() || thePrefs.GetMinFreeDiskSpace() == 0)
			return PARTMET_DISKSPACE_ALLOWED;

		ULARGE_INTEGER nFreeDiskSpace;
		if (!::GetDiskFreeSpaceEx(strRootPath, &nFreeDiskSpace, NULL, NULL)) {
			DebugLogWarning(_T("Shutdown part flush guard could not query \"%s\" (%s), keeping flush enabled."),
				(LPCTSTR)EscPercent(strRootPath), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));
			return PARTMET_DISKSPACE_ALLOWED;
		}

		if (nFreeDiskSpace.QuadPart < thePrefs.GetMinFreeDiskSpace()) {
			AddDebugLogLine(DLP_LOW, false, _T("Shutdown part flush guard blocked \"%s\" (%I64u bytes free, need %I64u)."),
				(LPCTSTR)EscPercent(strRootPath), nFreeDiskSpace.QuadPart, thePrefs.GetMinFreeDiskSpace());
			return PARTMET_DISKSPACE_BLOCKED;
		}

		return PARTMET_DISKSPACE_ALLOWED;
	}
}

///////////////////////////////////////////////////////////////////////////////
// CemuleApp

BEGIN_MESSAGE_MAP(CemuleApp, CWinApp)
	ON_COMMAND(ID_HELP, OnHelp)
END_MESSAGE_MAP()

CemuleApp::CemuleApp(LPCTSTR lpszAppName)
	: CWinApp(lpszAppName)
	, emuledlg()
	, m_iDfltImageListColorFlags(ILC_COLOR)
	, m_ullComCtrlVer(MAKEDLLVERULL(4, 0, 0, 0))
	, m_app_state(APP_STATE_STARTING)
	, m_hSystemImageList()
	, m_sizSmallSystemIcon(16, 16)
	, m_hBigSystemImageList()
	, m_sizBigSystemIcon(32, 32)
	, m_strDefaultFontFaceName(_T("MS Shell Dlg 2"))
	, m_dwPublicIP()
	, m_uDisplayedUploadDatarate(0)
	, m_uDisplayedDownloadDatarate(0)
	, m_bGuardClipboardPrompt()
	, m_bAutoStart()
	, m_bStandbyOff()
	, m_lNetworkBindSocketCreationPermit(0)
	, m_bFirstIPv4(true)
	, m_bFirstIPv6(true)
	, m_dwLastValidIPv4()
	, m_tLastDiskSpaceCheckTime()
	, tLastBackupTime()
	, m_lBackupWorkerActive(0)
	, m_nConnectionState(CONSTATE_ONLINE)
	, m_downloadLinkParseQueue()
	, m_bDownloadLinkParseWorkerActive(false)
	, m_chunkedDownloadParseJobs()
	, m_bChunkedDownloadParseMessagePending(false)
	, m_chunkedDownloadJobs()
	, m_bChunkedDownloadMessagePending(false)
	, m_bActiveDownloadAddOperation(false)
	, m_bActiveDownloadAddSavingToDisk(false)
	, m_uActiveDownloadAddDone(0)
	, m_uActiveDownloadAddTotal(0)
	, m_backendCommandQueue()
	, m_lBackendCommandMessagePending(0)
	, m_lBackendCommandDispatching(0)
	, m_lBackendCommandReentryTraceTick(0)
	, m_persistenceCommandQueue()
	, m_applicationEventQueue()
	, m_collectionImportResults()
	, m_sharedFilesFileSystemReloadGenerations()
	, m_sharedFilesFileSystemReloadTokens()
	, m_uNextSharedFilesFileSystemReloadToken(0)
	, m_uNextStartupMetadataCancellationToken(0)
	, m_lApplicationEventMessagePending(0)
	, m_bApplicationEventDispatching(false)
	, m_uNextWorkerTopologySequence(0)
	, m_lSearchIngestProcessingPending(0)
	, m_uNextBackendCommandSequence(0)
	, m_uNextBackendCommandCancellationToken(0)
	, m_dwBackendOwnerThreadId(0)
	, m_dwNetworkParserOwnerThreadId(0)
	, m_lBackendLifecycleState(BackendLifecycleStarting)
	, m_pBackendCommandThread(NULL)
	, m_hBackendCommandEvent(NULL)
	, m_hBackendCommandStopEvent(NULL)
	, m_dwBackendCommandThreadId(0)

{
	for (int i = 0; i < WorkerTopologyRoleCount; ++i) {
		m_pWorkerTopologyThreads[i] = NULL;
		m_hWorkerTopologyEvents[i] = NULL;
		m_hWorkerTopologyStopEvents[i] = NULL;
		m_dwWorkerTopologyThreadIds[i] = 0;
		m_bWorkerTopologyAccepting[i] = false;
		m_alWorkerTopologyStates[i] = WorkerTopologyStopped;
		m_alWorkerTopologyInFlight[i] = 0;
		m_uWorkerTopologyCancellationTokens[i] = 0;
	}

	// Initialize Windows security features.
#if !defined(_DEBUG) && !defined(_WIN64)
	InitSafeSEH();
#endif
	InitDEP();
	InitHeapCorruptionDetection();

#ifdef DEBUGLEAKHELPER
	DebugLeakHelper::Init();
	InstallCrtAllocHookIfRequested();
#endif

	// This does not seem to work well with multithreading, although there is no reason why it should not.

	srand((unsigned)time(NULL));

// MOD Note: Do not change this part - Merkur

	// this is the "base" version number <major>.<minor>.<update>.<build>
	m_dwProductVersionMS = MAKELONG(CemuleApp::m_nVersionMin, CemuleApp::m_nVersionMjr);
	m_dwProductVersionLS = MAKELONG(CemuleApp::m_nVersionBld, CemuleApp::m_nVersionUpd);

	// create a string version (e.g. "0.30a")
	ASSERT(CemuleApp::m_nVersionUpd + 'a' <= 'f');
	m_strCurVersionLongDbg.Format(_T("%u.%u%c.%u"), CemuleApp::m_nVersionMjr, CemuleApp::m_nVersionMin, _T('a') + CemuleApp::m_nVersionUpd, CemuleApp::m_nVersionBld);
#if defined( _DEBUG) || defined(_DEVBUILD)
	m_strCurVersionLong = m_strCurVersionLongDbg;
#else
	m_strCurVersionLong.Format(_T("%u.%u%c"), CemuleApp::m_nVersionMjr, CemuleApp::m_nVersionMin, _T('a') + CemuleApp::m_nVersionUpd);
#endif
	m_strCurVersionLong += CemuleApp::m_sPlatform;

#if defined( _DEBUG) && !defined(_BOOTSTRAPNODESDAT)
	m_strCurVersionLong += _T(" DEBUG");
#endif
#ifdef _BETA
	m_strCurVersionLong += _T(" BETA");
#endif
#ifdef _DEVBUILD
	m_strCurVersionLong += _T(" DEVBUILD");
#endif
#ifdef _BOOTSTRAPNODESDAT
	m_strCurVersionLong += _T(" BOOTSTRAP BUILD");
#endif

	// create the protocol version number
	CString strTmp;
	strTmp.Format(_T("0x%lu"), m_dwProductVersionMS);
	VERIFY(_stscanf(strTmp, _T("0x%x"), &m_uCurVersionShort) == 1);
	ASSERT(m_uCurVersionShort < 0x99);

	// create the version check number
	strTmp.Format(_T("0x%lu%c"), m_dwProductVersionMS, _T('A') + CemuleApp::m_nVersionUpd);
	VERIFY(_stscanf(strTmp, _T("0x%x"), &m_uCurVersionCheck) == 1);
	ASSERT(m_uCurVersionCheck < 0x999);
// MOD Note: end

	EnableHtmlHelp();
}

// Barry - To find out if app is running or shutting/shut down
bool CemuleApp::IsRunning() const
{
	return m_app_state == APP_STATE_RUNNING || m_app_state == APP_STATE_ASKCLOSE;
}

bool CemuleApp::IsClosing() const
{
	return m_app_state == APP_STATE_SHUTTINGDOWN || m_app_state == APP_STATE_DONE || IsBackendLifecycleStopping();
}

bool CemuleApp::IsNetworkActivityBlockedByBind() const
{
	return emuledlg != NULL && emuledlg->IsSessionNetworkBlocked();
}

bool CemuleApp::IsNetworkSocketCreationBlockedByBind() const
{
	return IsNetworkActivityBlockedByBind() && ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lNetworkBindSocketCreationPermit), 0, 0) <= 0;
}

void CemuleApp::BeginNetworkBindSocketCreation()
{
	::InterlockedIncrement(&m_lNetworkBindSocketCreationPermit);
}

void CemuleApp::EndNetworkBindSocketCreation()
{
	if (::InterlockedDecrement(&m_lNetworkBindSocketCreationPermit) < 0)
		::InterlockedExchange(&m_lNetworkBindSocketCreationPermit, 0);
}

CString CemuleApp::GetNetworkActivityBlockMessage() const
{
	if (!IsNetworkActivityBlockedByBind())
		return CString();

	return emuledlg->GetSessionNetworkBlockReason();
}

void CemuleApp::RefreshShutdownPartFlushDiskSpaceCache()
{
	{
		CSingleLock sShutdownPartFlushDiskSpaceLock(&m_shutdownPartFlushDiskSpaceLock, TRUE);
		m_mapShutdownPartFlushDiskSpaceState.RemoveAll();
	}

	if (downloadqueue == NULL || !thePrefs.IsCheckDiskspaceEnabled() || thePrefs.GetMinFreeDiskSpace() == 0)
		return;

	CMapStringToPtr mapSeenRoots;
	for (POSITION pos = downloadqueue->filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile* pPartFile = downloadqueue->filelist.GetNext(pos);
		if (pPartFile == NULL)
			continue;

		CString strRootPath;
		if (!GetVolumeRootPath(pPartFile->GetTmpPath(), strRootPath))
			continue;

		strRootPath.MakeLower();
		void* pDummy = NULL;
		if (mapSeenRoots.Lookup(strRootPath, pDummy))
			continue;

		mapSeenRoots.SetAt(strRootPath, this);
		(void)CanShutdownFlushPartFile(strRootPath, true);
	}
}

bool CemuleApp::CanShutdownFlushPartFile(LPCTSTR pszPath, bool bForceRefresh)
{
	if (!thePrefs.IsCheckDiskspaceEnabled() || thePrefs.GetMinFreeDiskSpace() == 0)
		return true;

	CString strRootPath;
	if (!GetVolumeRootPath(pszPath, strRootPath))
		return true;

	strRootPath.MakeLower();

	BYTE byState = PARTMET_DISKSPACE_UNKNOWN;
	{
		CSingleLock sShutdownPartFlushDiskSpaceLock(&m_shutdownPartFlushDiskSpaceLock, TRUE);
		if (!bForceRefresh && m_mapShutdownPartFlushDiskSpaceState.Lookup(strRootPath, byState))
			return byState != PARTMET_DISKSPACE_BLOCKED;
	}

	byState = QueryShutdownPartFlushDiskSpaceState(strRootPath);

	{
		CSingleLock sShutdownPartFlushDiskSpaceLock(&m_shutdownPartFlushDiskSpaceLock, TRUE);
		m_mapShutdownPartFlushDiskSpaceState.SetAt(strRootPath, byState);
	}

	return byState != PARTMET_DISKSPACE_BLOCKED;
}

void CemuleApp::RefreshPartMetDiskSpaceCache()
{
	{
		CSingleLock sPartMetDiskSpaceLock(&m_partMetDiskSpaceLock, TRUE);
		m_mapPartMetDiskSpaceState.RemoveAll();
	}

	if (downloadqueue == NULL)
		return;

	CMapStringToPtr mapSeenRoots;
	for (POSITION pos = downloadqueue->filelist.GetHeadPosition(); pos != NULL;) {
		CPartFile* pPartFile = downloadqueue->filelist.GetNext(pos);
		if (pPartFile == NULL)
			continue;

		CString strRootPath;
		if (!GetVolumeRootPath(pPartFile->GetTmpPath(), strRootPath))
			continue;

		strRootPath.MakeLower();
		void* pDummy = NULL;
		if (mapSeenRoots.Lookup(strRootPath, pDummy))
			continue;

		mapSeenRoots.SetAt(strRootPath, this);
		(void)CanWritePartMetFiles(strRootPath, true);
	}
}

bool CemuleApp::CanWritePartMetFiles(LPCTSTR pszPath, bool bForceRefresh)
{
	CString strRootPath;
	if (!GetVolumeRootPath(pszPath, strRootPath))
		return true;

	strRootPath.MakeLower();

	BYTE byState = PARTMET_DISKSPACE_UNKNOWN;
	{
		CSingleLock sPartMetDiskSpaceLock(&m_partMetDiskSpaceLock, TRUE);
		if (!bForceRefresh && m_mapPartMetDiskSpaceState.Lookup(strRootPath, byState))
			return byState != PARTMET_DISKSPACE_BLOCKED;
	}

	byState = QueryPartMetDiskSpaceState(strRootPath);

	{
		CSingleLock sPartMetDiskSpaceLock(&m_partMetDiskSpaceLock, TRUE);
		m_mapPartMetDiskSpaceState.SetAt(strRootPath, byState);
	}

	return byState != PARTMET_DISKSPACE_BLOCKED;
}

CString CemuleApp::GetAppVersion() const
{
	CString platform;
#if defined(_M_ARM64)
	platform = _T("arm64");
#elif defined(_M_X64) || defined(_M_AMD64) || defined(_WIN64)
	platform = _T("x64");
#elif defined(_M_IX86) || defined(_X86_)
	platform = _T("x86");
#else
	platform = _T("unknown");
#endif

	CString suffix;
#ifdef _DEBUG
	suffix = _T("DEBUG ") + platform;
#else
	suffix = platform;
#endif

	return CString(_T("eMule ")) + MOD_VERSION + _T(" ") + suffix;
}


CemuleApp theApp(_T("eMule"));


// Workaround for bugged 'AfxSocketTerm' (needed at least for MFC 7.0 - 14.14)
void __cdecl __AfxSocketTerm() noexcept
{
	_AFX_SOCK_STATE *pState = _afxSockState.GetData();

#ifndef _AFXDLL
	// Explicitly free MFC socket thread-state containers (avoid client-block leaks)
	_AFX_SOCK_THREAD_STATE* pThreadState = _afxSockThreadState;
	if (pThreadState) {
		if (pThreadState->m_plistSocketNotifications) {
			delete pThreadState->m_plistSocketNotifications; 
			pThreadState->m_plistSocketNotifications = NULL;
		}

		if (pThreadState->m_pmapDeadSockets) {
			delete pThreadState->m_pmapDeadSockets;
			pThreadState->m_pmapDeadSockets = NULL;
		}

		if (pThreadState->m_pmapSocketHandle) { 
			delete pThreadState->m_pmapSocketHandle;
			pThreadState->m_pmapSocketHandle = NULL; 
		}
	}
#endif // !_AFXDLL

	if (pState->m_pfnSockTerm != NULL) {
		VERIFY(WSACleanup() == 0);
		pState->m_pfnSockTerm = NULL;
	}
}

BOOL InitWinsock2(WSADATA *lpwsaData)
{
	_AFX_SOCK_STATE *pState = _afxSockState.GetData();
	if (pState->m_pfnSockTerm == NULL) {
		// initialize Winsock library
		WSADATA wsaData;
		if (lpwsaData == NULL)
			lpwsaData = &wsaData;
		static const WORD wVersionRequested = MAKEWORD(2, 2);
		int nResult = WSAStartup(wVersionRequested, lpwsaData);
		if (nResult != 0)
			return FALSE;
		if (lpwsaData->wVersion != wVersionRequested) {
			WSACleanup();
			return FALSE;
		}
		// setup for termination of sockets
		pState->m_pfnSockTerm = &AfxSocketTerm;
	}
#ifndef _AFXDLL
	//BLOCK: setup maps and lists specific to socket state
	{
		_AFX_SOCK_THREAD_STATE *pThreadState = _afxSockThreadState;
		if (pThreadState->m_pmapSocketHandle == NULL)
			pThreadState->m_pmapSocketHandle = new CMapPtrToPtr;
		if (pThreadState->m_pmapDeadSockets == NULL)
			pThreadState->m_pmapDeadSockets = new CMapPtrToPtr;
		if (pThreadState->m_plistSocketNotifications == NULL)
			pThreadState->m_plistSocketNotifications = new CPtrList;
	}
#endif
	return TRUE;
}

// CemuleApp Initialisierung

BOOL CemuleApp::InitInstance()
{
#ifdef DEBUGLEAKHELPER
	DebugLeakHelper::Init(); // Enable CRT leak checks & report to debugger
	_CrtMemCheckpoint(&g_msStart); // Take "start of app" snapshot AFTER enabling flags
	// Disable ATL/MFC tracing to avoid benign trace-buffer allocations polluting leak dumps.
	ATL::CTrace::SetCategories(0);
#endif

#ifdef _DEBUG
	// set Floating Point Processor to throw several exceptions, in particular the 'Floating point divide by zero'
	UINT uEmCtrlWord = _control87(0, 0) & _MCW_EM;
	_control87(uEmCtrlWord & ~(/*_EM_INEXACT |*/ _EM_UNDERFLOW | _EM_OVERFLOW | _EM_ZERODIVIDE | _EM_INVALID), _MCW_EM);

	// output all ASSERT messages to debug device
	_CrtSetReportMode(_CRT_ASSERT, _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_REPORT_MODE) | _CRTDBG_MODE_DEBUG);

	ApplyCrtBreakAllocsFromEnv(); // Apply optional per-run CRT breakallocs (from EMULE_CRT_BREAKALLOCS env or FORCE_CRT_BREAKALLOCS macro)
#endif
	free((void*)m_pszProfileName);
	const CString &sConfDir(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
	m_pszProfileName = _tcsdup(sConfDir + _T("preferences.ini"));

#ifdef _DEBUG
	oldMemState.Checkpoint();
#ifdef DEBUGLEAKHELPER
	// Installing that memory debug code works fine in Debug builds when running within VS Debugger,
	// but some other test applications don't like that all....
	InstallCrtAllocHookIfRequested();
#endif
#endif


	///////////////////////////////////////////////////////////////////////////
	// Install crash dump creation
	//
	theCrashDumper.uCreateCrashDump = CPreferences::bCreateCrashDump;
#if !defined(_BETA) && !defined(_DEVBUILD)
	if (theCrashDumper.uCreateCrashDump > 0)
#endif
		theCrashDumper.Enable(_T("eMule ") + m_strCurVersionLongDbg, true, sConfDir);

	///////////////////////////////////////////////////////////////////////////
	// Locale initialization -- BE VERY CAREFUL HERE!!!
	//
	_tsetlocale(LC_ALL, EMPTY);		// set all categories of locale to user-default ANSI code page obtained from the OS.
	_tsetlocale(LC_NUMERIC, _T("C"));	// set numeric category to 'C'

	AfxOleInit();

	DetectWin32LongPathsSupportAtStartup(); // Query OS long path support once per process.

#ifdef _ASSERTFILE
#pragma message (" ***NOTE: All asserts will be loged to a CRT Debug Log file, and no window will be displayed")
	_CrtSetReportHook(CrtDebugReportCB);
#endif

	if (ProcessCommandline())
		return FALSE;

	///////////////////////////////////////////////////////////////////////////
	// Common Controls initialization
	//
	//						Mjr Min
	// ----------------------------
	// W98 SE, IE5			5	8
	// W2K SP4, IE6 SP1		5	81
	// XP SP2				6   0
	// XP SP3				6   0
	// Vista SP1			6   16
	InitCommonControls();
	switch (thePrefs.GetWindowsVersion()) {
	case _WINVER_2K_:
		m_ullComCtrlVer = MAKEDLLVERULL(5, 81, 0, 0);
		break;
	case _WINVER_XP_:
	case _WINVER_2003_:
		m_ullComCtrlVer = MAKEDLLVERULL(6, 0, 0, 0);
		break;
	default:  //Vista .. Win11
		m_ullComCtrlVer = MAKEDLLVERULL(6, 16, 0, 0);
	};

	m_sizSmallSystemIcon.cx = ::GetSystemMetrics(SM_CXSMICON);
	m_sizSmallSystemIcon.cy = ::GetSystemMetrics(SM_CYSMICON);
	UpdateLargeIconSize();
	UpdateDesktopColorDepth();

	CWinApp::InitInstance();

	if (!InitWinsock2(&m_wsaData) && !AfxSocketInit(&m_wsaData)) {
		LocMessageBox(_T("SOCKETS_INIT_FAILED"), MB_OK, 0);
		return FALSE;
	}

	atexit(__AfxSocketTerm);

	AfxEnableControlContainer();
	if (!AfxInitRichEdit2() && !AfxInitRichEdit())
		CDarkMode::MessageBox(_T("Fatal Error: No Rich Edit control library found!")); // should never happen.

	if (!Kademlia::CKademlia::InitUnicode(AfxGetInstanceHandle())) {
		CDarkMode::MessageBox(_T("Fatal Error: Failed to load Unicode character tables for Kademlia!")); // should never happen.
		return FALSE; // DO *NOT* START !!!
	}

	extern bool SelfTest();
	if (!SelfTest())
		return FALSE; // DO *NOT* START !!!

	// create & initialize all the important stuff
	theAntiNickClass.Init();
	thePrefs.Init();
	CAICHRecoveryHashSet::ClearDeferredHashSetSaves(); // Remove stale deferred AICH spool data from an interrupted previous run.

	// First-run or missing Ui.Language: prompt for language before creating UI
	if (!thePrefs.IsUiLanguagePresent()) {
		// Show minimal language chooser; do not depend on main window
		CLanguageSelectDlg dlg;
		dlg.DoModal();
	}
	if (thePrefs.ShouldAutoShowMigrationWizard()) {
		CMigrationWizardDlg dlg(true);
		dlg.DoModal();
		thePrefs.SetMigrationWizardRunOnNextStart(false);
		thePrefs.SetMigrationWizardHandled(true);
	}
	theStats.Init();

	// check if we have to restart eMule as Secure user
	if (thePrefs.IsRunAsUserEnabled()) {
		CSecRunAsUser rau;
		eResult res = rau.RestartSecure();
		if (res == RES_OK_NEED_RESTART)
			return FALSE; // emule restart as secure user, kill this instance
		if (res == RES_FAILED)
			// something went wrong
			theApp.QueueLogLine(false, GetResString(_T("RAU_FAILED")), (LPCTSTR)rau.GetCurrentUserW());
	}

	if (thePrefs.GetRTLWindowsLayout())
		EnableRTLWindowsLayout();

#ifdef _DEBUG
	_sntprintf(s_szCrtDebugReportFilePath, _countof(s_szCrtDebugReportFilePath) - 1, _T("%s%s"), (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_LOGDIR, false), APP_CRT_DEBUG_LOG_FILE);
#ifdef DEBUGLEAKHELPER
	DebugLeakHelper::LateInit(); // parse {allocId} from existing CRT log
	DebugLeakHelper::ParseAndApplyBreakAllocsFromEnv(); // re-apply ENV (if provided) to take precedence
#endif
#endif
	VERIFY(theLog.SetFilePath(thePrefs.GetMuleDirectory(EMULE_LOGDIR, thePrefs.GetLog2Disk()) + _T("eMule.log")));
	VERIFY(theVerboseLog.SetFilePath(thePrefs.GetMuleDirectory(EMULE_LOGDIR, false) + _T("eMule_Verbose.log")));
	theLog.SetMaxFileSize(thePrefs.GetMaxLogFileSize());
	theLog.SetFileFormat(thePrefs.GetLogFileFormat());
	theVerboseLog.SetMaxFileSize(thePrefs.GetMaxLogFileSize());
	theVerboseLog.SetFileFormat(thePrefs.GetLogFileFormat());
	if (thePrefs.GetLog2Disk()) {
		theLog.Open();
		theLog.Log(_T("\r\n"));
	}
	if (thePrefs.GetDebug2Disk()) {
		theVerboseLog.Open();
		theVerboseLog.Log(_T("\r\n"));
	}
	Log(GetResString(_T("APP_STARTING")), (LPCTSTR)theApp.GetAppVersion());

	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	CDarkMode::Initialize();

	emuledlg = new CemuleDlg;
	m_pMainWnd = emuledlg;

	QueueStartupDirWatchInit();

#ifdef DEBUGLEAKHELPER
	_CrtMemCheckpoint(&g_msAfterInit); // Snapshot after init
#endif

	// Barry - Auto-take ed2k links
	if (thePrefs.AutoTakeED2KLinks())
		Ask4RegFix(false, true, false);

	SetAutoStart(thePrefs.GetAutoStart());

	if (thePrefs.IsOpenListenPortsInWindowsFirewallEnabled())
		EnsureWindowsFirewallListenPortRules(true);

	// UPnP Port forwarding
	m_pUPnPFinder = new CUPnPImplWrapper();

	// Highres scheduling gives better resolution for Sleep(...) calls, and timeGetTime() calls
	m_wTimerRes = 0;
	if (thePrefs.GetHighresTimer()) {
		TIMECAPS tc;
		if (timeGetDevCaps(&tc, sizeof(TIMECAPS)) == TIMERR_NOERROR) {
			m_wTimerRes = min(max(tc.wPeriodMin, 1), tc.wPeriodMax);
			if (m_wTimerRes > 0) {
				MMRESULT mmResult = timeBeginPeriod(m_wTimerRes);
				if (thePrefs.GetVerbose()) {
					if (mmResult == TIMERR_NOERROR)
						theApp.QueueDebugLogLine(false, _T("Succeeded to set timer/scheduler resolution to %i ms."), m_wTimerRes);
					else {
						theApp.QueueDebugLogLine(false, _T("Failed to set timer/scheduler resolution to %i ms."), m_wTimerRes);
						m_wTimerRes = 0;
					}
				}
			} else
				theApp.QueueDebugLogLine(false, _T("m_wTimerRes == 0. Not setting timer/scheduler resolution."));
		}
	}

	thePrefs.LoadBlacklistFile();

	shield = new CShield();
	ipgeolocation = new CIPGeolocation();
	clientlist = new CClientList();
	if (!thePrefs.GetClientHistory()) {
		uint64 uClientHistoryToken = 0;
		const LONG lClientHistoryGeneration = BeginStartupMetadataLoad(StartupMetadataClientHistory, &uClientHistoryToken, _T("client-history-disabled"));
		CompleteStartupMetadataLoad(StartupMetadataClientHistory, lClientHistoryGeneration, uClientHistoryToken, true, 0, _T("client-history-disabled"));
	}
	friendlist = new CFriendList();
	searchlist = new CSearchList();
	knownfiles = new CKnownFileList(false);
	serverlist = new CServerList();
	serverconnect = new CServerConnect();
	sharedfiles = new CSharedFileList(serverconnect);
	listensocket = new CListenSocket();
	clientudp = new CClientUDPSocket();
	clientcredits = new CClientCreditsList();
	downloadqueue = new CDownloadQueue();	// bugfix - do this before creating the upload queue
	uploadqueue = new CUploadQueue();
	ipfilter = new CIPFilter();
	webserver = new CWebServer(); // Web Server [kuchin]
	scheduler = new CScheduler();
	ConChecker = new CConChecker();
	DownloadValidator = new CDownloadValidator();

	lastCommonRouteFinder = new LastCommonRouteFinder();
	uploadBandwidthThrottler = new UploadBandwidthThrottler();

	m_pUploadDiskIOThread = new CUploadDiskIOThread();
	m_pPartFileWriteThread = new CPartFileWriteThread();
	StartBackendCommandThread();
	if (!StartWorkerTopology(_T("init-complete")))
		AddDebugLogLine(DLP_HIGH, false, _T("Worker topology start failed during init.\n"));
	SetBackendLifecyclePhase(BackendLifecycleRunning, _T("init-complete"));

	thePerfLog.Startup();

	try
	{
		emuledlg->DoModal();
	}
	catch (CException* e)
	{
		CString	msg;
		TCHAR szError[1024];
		e->GetErrorMessage(szError, _countof(szError));
		msg.Format(_T("eMule failed due to an unhandled MFC exception:\n%s"), szError);
		CDarkMode::MessageBox(msg);
		e->Delete();
	}
	catch (CString& error)
	{
		CString	msg;
		msg.Format(_T("eMule failed due to an unhandled exception:\n%s"), error);
		CDarkMode::MessageBox(msg);
	}
	catch (const std::exception& e)
	{
		CString	msg;
#ifdef _UNICODE
		msg.Format(_T("eMule failed due to an unhandled C++ exception:\n%hs"), e.what());
#else
		msg.Format("eMule failed due to an unhandled C++ exception:\n%s", e.what());
#endif
		CDarkMode::MessageBox(msg);
	}
	catch (...)
	{
		CDarkMode::MessageBox(_T("eMule failed due to an unhandled exception!"));
	}

	DisableRTLWindowsLayout();

	// Barry - Restore old registry if required
	if (thePrefs.AutoTakeED2KLinks())
		RevertReg();

	::CloseHandle(m_hMutexOneInstance);
#ifdef _DEBUG
	if (g_pfnPrevCrtAllocHook)
		_CrtSetAllocHook(g_pfnPrevCrtAllocHook);

	newMemState.Checkpoint();
	if (diffMemState.Difference(oldMemState, newMemState)) {
		TRACE("Memory usage:\n");
		diffMemState.DumpStatistics();
	}

#endif //_DEBUG

	ClearDebugLogQueue(true);
	ClearLogQueue(true);

	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning: FALSE"), __FUNCTION__);
	delete emuledlg;
	emuledlg = NULL;
	return FALSE;
}

int CemuleApp::ExitInstance()
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);

	PrepareBackendShutdownForDiskIo(_T("ExitInstance"));
	if (downloadqueue != NULL)
		downloadqueue->DrainDeferredPartFileDiskWorkForShutdown();
	if (m_pUploadDiskIOThread != NULL)
		m_pUploadDiskIOThread->EndThread();
	if (m_pPartFileWriteThread != NULL)
		m_pPartFileWriteThread->EndThread();
	SetBackendLifecyclePhase(BackendLifecycleStoppingNetwork, _T("network-stop"));
	SetBackendLifecyclePhase(BackendLifecycleStoppingUiUpdates, _T("ui-updates-stop"));
	ClearApplicationEventQueue();

	if (m_wTimerRes != 0)
		timeEndPeriod(m_wTimerRes);

	CDarkMode::OnDestroy();

	if (AfxOleGetMessageFilter() != NULL) { // Call only if OLE was initialized by MFC
		__try { 
			AfxOleTerm(FALSE);
		} __except (EXCEPTION_EXECUTE_HANDLER) { 
			/* Keep shutdown stable even if OLE state is inconsistent. */ 
		}
	}

	__AfxSocketTerm(); // Ensure MFC socket state containers are freed before leak snapshot

	// eMule AI: Explicitly delete global objects allocated in InitInstance BEFORE leak report.
	// This ensures that major buffers (like MMDB geolocation database buffers) are freed and not reported as leaks.
	if (m_pPartFileWriteThread != NULL)
		m_pPartFileWriteThread->EndThread();
	if (m_pUploadDiskIOThread != NULL)
		m_pUploadDiskIOThread->EndThread();
	delete m_pPartFileWriteThread;
	m_pPartFileWriteThread = NULL;
	delete m_pUploadDiskIOThread;
	m_pUploadDiskIOThread = NULL;
	delete uploadBandwidthThrottler;
	FreeEncryptedDatagramSocketRandomPool();
	FreeEncryptedStreamSocketRandomPool();
	uploadBandwidthThrottler = NULL;
	delete lastCommonRouteFinder;
	lastCommonRouteFinder = NULL;
	delete DownloadValidator;
	DownloadValidator = NULL;
	delete ConChecker;
	ConChecker = NULL;
	delete scheduler;
	scheduler = NULL;
	delete webserver;
	webserver = NULL;
	delete ipfilter;
	ipfilter = NULL;
	delete uploadqueue;
	uploadqueue = NULL;
	delete downloadqueue;
	downloadqueue = NULL;
	delete clientcredits;
	clientcredits = NULL;
	delete clientudp;
	clientudp = NULL;
	delete listensocket;
	listensocket = NULL;
	delete sharedfiles;
	sharedfiles = NULL;
	delete serverconnect;
	serverconnect = NULL;
	delete serverlist;
	serverlist = NULL;
	delete knownfiles;
	knownfiles = NULL;
	delete searchlist;
	searchlist = NULL;
	delete friendlist;
	friendlist = NULL;
	delete clientlist;
	clientlist = NULL;
	delete ipgeolocation;
	ipgeolocation = NULL;
	delete shield;
	shield = NULL;
	delete m_pUPnPFinder;
	m_pUPnPFinder = NULL;

	CAICHRecoveryHashSet::ClearDeferredHashSetSaves(); // Clears pending deferred AICH saves.

	// Ensure thread-local tooltips are destroyed for the main UI thread.
	AFX_MODULE_THREAD_STATE* pThreadState = AfxGetModuleThreadState();
	if (pThreadState != NULL && pThreadState->m_pToolTip != NULL) {
		pThreadState->m_pToolTip->DestroyWindow();
		delete pThreadState->m_pToolTip;
		pThreadState->m_pToolTip = NULL;
	}

	thePrefs.Uninit();
	theStats.Uninit();
	theAntiNickClass.UnInit();
	FreePreviewAppsStorage();
	FreeStatisticsStorage();
	FreePreferencesStorage();
	CKnownFile::ReleaseBarShaderBuffers();
	CPartFile::ReleaseBarShaderBuffers();
	CUpDownClient::ReleaseBarShaders();

	// Release last clipboard snapshot buffer.
	m_strLastClipboardContents.Empty();
	m_strLastClipboardContents.FreeExtra();
	m_strCurVersionLong.Empty();
	m_strCurVersionLong.FreeExtra();
	m_strCurVersionLongDbg.Empty();
	m_strCurVersionLongDbg.FreeExtra();

	CAICHRecoveryHashSet::ClearStoredAICHHashes(); // Clears the AICH hash set.

	// Also clear pending AICH request list to release CList internals
	while (!CAICHRecoveryHashSet::m_liRequestedData.IsEmpty())
		CAICHRecoveryHashSet::m_liRequestedData.RemoveHead();

	// 1) Free duplicated help file path to avoid reporting a leaked Unicode path buffer.
	if (m_pszHelpFilePath != NULL) {
		free((void*)m_pszHelpFilePath);
		m_pszHelpFilePath = NULL;
	}

	// 1b) Free duplicated profile name to avoid benign shutdown-time leak in CRT snapshot.
	if (m_pszProfileName != NULL) {
		free((void*)m_pszProfileName);
		m_pszProfileName = NULL;
	}

	// 2) Clear extension->system image index caches to release MFC CPlex blocks before leak dump.
	m_aExtToSysImgIdx.RemoveAll();
	m_aBigExtToSysImgIdx.RemoveAll();

#ifdef _DEBUG
	// 3) Clear allocation statistics map to avoid reporting its internal CPlex blocks.
	g_allocations.RemoveAll();
#endif

	// 4) Ensure rich edit global smiley caches are purged before CRT leak snapshot.
	CHTRichEditCtrl::ForcePurgeSmileysForShutdown();

	// 5) Clear volume info cache (map of volume->FS name) to release internal CPlex blocks.
	ClearVolumeInfoCache();

	// 5b) Clear MediaInfo display-name caches to release CPlex blocks.
	ClearMediaInfoCaches();

	// 5c) Release MediaInfoLib global caches before leak dump.

	// 5d) Release translation key index cache before leak dump.
	ClearTranslationKeyIndex();

#ifdef DEBUGLEAKHELPER
	// 5e) Release Crypto++ singleton caches before leak dump.
#if defined(CRYPTOPP_HAS_SINGLETON_CLEANUP)
	CryptoPP::CleanupNbTheorySingletons();
	CryptoPP::CleanupPkcspadSingletons();
#endif
#endif

	// 5f) Close log files and release their path buffers before leak dump.
	theLog.Close();
	theVerboseLog.Close();
#if defined(_DEBUG) && defined(DEBUGLEAKHELPER)
	theLog.ClearFilePath();
	theVerboseLog.ClearFilePath();
#endif

	// 6) Reset Kademlia global tracking maps to avoid benign shutdown-time leaks in CRT snapshot.
	Kademlia::CSearchManager::StopAllSearches();
	Kademlia::CKeyEntry::ResetGlobalTrackingMap();
	Kademlia::CRoutingBin::ResetGlobalTrackingMaps();
	Kademlia::safeKad.ShutdownCleanup();
	Kademlia::fastKad.ShutdownCleanup();

	const int nExitCode = CWinApp::ExitInstance();

#ifdef DEBUGLEAKHELPER
	// Guard against debug-iterator shutdown crashes inside MediaInfoLib cleanup.
	__try {
		MediaInfoLib::Config.ShutdownCleanup();
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		TRACE(_T("[DebugLeak][MediaInfo] ShutdownCleanup raised SEH exception, skipped.\n"));
	}
	TCHAR szManualDump[8] = {};
	const DWORD dwManualDump = GetEnvironmentVariable(_T("EMULE_CRT_FORCE_MANUAL_DUMP"), szManualDump, _countof(szManualDump));
	if (dwManualDump > 0 && dwManualDump < _countof(szManualDump) && szManualDump[0] != _T('0') && !IsExitCommandInvocationForLeakDump())
		DebugLeakHelper::DumpLeaksToCrt();
	int crtFlags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
	crtFlags &= ~_CRTDBG_LEAK_CHECK_DF;
	_CrtSetDbgFlag(crtFlags);
	// Leak dump runs via atexit (registered in DebugLeakHelper::Init) to avoid false positives from static teardown.

	// Safer than walking from a stale header: use snapshot differences.
	_CrtMemState now, diff;
	_CrtMemCheckpoint(&now);

	_RPT0(_CRT_WARN, "==== Stats since app start ====\n");
	if (_CrtMemDifference(&diff, &g_msStart, &now))
		_CrtMemDumpStatistics(&diff);
	else
		_RPT0(_CRT_WARN, "[DebugLeak] No differences since app start.\n");

	_RPT0(_CRT_WARN, "==== Stats since after init ====\n");
	if (_CrtMemDifference(&diff, &g_msAfterInit, &now))
		_CrtMemDumpStatistics(&diff);
	else
		_RPT0(_CRT_WARN, "[DebugLeak] No differences since after init.\n");

	_CRT_ALLOC_HOOK oldHook = _CrtSetAllocHook(NULL);
	// Restore any previous hook (if ours was active) before tearing down the CS.
	if (oldHook == &eMuleAllocHook)
		_CrtSetAllocHook(g_pfnPrevCrtAllocHook);
	else
		_CrtSetAllocHook(oldHook);
	DeleteCriticalSection(&g_allocCS);
#endif

	SetBackendLifecyclePhase(BackendLifecycleStopped, _T("exit-complete"));
	return nExitCode;
}

#ifdef _DEBUG
static bool IsCrtLeakDumpReportNoise(const char* message) noexcept
{
	if (message == NULL || *message == '\0')
		return false;
	if (strstr(message, "Assertion failed") != NULL)
		return false;
	if (strstr(message, "Detected memory leaks!") != NULL || strstr(message, "Dumping objects ->") != NULL || strstr(message, "Object dump complete.") != NULL)
		return true;
	if (strstr(message, " bytes long.") != NULL || strstr(message, " Data: <") != NULL || strstr(message, " normal block at ") != NULL || strstr(message, " client block at ") != NULL)
		return true;
	if (strstr(message, ") :") != NULL && (strstr(message, ".cpp(") != NULL || strstr(message, ".h(") != NULL || strstr(message, ".c(") != NULL || strstr(message, ".inl(") != NULL))
		return true;
	return false;
}

int CrtDebugReportCB(int reportType, char *message, int *returnValue) noexcept
{
	if (IsCrtLeakDumpReportNoise(message)) {
		if (returnValue != NULL)
			*returnValue = 0;
		return TRUE;
	}
#ifdef DEBUGLEAKHELPER
	if (DebugLeakHelper::IsLeakDumpInProgress()) {
		if (returnValue != NULL)
			*returnValue = 0;
		return TRUE;
	}
#endif
	FILE *fp = _tfsopen(s_szCrtDebugReportFilePath, _T("a"), _SH_DENYWR);
	if (fp) {
		time_t tNow = time(NULL);
		TCHAR szTime[40];
		_tcsftime(szTime, _countof(szTime), _T("%H:%M:%S"), localtime(&tNow));
		_ftprintf(fp, _T("%ls  %i  %hs"), szTime, reportType, message);
	#ifdef DEBUGLEAKHELPER
		if (message != NULL && strstr(message, "bytes long") != NULL) {
			const char* pIdStart = strchr(message, '{');
			if (pIdStart != NULL) {
				char* pIdEnd = NULL;
				const unsigned long allocId = strtoul(pIdStart + 1, &pIdEnd, 10);
				if (pIdEnd != NULL && *pIdEnd == '}') {
					TCHAR szTrackedInfo[512] = {};
					if (DebugLeakHelper::TryDescribeTrackedAlloc(allocId, szTrackedInfo, _countof(szTrackedInfo)))
						_ftprintf(fp, _T("%ls  %i  %ls\n"), szTime, reportType, szTrackedInfo);
				}
			}
		}
	#endif
		fclose(fp);
	}
	if (returnValue != NULL)
		*returnValue = 0; // avoid invocation of 'AfxDebugBreak' in ASSERT macros
	return TRUE; // avoid further processing of this debug report message by the CRT
}

// allocation hook - for memory statistics gathering
int eMuleAllocHook(int mode, void *pUserData, size_t nSize, int nBlockUse, long lRequest, const unsigned char *pszFileName, int nLine) noexcept
{
	if (g_bAllocHookReentry) {
		if (g_pfnPrevCrtAllocHook)
			return g_pfnPrevCrtAllocHook(mode, pUserData, nSize, nBlockUse, lRequest, pszFileName, nLine);
		return 1;
	}

	struct AllocHookGuard
	{
		AllocHookGuard() { g_bAllocHookReentry = true; }
		~AllocHookGuard() { g_bAllocHookReentry = false; }
	} guard;

#ifdef DEBUGLEAKHELPER
	// Break on selected allocation IDs (parsed from env/log)
	if (mode == _HOOK_ALLOC && DebugLeakHelper::ShouldBreakAlloc(lRequest)) {
		const char* fileName = pszFileName ? reinterpret_cast<const char*>(pszFileName) : "<null>";
		TRACE(_T("[DebugLeak][BreakAlloc] hit request=%ld size=%Iu block=%d file=%hs line=%d\n"),
			lRequest, nSize, nBlockUse, fileName, nLine);
	#ifdef _MSC_VER
			__debugbreak();
	#else
			DebugBreak();
	#endif
	}

	if (DebugLeakHelper::IsLeakDumpInProgress()) {
		if (g_pfnPrevCrtAllocHook)
			return g_pfnPrevCrtAllocHook(mode, pUserData, nSize, nBlockUse, lRequest, pszFileName, nLine);
		return 1;
	}
#endif

#ifdef DEBUGLEAKHELPER
	EnterCriticalSection(&g_allocCS);
#endif

#ifdef DEBUGLEAKHELPER
	// Do not touch MFC containers from the CRT allocation hook.
	DebugLeakHelper::TrackAllocHookEvent(mode, pUserData, nSize, nBlockUse, lRequest, pszFileName, nLine);
#endif

#ifdef DEBUGLEAKHELPER
	LeaveCriticalSection(&g_allocCS);
#endif

	// Be robust if there was no previous hook installed.
	if (g_pfnPrevCrtAllocHook)
		return g_pfnPrevCrtAllocHook(mode, pUserData, nSize, nBlockUse, lRequest, pszFileName, nLine);
	return 1; // default: allow allocation/free to proceed
}
#endif

bool CemuleApp::EnsureWindowsFirewallListenPortRules(bool bAllowElevation)
{
	const uint16 nTcpPort = thePrefs.GetPort();
	const uint16 nUdpPort = thePrefs.GetUDPPort();
	if (bAllowElevation && !IsProcessElevated())
		return QueueWindowsFirewallListenPortRules(true, true, nTcpPort, nUdpPort);
	return RunWindowsFirewallListenPortRules(true, true, bAllowElevation, nTcpPort, nUdpPort);
}

bool CemuleApp::ApplyWindowsFirewallListenPortRules(bool bAllowElevation)
{
	const uint16 nTcpPort = thePrefs.GetPort();
	const uint16 nUdpPort = thePrefs.GetUDPPort();
	if (bAllowElevation && !IsProcessElevated())
		return QueueWindowsFirewallListenPortRules(true, false, nTcpPort, nUdpPort);
	return RunWindowsFirewallListenPortRules(true, false, bAllowElevation, nTcpPort, nUdpPort);
}

bool CemuleApp::RemoveWindowsFirewallListenPortRules(bool bAllowElevation)
{
	if (bAllowElevation && !IsProcessElevated())
		return QueueWindowsFirewallListenPortRules(false, false, 0, 0);
	return RunWindowsFirewallListenPortRules(false, false, bAllowElevation, 0, 0);
}


bool CemuleApp::ProcessCommandline()
{
	if (TryProcessFirewallMaintenanceCommandLine())
		return true;

	int nIgnoreInstanceProfile = GetProfileInt(_T("eMule"), _T("IgnoreInstance"), 0);
	bool bIgnoreRunningInstances = (nIgnoreInstanceProfile != 0);
	bIgnoreRunningInstances |= CPreferences::bIgnoreInstances;
	for (int i = 1; i < __argc; ++i) {
		LPCTSTR pszParam = __targv[i];
		if (pszParam[0] == _T('-') || pszParam[0] == _T('/')) {
			++pszParam;
#ifdef _DEBUG
			if (_tcsicmp(pszParam, _T("assertfile")) == 0)
				_CrtSetReportHook(CrtDebugReportCB);
#endif
			if (_tcsicmp(pszParam, _T("ignoreinstances")) == 0) {
				bIgnoreRunningInstances = true;
			}

			m_bAutoStart |= (_tcsicmp(pszParam, _T("AutoStart")) == 0);
		}
	}

	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);

	// If we create our TCP listen socket with SO_REUSEADDR, we have to ensure that there are
	// no 2 eMules are running on the same port.
	// NOTE: This will not prevent from some other application using that port!
	const UINT uConfiguredTcpPort = GetProfileInt(_T("eMule"), _T("Port"), 0);
	const bool bRandomizeStartupPort = GetProfileInt(_T("eMule"), _T("RandomizePortsOnStartup"), 0) != 0;
	CPreferences::SetRandomPortRange(GetProfileInt(_T("eMule"), _T("RandomPortRangeStart"), CPreferences::GetDefaultRandomPortRangeStart()), GetProfileInt(_T("eMule"), _T("RandomPortRangeEnd"), CPreferences::GetDefaultRandomPortRangeEnd()));
	UINT uTcpPort = uConfiguredTcpPort;
	if (uTcpPort == 0 || bRandomizeStartupPort) {
		uTcpPort = CPreferences::GetRandomTCPPort();
		CPreferences::SetStartupTcpPortOverride(static_cast<uint16>(uTcpPort));
	}

	const CString strMutexName(GetSingleInstanceMutexName(uConfiguredTcpPort, bRandomizeStartupPort || uConfiguredTcpPort == 0));
	m_hMutexOneInstance = CreateMutex(NULL, FALSE, strMutexName);
	DWORD dwMutexErr = GetLastError();

	const CString &command(cmdInfo.m_strFileName);

	//this code part is to determine special cases when we do add a link to our eMule
	//because in this case it would be nonsense to start another instance!
	bool bAlreadyRunning = false;
	if (bIgnoreRunningInstances
		&& cmdInfo.m_nShellCommand == CCommandLineInfo::FileOpen
		&& (command.Find(_T("://")) > 0 || command.Find(_T("magnet:?")) >= 0 || CCollection::HasCollectionExtention(command)))
	{
		bIgnoreRunningInstances = false;
	}
	HWND maininst = NULL;
	if (!bIgnoreRunningInstances)
		switch (dwMutexErr) {
		case ERROR_ALREADY_EXISTS:
		case ERROR_ACCESS_DENIED:
			bAlreadyRunning = true;
			EnumWindows(SearchEmuleWindow, (LPARAM)&maininst);
		}

	if (cmdInfo.m_nShellCommand == CCommandLineInfo::FileOpen) {
		if (command.Find(_T("://")) > 0 || command.Find(_T("magnet:?")) >= 0) {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_ED2KLINK;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
				if (maininst) {
					SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
					return true;
				}

			m_strPendingLink = command;
		} else if (CCollection::HasCollectionExtention(command)) {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_COLLECTION;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
				if (maininst) {
					SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
					return true;
				}

			m_strPendingLink = command;
		} else {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_CLCOMMAND;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
				if (maininst) {
					SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
					return true;
				}
				// Don't start if we were invoked with 'exit' command.
				if (command.CompareNoCase(_T("exit")) == 0)
					return true;
		}
	}
	return (maininst || bAlreadyRunning);
}

BOOL CALLBACK CemuleApp::SearchEmuleWindow(HWND hWnd, LPARAM lParam) noexcept
{
	DWORD_PTR dwMsgResult;
	LRESULT res = ::SendMessageTimeout(hWnd, UWM_ARE_YOU_EMULE, 0, 0, SMTO_BLOCK | SMTO_ABORTIFHUNG, SEC2MS(10), &dwMsgResult);
	if (res != 0 && dwMsgResult == UWM_ARE_YOU_EMULE) {
		*reinterpret_cast<HWND*>(lParam) = hWnd;
		return FALSE;
	}
	return TRUE;
}


void CemuleApp::UpdateReceivedBytes(uint32 bytesToAdd)
{
	SetTimeOnTransfer();
	theStats.sessionReceivedBytes += bytesToAdd;
}

void CemuleApp::UpdateSentBytes(uint32 bytesToAdd, bool sentToFriend)
{
	SetTimeOnTransfer();
	theStats.sessionSentBytes += bytesToAdd;

	if (sentToFriend)
		theStats.sessionSentBytesToFriend += bytesToAdd;
}

void CemuleApp::SetTimeOnTransfer()
{
	if (theStats.transferStarttime <= 0)
		theStats.transferStarttime = ::GetTickCount();
}

CString CemuleApp::CreateKadSourceLink(const CAbstractFile *f)
{
	CString strLink;
	if (Kademlia::CKademlia::IsConnected() && theApp.clientlist->GetServingBuddy() && theApp.IsFirewalled()) {
		CString KadID;
		Kademlia::CKademlia::GetPrefs()->GetKadID().Xor(Kademlia::CUInt128(true)).ToHexString(KadID);
		strLink.Format(_T("ed2k://|file|%s|%I64u|%s|/|kadsources,%s:%s|/")
			, (LPCTSTR)EncodeUrlUtf8(StripInvalidFilenameChars(f->GetFileName()))
			, (uint64)f->GetFileSize()
			, (LPCTSTR)EncodeBase16(f->GetFileHash(), 16)
			, (LPCTSTR)md4str(thePrefs.GetUserHash()), (LPCTSTR)KadID);
	}
	return strLink;
}

//TODO: Move to emule-window
bool CemuleApp::CopyTextToClipboard(const CString &strText)
{
	if (strText.IsEmpty())
		return false;

	HGLOBAL hGlobalT = ::GlobalAlloc(GHND | GMEM_SHARE, (strText.GetLength() + 1) * sizeof(TCHAR));
	if (hGlobalT != NULL) {
		LPTSTR pGlobalT = static_cast<LPTSTR>(::GlobalLock(hGlobalT));
		if (pGlobalT != NULL) {
			_tcscpy(pGlobalT, strText);
			::GlobalUnlock(hGlobalT);
		} else {
			::GlobalFree(hGlobalT);
			hGlobalT = NULL;
		}
	}

	CStringA strTextA(strText);
	HGLOBAL hGlobalA = ::GlobalAlloc(GHND | GMEM_SHARE, (strTextA.GetLength() + 1) * sizeof(char));
	if (hGlobalA != NULL) {
		LPSTR pGlobalA = static_cast<LPSTR>(::GlobalLock(hGlobalA));
		if (pGlobalA != NULL) {
			strcpy(pGlobalA, strTextA);
			::GlobalUnlock(hGlobalA);
		} else {
			::GlobalFree(hGlobalA);
			hGlobalA = NULL;
		}
	}

	if (hGlobalT == NULL && hGlobalA == NULL)
		return false;

	int iCopied = 0;
	if (OpenClipboard(NULL)) {
		if (EmptyClipboard()) {
			if (hGlobalT) {
				if (SetClipboardData(CF_UNICODETEXT, hGlobalT) != NULL)
					++iCopied;
				else {
					::GlobalFree(hGlobalT);
					hGlobalT = NULL;
				}
			}
			if (hGlobalA) {
				if (SetClipboardData(CF_TEXT, hGlobalA) != NULL)
					++iCopied;
				else {
					::GlobalFree(hGlobalA);
					hGlobalA = NULL;
				}
			}
		}
		CloseClipboard();
	}

	if (iCopied == 0) {
		if (hGlobalT)
			::GlobalFree(hGlobalT);
		if (hGlobalA)
			::GlobalFree(hGlobalA);
		return false;
	}

	IgnoreClipboardLinks(strText); // this is so eMule won't think the clipboard has ed2k links for adding
	return true;
}

//TODO: Move to emule-window
CString CemuleApp::CopyTextFromClipboard()
{
	bool bResult = false;
	CString strClipboard;
	if (IsClipboardFormatAvailable(CF_UNICODETEXT) && OpenClipboard(NULL)) {
		HGLOBAL hMem = GetClipboardData(CF_UNICODETEXT);
		if (hMem) {
			LPCWSTR pwsz = (LPCWSTR)::GlobalLock(hMem);
			if (pwsz) {
				strClipboard = pwsz;
				::GlobalUnlock(hMem);
				bResult = true;
			}
		}
		CloseClipboard();
	}
	if (!bResult && IsClipboardFormatAvailable(CF_TEXT) && OpenClipboard(NULL)) {
		HGLOBAL hMem = GetClipboardData(CF_TEXT);
		if (hMem != NULL) {
			LPCSTR lptstr = (LPCSTR)::GlobalLock(hMem);
			if (lptstr != NULL) {
				strClipboard = lptstr;
				::GlobalUnlock(hMem);
			}
		}
		CloseClipboard();
	}
	return strClipboard;
}

void CemuleApp::OnlineSig() // Added By Bouc7
{
	if (!thePrefs.IsOnlineSignatureEnabled())
		return;

	static LPCTSTR const _szFileName = _T("onlinesig.dat");
	const CString &strSigPath(thePrefs.GetMuleDirectory(EMULE_CONFIGBASEDIR) + _szFileName);

	char buffer[20];
	CStringA strPayload;
	CStringA strBuff;
	if (IsConnected()) {
		strPayload += "1|";
		if (serverconnect->IsConnected())
			strBuff = serverconnect->GetCurrentServer()->GetListName();
		else
			strBuff = "Kademlia";
		strPayload += strBuff;

		strPayload += "|";
		if (serverconnect->IsConnected())
			strBuff = serverconnect->GetCurrentServer()->GetAddress();
		else
			strBuff = "0.0.0.0";
		strPayload += strBuff;

		strPayload += "|";
		if (serverconnect->IsConnected()) {
			_itoa(serverconnect->GetCurrentServer()->GetPort(), buffer, 10);
			strPayload += buffer;
		} else
			strPayload += "0";
	} else
		strPayload += "0";
	strPayload += "\n";

	UINT uUploadDatarate = 0;
	UINT uDownloadDatarate = 0;
	GetDisplayedTransferRates(uUploadDatarate, uDownloadDatarate);

	snprintf(buffer, _countof(buffer), "%.1f", (float)uDownloadDatarate / 1024);
	strPayload += buffer;
	strPayload += "|";

	snprintf(buffer, _countof(buffer), "%.1f", (float)uUploadDatarate / 1024);
	strPayload += buffer;
	strPayload += "|";

	_itoa((int)uploadqueue->GetWaitingUserCount(), buffer, 10);
	strPayload += buffer;

	AsyncDiskWriteData* pData = new AsyncDiskWriteData;
	pData->lGeneration = ::InterlockedIncrement(&g_lOnlineSigSaveGeneration);
	pData->plGeneration = &g_lOnlineSigSaveGeneration;
	pData->strTempPath = strSigPath + _T(".tmp");
	pData->strFinalPath = strSigPath;
	pData->strLogName = _szFileName;
	pData->strPayloadName = _T("online-signature");
	pData->eConflictPolicy = AsyncDiskWriteConflictLastSnapshotWins;
	pData->eReplacePolicy = AsyncDiskWriteReplaceFinal;
	pData->data.assign(reinterpret_cast<const BYTE*>((LPCSTR)strPayload), reinterpret_cast<const BYTE*>((LPCSTR)strPayload) + strPayload.GetLength());
	if (!CPartFileWriteThread::QueueOrWriteDiskSnapshot(pData))
		LogError(LOG_STATUSBAR, _T("%s %s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), _szFileName);
} //End Added By Bouc7

void CemuleApp::UpdateDisplayedTransferRates()
{
	m_uDisplayedUploadDatarate = uploadqueue != NULL && uploadqueue->HasActiveUploads() ? uploadqueue->GetDatarate() : 0;
	m_uDisplayedDownloadDatarate = downloadqueue != NULL && (theStats.m_dwOverallStatus & STATE_DOWNLOADING) != 0 ? downloadqueue->GetDatarate() : 0;
}

void CemuleApp::GetDisplayedTransferRates(UINT& ruUploadDatarate, UINT& ruDownloadDatarate) const
{
	ruUploadDatarate = m_uDisplayedUploadDatarate;
	ruDownloadDatarate = m_uDisplayedDownloadDatarate;
}

bool CemuleApp::GetLangHelpFilePath(CString &strResult)
{
	// Change extension for help file
	strResult = m_pszHelpFilePath;
	WORD langID = thePrefs.GetLanguageID();
	CString temp;
	if (langID == MAKELANGID(LANG_ENGLISH, SUBLANG_DEFAULT))
		langID = (WORD)(-1);
	else
		temp.Format(_T(".%u"), langID);
	int pos = strResult.ReverseFind(_T('\\'));   //CML
	if (pos < 0)
		strResult.Replace(_T(".HLP"), _T(".chm"));
	else {
		strResult.Truncate(pos);
		strResult.AppendFormat(_T("\\eMule%s.chm"), (LPCTSTR)temp);
	}
	bool bFound = ::PathFileExists(strResult);
	if (!bFound && langID > 0) {
		strResult = m_pszHelpFilePath; // if not exists, use original help (English)
		strResult.Replace(_T(".HLP"), _T(".chm"));
	}
	return bFound;
}

void CemuleApp::SetHelpFilePath(LPCTSTR pszHelpFilePath)
{
	free((void*)m_pszHelpFilePath);
	m_pszHelpFilePath = _tcsdup(pszHelpFilePath);
}

void CemuleApp::OnHelp()
{
	if (m_dwPromptContext != 0) {
		// do not call WinHelp when the error is failing to lauch help
		if (m_dwPromptContext != HID_BASE_PROMPT + AFX_IDP_FAILED_TO_LAUNCH_HELP)
			ShowHelp(m_dwPromptContext);
		return;
	}
	ShowHelp(0, HELP_CONTENTS);
}

void CemuleApp::ShowHelp(UINT uTopic, UINT uCmd)
{
	CString strHelpFilePath;
	if (GetLangHelpFilePath(strHelpFilePath) || !ShowWebHelp(uTopic)) {
		SetHelpFilePath(strHelpFilePath);
		WinHelpInternal(uTopic, uCmd);
	}
}

bool CemuleApp::ShowWebHelp(UINT uTopic)
{
	CString strHelpURL;
	strHelpURL.Format(_T("https://onlinehelp.emule-project.net/help.php?language=%u&topic=%u"), thePrefs.GetLanguageID(), uTopic);
	BrowserOpen(strHelpURL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
	return true;
}

int CemuleApp::GetFileTypeSystemImageIdx(LPCTSTR pszFilePath, int iLength /* = -1 */, bool bNormalsSize)
{
	CSingleLock lock(&m_fileTypeSystemImageLock, TRUE);
	DWORD dwFileAttributes;
	LPCTSTR pszCacheExt;
	if (iLength == -1)
		iLength = (int)_tcslen(pszFilePath);
	if (iLength > 0 && (pszFilePath[iLength - 1] == _T('\\') || pszFilePath[iLength - 1] == _T('/'))) {
		// it's a directory
		pszCacheExt = _T("\\");
		dwFileAttributes = FILE_ATTRIBUTE_DIRECTORY;
	} else {
		dwFileAttributes = FILE_ATTRIBUTE_NORMAL;
		// search last '.' character *after* the last '\\' character
		pszCacheExt = EMPTY; //default is an empty extension
		for (int i = iLength; --i >= 0;) {
			if (pszFilePath[i] == _T('\\') || pszFilePath[i] == _T('/'))
				break;
			if (pszFilePath[i] == _T('.')) {
				// point to 1st character of extension (skip the '.')
				pszCacheExt = &pszFilePath[i + 1];
				break;
			}
		}
	}

	// Search extension in "ext->idx" cache.
	LPVOID vData;
	if (bNormalsSize) {
		if (!m_aBigExtToSysImgIdx.Lookup(pszCacheExt, vData)) {
			// Get index for the system's big icon image list
			SHFILEINFO sfi;
			HIMAGELIST hResult = (HIMAGELIST)::SHGetFileInfo(pszFilePath, dwFileAttributes, &sfi, sizeof(sfi), SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX);
			if (hResult == 0)
				return 0;
			ASSERT(m_hBigSystemImageList == NULL || m_hBigSystemImageList == hResult);
			m_hBigSystemImageList = hResult;

			// Store icon index in local cache
			m_aBigExtToSysImgIdx[pszCacheExt] = (LPVOID)sfi.iIcon;
			return sfi.iIcon;
		}
	} else if (!m_aExtToSysImgIdx.Lookup(pszCacheExt, vData)) {
		// Get index for the system's small icon image list
		SHFILEINFO sfi;
		HIMAGELIST hResult = (HIMAGELIST)::SHGetFileInfo(pszFilePath, dwFileAttributes, &sfi, sizeof(sfi)
			, SHGFI_USEFILEATTRIBUTES | SHGFI_SYSICONINDEX | SHGFI_SMALLICON);
		if (hResult == 0)
			return 0;
		ASSERT(m_hSystemImageList == NULL || m_hSystemImageList == hResult);
		m_hSystemImageList = hResult;

		// Store icon index in local cache
		m_aExtToSysImgIdx[pszCacheExt] = (LPVOID)sfi.iIcon;
		return sfi.iIcon;
	}

	// Return already cached value
	return reinterpret_cast<int>(vData);
}

bool CemuleApp::IsConnected(bool bIgnoreEd2k, bool bIgnoreKad)
{
	return (!bIgnoreEd2k && theApp.serverconnect->IsConnected()) || (!bIgnoreKad && Kademlia::CKademlia::IsConnected());
}

bool CemuleApp::IsPortchangeAllowed()
{
	return theApp.clientlist->GetClientCount() == 0 && !IsConnected();
}

uint32 CemuleApp::GetID()
{
	if (Kademlia::CKademlia::IsConnected() && !Kademlia::CKademlia::IsFirewalled())
		return ntohl(Kademlia::CKademlia::GetIPAddress());
	if (theApp.serverconnect->IsConnected())
		return theApp.serverconnect->GetClientID();
	return static_cast<uint32>(Kademlia::CKademlia::IsConnected() && Kademlia::CKademlia::IsFirewalled());
}

uint32 CemuleApp::GetED2KPublicIPv4() const
{
	return m_dwPublicIP;
}

uint32 CemuleApp::GetPublicIPv4() const
{
	if (m_dwPublicIP == 0 && Kademlia::CKademlia::IsConnected()) {
		uint32 uIP = Kademlia::CKademlia::GetIPAddress();
		if (uIP)
			return ntohl(uIP);
	}
	return m_dwPublicIP;
}


void CemuleApp::SetPublicIPv4(const uint32 dwIP)
{
	if (dwIP)
		ASSERT(!::IsLowID(dwIP));

	if (dwIP != m_dwPublicIP) {
		thePrefs.ClearEServerDiscoveredExternalUdpPort();
		m_dwPublicIP = dwIP;
		if (dwIP && clientudp != NULL)
			clientudp->Rebind();

		if (dwIP && dwIP != m_dwLastValidIPv4) {
			if (m_bFirstIPv4)
				AddLogLine(true, GetResString(_T("PUBLIC_IP_FOUND")), _T("IPv4"), ipstr(dwIP));
			else
				AddLogLine(true, GetResString(_T("PUBLIC_IP_UPDATED")), _T("IPv4"), ipstr(dwIP));

			if (serverlist != NULL)
				serverlist->CheckForExpiredUDPKeys();

			if (!m_bFirstIPv4 && (thePrefs.IsRASAIC() || thePrefs.IsIQCAOC()))
				clientlist->TrigReask(false); // All sources would be informed during their next session refresh (with TCP) about the new IP.

			m_dwLastValidIPv4 = dwIP;
			m_bFirstIPv4 = false;
		}
	}

	if (dwIP && Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::GetPrefs() != NULL) {
		const uint32 uKadIP = Kademlia::CKademlia::GetPrefs()->GetIPAddress();
		if (uKadIP == 0)
			Kademlia::CKademlia::GetPrefs()->ForceIPAddress(htonl(dwIP));
		else if (htonl(uKadIP) != dwIP) {
			AddDebugLogLine(DLP_DEFAULT, false, _T("Public IPv4 Address reported by Kademlia (%s) differs from new-found (%s)"), (LPCTSTR)ipstr(htonl(uKadIP)), (LPCTSTR)ipstr(dwIP));
			clientlist->ClearAllServedBuddies(); // Clear served-buddy pointers before Kad restart.
			Kademlia::CKademlia::Stop();
			Kademlia::CKademlia::Start();
			if (Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::GetPrefs() != NULL)
				Kademlia::CKademlia::GetPrefs()->ForceIPAddress(htonl(dwIP));
		}
	}
}

void CemuleApp::SetPublicIPv6(const CAddress& IP)
{
	if (IP.GetType() != CAddress::IPv4 && IP != m_PublicIPv6) { // GetType() == None is possible and valid when we set IPv6 to NULL.
		m_PublicIPv6 = IP;

		if (!IP.IsNull() && clientudp != NULL)
			clientudp->Rebind();

		if (!IP.IsNull() && IP != m_LastValidIPv6) {
			if (m_bFirstIPv6)
				AddLogLine(true, GetResString(_T("PUBLIC_IP_FOUND")), _T("IPv6"), ipstr(IP));
			else {
				AddLogLine(true, GetResString(_T("PUBLIC_IP_UPDATED")), _T("IPv6"), ipstr(IP));
				if ((thePrefs.IsRASAIC() || thePrefs.IsIQCAOC()) && !IP.IsNull())
					clientlist->TrigReask(true); // All sources would be informed during their next session refresh (with TCP) about the new IP.
			}

			m_LastValidIPv6 = IP;
			m_bFirstIPv6 = false;
		}
	}
}

void CemuleApp::UpdatePublicIPv6()
{
	CAddress configuredBindAddress;
	if (thePrefs.GetActiveBindResolveResult() == NBR_Resolved && !thePrefs.GetActiveConfiguredBindAddr().IsEmpty()
		&& CNetBind::TryParseAddress(thePrefs.GetActiveConfiguredBindAddr(), configuredBindAddress)
		&& configuredBindAddress.GetType() == CAddress::IPv6 && configuredBindAddress.IsPublicIP()) {
		if (configuredBindAddress != GetPublicIPv6())
			SetPublicIPv6(configuredBindAddress);
		return;
	}

	/* https://learn.microsoft.com/en-us/windows/win32/api/iphlpapi/nf-iphlpapi-getadaptersaddresses
	To determine the memory needed to return the IP_ADAPTER_ADDRESSES structures pointed to by the AdapterAddresses parameter is to pass
	too small a buffer size as indicated in the SizePointer parameter in the first call to the GetAdaptersAddresses function, so the function
	will fail with ERROR_BUFFER_OVERFLOW.When the return value is ERROR_BUFFER_OVERFLOW, the SizePointer parameter returned points to the
	required size of the buffer to hold the adapter information. */
	ULONG outBufLen = 0;
	DWORD dwRetVal = GetAdaptersAddresses(AF_INET6/*AF_UNSPEC*/, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_TUNNEL_BINDINGORDER, NULL, NULL, &outBufLen);
	PIP_ADAPTER_ADDRESSES pAddresses = (IP_ADAPTER_ADDRESSES*)malloc(outBufLen);
	if (GetAdaptersAddresses(AF_INET6/*AF_UNSPEC*/, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen) == NO_ERROR) {
		CAddress IPv6;
		bool m_bNewIPv6Found = false;
		bool m_bCurrentIPv6Found = false;
		bool m_LastReceivedIPv6Found = false;
		CList<CAddress> possibleIPv6Addresses;
		if (GetAdaptersAddresses(AF_INET6/*AF_UNSPEC*/, GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER | GAA_FLAG_INCLUDE_TUNNEL_BINDINGORDER, NULL, pAddresses, &outBufLen) == NO_ERROR)	{
			for (PIP_ADAPTER_ADDRESSES pCurrAddresses = pAddresses; pCurrAddresses; pCurrAddresses = pCurrAddresses->Next) {
				if (!CNetBind::InterfaceMatchesAdapter(thePrefs.GetActiveBindInterfaceId(), thePrefs.GetActiveBindInterfaceName(), pCurrAddresses))
					continue;
				for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurrAddresses->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next) {
					if (pUnicast->Address.lpSockaddr->sa_family == AF_INET6) {
						IPv6.FromSA(pUnicast->Address.lpSockaddr, pUnicast->Address.iSockaddrLength);
						if (IPv6.IsPublicIP())
							possibleIPv6Addresses.AddTail(IPv6);
					}
				}
			}
		}

		if (!possibleIPv6Addresses.IsEmpty()) {
			if (possibleIPv6Addresses.GetCount() == 1) {
				IPv6 = possibleIPv6Addresses.GetHead();
				if (IPv6 != GetPublicIPv6())
					m_bNewIPv6Found = true;
			} else {
				IPv6 = CAddress();
				for (POSITION pos = possibleIPv6Addresses.GetHeadPosition(); pos;) {
					CAddress tmpIPv6 = possibleIPv6Addresses.GetNext(pos);
					if (tmpIPv6 != GetPublicIPv6()) {
						if (!m_bNewIPv6Found) { // Only take first IPv6 candidate. Otherwise we'll be calling SetPublicIPv6 SetPublicIPv6.
							m_bNewIPv6Found = true;
							IPv6 = tmpIPv6;
						}
						// We'll continue to the loop until we find the most likely IPv6 by checking against m_LastReceivedIPv6 (which is set by received CT_MOD_YOUR_IP tag of hello message)
						if (m_LastReceivedIPv6.IsPublicIP() && tmpIPv6 == m_LastReceivedIPv6) {
							m_LastReceivedIPv6Found = true;
							break;
						}
					} else // We found our current IPv6
						m_bCurrentIPv6Found = true;
				}
			}

			if (m_LastReceivedIPv6Found || (!m_bCurrentIPv6Found && m_bNewIPv6Found)) // We accept if found IPv6 matches last received IPv6, otherwise we our current IPv6 should't be in the list.
				SetPublicIPv6(IPv6);
			else if (GetPublicIPv6().IsNull() && m_LastReceivedIPv6.IsPublicIP())
				SetPublicIPv6(m_LastReceivedIPv6);
		}

		possibleIPv6Addresses.RemoveAll();
	}

	if (pAddresses)
		free(pAddresses);
}

bool CemuleApp::IsFirewalled()
{
	if (theApp.serverconnect->IsConnected() && !theApp.serverconnect->IsLowID())
		return false; // we have an eD2K HighID -> not firewalled

	if (Kademlia::CKademlia::IsConnected() && !Kademlia::CKademlia::IsFirewalled())
		return false; // we have a Kad HighID -> not firewalled

	return true; // firewalled
}

bool CemuleApp::CanDoCallback(CUpDownClient *client)
{
	bool ed2k = theApp.serverconnect->IsConnected();
	bool eLow = theApp.serverconnect->IsLowID();

	// Special NAT-T check for LowID <-> LowID (rendezvous)
	// If both sides are firewalled and remote has serving buddy, rendezvous is possible
	// IMPORTANT: Only allow this for LowID <-> LowID! HighID should connect directly, not via callback.
	if (!Kademlia::CKademlia::IsConnected() || Kademlia::CKademlia::IsFirewalled()) {
		// We are firewalled
		// Check 1: KAD Buddy mechanism (original)
		if (thePrefs.IsEnableNatTraversal() && client->HasValidServingBuddyID() && Kademlia::CKademlia::IsConnected()) {
			// Remote has serving buddy and NAT-T enabled -> rendezvous possible for LowID <-> LowID
			return true;
		}

		// Check 2: eServer Buddy mechanism (new)
		// If we have an eServer Buddy and the remote client is on the same server, callback is possible
		if (thePrefs.IsEnableNatTraversal() && ed2k && eLow) {
			// Check if we have an eServer Buddy
			CUpDownClient* pEServerBuddy = theApp.clientlist->GetServingEServerBuddy();
			if (pEServerBuddy && theApp.clientlist->IsValidClient(pEServerBuddy)
				&& theApp.clientlist->GetEServerBuddyStatus() != Disconnected) {
				// We have an eServer Buddy - check if remote client is on the same server
				if (theApp.serverconnect && theApp.serverconnect->GetCurrentServer()) {
					CServer* pCurServer = theApp.serverconnect->GetCurrentServer();
					if (client->GetServerIP() == pCurServer->GetIP() && client->GetServerPort() == pCurServer->GetPort()) {
						// Same server - callback possible via eServer
						if (thePrefs.GetLogNatTraversalEvents()) {
							DebugLog(_T("[eServerBuddy] CanDoCallback: Allowing callback via eServer for LowID client on same server: %s"), (LPCTSTR)EscPercent(client->DbgGetClientInfo()));
						}
						return true;
					}
				}
			}
		}

		return ed2k && !eLow; //callback for high ID server connection
	}

	//KAD is connected and Open (we are HighID)
	//Special case of a low ID server connection
	//If the client connects to the same server, we prevent callback
	//as it breaks the protocol and will get us banned.
	if ((ed2k & eLow) != 0) {
		const CServer *srv = theApp.serverconnect->GetCurrentServer();
		return (client->GetServerIP() != srv->GetIP() || client->GetServerPort() != srv->GetPort());
	}
	return true;
}

HICON CemuleApp::LoadIcon(UINT nIDResource) const
{
	// use string resource identifiers!!
	return CWinApp::LoadIcon(nIDResource);
}

HICON CemuleApp::LoadIcon(LPCTSTR lpszResourceName, int cx, int cy, UINT uFlags) const
{
	// Test using of 16 color icons. If 'LR_VGACOLOR' is specified _and_ the icon resource
	// contains a 16 color version, that 16 color version will be loaded. If there is no
	// 16 color version available, Windows will use the next (better) color version found.
#ifdef _DEBUG
	if (g_bLowColorDesktop)
		uFlags |= LR_VGACOLOR;
#endif

	HICON hIcon = NULL;
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		// load icon resource file specification from skin profile
		TCHAR szSkinResource[MAX_PATH];
		GetPrivateProfileString(_T("Icons"), lpszResourceName, NULL, szSkinResource, _countof(szSkinResource), sSkinProfile);
		if (szSkinResource[0] != _T('\0')) {
			// expand any optional available environment strings
			TCHAR szExpSkinRes[MAX_PATH];
			if (::ExpandEnvironmentStrings(szSkinResource, szExpSkinRes, _countof(szExpSkinRes)) != 0) {
				_tcsncpy(szSkinResource, szExpSkinRes, _countof(szSkinResource));
				szSkinResource[_countof(szSkinResource) - 1] = _T('\0');
			}

			// create absolute path to icon resource file
			TCHAR szFullResPath[MAX_PATH];
			if (::PathIsRelative(szSkinResource)) {
				TCHAR szSkinResFolder[MAX_PATH];
				_tcsncpy(szSkinResFolder, sSkinProfile, _countof(szSkinResFolder));
				szSkinResFolder[_countof(szSkinResFolder) - 1] = _T('\0');
				::PathRemoveFileSpec(szSkinResFolder);
				_tmakepathlimit(szFullResPath, NULL, szSkinResFolder, szSkinResource, NULL);
			} else {
				_tcsncpy(szFullResPath, szSkinResource, _countof(szFullResPath));
				szFullResPath[_countof(szFullResPath) - 1] = _T('\0');
			}

			// check for optional icon index or resource identifier within the icon resource file
			bool bExtractIcon = false;
			CString strFullResPath(szFullResPath);
			int iIconIndex = 0;
			int iComma = strFullResPath.ReverseFind(_T(','));
			if (iComma >= 0) {
				bExtractIcon |= (_stscanf(CPTR(strFullResPath, iComma + 1), _T("%d"), &iIconIndex) == 1);
				strFullResPath.Truncate(iComma);
			}

			if (bExtractIcon) {
				if (uFlags != 0 || !(cx == cy && (cx == 16 || cx == 32))) {
					UINT uIconId;
					::PrivateExtractIcons(strFullResPath, iIconIndex, cx, cy, &hIcon, &uIconId, 1, uFlags);
				}

				if (hIcon == NULL) {
					HICON aIconsLarge[1], aIconsSmall[1];
					int iExtractedIcons = ExtractIconEx(strFullResPath, iIconIndex, aIconsLarge, aIconsSmall, 1);
					if (iExtractedIcons > 0) { // 'iExtractedIcons' is 2(!) if we get a large and a small icon
						// alway try to return the icon size which was requested
						if (cx == 16 && aIconsSmall[0] != NULL) {
							hIcon = aIconsSmall[0];
							aIconsSmall[0] = NULL;
						} else if (cx == 32 && aIconsLarge[0] != NULL) {
							hIcon = aIconsLarge[0];
							aIconsLarge[0] = NULL;
						} else {
							if (aIconsSmall[0] != NULL) {
								hIcon = aIconsSmall[0];
								aIconsSmall[0] = NULL;
							} else if (aIconsLarge[0] != NULL) {
								hIcon = aIconsLarge[0];
								aIconsLarge[0] = NULL;
							}
						}

						DestroyIconsArr(aIconsLarge, _countof(aIconsLarge));
						DestroyIconsArr(aIconsSmall, _countof(aIconsSmall));
					}
				}
			} else {
				// WINBUG???: 'ExtractIcon' does not work well on ICO-files when using the color
				// scheme 'Windows-Standard (extragroß)' -> always try to use 'LoadImage'!
				//
				// If the ICO file contains a 16x16 icon, 'LoadImage' will though return a 32x32 icon,
				// if LR_DEFAULTSIZE is specified! -> always specify the requested size!
				hIcon = (HICON)::LoadImage(NULL, szFullResPath, IMAGE_ICON, cx, cy, uFlags | LR_LOADFROMFILE);
				if (hIcon == NULL && ::GetLastError() != ERROR_PATH_NOT_FOUND/* && g_bGdiPlusInstalled*/) {
					// NOTE: Do *NOT* forget to specify /DELAYLOAD:gdiplus.dll as link parameter.
					ULONG_PTR gdiplusToken = 0;
					Gdiplus::GdiplusStartupInput gdiplusStartupInput;
					if (Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusStartupInput, NULL) == Gdiplus::Ok) {
						Gdiplus::Bitmap bmp(szFullResPath);
						bmp.GetHICON(&hIcon);
					}
					Gdiplus::GdiplusShutdown(gdiplusToken);
				}
			}
		}
	}

	if (hIcon == NULL) {
		if (cx != LR_DEFAULTSIZE || cy != LR_DEFAULTSIZE || uFlags != LR_DEFAULTCOLOR)
			hIcon = (HICON)::LoadImage(AfxGetResourceHandle(), lpszResourceName, IMAGE_ICON, cx, cy, uFlags);
		if (hIcon == NULL && !IS_INTRESOURCE(lpszResourceName)) {
			CString strUpperResourceName(lpszResourceName);
			strUpperResourceName.MakeUpper();
			if (strUpperResourceName.Compare(lpszResourceName) != 0) {
				if (cx != LR_DEFAULTSIZE || cy != LR_DEFAULTSIZE || uFlags != LR_DEFAULTCOLOR)
					hIcon = (HICON)::LoadImage(AfxGetResourceHandle(), strUpperResourceName, IMAGE_ICON, cx, cy, uFlags);
				if (hIcon == NULL)
					hIcon = CWinApp::LoadIcon(strUpperResourceName);
			}
		}
		if (hIcon == NULL) {
			//TODO: Either do not use that function or copy the returned icon. All the calling code is designed
			// in a way that the icons returned by this function are to be freed with 'DestroyIcon'. But an
			// icon which was loaded with 'LoadIcon', is not be freed with 'DestroyIcon'.
			// Right now, we never come here...
			hIcon = CWinApp::LoadIcon(lpszResourceName);

		}
	}

	return hIcon;
}

HBITMAP CemuleApp::LoadImage(LPCTSTR lpszResourceName, LPCTSTR pszResourceType) const
{
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		// load resource file specification from skin profile
		TCHAR szSkinResource[MAX_PATH];
		GetPrivateProfileString(_T("Bitmaps"), lpszResourceName, NULL, szSkinResource, _countof(szSkinResource), sSkinProfile);
		if (szSkinResource[0] != _T('\0')) {
			// expand any optional available environment strings
			TCHAR szExpSkinRes[MAX_PATH];
			if (::ExpandEnvironmentStrings(szSkinResource, szExpSkinRes, _countof(szExpSkinRes)) != 0) {
				_tcsncpy(szSkinResource, szExpSkinRes, _countof(szSkinResource));
				szSkinResource[_countof(szSkinResource) - 1] = _T('\0');
			}

			// create absolute path to resource file
			TCHAR szFullResPath[MAX_PATH];
			if (::PathIsRelative(szSkinResource)) {
				TCHAR szSkinResFolder[MAX_PATH];
				_tcsncpy(szSkinResFolder, sSkinProfile, _countof(szSkinResFolder));
				szSkinResFolder[_countof(szSkinResFolder) - 1] = _T('\0');
				::PathRemoveFileSpec(szSkinResFolder);
				_tmakepathlimit(szFullResPath, NULL, szSkinResFolder, szSkinResource, NULL);
			} else {
				_tcsncpy(szFullResPath, szSkinResource, _countof(szFullResPath));
				szFullResPath[_countof(szFullResPath) - 1] = _T('\0');
			}

			CEnBitmap bmp;
			if (bmp.LoadImage(szFullResPath))
				return (HBITMAP)bmp.Detach();
		}
	}

	CEnBitmap bmp;
	return bmp.LoadImage(lpszResourceName, pszResourceType) ? (HBITMAP)bmp.Detach() : NULL;
}

CString CemuleApp::GetSkinFileItem(LPCTSTR lpszResourceName, LPCTSTR pszResourceType) const
{
	TCHAR szFullResPath[MAX_PATH];
	*szFullResPath = _T('\0');
	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		// load resource file specification from skin profile
		TCHAR szSkinResource[MAX_PATH];
		GetPrivateProfileString(pszResourceType, lpszResourceName, NULL, szSkinResource, _countof(szSkinResource), sSkinProfile);
		if (szSkinResource[0] != _T('\0')) {
			// expand any optional available environment strings
			TCHAR szExpSkinRes[MAX_PATH];
			if (::ExpandEnvironmentStrings(szSkinResource, szExpSkinRes, _countof(szExpSkinRes)) != 0) {
				_tcsncpy(szSkinResource, szExpSkinRes, _countof(szSkinResource));
				szSkinResource[_countof(szSkinResource) - 1] = _T('\0');
			}

			// create absolute path to resource file
			if (::PathIsRelative(szSkinResource)) {
				TCHAR szSkinResFolder[MAX_PATH];
				_tcsncpy(szSkinResFolder, sSkinProfile, _countof(szSkinResFolder));
				szSkinResFolder[_countof(szSkinResFolder) - 1] = _T('\0');
				::PathRemoveFileSpec(szSkinResFolder);
				_tmakepathlimit(szFullResPath, NULL, szSkinResFolder, szSkinResource, NULL);
			} else {
				_tcsncpy(szFullResPath, szSkinResource, _countof(szFullResPath));
				szFullResPath[_countof(szFullResPath) - 1] = _T('\0');
			}
		}
	}
	return CString(szFullResPath);
}

bool CemuleApp::LoadSkinColor(LPCTSTR pszKey, COLORREF &crColor) const
{
	if (IsDarkModeEnabled()) {
		CString strKey = pszKey;
		if (strKey.Right(2).CompareNoCase(_T("Fg")) == 0) // Foreground color
			crColor = GetCustomSysColor(COLOR_WINDOWTEXT);
		else if (strKey.Right(2).CompareNoCase(_T("Bk")) == 0) // Background color
			crColor = GetCustomSysColor(COLOR_WINDOW);
		return true;
	}

	const CString &sSkinProfile(thePrefs.GetSkinProfile());
	if (!sSkinProfile.IsEmpty()) {
		TCHAR szColor[MAX_PATH];
		GetPrivateProfileString(_T("Colors"), pszKey, NULL, szColor, _countof(szColor), sSkinProfile);
		if (szColor[0] != _T('\0')) {
			int red, grn, blu;
			if (_stscanf(szColor, _T("%i , %i , %i"), &red, &grn, &blu) == 3) {
				crColor = RGB(red, grn, blu);
				return true;
			}
		}
	}
	return false;
}

bool CemuleApp::LoadSkinColorAlt(LPCTSTR pszKey, LPCTSTR pszAlternateKey, COLORREF &crColor) const
{
	return LoadSkinColor(pszKey, crColor) || LoadSkinColor(pszAlternateKey, crColor);
}

void CemuleApp::ApplySkin(LPCTSTR pszSkinProfile)
{
	thePrefs.SetSkinProfile(pszSkinProfile);

	CWnd* pMainWnd = AfxGetMainWnd();
	if (pMainWnd == NULL)
		return;

	const HWND hMainWnd = pMainWnd->GetSafeHwnd();
	if (hMainWnd == NULL || !::IsWindow(hMainWnd) || IsClosing())
		return;

	pMainWnd->SendMessage(WM_SYSCOLORCHANGE);
}

CTempIconLoader::CTempIconLoader(LPCTSTR pszResourceID, int cx, int cy, UINT uFlags)
{
	m_hIcon = theApp.LoadIcon(pszResourceID, cx, cy, uFlags);
}

CTempIconLoader::CTempIconLoader(UINT uResourceID, int /*cx*/, int /*cy*/, UINT uFlags)
{
	UNREFERENCED_PARAMETER(uFlags);
	ASSERT(uFlags == 0);
	m_hIcon = theApp.LoadIcon(uResourceID);
}

CTempIconLoader::~CTempIconLoader()
{
	if (m_hIcon)
		VERIFY(::DestroyIcon(m_hIcon));
}

namespace
{
	const DWORD kTimeBudgetDefaultSliceMs = 8;
	const DWORD kTimeBudgetFastSliceMs = 4;
	const DWORD kTimeBudgetHardSliceMs = 12;
	const DWORD kTimeBudgetProgressTraceMs = 250;

	struct CStringNoCaseLess
	{
		bool operator()(const CString &left, const CString &right) const { return left.CompareNoCase(right) < 0; }
	};

	const UINT kLargeDownloadBatchThreshold = 256;
	const size_t kBackendDownloadRemoveRowFlushThreshold = 1024;

	UINT CountEd2kFileLinkPrefixes(const CString &strLinks)
	{
		static const TCHAR szFilePrefix[] = _T("ed2k://|file|");
		static const int iFilePrefixLen = _countof(szFilePrefix) - 1;
		UINT uCount = 0;
		LPCTSTR pszLinks = strLinks;
		if (pszLinks == NULL)
			return 0;
		for (int i = 0, iLength = strLinks.GetLength(); i <= iLength - iFilePrefixLen; ++i) {
			if ((pszLinks[i] == _T('e') || pszLinks[i] == _T('E')) && _tcsnicmp(pszLinks + i, szFilePrefix, iFilePrefixLen) == 0) {
				if (uCount != UINT_MAX)
					++uCount;
				i += iFilePrefixLen - 1;
			}
		}
		return uCount;
	}

	LPCTSTR GetTimeBudgetedSliceName(CemuleApp::ETimeBudgetedSliceKind eKind)
	{
		switch (eKind) {
			case CemuleApp::TimeBudgetBackendCommandDispatch:	return _T("backend-command");
			case CemuleApp::TimeBudgetApplicationEventDispatch:	return _T("application-event");
			case CemuleApp::TimeBudgetDownloadParse:	return _T("download-parse");
			case CemuleApp::TimeBudgetDownloadAdd:	return _T("download-add");
			case CemuleApp::TimeBudgetSearchResultDownload:	return _T("search-result-download");
			case CemuleApp::TimeBudgetSearchIngest:	return _T("search-ingest");
			case CemuleApp::TimeBudgetSearchResultRemove:	return _T("search-result-remove");
			case CemuleApp::TimeBudgetSearchRedraw:	return _T("search-redraw");
			case CemuleApp::TimeBudgetUiNotificationApply:	return _T("ui-notification-apply");
			case CemuleApp::TimeBudgetStartupApply:	return _T("startup-apply");
			case CemuleApp::TimeBudgetDownloadRemove:	return _T("download-remove");
			case CemuleApp::TimeBudgetDownloadState:	return _T("download-state");
			case CemuleApp::TimeBudgetPersistenceSave:	return _T("persistence-save");
			case CemuleApp::TimeBudgetSharedFilesReload:	return _T("shared-files-reload");
			case CemuleApp::TimeBudgetSharedFilesFound:	return _T("shared-files-found");
			case CemuleApp::TimeBudgetSharedFilesBulk:	return _T("shared-files-bulk");
			case CemuleApp::TimeBudgetUploadTimerMaintenance:	return _T("upload-timer-maintenance");
		}
		return _T("unknown");
	}

	bool ShouldTraceRateLimited(volatile LONG& lLastTraceTick, DWORD dwNow, DWORD dwIntervalMs)
	{
		for (;;) {
			const LONG lPrevious = ::InterlockedCompareExchange(&lLastTraceTick, 0, 0);
			if (lPrevious != 0 && static_cast<DWORD>(dwNow - static_cast<DWORD>(lPrevious)) < dwIntervalMs)
				return false;
			if (::InterlockedCompareExchange(&lLastTraceTick, static_cast<LONG>(dwNow), lPrevious) == lPrevious)
				return true;
		}
	}

	DWORD GetRecentUserInputAgeMs(DWORD dwNow)
	{
		LASTINPUTINFO lastInput;
		memset(&lastInput, 0, sizeof(lastInput));
		lastInput.cbSize = sizeof(lastInput);
		return ::GetLastInputInfo(&lastInput) ? static_cast<DWORD>(dwNow - lastInput.dwTime) : static_cast<DWORD>(-1);
	}

	void GetChunkedDownloadUiSnapshotSliceLimits(DWORD &dwSliceBudgetMs, UINT &uMaxItemsPerSlice)
	{
		const DWORD dwNow = ::GetTickCount();
		const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT | QS_TIMER | QS_POSTMESSAGE));
		const bool bInputPending = (uQueueStatus & (QS_KEY | QS_MOUSE)) != 0;
		const bool bPaintPending = (uQueueStatus & QS_PAINT) != 0;
		const bool bDispatchPending = (uQueueStatus & (QS_TIMER | QS_POSTMESSAGE)) != 0;
		const DWORD dwInputAge = GetRecentUserInputAgeMs(dwNow);

		if (bInputPending || dwInputAge < 250) {
			dwSliceBudgetMs = 2;
			uMaxItemsPerSlice = 48;
			return;
		}
		if (bPaintPending || bDispatchPending) {
			dwSliceBudgetMs = 4;
			uMaxItemsPerSlice = 128;
			return;
		}
		if (dwInputAge < 1000) {
			dwSliceBudgetMs = 5;
			uMaxItemsPerSlice = 192;
			return;
		}

		dwSliceBudgetMs = 10;
		uMaxItemsPerSlice = 1024;
	}

	void AddTrimmedToken(CStringArray &astrLinks, const CString &strToken)
	{
		CString strLink(strToken);
		strLink.Trim();
		if (!strLink.IsEmpty())
			astrLinks.Add(strLink);
	}

	void CopyStringArray(const CStringArray &source, CStringArray &target)
	{
		for (INT_PTR i = 0; i < source.GetSize(); ++i)
			target.Add(source.GetAt(i));
	}

	void CopyStringArrayToVector(const CStringArray &source, std::vector<CString> &target)
	{
		target.clear();
		for (INT_PTR i = 0; i < source.GetSize(); ++i)
			target.push_back(source.GetAt(i));
	}

	void CanonicalizeDownloadItemHashes(CStringArray &astrItemHashes)
	{
		CStringArray astrCanonical;
		std::set<CString, CStringNoCaseLess> setHashes;
		for (INT_PTR i = 0; i < astrItemHashes.GetSize(); ++i) {
			CString strHash(astrItemHashes.GetAt(i));
			strHash.Trim();
			SDownloadItemId id;
			if (strHash.IsEmpty() || !strmd4(strHash, id.m_abyFileHash))
				continue;
			const CString strCanonical(md4str(id.m_abyFileHash));
			if (setHashes.insert(strCanonical).second)
				astrCanonical.Add(strCanonical);
		}
		astrItemHashes.RemoveAll();
		CopyStringArray(astrCanonical, astrItemHashes);
	}


	enum EBackendDownloadStateActionValue
	{
		BackendDownloadStatePermissionDefault,
		BackendDownloadStatePermissionNone,
		BackendDownloadStatePermissionFriends,
		BackendDownloadStatePermissionAll,
		BackendDownloadStatePriorityHigh,
		BackendDownloadStatePriorityLow,
		BackendDownloadStatePriorityNormal,
		BackendDownloadStatePriorityAuto,
		BackendDownloadStatePause,
		BackendDownloadStateResume,
		BackendDownloadStateStop,
		BackendDownloadStateSetSourceLimit,
		BackendDownloadStateSetCategory,
		BackendDownloadStateSetPauseOnPreview,
		BackendDownloadStateToggleAutoRenameToMajorityName,
		BackendDownloadStateCleanupFilename,
		BackendDownloadStateClearCompleted,
		BackendDownloadStateSetFileName,
		BackendDownloadStateTogglePreviewPriority,
		BackendDownloadStateImportParts,
		BackendDownloadStateSetCategoryPriority
	};

	bool IsBackendDownloadChangeStateOwnerSafeAction(UINT uAction)
	{
		switch (uAction) {
			case BackendDownloadStatePermissionDefault:
			case BackendDownloadStatePermissionNone:
			case BackendDownloadStatePermissionFriends:
			case BackendDownloadStatePermissionAll:
			case BackendDownloadStatePriorityHigh:
			case BackendDownloadStatePriorityLow:
			case BackendDownloadStatePriorityNormal:
			case BackendDownloadStatePriorityAuto:
			case BackendDownloadStatePause:
			case BackendDownloadStateResume:
			case BackendDownloadStateStop:
			case BackendDownloadStateSetSourceLimit:
			case BackendDownloadStateSetCategory:
			case BackendDownloadStateSetPauseOnPreview:
			case BackendDownloadStateToggleAutoRenameToMajorityName:
			case BackendDownloadStateCleanupFilename:
			case BackendDownloadStateClearCompleted:
			case BackendDownloadStateSetFileName:
			case BackendDownloadStateTogglePreviewPriority:
			case BackendDownloadStateImportParts:
			case BackendDownloadStateSetCategoryPriority:
				return true;
		}
		return false;
	}


	const UINT kSharedFilesCommandToggleShareStatusAction = 0xFFFFFFFFU;

	CemuleApp::ESharedFilesCommandType GetSharedFilesCommandTypeForAction(UINT uAction)
	{
		switch (uAction) {
		case MP_VIEWPARTFILES:
		case MP_VIEWSHAREDFILES:
		case MP_VIEWDUPLICATEFILES:
			return CemuleApp::SharedFilesCommandReload;
		case MP_REMOVE:
		case MPG_DELETE:
			return CemuleApp::SharedFilesCommandBulkDelete;
		case MP_CANCEL:
		case MP_CANCEL_FORGET:
			return CemuleApp::SharedFilesCommandBulkCancelDownloads;
		case MP_UNSHAREFILE:
			return CemuleApp::SharedFilesCommandBulkUnshare;
		case MP_REMOVEFROMHISTORY:
		case MP_CLEARHISTORY:
			return CemuleApp::SharedFilesCommandBulkHistoryRemove;
		case MP_UPDATE_METADATA:
			return CemuleApp::SharedFilesCommandBulkMetadataUpdate;
		case MP_PRIOVERYLOW:
		case MP_PRIOLOW:
		case MP_PRIONORMAL:
		case MP_PRIOHIGH:
		case MP_PRIOVERYHIGH:
		case MP_PRIOAUTO:
			return CemuleApp::SharedFilesCommandBulkPriority;
		case MP_CREATECOLLECTION:
			return CemuleApp::SharedFilesCommandCreateCollection;
		case kSharedFilesCommandToggleShareStatusAction:
			return CemuleApp::SharedFilesCommandToggleShareStatus;
		}
		return CemuleApp::SharedFilesCommandSelectionAction;
	}

	bool IsSharedFilesCommandTypeValid(CemuleApp::ESharedFilesCommandType eType)
	{
		switch (eType) {
		case CemuleApp::SharedFilesCommandMenuAction:
		case CemuleApp::SharedFilesCommandSelectionAction:
		case CemuleApp::SharedFilesCommandReload:
		case CemuleApp::SharedFilesCommandBulkDelete:
		case CemuleApp::SharedFilesCommandBulkCancelDownloads:
		case CemuleApp::SharedFilesCommandBulkUnshare:
		case CemuleApp::SharedFilesCommandBulkHistoryRemove:
		case CemuleApp::SharedFilesCommandBulkMetadataUpdate:
		case CemuleApp::SharedFilesCommandBulkPriority:
		case CemuleApp::SharedFilesCommandCreateCollection:
		case CemuleApp::SharedFilesCommandToggleShareStatus:
			return true;
		}
		return false;
	}

	bool IsPersistenceCommandTypeValid(CemuleApp::EPersistenceCommandType eType)
	{
		switch (eType) {
		case CemuleApp::PersistenceCommandSaveAppState:
		case CemuleApp::PersistenceCommandSaveStats:
		case CemuleApp::PersistenceCommandSaveKnownFiles:
		case CemuleApp::PersistenceCommandSavePreferences:
		case CemuleApp::PersistenceCommandSaveFriends:
		case CemuleApp::PersistenceCommandSaveClientCredits:
		case CemuleApp::PersistenceCommandSaveServerList:
		case CemuleApp::PersistenceCommandSaveClientHistory:
		case CemuleApp::PersistenceCommandSaveSearchStore:
		case CemuleApp::PersistenceCommandSaveSearchSpam:
		case CemuleApp::PersistenceCommandSaveSharedFiles:
		case CemuleApp::PersistenceCommandSaveKadNodes:
			return true;
		}
		return false;
	}

#define BACKEND_COMMAND_SOURCE_MASK(source) (1UL << (source))
#define BACKEND_COMMAND_SCOPE_MASK(scope) (1UL << (scope))

	const DWORD kBackendCommandSourceUiWebMask =
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceUi) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceWebServer);
	const DWORD kBackendCommandSourcePersistenceMask =
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourcePersistence) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceUi) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceTimer) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceShutdown) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceDiskIo);
	const DWORD kBackendCommandSourceSharedFilesMask =
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceUi) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceWebServer) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceTimer);
	const DWORD kBackendCommandSourceUploadListUpdateMask =
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceUi) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceTimer) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceUploadModel);
	const DWORD kBackendCommandSourceSearchIngestMask =
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceTimer) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkServer) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkUdp) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkKad) |
		BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceSearchIngest);
	const DWORD kBackendCommandScopeDownloadMask =
		BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingDownloadList) |
		BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingWebRequest);
	const DWORD kBackendCommandScopeClientMask =
		BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingClient) |
		BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingUploadList);
	const DWORD kBackendCommandScopeSearchMask = BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingSearch);
	const DWORD kBackendCommandScopeFileHashMask =
		BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingFileHash) |
		BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingClient);
	const DWORD kBackendCommandScopeGlobalMask = BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingGlobal);
	const DWORD kBackendCommandScopePersistenceMask = BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingPersistence);
	const DWORD kBackendCommandScopeSharedFilesMask = BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingSharedFiles);

	const CemuleApp::SBackendCommandContract g_backendCommandContracts[] =
	{
		{
			CemuleApp::BackendCommandFamilyDownloadAddFileLinks,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingDownloadList,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourceUiWebMask,
			kBackendCommandScopeDownloadMask,
			true,
			true,
			_T("download:add-file-links")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadProcessLinks,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingDownloadList,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourceUiWebMask,
			kBackendCommandScopeDownloadMask,
			true,
			true,
			_T("download:process-links")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadRemoveItems,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingDownloadList,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceUiWebMask,
			kBackendCommandScopeDownloadMask,
			true,
			true,
			_T("download:remove-items")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadChangeState,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingDownloadList,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceUiWebMask,
			kBackendCommandScopeDownloadMask,
			true,
			true,
			_T("download:change-state")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadChangeStateOwnerSafe,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingDownloadList,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceUiWebMask,
			kBackendCommandScopeDownloadMask,
			true,
			true,
			_T("download:change-state-owner-safe")
		},
		{
			CemuleApp::BackendCommandFamilyUploadClientRowsChanged,
			CemuleApp::BackendCommandUpload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceUploadModel,
			CemuleApp::BackendCommandOrderingClient,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			kBackendCommandSourceUploadListUpdateMask,
			kBackendCommandScopeClientMask,
			false,
			true,
			_T("upload:client-rows-changed")
		},
		{
			CemuleApp::BackendCommandFamilyUploadQueueListChanged,
			CemuleApp::BackendCommandUpload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceUploadModel,
			CemuleApp::BackendCommandOrderingUploadList,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			kBackendCommandSourceUploadListUpdateMask,
			kBackendCommandScopeClientMask,
			false,
			true,
			_T("upload:queue-list-changed")
		},
		{
			CemuleApp::BackendCommandFamilyUploadListChanged,
			CemuleApp::BackendCommandUpload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceUploadModel,
			CemuleApp::BackendCommandOrderingUploadList,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			kBackendCommandSourceUploadListUpdateMask,
			kBackendCommandScopeClientMask,
			false,
			true,
			_T("upload:list-changed")
		},
		{
			CemuleApp::BackendCommandFamilyUploadBandwidthSnapshot,
			CemuleApp::BackendCommandUpload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceUploadModel,
			CemuleApp::BackendCommandOrderingUploadList,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			kBackendCommandSourceUploadListUpdateMask,
			kBackendCommandScopeClientMask,
			false,
			true,
			_T("upload:bandwidth-snapshot")
		},
		{
			CemuleApp::BackendCommandFamilyUploadDiskIoResult,
			CemuleApp::BackendCommandUpload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceDiskIo,
			CemuleApp::BackendCommandOrderingClient,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceDiskIo),
			kBackendCommandScopeClientMask,
			false,
			true,
			_T("upload:disk-io-result")
		},
		{
			CemuleApp::BackendCommandFamilySearchStart,
			CemuleApp::BackendCommandSearch,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSearch,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourceUiWebMask,
			kBackendCommandScopeSearchMask,
			true,
			true,
			_T("search:start")
		},
		{
			CemuleApp::BackendCommandFamilySearchCancel,
			CemuleApp::BackendCommandSearch,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSearch,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			kBackendCommandSourceUiWebMask,
			kBackendCommandScopeSearchMask,
			true,
			true,
			_T("search:cancel")
		},
		{
			CemuleApp::BackendCommandFamilySearchIngestApply,
			CemuleApp::BackendCommandSearch,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceTimer,
			CemuleApp::BackendCommandOrderingSearch,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			kBackendCommandSourceSearchIngestMask,
			kBackendCommandScopeSearchMask,
			true,
			true,
			_T("search:ingest-apply")
		},
		{
			CemuleApp::BackendCommandFamilySearchKnownTypeRefresh,
			CemuleApp::BackendCommandSearch,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceTimer,
			CemuleApp::BackendCommandOrderingSearch,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceTimer) | BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceUi),
			kBackendCommandScopeSearchMask,
			true,
			true,
			_T("search:known-type-refresh")
		},
		{
			CemuleApp::BackendCommandFamilyCollectionImport,
			CemuleApp::BackendCommandCollection,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingGlobal,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourceUiWebMask,
			kBackendCommandScopeGlobalMask,
			true,
			true,
			_T("collection:import")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveAppState,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:app-state")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveStats,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:stats")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveKnownFiles,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:known-files")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSavePreferences,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:preferences")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveFriends,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:friends")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveClientCredits,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:client-credits")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveServerList,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:server-list")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveClientHistory,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:client-history")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveSearchStore,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:search-store")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveSearchSpam,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:search-spam")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveSharedFiles,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:shared-files")
		},
		{
			CemuleApp::BackendCommandFamilyPersistenceSaveKadNodes,
			CemuleApp::BackendCommandPersistence,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourcePersistence,
			CemuleApp::BackendCommandOrderingPersistence,
			CemuleApp::BackendCommandFailurePolicyReport,
			kBackendCommandSourcePersistenceMask,
			kBackendCommandScopePersistenceMask,
			false,
			true,
			_T("persistence:kad-nodes")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesMenuAction,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:menu-action")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesSelectionAction,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:selection-action")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesReload,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:reload")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesBulkDelete,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:bulk-delete")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesBulkCancelDownloads,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:bulk-cancel-downloads")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesBulkUnshare,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:bulk-unshare")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesBulkHistoryRemove,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:bulk-history-remove")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesBulkMetadataUpdate,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:bulk-metadata-update")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesBulkPriority,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:bulk-priority")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesCreateCollection,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:create-collection")
		},
		{
			CemuleApp::BackendCommandFamilySharedFilesToggleShareStatus,
			CemuleApp::BackendCommandSharedFiles,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceUi,
			CemuleApp::BackendCommandOrderingSharedFiles,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			kBackendCommandSourceSharedFilesMask,
			kBackendCommandScopeSharedFilesMask,
			true,
			true,
			_T("shared-files:toggle-share-status")
		},
		{
			CemuleApp::BackendCommandFamilyNetworkClientSearchAnswer,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkClient,
			CemuleApp::BackendCommandOrderingSearch,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient),
			kBackendCommandScopeSearchMask,
			false,
			true,
			_T("network:client-search-answer")
		},
		{
			CemuleApp::BackendCommandFamilyNetworkServerSearchAnswer,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkServer,
			CemuleApp::BackendCommandOrderingSearch,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkServer),
			kBackendCommandScopeSearchMask,
			false,
			true,
			_T("network:server-search-answer")
		},
		{
			CemuleApp::BackendCommandFamilyNetworkDownloadFileStatus,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkClient,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient) | BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkUdp),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("network:download-file-status")
		},
		{
			CemuleApp::BackendCommandFamilyNetworkDownloadHashSet,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkClient,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("network:download-hashset")
		},
		{
			CemuleApp::BackendCommandFamilyNetworkDownloadFoundSources,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkServer,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkServer) | BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkUdp),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("network:download-found-sources")
		},
		{
			CemuleApp::BackendCommandFamilyNetworkDownloadSourceExchange,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkClient,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("network:download-source-exchange")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadBlockRequest,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkClient,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("download:block-request")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadBlockReceive,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkClient,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("download:block-receive")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadCorruptedBlock,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkClient,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("download:corrupted-block")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadCompletePart,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceDiskIo,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceDiskIo) | BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("download:complete-part")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadFileCompletion,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceDiskIo,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceDiskIo),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("download:file-completion")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadAichVerification,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceDiskIo,
			CemuleApp::BackendCommandOrderingFileHash,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceDiskIo) | BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkClient),
			kBackendCommandScopeFileHashMask,
			false,
			true,
			_T("download:aich-verification")
		},
		{
			CemuleApp::BackendCommandFamilyDownloadPartMetSnapshotWrite,
			CemuleApp::BackendCommandDownload,
			CemuleApp::BackendExecutorDiskIo,
			CemuleApp::BackendCommandApplyUiCompatibilityOnly,
			CemuleApp::BackendCommandSourceDiskIo,
			CemuleApp::BackendCommandOrderingDiskIo,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceDiskIo) | BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourcePersistence),
			BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingDiskIo) | BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingFileHash),
			false,
			true,
			_T("download:partmet-snapshot-write")
		},
		{
			CemuleApp::BackendCommandFamilyNetworkServerUdpSearchAnswer,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkUdp,
			CemuleApp::BackendCommandOrderingSearch,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkUdp),
			kBackendCommandScopeSearchMask,
			false,
			true,
			_T("network:server-udp-search-answer")
		},
		{
			CemuleApp::BackendCommandFamilyNetworkKadPacket,
			CemuleApp::BackendCommandNetworkPacket,
			CemuleApp::BackendExecutorBackendOwner,
			CemuleApp::BackendCommandApplyBackendOwnerSafe,
			CemuleApp::BackendCommandSourceNetworkKad,
			CemuleApp::BackendCommandOrderingKad,
			CemuleApp::BackendCommandFailurePolicyReportAndDropStale,
			BACKEND_COMMAND_SOURCE_MASK(CemuleApp::BackendCommandSourceNetworkKad),
			BACKEND_COMMAND_SCOPE_MASK(CemuleApp::BackendCommandOrderingKad),
			false,
			true,
			_T("network:kad-packet")
		}
	};
	const CemuleApp::SBackendCommandContract* FindBackendCommandContract(CemuleApp::EBackendCommandFamily eFamily)
	{
		for (int i = 0; i < static_cast<int>(sizeof(g_backendCommandContracts) / sizeof(g_backendCommandContracts[0])); ++i) {
			if (g_backendCommandContracts[i].m_eFamily == eFamily)
				return &g_backendCommandContracts[i];
		}
		return NULL;
	}

#define BACKEND_COMMAND_READINESS_ENTRY(family, executor, applyMode, readiness, enableBackendOwnerDispatch, reason) \
	{ CemuleApp::family, CemuleApp::executor, CemuleApp::applyMode, CemuleApp::readiness, enableBackendOwnerDispatch, _T(reason) }

	const CemuleApp::SBackendCommandReadiness g_backendCommandReadiness[] =
	{
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadAddFileLinks, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "download add-file-links uses owner-safe model mutations and UI list notifications"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadProcessLinks, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "download process-links adds file links on the owner lane and posts non-file link handling to the UI bridge"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadRemoveItems, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "download removal uses stable hash DTOs and removes UI rows through value-only list events"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadChangeState, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "download state changes still resolve live UI/model pointers"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadChangeStateOwnerSafe, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "owner-safe download state changes use file-hash DTOs, guarded source/Kad mutations, import-part owner work items and UI list notifications"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyUploadClientRowsChanged, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "upload row changes produce value-only UI list events on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyUploadQueueListChanged, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "upload queue changes produce value-only UI list events on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyUploadListChanged, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "upload list changes produce value-only UI list events on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyUploadBandwidthSnapshot, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "upload bandwidth snapshot produces value-only UI list events on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyUploadDiskIoResult, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, false, "upload disk IO result is value-only; dispatch deferred until final audit"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySearchStart, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "search start still depends on UI compatibility bridge"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySearchCancel, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "search cancel still depends on UI compatibility bridge"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySearchIngestApply, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "search ingest apply drains parser-produced value batches on the backend owner lane and posts search result change events"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySearchKnownTypeRefresh, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "search known-type refresh uses UI-compatible lookup tables and runs chunked on the UI lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyCollectionImport, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "collection import still posts UI compatibility events"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveAppState, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveStats, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveKnownFiles, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSavePreferences, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveFriends, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveClientCredits, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveServerList, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveClientHistory, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveSearchStore, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveSearchSpam, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveSharedFiles, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyPersistenceSaveKadNodes, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "persistence belongs to disk IO executor"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesMenuAction, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files actions still depend on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesSelectionAction, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files selection still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesReload, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files reload still uses UI compatibility continuation"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesBulkDelete, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files bulk delete still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesBulkCancelDownloads, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files bulk cancel still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesBulkUnshare, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files bulk unshare still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesBulkHistoryRemove, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files history remove still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesBulkMetadataUpdate, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files metadata update still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesBulkPriority, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files priority still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesCreateCollection, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files collection creation still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilySharedFilesToggleShareStatus, BackendExecutorBackendOwner, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "shared files share status still depends on UI control state"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyNetworkClientSearchAnswer, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "client search answer applies immutable packet snapshots on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyNetworkServerSearchAnswer, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "server search answer applies immutable packet snapshots on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyNetworkDownloadFileStatus, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "download file status applies immutable packet snapshots on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyNetworkDownloadHashSet, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "download hashset applies immutable packet snapshots on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyNetworkDownloadFoundSources, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "found-sources applies immutable packet snapshots on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyNetworkDownloadSourceExchange, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "source exchange applies immutable packet snapshots on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadBlockRequest, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "block request pending-block mutation is owner guarded and dispatch-enabled"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadBlockReceive, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "block receive applies immutable TCP block payload snapshots on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadCorruptedBlock, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "corrupted block accounting and source recovery are owner guarded and dispatch-enabled"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadCompletePart, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "complete part verification and shared-list promotion are owner guarded and dispatch-enabled"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadFileCompletion, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "file completion handoff and finalization mutations are owner guarded and dispatch-enabled"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadAichVerification, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "AICH request, answer, recovery and completion mutations are owner guarded and dispatch-enabled"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyDownloadPartMetSnapshotWrite, BackendExecutorDiskIo, BackendCommandApplyUiCompatibilityOnly, BackendCommandReadinessUiCompatibilityOnly, false, "part.met writes remain on the disk IO executor and do not enable backend owner dispatch"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyNetworkServerUdpSearchAnswer, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "server UDP search answer applies immutable packet snapshots on the backend owner lane"),
		BACKEND_COMMAND_READINESS_ENTRY(BackendCommandFamilyNetworkKadPacket, BackendExecutorBackendOwner, BackendCommandApplyBackendOwnerSafe, BackendCommandReadinessBackendOwnerReady, true, "Kad packet apply uses immutable packet snapshots and value-only UI status/list events on the backend owner lane")
	};

#undef BACKEND_COMMAND_READINESS_ENTRY

	const CemuleApp::SBackendCommandReadiness* FindBackendCommandReadiness(CemuleApp::EBackendCommandFamily eFamily)
	{
		for (int i = 0; i < static_cast<int>(sizeof(g_backendCommandReadiness) / sizeof(g_backendCommandReadiness[0])); ++i) {
			if (g_backendCommandReadiness[i].m_eFamily == eFamily)
				return &g_backendCommandReadiness[i];
		}
		return NULL;
	}

	bool IsBackendCommandReadinessConsistent(const CemuleApp::SBackendCommandReadiness &readiness)
	{
		const CemuleApp::SBackendCommandContract *pContract = FindBackendCommandContract(readiness.m_eFamily);
		if (pContract == NULL || pContract->m_eExecutorDomain != readiness.m_eExecutorDomain || pContract->m_eApplyMode != readiness.m_eApplyMode)
			return false;
		if (readiness.m_bEnableBackendOwnerDispatch) {
			const bool bBackendOwnerSafe = readiness.m_eExecutorDomain == CemuleApp::BackendExecutorBackendOwner
				&& readiness.m_eApplyMode == CemuleApp::BackendCommandApplyBackendOwnerSafe
				&& readiness.m_eReadiness == CemuleApp::BackendCommandReadinessBackendOwnerReady;
			if (!bBackendOwnerSafe)
				return false;
		}
		return true;
	}

	bool IsBackendCommandReadinessRegistryComplete()
	{
		const int iContractCount = static_cast<int>(sizeof(g_backendCommandContracts) / sizeof(g_backendCommandContracts[0]));
		const int iReadinessCount = static_cast<int>(sizeof(g_backendCommandReadiness) / sizeof(g_backendCommandReadiness[0]));
		for (int i = 0; i < iContractCount; ++i) {
			if (FindBackendCommandReadiness(g_backendCommandContracts[i].m_eFamily) == NULL)
				return false;
			for (int j = i + 1; j < iContractCount; ++j) {
				if (g_backendCommandContracts[i].m_eFamily == g_backendCommandContracts[j].m_eFamily)
					return false;
			}
		}
		for (int i = 0; i < iReadinessCount; ++i) {
			if (!IsBackendCommandReadinessConsistent(g_backendCommandReadiness[i]))
				return false;
			for (int j = i + 1; j < iReadinessCount; ++j) {
				if (g_backendCommandReadiness[i].m_eFamily == g_backendCommandReadiness[j].m_eFamily)
					return false;
			}
		}
		return true;
	}

	CDownloadListCtrl* GetDownloadListCtrlForCommandBridge()
	{
		return theApp.emuledlg != NULL && theApp.emuledlg->transferwnd != NULL ? theApp.emuledlg->transferwnd->GetDownloadList() : NULL;
	}

	void UpdateDownloadAddMirroredOverlay(UINT uDone, UINT uTotal)
	{
		if (!theApp.IsUiThread())
			return;

		CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
		if (pDownloadList == NULL || !::IsWindow(pDownloadList->GetSafeHwnd()))
			return;

		if (uTotal < BULK_OPERATION_MIN_ITEMS) {
			pDownloadList->HideMirroredSearchDownloadOverlay();
			return;
		}

		CString strDetail;
		strDetail.Format(GetResString(_T("BULKOP_PROGRESS_FINAL_RELOAD_DETAIL")), uDone, uTotal);
		pDownloadList->UpdateMirroredSearchDownloadOverlay(GetResString(_T("BULKOP_ADD_DOWNLOADS_TITLE")), strDetail, uDone, uTotal);
	}

	CPartFile* ResolveDownloadFileForCommand(const SDownloadItemId &id)
	{
		CPartFile *pFile = theApp.downloadqueue != NULL ? theApp.downloadqueue->GetFileByItemId(id) : NULL;
		if (pFile != NULL)
			return pFile;
		CDownloadListCtrl *pDownloadList = theApp.IsUiThread() ? GetDownloadListCtrlForCommandBridge() : NULL;
		return pDownloadList != NULL ? pDownloadList->ResolveDownloadItemForCommand(id) : NULL;
	}


	void RefreshDownloadListAfterCommand(UINT uAction)
	{
		if (!theApp.IsUiThread())
			return;
		CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
		if (pDownloadList != NULL)
			pDownloadList->RefreshAfterBackendDownloadCommand(uAction);
	}

	void RefreshDownloadItemFromOwnerEvent(const CString &strFileHash, bool bForce)
	{
		if (!theApp.IsUiThread() || strFileHash.IsEmpty())
			return;

		SDownloadItemId id;
		if (!strmd4(strFileHash, id.m_abyFileHash))
			return;

		CPartFile *pFile = ResolveDownloadFileForCommand(id);
		CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
		if (pFile != NULL && pDownloadList != NULL)
			pDownloadList->UpdateItem(pFile, bForce);
	}

	CSharedFilesCtrl* GetSharedFilesCtrlForCommandBridge()
	{
		return theApp.emuledlg != NULL && theApp.emuledlg->sharedfileswnd != NULL ? &theApp.emuledlg->sharedfileswnd->sharedfilesctrl : NULL;
	}

	void UpdateBackendDownloadCommandOverlays(bool bRemove, UINT uDone, UINT uTotal, uint64 uSequence, uint64 uCorrelationId)
	{
		if (!theApp.IsUiThread())
			return;
		CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
		if (pDownloadList != NULL)
			pDownloadList->UpdateBackendDownloadCommandOverlay(bRemove, uDone, uTotal, uSequence, uCorrelationId);
		if (bRemove) {
			CSharedFilesCtrl *pSharedFilesCtrl = GetSharedFilesCtrlForCommandBridge();
			if (pSharedFilesCtrl != NULL && ::IsWindow(pSharedFilesCtrl->GetSafeHwnd()))
				pSharedFilesCtrl->UpdateBackendDownloadRemoveOverlay(uDone, uTotal, uSequence, uCorrelationId);
		}
	}

	void HideBackendDownloadCommandOverlays(bool bRemove, uint64 uSequence, uint64 uCorrelationId)
	{
		if (!theApp.IsUiThread())
			return;
		CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
		if (pDownloadList != NULL)
			pDownloadList->HideBackendDownloadCommandOverlay(uSequence, uCorrelationId);
		if (bRemove) {
			CSharedFilesCtrl *pSharedFilesCtrl = GetSharedFilesCtrlForCommandBridge();
			if (pSharedFilesCtrl != NULL && ::IsWindow(pSharedFilesCtrl->GetSafeHwnd()))
				pSharedFilesCtrl->HideBackendDownloadRemoveOverlay(uSequence, uCorrelationId);
		}
	}


	class CScopedBackendCommandPtr
	{
	public:
		explicit CScopedBackendCommandPtr(CemuleApp::SBackendCommand *pCommand) : m_pCommand(pCommand) {}
		~CScopedBackendCommandPtr() { delete m_pCommand; }
		CemuleApp::SBackendCommand* Get() const { return m_pCommand; }
		CemuleApp::SBackendCommand* Release() { CemuleApp::SBackendCommand *pCommand = m_pCommand; m_pCommand = NULL; return pCommand; }
	private:
		CScopedBackendCommandPtr(const CScopedBackendCommandPtr&);
		CScopedBackendCommandPtr& operator=(const CScopedBackendCommandPtr&);
		CemuleApp::SBackendCommand *m_pCommand;
	};

	class CScopedBoolReset
	{
	public:
		CScopedBoolReset(bool &bFlag, bool bValue) : m_bFlag(bFlag), m_bPreviousValue(bFlag) { m_bFlag = bValue; }
		~CScopedBoolReset() { m_bFlag = m_bPreviousValue; }
	private:
		CScopedBoolReset(const CScopedBoolReset&);
		CScopedBoolReset& operator=(const CScopedBoolReset&);
		bool &m_bFlag;
		bool m_bPreviousValue;
	};

	class CScopedInterlockedCounter
	{
	public:
		explicit CScopedInterlockedCounter(volatile LONG &lCounter) : m_lCounter(lCounter) { ::InterlockedIncrement(&m_lCounter); }
		~CScopedInterlockedCounter() { ::InterlockedDecrement(&m_lCounter); }
	private:
		CScopedInterlockedCounter(const CScopedInterlockedCounter&);
		CScopedInterlockedCounter& operator=(const CScopedInterlockedCounter&);
		volatile LONG &m_lCounter;
	};

	class CScopedInterlockedFlag
	{
	public:
		explicit CScopedInterlockedFlag(volatile LONG &lFlag) : m_lFlag(lFlag) {}
		~CScopedInterlockedFlag() { ::InterlockedExchange(&m_lFlag, 0); }
	private:
		CScopedInterlockedFlag(const CScopedInterlockedFlag&);
		CScopedInterlockedFlag& operator=(const CScopedInterlockedFlag&);
		volatile LONG &m_lFlag;
	};

	void CopyDownloadCommand(const CemuleApp::SDownloadCommand &source, CemuleApp::SDownloadCommand &target)
	{
		target.m_eType = source.m_eType;
		target.m_iCat = source.m_iCat;
		target.m_uAction = source.m_uAction;
		target.m_iActionValue = source.m_iActionValue;
		target.m_strActionValue = source.m_strActionValue;
		target.m_bAddToCanceledMet = source.m_bAddToCanceledMet;
		target.m_bDeleteCompletedFile = source.m_bDeleteCompletedFile;
		target.m_strRawLinks = source.m_strRawLinks;
		target.m_strTokenDelimiters = source.m_strTokenDelimiters;
		target.m_astrLinks.RemoveAll();
		CopyStringArray(source.m_astrLinks, target.m_astrLinks);
		target.m_pFileSnapshots = source.m_pFileSnapshots;
		target.m_astrItemHashes.RemoveAll();
		CopyStringArray(source.m_astrItemHashes, target.m_astrItemHashes);
	}

	size_t GetDownloadCommandSnapshotCount(const CemuleApp::SDownloadCommand &command)
	{
		return command.m_pFileSnapshots.get() != NULL ? command.m_pFileSnapshots->size() : 0;
	}

	bool IsDownloadCommandLinkPayloadEmpty(const CemuleApp::SDownloadCommand &command)
	{
		return command.m_strRawLinks.IsEmpty() && command.m_astrLinks.GetSize() == 0 && GetDownloadCommandSnapshotCount(command) == 0;
	}

	void CopyUploadCommand(const CemuleApp::SUploadCommand &source, CemuleApp::SUploadCommand &target)
	{
		target.m_eType = source.m_eType;
		target.m_uRuntimeID = source.m_uRuntimeID;
		target.m_lRuntimeGeneration = source.m_lRuntimeGeneration;
		target.m_uTargetFlags = source.m_uTargetFlags;
		target.m_uWaitingCount = source.m_uWaitingCount;
		target.m_uUploadingCount = source.m_uUploadingCount;
		target.m_uActiveUploadCount = source.m_uActiveUploadCount;
		target.m_uDataRate = source.m_uDataRate;
		target.m_uToNetworkDataRate = source.m_uToNetworkDataRate;
		target.m_strStage = source.m_strStage;
	}

	void CopySearchCommand(const CemuleApp::SSearchCommand &source, CemuleApp::SSearchCommand &target)
	{
		target.m_eType = source.m_eType;
		target.m_searchParams = source.m_searchParams;
		target.m_uSearchID = source.m_uSearchID;
		target.m_bStartupRefresh = source.m_bStartupRefresh;
	}

	void CopyCollectionCommand(const CemuleApp::SCollectionCommand &source, CemuleApp::SCollectionCommand &target)
	{
		target.m_strFilePath = source.m_strFilePath;
	}

	void CopyPersistenceCommand(const CemuleApp::SPersistenceCommand &source, CemuleApp::SPersistenceCommand &target)
	{
		target.m_eType = source.m_eType;
		target.m_bAutoSave = source.m_bAutoSave;
		target.m_bWorkRequest = source.m_bWorkRequest;
		target.m_strReason = source.m_strReason;
	}

	void CopySharedFilesCommand(const CemuleApp::SSharedFilesCommand &source, CemuleApp::SSharedFilesCommand &target)
	{
		target.m_eType = source.m_eType;
		target.m_uAction = source.m_uAction;
		target.m_astrItemHashes.RemoveAll();
		CopyStringArray(source.m_astrItemHashes, target.m_astrItemHashes);
	}

	bool IsZeroHash(const BYTE *pHash)
	{
		if (pHash == NULL)
			return true;
		static const BYTE abyZeroHash[16] = { 0 };
		return memcmp(pHash, abyZeroHash, sizeof(abyZeroHash)) == 0;
	}

	bool IsDownloadNetworkPacketCommand(CemuleApp::ENetworkPacketCommandType eType)
	{
		switch (eType) {
			case CemuleApp::NetworkPacketCommandDownloadFileStatus:
			case CemuleApp::NetworkPacketCommandDownloadHashSet:
			case CemuleApp::NetworkPacketCommandDownloadFoundSources:
			case CemuleApp::NetworkPacketCommandDownloadSourceExchange:
			case CemuleApp::NetworkPacketCommandDownloadBlockReceive:
				return true;
		}
		return false;
	}

	void SetNetworkPacketApplyFailure(CString *pstrFailureStage, CemuleApp::EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError, LPCTSTR pszStage,
		CemuleApp::EBackendCommandFailureKind eFailureKind, DWORD dwLastError)
	{
		if (pstrFailureStage != NULL)
			*pstrFailureStage = pszStage != NULL ? pszStage : _T("");
		if (peFailureKind != NULL)
			*peFailureKind = eFailureKind;
		if (pdwLastError != NULL)
			*pdwLastError = dwLastError;
	}

	CString BuildNetworkPacketOrderingKey(const CemuleApp::SNetworkPacketCommand &command)
	{
		CString strKey;
		const CString strHash(IsZeroHash(command.m_abyFileHash) ? CString(_T("")) : md4str(command.m_abyFileHash));
		switch (command.m_eType) {
			case CemuleApp::NetworkPacketCommandClientSearchAnswer:
				strKey.Format(_T("client:%lu/search:%u"), command.m_uClientRuntimeID, command.m_uSearchID);
				break;
			case CemuleApp::NetworkPacketCommandServerSearchAnswer:
			case CemuleApp::NetworkPacketCommandServerUdpSearchAnswer:
				strKey.Format(_T("server-search:%u/%08x:%u"), command.m_uSearchID, command.m_nClientServerIP, command.m_nClientServerPort);
				break;
			case CemuleApp::NetworkPacketCommandDownloadFoundSources:
				strKey.Format(_T("file:%s"), (LPCTSTR)strHash);
				break;
			case CemuleApp::NetworkPacketCommandKadPacket:
				strKey.Format(_T("kad:%08x:%u/op:%02x/tx:%08x"), command.m_nClientServerIP, command.m_nClientServerPort, command.m_uOpcode, command.m_uTransactionID);
				break;
			case CemuleApp::NetworkPacketCommandDownloadFileStatus:
			case CemuleApp::NetworkPacketCommandDownloadHashSet:
			case CemuleApp::NetworkPacketCommandDownloadSourceExchange:
			case CemuleApp::NetworkPacketCommandDownloadBlockReceive:
				strKey.Format(_T("file:%s"), (LPCTSTR)strHash);
				break;
			default:
				strKey = _T("network-packet");
				break;
		}
		return strKey;
	}

	CemuleApp::EBackendCommandOrderingScope GetNetworkPacketOrderingScope(const CemuleApp::SNetworkPacketCommand &command)
	{
		switch (command.m_eType) {
			case CemuleApp::NetworkPacketCommandClientSearchAnswer:
			case CemuleApp::NetworkPacketCommandServerSearchAnswer:
			case CemuleApp::NetworkPacketCommandServerUdpSearchAnswer:
				return CemuleApp::BackendCommandOrderingSearch;
			case CemuleApp::NetworkPacketCommandDownloadFileStatus:
			case CemuleApp::NetworkPacketCommandDownloadHashSet:
			case CemuleApp::NetworkPacketCommandDownloadFoundSources:
			case CemuleApp::NetworkPacketCommandDownloadSourceExchange:
			case CemuleApp::NetworkPacketCommandDownloadBlockReceive:
				return CemuleApp::BackendCommandOrderingFileHash;
			case CemuleApp::NetworkPacketCommandKadPacket:
				return CemuleApp::BackendCommandOrderingKad;
		}
		return CemuleApp::BackendCommandOrderingGlobal;
	}

	void CopyNetworkPacketCommand(const CemuleApp::SNetworkPacketCommand &source, CemuleApp::SNetworkPacketCommand &target)
	{
		target.m_eType = source.m_eType;
		target.m_eParseDomain = source.m_eParseDomain;
		target.m_packet = source.m_packet;
		target.m_uClientRuntimeID = source.m_uClientRuntimeID;
		target.m_lClientRuntimeGeneration = source.m_lClientRuntimeGeneration;
		memcpy(target.m_abyClientUserHash, source.m_abyClientUserHash, sizeof(target.m_abyClientUserHash));
		target.m_nClientIP = source.m_nClientIP;
		target.m_nClientUserPort = source.m_nClientUserPort;
		target.m_uSearchID = source.m_uSearchID;
		target.m_lSearchGeneration = source.m_lSearchGeneration;
		target.m_strClientHash = source.m_strClientHash;
		target.m_strSenderName = source.m_strSenderName;
		target.m_strDirectory = source.m_strDirectory;
		target.m_nClientID = source.m_nClientID;
		target.m_nClientPort = source.m_nClientPort;
		target.m_nClientServerIP = source.m_nClientServerIP;
		target.m_nClientServerPort = source.m_nClientServerPort;
		target.m_uProtocol = source.m_uProtocol;
		target.m_uOpcode = source.m_uOpcode;
		target.m_uTransactionID = source.m_uTransactionID;
		target.m_uContactID = source.m_uContactID;
		target.m_lSessionGeneration = source.m_lSessionGeneration;
		target.m_bValidReceiverKey = source.m_bValidReceiverKey;
		target.m_uSenderUDPKey = source.m_uSenderUDPKey;
		target.m_bOptUTF8 = source.m_bOptUTF8;
		target.m_bClientResponse = source.m_bClientResponse;
		target.m_bPreviewSupport = source.m_bPreviewSupport;
		target.m_bSupportsLargeFiles = source.m_bSupportsLargeFiles;
		target.m_bDoSpamRating = source.m_bDoSpamRating;
		target.m_bUseKadReloadThrottle = source.m_bUseKadReloadThrottle;
		target.m_bUdpPacket = source.m_bUdpPacket;
		target.m_bFileIdentifiers = source.m_bFileIdentifiers;
		target.m_bSourceExchange2 = source.m_bSourceExchange2;
		target.m_bWithObfuscationAndHash = source.m_bWithObfuscationAndHash;
		target.m_bCompressedBlock = source.m_bCompressedBlock;
		target.m_bI64Offsets = source.m_bI64Offsets;
		target.m_uSourceExchangeVersion = source.m_uSourceExchangeVersion;
		target.m_uPacketPosition = source.m_uPacketPosition;
		target.m_uFileRuntimeID = source.m_uFileRuntimeID;
		memcpy(target.m_abyFileHash, source.m_abyFileHash, sizeof(target.m_abyFileHash));
	}

	void CopyBackendCommand(const CemuleApp::SBackendCommand &source, CemuleApp::SBackendCommand &target)
	{
		target.m_eType = source.m_eType;
		target.m_eFamily = source.m_eFamily;
		target.m_eSource = source.m_eSource;
		target.m_eOrderingScope = source.m_eOrderingScope;
		target.m_eFailurePolicy = source.m_eFailurePolicy;
		target.m_strOrderingKey = source.m_strOrderingKey;
		target.m_dwCreatedTick = source.m_dwCreatedTick;
		target.m_lGenerationGuard = source.m_lGenerationGuard;
		target.m_bCancelable = source.m_bCancelable;
		target.m_bDropIfStale = source.m_bDropIfStale;
		target.m_uSequence = source.m_uSequence;
		target.m_uCorrelationId = source.m_uCorrelationId;
		target.m_uCancellationToken = source.m_uCancellationToken;
		CopyDownloadCommand(source.m_downloadCommand, target.m_downloadCommand);
		CopyUploadCommand(source.m_uploadCommand, target.m_uploadCommand);
		CopySearchCommand(source.m_searchCommand, target.m_searchCommand);
		CopyCollectionCommand(source.m_collectionCommand, target.m_collectionCommand);
		CopyPersistenceCommand(source.m_persistenceCommand, target.m_persistenceCommand);
		CopySharedFilesCommand(source.m_sharedFilesCommand, target.m_sharedFilesCommand);
		CopyNetworkPacketCommand(source.m_networkPacketCommand, target.m_networkPacketCommand);
	}

	void SetApplicationEventBackendEnvelope(CemuleApp::SApplicationEvent &event, CemuleApp::EBackendCommandType eType, CemuleApp::EBackendCommandSource eSource, CemuleApp::EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey, uint64 uSequence, uint64 uCorrelationId)
	{
		event.m_eBackendCommandType = eType;
		event.m_eBackendCommandFamily = CemuleApp::BackendCommandFamilyUnknown;
		event.m_eBackendCommandSource = eSource;
		event.m_eBackendCommandOrderingScope = eScope;
		event.m_eBackendCommandFailurePolicy = CemuleApp::BackendCommandFailurePolicyReport;
		event.m_lBackendCommandGenerationGuard = 0;
		event.m_strBackendCommandOrderingKey = pszOrderingKey != NULL ? pszOrderingKey : _T("");
		event.m_uSequence = uSequence;
		event.m_uCorrelationId = uCorrelationId;
	}

	void CopyBackendCommandEnvelopeToEvent(const CemuleApp::SBackendCommand &command, CemuleApp::SApplicationEvent &event)
	{
		SetApplicationEventBackendEnvelope(event, command.m_eType, command.m_eSource, command.m_eOrderingScope, command.m_strOrderingKey, command.m_uSequence, command.m_uCorrelationId);
		event.m_eBackendCommandFamily = command.m_eFamily;
		event.m_eBackendCommandFailurePolicy = command.m_eFailurePolicy;
		event.m_lBackendCommandGenerationGuard = command.m_lGenerationGuard;
		event.m_uCancellationToken = command.m_uCancellationToken;
	}

	CemuleApp::EBackendCommandType GetBackendCommandTypeForModelMutationDomain(CemuleApp::EModelMutationDomain eDomain)
	{
		switch (eDomain) {
			case CemuleApp::ModelMutationSearchList:
			case CemuleApp::ModelMutationSearchFile:
				return CemuleApp::BackendCommandSearch;
			case CemuleApp::ModelMutationUploadQueue:
			case CemuleApp::ModelMutationClientList:
			case CemuleApp::ModelMutationUpDownClient:
				return CemuleApp::BackendCommandUpload;
			case CemuleApp::ModelMutationDownloadQueue:
			case CemuleApp::ModelMutationPartFile:
				return CemuleApp::BackendCommandDownload;
			case CemuleApp::ModelMutationSharedFiles:
			case CemuleApp::ModelMutationKnownFiles:
				return CemuleApp::BackendCommandSharedFiles;
			case CemuleApp::ModelMutationPreferences:
			case CemuleApp::ModelMutationFriendList:
			case CemuleApp::ModelMutationServerList:
			case CemuleApp::ModelMutationKad:
				return CemuleApp::BackendCommandPersistence;
			case CemuleApp::ModelMutationWebServer:
				return CemuleApp::BackendCommandNetworkPacket;
		}
		return CemuleApp::BackendCommandDownload;
	}

	CemuleApp::EBackendCommandOrderingScope GetBackendCommandOrderingScopeForModelMutationDomain(CemuleApp::EModelMutationDomain eDomain)
	{
		switch (eDomain) {
			case CemuleApp::ModelMutationSearchList:
			case CemuleApp::ModelMutationSearchFile:
				return CemuleApp::BackendCommandOrderingSearch;
			case CemuleApp::ModelMutationUploadQueue:
			case CemuleApp::ModelMutationClientList:
			case CemuleApp::ModelMutationUpDownClient:
				return CemuleApp::BackendCommandOrderingClient;
			case CemuleApp::ModelMutationDownloadQueue:
				return CemuleApp::BackendCommandOrderingDownloadList;
			case CemuleApp::ModelMutationPartFile:
				return CemuleApp::BackendCommandOrderingFileHash;
			case CemuleApp::ModelMutationSharedFiles:
			case CemuleApp::ModelMutationKnownFiles:
				return CemuleApp::BackendCommandOrderingSharedFiles;
			case CemuleApp::ModelMutationPreferences:
			case CemuleApp::ModelMutationFriendList:
			case CemuleApp::ModelMutationServerList:
			case CemuleApp::ModelMutationKad:
				return CemuleApp::BackendCommandOrderingPersistence;
			case CemuleApp::ModelMutationWebServer:
				return CemuleApp::BackendCommandOrderingWebRequest;
		}
		return CemuleApp::BackendCommandOrderingGlobal;
	}

	CemuleApp::EModelMutationDomain GetPersistenceCommandMutationDomain(CemuleApp::EPersistenceCommandType eCommand)
	{
		switch (eCommand) {
			case CemuleApp::PersistenceCommandSaveAppState:
			case CemuleApp::PersistenceCommandSaveClientCredits:
				return CemuleApp::ModelMutationUploadQueue;
			case CemuleApp::PersistenceCommandSaveStats:
			case CemuleApp::PersistenceCommandSavePreferences:
				return CemuleApp::ModelMutationPreferences;
			case CemuleApp::PersistenceCommandSaveKnownFiles:
				return CemuleApp::ModelMutationKnownFiles;
			case CemuleApp::PersistenceCommandSaveFriends:
				return CemuleApp::ModelMutationFriendList;
			case CemuleApp::PersistenceCommandSaveServerList:
				return CemuleApp::ModelMutationServerList;
			case CemuleApp::PersistenceCommandSaveClientHistory:
				return CemuleApp::ModelMutationClientList;
			case CemuleApp::PersistenceCommandSaveSearchStore:
			case CemuleApp::PersistenceCommandSaveSearchSpam:
				return CemuleApp::ModelMutationSearchList;
			case CemuleApp::PersistenceCommandSaveSharedFiles:
				return CemuleApp::ModelMutationSharedFiles;
			case CemuleApp::PersistenceCommandSaveKadNodes:
				return CemuleApp::ModelMutationKad;
		}
		return CemuleApp::ModelMutationKnownFiles;
	}

}

CemuleApp::SDownloadCommand::SDownloadCommand()
	: m_eType(DownloadCommandAddFileLinks)
	, m_uAction(0)
	, m_iActionValue(0)
	, m_bAddToCanceledMet(false)
	, m_bDeleteCompletedFile(false)
	, m_iCat(0)
{
}

CemuleApp::SUploadCommand::SUploadCommand()
	: m_eType(UploadCommandClientRowsChanged)
	, m_uRuntimeID(0)
	, m_lRuntimeGeneration(0)
	, m_uTargetFlags(UploadClientUiTargetAll)
	, m_uWaitingCount(0)
	, m_uUploadingCount(0)
	, m_uActiveUploadCount(0)
	, m_uDataRate(0)
	, m_uToNetworkDataRate(0)
{
}

CemuleApp::SSearchCommand::SSearchCommand()
	: m_eType(SearchCommandStart)
	, m_uSearchID(0)
	, m_bStartupRefresh(false)
{
}

CemuleApp::SCollectionCommand::SCollectionCommand()
{
}

CemuleApp::SPersistenceCommand::SPersistenceCommand()
	: m_eType(PersistenceCommandSaveAppState)
	, m_bAutoSave(false)
	, m_bWorkRequest(false)
{
}

CemuleApp::SSharedFilesCommand::SSharedFilesCommand()
	: m_eType(SharedFilesCommandMenuAction)
	, m_uAction(0)
{
}

CemuleApp::SStartupMetadataLoadState::SStartupMetadataLoadState()
	: m_eState(StartupMetadataStateNotStarted)
	, m_lGeneration(0)
	, m_uCancellationToken(0)
	, m_dwLastError(0)
	, m_bCancelRequested(false)
	, m_uProgressDone(0)
	, m_uProgressTotal(0)
{
}

CemuleApp::SWorkerTopologyItem::SWorkerTopologyItem()
	: m_eRole(WorkerTopologyBackendCommand)
	, m_eType(WorkerTopologyItemNone)
	, m_dwCreatedTick(0)
	, m_dwDueTick(0)
	, m_uSequence(0)
	, m_uCorrelationId(0)
	, m_uCancellationToken(0)
	, m_lWorkerGeneration(0)
	, m_uFlags(0)
	, m_iPayloadCursor(0)
	, m_hNotifyWnd(NULL)
{
}

CemuleApp::SWorkerTopologyThreadParam::SWorkerTopologyThreadParam()
	: m_pApp(NULL)
	, m_eRole(WorkerTopologyBackendCommand)
{
}

CemuleApp::SNetworkPacketCommand::SNetworkPacketCommand()
	: m_eType(NetworkPacketCommandClientSearchAnswer)
	, m_eParseDomain(NetworkParseSearchAnswer)
	, m_uClientRuntimeID(0)
	, m_lClientRuntimeGeneration(0)
	, m_nClientIP(0)
	, m_nClientUserPort(0)
	, m_uSearchID(0)
	, m_lSearchGeneration(0)
	, m_nClientID(0)
	, m_nClientPort(0)
	, m_nClientServerIP(0)
	, m_nClientServerPort(0)
	, m_uProtocol(0)
	, m_uOpcode(0)
	, m_uTransactionID(0)
	, m_uContactID(0)
	, m_lSessionGeneration(0)
	, m_bValidReceiverKey(false)
	, m_uSenderUDPKey(0)
	, m_bOptUTF8(false)
	, m_bClientResponse(false)
	, m_bPreviewSupport(false)
	, m_bSupportsLargeFiles(false)
	, m_bDoSpamRating(false)
	, m_bUseKadReloadThrottle(false)
	, m_bUdpPacket(false)
	, m_bFileIdentifiers(false)
	, m_bSourceExchange2(false)
	, m_bWithObfuscationAndHash(false)
	, m_bCompressedBlock(false)
	, m_bI64Offsets(false)
	, m_uSourceExchangeVersion(0)
	, m_uPacketPosition(0)
	, m_uFileRuntimeID(0)
{
	memset(m_abyClientUserHash, 0, sizeof(m_abyClientUserHash));
	memset(m_abyFileHash, 0, sizeof(m_abyFileHash));
}

CemuleApp::SDownloadFileSnapshot::SDownloadFileSnapshot()
	: m_uFileSize(0)
{
	memset(m_abyFileHash, 0, sizeof(m_abyFileHash));
}

CemuleApp::SBackendCommand::SBackendCommand()
	: m_eType(BackendCommandDownload)
	, m_eFamily(BackendCommandFamilyUnknown)
	, m_eSource(BackendCommandSourceUnknown)
	, m_eOrderingScope(BackendCommandOrderingGlobal)
	, m_eFailurePolicy(BackendCommandFailurePolicyUnknown)
	, m_dwCreatedTick(0)
	, m_lGenerationGuard(0)
	, m_bCancelable(false)
	, m_bDropIfStale(true)
	, m_uSequence(0)
	, m_uCorrelationId(0)
	, m_uCancellationToken(0)
{
}

CemuleApp::SApplicationEvent::SApplicationEvent()
	: m_eType(ApplicationEventDownloadBatchCompleted)
	, m_eBackendCommandType(BackendCommandDownload)
	, m_eBackendCommandFamily(BackendCommandFamilyUnknown)
	, m_eBackendCommandSource(BackendCommandSourceUnknown)
	, m_eBackendCommandOrderingScope(BackendCommandOrderingGlobal)
	, m_eBackendCommandFailureKind(BackendCommandFailureNone)
	, m_eBackendCommandFailurePolicy(BackendCommandFailurePolicyUnknown)
	, m_lBackendCommandGenerationGuard(0)
	, m_eDownloadCommandType(DownloadCommandAddFileLinks)
	, m_eUploadCommandType(UploadCommandClientRowsChanged)
	, m_eSearchCommandType(SearchCommandStart)
	, m_ePersistenceCommandType(PersistenceCommandSaveAppState)
	, m_eSharedFilesCommandType(SharedFilesCommandMenuAction)
	, m_eStartupMetadataDomain(StartupMetadataKnownFiles)
	, m_eStartupMetadataState(StartupMetadataStateNotStarted)
	, m_lStartupMetadataGeneration(0)
	, m_uSequence(0)
	, m_uCorrelationId(0)
	, m_uCancellationToken(0)
	, m_uClientRuntimeID(0)
	, m_lClientRuntimeGeneration(0)
	, m_uUploadTargetFlags(0)
	, m_uUploadWaitingCount(0)
	, m_uUploadUploadingCount(0)
	, m_uUploadActiveCount(0)
	, m_uUploadDataRate(0)
	, m_uUploadToNetworkDataRate(0)
	, m_uAction(0)
	, m_iActionValue(0)
	, m_bAddToCanceledMet(false)
	, m_bDeleteCompletedFile(false)
	, m_bAutoSave(false)
	, m_uProcessed(0)
	, m_uFailed(0)
	, m_uStale(0)
	, m_uTotal(0)
	, m_dwLastError(0)
	, m_lAsyncGeneration(0)
	, m_bAsyncShutdownFallback(false)
	, m_uSearchID(0)
	, m_lSearchGeneration(0)
	, m_bUseKadReloadThrottle(false)
	, m_bMoreResultsAvailable(false)
	, m_hClientBitmap(NULL)
{
}

CemuleApp::SChunkedDownloadParseJob::SChunkedDownloadParseJob()
	: m_iNextParsePos(0)
	, m_uParsed(0)
	, m_dwStartedTick(0)
	, m_dwLastProgressTick(0)
{
}

CemuleApp::SDownloadLinkParseThreadParam::SDownloadLinkParseThreadParam()
	: m_pApp(NULL)
{
}

CemuleApp::SChunkedDownloadJob::SChunkedDownloadJob()
	: m_uSequence(0)
	, m_uCorrelationId(0)
	, m_uCancellationToken(0)
	, m_eSource(BackendCommandSourceUnknown)
	, m_eOrderingScope(BackendCommandOrderingDownloadList)
	, m_iNextIndex(0)
	, m_uProcessed(0)
	, m_uFailed(0)
	, m_bBackendOwnerSafe(false)
	, m_dwStartedTick(0)
	, m_dwLastProgressTick(0)
	, m_bBulkAddActive(false)
{
}

bool CemuleApp::IsBackendDownloadListJobRunnable(const SBackendDownloadListJob *pJob) const
{
	return pJob != NULL && (!pJob->m_bWaitingForDiskCleanup || pJob->m_uPendingDiskDeletes == 0);
}

bool CemuleApp::CanProcessBackendDownloadListJobOnCurrentThread(const SBackendDownloadListJob *pJob) const
{
	if (!IsBackendDownloadListJobRunnable(pJob))
		return false;
	if (pJob->m_bBackendOwnerSafe && HasBackendCommandThreadSignalTarget())
		return m_dwBackendCommandThreadId != 0 && ::GetCurrentThreadId() == m_dwBackendCommandThreadId;
	if (!UseAsyncBackendCommandExecution())
		return IsUiThread();
	if (IsBackendOwnerThread())
		return pJob->m_bBackendOwnerSafe;
	if (IsUiThread())
		return !pJob->m_bBackendOwnerSafe;
	return false;
}

bool CemuleApp::BackendDownloadListJobNeedsUiCompatibility(const SBackendDownloadListJob *pJob) const
{
	return pJob != NULL && IsBackendDownloadListJobRunnable(pJob) && !pJob->m_bBackendOwnerSafe;
}

bool CemuleApp::CanProcessImportPartWorkItemsOnCurrentThread() const
{
	{
		CSingleLock lock(const_cast<CCriticalSection*>(&m_importPartWorkQueueLock), TRUE);
		if (m_importPartWorkItems.IsEmpty())
			return false;
	}
	if (HasBackendCommandThreadSignalTarget())
		return m_dwBackendCommandThreadId != 0 && ::GetCurrentThreadId() == m_dwBackendCommandThreadId;
	if (!UseAsyncBackendCommandExecution())
		return IsUiThread();
	return IsBackendOwnerThread();
}

void CemuleApp::ReleaseImportPartWorkItem(SImportPartWorkItem *pItem, bool bAbort)
{
	if (pItem == NULL)
		return;

	ImportOperationContext *pContext = pItem->m_pContext;
	if (bAbort && pContext != NULL)
		AbortImportOperationContext(pContext);

	if (pItem->m_pImportPart != NULL) {
		delete[] pItem->m_pImportPart->data;
		if (pContext != NULL) {
			const LONG lQueuedBlocks = ::InterlockedDecrement(&pContext->lQueuedBlocks);
			if (lQueuedBlocks < 0) {
				if (thePrefs.GetLogUiResponsivenessEvents())
					AddDebugLogLine(DLP_LOW, false, _T("Import part queued block count underflow ignored.\n"));
				::InterlockedExchange(&pContext->lQueuedBlocks, 0);
			}
		}
		delete pItem->m_pImportPart;
	}

	ReleaseImportOperationContext(pContext);
	delete pItem;
}

void CemuleApp::ClearImportPartWorkItems()
{
	CTypedPtrList<CPtrList, SImportPartWorkItem*> items;
	{
		CSingleLock lock(&m_importPartWorkQueueLock, TRUE);
		while (!m_importPartWorkItems.IsEmpty())
			items.AddTail(m_importPartWorkItems.RemoveHead());
	}
	while (!items.IsEmpty())
		ReleaseImportPartWorkItem(items.RemoveHead(), true);
}

void CemuleApp::ProcessImportPartWorkItem(SImportPartWorkItem *pItem)
{
	if (pItem == NULL)
		return;

	ImportOperationContext *pContext = pItem->m_pContext;
	CPartFile *pPartFile = NULL;
	if (pContext != NULL && !IsClosing() && downloadqueue != NULL) {
		pPartFile = downloadqueue->GetFileByID(pContext->aucFileHash);
		if (pPartFile == NULL || pPartFile->GetRuntimeID() != pContext->uPartFileRuntimeID || !pPartFile->IsImportOperationCurrent(pContext))
			pPartFile = NULL;
	}

	switch (pItem->m_eType) {
	case ImportPartWorkWrite:
		if (pItem->m_pImportPart != NULL && pPartFile != NULL && GuardModelMutation(ModelMutationPartFile, _T("CemuleApp::ProcessImportPartWorkItem"))) {
			ImportPart_Struct *pImportPart = pItem->m_pImportPart;
			if (pPartFile->WriteToBuffer(pImportPart->end - pImportPart->start + 1, pImportPart->data, pImportPart->start, pImportPart->end, NULL, NULL, false))
				pImportPart->data = NULL;
			pPartFile->MarkImportPartHandled(pContext);
		} else if (pContext != NULL) {
			const LONG lQueuedBlocks = ::InterlockedDecrement(&pContext->lQueuedBlocks);
			if (lQueuedBlocks < 0) {
				if (thePrefs.GetLogUiResponsivenessEvents())
					AddDebugLogLine(DLP_LOW, false, _T("Import part queued block count underflow ignored.\n"));
				::InterlockedExchange(&pContext->lQueuedBlocks, 0);
			}
			AbortImportOperationContext(pContext);
		}
		break;
	case ImportPartWorkProgress:
		if (pPartFile != NULL && GuardModelMutation(ModelMutationPartFile, _T("CemuleApp::ProcessImportPartProgress"))) {
			pPartFile->SetFileOpProgress(pItem->m_uProgress);
			pPartFile->UpdateDisplayedInfo(true);
		}
		break;
	case ImportPartWorkFinished:
		if (pItem->m_bAborted)
			AbortImportOperationContext(pContext);
		if (pPartFile != NULL && GuardModelMutation(ModelMutationPartFile, _T("CemuleApp::ProcessImportPartFinished")))
			pPartFile->TryFinalizeImportPartsOperation(pContext);
		break;
	}

	if (pItem->m_pImportPart != NULL) {
		delete[] pItem->m_pImportPart->data;
		delete pItem->m_pImportPart;
	}
	ReleaseImportOperationContext(pContext);
	delete pItem;
}

void CemuleApp::ProcessImportPartWorkItemsOnCurrentThread(DWORD dwSliceStart)
{
	if (!CanProcessImportPartWorkItemsOnCurrentThread())
		return;

	UINT uProcessed = 0;
	for (;;) {
		SImportPartWorkItem *pItem = NULL;
		{
			CSingleLock lock(&m_importPartWorkQueueLock, TRUE);
			if (m_importPartWorkItems.IsEmpty())
				break;
			pItem = m_importPartWorkItems.RemoveHead();
		}

		ProcessImportPartWorkItem(pItem);
		++uProcessed;
		if (uProcessed != 0 && IsTimeBudgetExceeded(dwSliceStart, TimeBudgetDownloadState))
			break;
	}
}

bool CemuleApp::IsChunkedDownloadJobRunnable(const SChunkedDownloadJob *pJob) const
{
	return pJob != NULL;
}

bool CemuleApp::CanProcessChunkedDownloadJobOnCurrentThread(const SChunkedDownloadJob *pJob) const
{
	if (!IsChunkedDownloadJobRunnable(pJob))
		return false;
	if (pJob->m_bBackendOwnerSafe && HasBackendCommandThreadSignalTarget())
		return m_dwBackendCommandThreadId != 0 && ::GetCurrentThreadId() == m_dwBackendCommandThreadId;
	if (!UseAsyncBackendCommandExecution())
		return IsUiThread();
	return pJob->m_bBackendOwnerSafe ? IsBackendOwnerThread() : IsUiThread();
}

bool CemuleApp::ChunkedDownloadJobNeedsUiCompatibility(const SChunkedDownloadJob *pJob) const
{
	return pJob != NULL && IsChunkedDownloadJobRunnable(pJob) && !pJob->m_bBackendOwnerSafe;
}

bool CemuleApp::ShouldUseChunkedDownloadUiTimer(const SChunkedDownloadJob *pJob) const
{
	return IsUiThread() && pJob != NULL && pJob->m_bBackendOwnerSafe
		&& GetChunkedDownloadJobItemCount(*pJob) >= kLargeDownloadBatchThreshold
		&& !HasBackendCommandThreadSignalTarget();
}

CemuleApp::SImportPartWorkItem::SImportPartWorkItem()
	: m_eType(ImportPartWorkWrite)
	, m_pImportPart(NULL)
	, m_pContext(NULL)
	, m_uProgress(0)
	, m_bAborted(false)
{
}

CemuleApp::SBackendDownloadListJob::SBackendDownloadListJob()
	: m_eType(DownloadCommandRemoveItems)
	, m_iNextIndex(0)
	, m_uAction(0)
	, m_iActionValue(0)
	, m_bAddToCanceledMet(false)
	, m_bDeleteCompletedFile(false)
	, m_bBulkRemoveActive(false)
	, m_bListUpdateBatchActive(false)
	, m_bBackendOwnerSafe(false)
	, m_bWaitingForDiskCleanup(false)
	, m_uPendingDiskDeletes(0)
	, m_uProcessed(0)
	, m_uFailed(0)
	, m_uStale(0)
	, m_uSequence(0)
	, m_uCorrelationId(0)
	, m_uCancellationToken(0)
	, m_eSource(BackendCommandSourceUnknown)
	, m_eOrderingScope(BackendCommandOrderingDownloadList)
	, m_dwStartedTick(0)
	, m_dwLastProgressTick(0)
{
}

DWORD CemuleApp::GetTimeBudgetedSliceBudgetMs(ETimeBudgetedSliceKind eKind) const
{
	switch (eKind) {
		case TimeBudgetBackendCommandDispatch:
		case TimeBudgetApplicationEventDispatch:
		case TimeBudgetDownloadAdd:
		case TimeBudgetSearchIngest:
		case TimeBudgetUiNotificationApply:
			return kTimeBudgetFastSliceMs;
		case TimeBudgetDownloadParse:
		case TimeBudgetSearchResultDownload:
		case TimeBudgetSearchResultRemove:
		case TimeBudgetSearchRedraw:
		case TimeBudgetStartupApply:
		case TimeBudgetDownloadRemove:
		case TimeBudgetDownloadState:
		case TimeBudgetPersistenceSave:
		case TimeBudgetSharedFilesReload:
		case TimeBudgetSharedFilesFound:
		case TimeBudgetSharedFilesBulk:
		case TimeBudgetUploadTimerMaintenance:
			return kTimeBudgetDefaultSliceMs;
	}
	return kTimeBudgetDefaultSliceMs;
}

DWORD CemuleApp::GetTimeBudgetedSliceHardBudgetMs(ETimeBudgetedSliceKind eKind) const
{
	if (eKind == TimeBudgetStartupApply)
		return 16;
	return kTimeBudgetHardSliceMs;
}

DWORD CemuleApp::GetTimeBudgetedProgressTraceMs(ETimeBudgetedSliceKind /*eKind*/) const
{
	return kTimeBudgetProgressTraceMs;
}

bool CemuleApp::IsTimeBudgetExceeded(DWORD dwSliceStartTick, ETimeBudgetedSliceKind eKind) const
{
	return static_cast<DWORD>(::GetTickCount() - dwSliceStartTick) >= GetTimeBudgetedSliceBudgetMs(eKind);
}

bool CemuleApp::IsTimeBudgetHardExceeded(DWORD dwSliceStartTick, ETimeBudgetedSliceKind eKind, DWORD *pdwElapsed) const
{
	const DWORD dwElapsed = static_cast<DWORD>(::GetTickCount() - dwSliceStartTick);
	if (pdwElapsed != NULL)
		*pdwElapsed = dwElapsed;
	return dwElapsed >= GetTimeBudgetedSliceHardBudgetMs(eKind);
}

void CemuleApp::TraceTimeBudgetSlice(ETimeBudgetedSliceKind eKind, LPCTSTR pszContext, DWORD dwElapsed, UINT uProcessed, INT_PTR iRemaining) const
{
	static volatile LONG s_lLastTraceTick[64] = {};
	static const DWORD kTraceRateLimitMs = 1000;

	UINT uHash = static_cast<UINT>(eKind) * 2654435761u;
	if (pszContext != NULL) {
		for (LPCTSTR psz = pszContext; *psz != _T('\0'); ++psz)
			uHash = (uHash * 33u) ^ static_cast<UINT>(*psz);
	}
	const UINT uSlot = uHash % static_cast<UINT>(_countof(s_lLastTraceTick));
	const DWORD dwNow = ::GetTickCount();
	for (;;) {
		const LONG lPrevious = ::InterlockedCompareExchange(&s_lLastTraceTick[uSlot], 0, 0);
		if (lPrevious != 0 && static_cast<DWORD>(dwNow - static_cast<DWORD>(lPrevious)) < kTraceRateLimitMs)
			return;
		if (::InterlockedCompareExchange(&s_lLastTraceTick[uSlot], static_cast<LONG>(dwNow), lPrevious) == lPrevious)
			break;
	}

	AddDebugLogLine(DLP_VERYLOW, false, _T("[TimeBudget] slice=%s context=%s elapsed=%u budget=%u hard=%u processed=%u remaining=%d\n"), GetTimeBudgetedSliceName(eKind), pszContext != NULL ? pszContext : _T(""), dwElapsed, GetTimeBudgetedSliceBudgetMs(eKind), GetTimeBudgetedSliceHardBudgetMs(eKind), uProcessed, static_cast<int>(iRemaining));
}

void CemuleApp::AddEd2kLinksToDownload(const CString &strLinks, int cat)
{
	if (strLinks.IsEmpty())
		return;

	const UINT uEstimatedFileLinks = CountEd2kFileLinkPrefixes(strLinks);
	if (uEstimatedFileLinks >= kLargeDownloadBatchThreshold && !HasBackendCommandThreadSignalTarget())
		StartBackendCommandThread();
	if (uEstimatedFileLinks != 0) {
		SetActiveDownloadAddOperationProgress(0, uEstimatedFileLinks, true);
		UpdateDownloadAddMirroredOverlay(0, uEstimatedFileLinks);
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
	}

	SDownloadCommand command;
	command.m_eType = DownloadCommandAddFileLinks;
	command.m_iCat = cat;
	command.m_strRawLinks = strLinks;
	command.m_strTokenDelimiters = _T(" \t\r\n");
	if (!ExecuteDownloadCommand(command) && uEstimatedFileLinks != 0) {
		SetActiveDownloadAddOperationProgress(0, 0, false);
		UpdateDownloadAddMirroredOverlay(0, 0);
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
	}
}

void CemuleApp::AddEd2kLinkArrayToDownload(const CStringArray &astrLinks, int cat)
{
	const INT_PTR iLinks = astrLinks.GetSize();
	if (iLinks >= static_cast<INT_PTR>(kLargeDownloadBatchThreshold) && !HasBackendCommandThreadSignalTarget())
		StartBackendCommandThread();
	if (iLinks > 0) {
		const UINT uOverlayTotal = iLinks > static_cast<INT_PTR>(UINT_MAX) ? UINT_MAX : static_cast<UINT>(iLinks);
		SetActiveDownloadAddOperationProgress(0, uOverlayTotal, true);
		UpdateDownloadAddMirroredOverlay(0, uOverlayTotal);
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
	}

	SDownloadCommand command;
	command.m_eType = DownloadCommandAddFileLinks;
	command.m_iCat = cat;
	CopyStringArray(astrLinks, command.m_astrLinks);
	if (!ExecuteDownloadCommand(command) && iLinks > 0) {
		SetActiveDownloadAddOperationProgress(0, 0, false);
		UpdateDownloadAddMirroredOverlay(0, 0);
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
	}
}

void CemuleApp::AddFileSnapshotsToDownload(const std::shared_ptr<std::vector<SDownloadFileSnapshot> > &pFileSnapshots, int cat)
{
	if (pFileSnapshots.get() == NULL || pFileSnapshots->empty())
		return;

	SDownloadCommand command;
	command.m_eType = DownloadCommandAddFileLinks;
	command.m_iCat = cat;
	command.m_pFileSnapshots = pFileSnapshots;

	const size_t uSnapshotTotal = pFileSnapshots->size();
	if (uSnapshotTotal >= kLargeDownloadBatchThreshold && !HasBackendCommandThreadSignalTarget())
		StartBackendCommandThread();
	const UINT uSnapshotOverlayTotal = uSnapshotTotal > UINT_MAX ? UINT_MAX : static_cast<UINT>(uSnapshotTotal);
	SetActiveDownloadAddOperationProgress(0, uSnapshotOverlayTotal, true);
	UpdateDownloadAddMirroredOverlay(0, uSnapshotOverlayTotal);
	if (emuledlg != NULL)
		emuledlg->RefreshActiveBulkOperationOverlays();

	if (!ExecuteDownloadCommand(command, BackendCommandSourceUi, BackendCommandOrderingDownloadList, _T("download:add"))) {
		SetActiveDownloadAddOperationProgress(0, 0, false);
		UpdateDownloadAddMirroredOverlay(0, 0);
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
	}
}

void CemuleApp::ProcessED2KLinksChunked(const CString &strLinks)
{
	if (strLinks.IsEmpty())
		return;

	SDownloadCommand command;
	command.m_eType = DownloadCommandProcessLinks;
	command.m_iCat = (emuledlg != NULL && emuledlg->searchwnd != NULL) ? emuledlg->searchwnd->GetSelectedCat() : 0;
	command.m_strRawLinks = strLinks;
	command.m_strTokenDelimiters = _T("\r\n");
	ExecuteDownloadCommand(command);
}

void CemuleApp::ProcessED2KLinkArrayChunked(const CStringArray &astrLinks)
{
	SDownloadCommand command;
	command.m_eType = DownloadCommandProcessLinks;
	command.m_iCat = (emuledlg != NULL && emuledlg->searchwnd != NULL) ? emuledlg->searchwnd->GetSelectedCat() : 0;
	CopyStringArray(astrLinks, command.m_astrLinks);
	ExecuteDownloadCommand(command);
}

bool CemuleApp::ExecuteDownloadCommand(const SDownloadCommand &command, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandDownload;
	CopyDownloadCommand(command, backendCommand.m_downloadCommand);
	PrepareBackendCommandEnvelope(backendCommand, eSource, eScope, pszOrderingKey != NULL ? pszOrderingKey : (LPCTSTR)_T("download-list"));
	return EnqueueBackendCommand(backendCommand);
}

void CemuleApp::ExecuteSearchStartCommand(SSearchParams *pParams, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	if (pParams == NULL)
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandSearch;
	backendCommand.m_searchCommand.m_eType = SearchCommandStart;
	backendCommand.m_searchCommand.m_searchParams = *pParams;
	delete pParams;
	PrepareBackendCommandEnvelope(backendCommand, eSource, eScope, pszOrderingKey != NULL ? pszOrderingKey : (LPCTSTR)_T("search:start"));
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::ExecuteSearchCancelCommand(uint32 uSearchID, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	if (uSearchID == 0)
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandSearch;
	backendCommand.m_searchCommand.m_eType = SearchCommandCancel;
	backendCommand.m_searchCommand.m_uSearchID = uSearchID;
	CString strOrderingKey;
	strOrderingKey.Format(_T("search:%u"), uSearchID);
	PrepareBackendCommandEnvelope(backendCommand, eSource, eScope, pszOrderingKey != NULL ? pszOrderingKey : (LPCTSTR)strOrderingKey);
	EnqueueBackendCommand(backendCommand);
}

bool CemuleApp::ExecuteSearchKnownTypeRefreshCommand(LPCTSTR pszReason, bool bStartupRefresh)
{
	if (searchlist == NULL || IsClosing())
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandSearch;
	backendCommand.m_searchCommand.m_eType = SearchCommandKnownTypeRefresh;
	backendCommand.m_searchCommand.m_bStartupRefresh = bStartupRefresh;
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceTimer, BackendCommandOrderingSearch, pszReason != NULL ? pszReason : (LPCTSTR)_T("search:known-type-refresh"));
	return EnqueueBackendCommand(backendCommand);
}

bool CemuleApp::WakeSearchKnownTypeRefreshWork()
{
	if (searchlist == NULL || IsClosing())
		return false;
	return PostBackendCommandUiMessage();
}

void CemuleApp::ExecuteCollectionImportCommand(const CString &strFilePath)
{
	if (strFilePath.IsEmpty())
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandCollection;
	backendCommand.m_collectionCommand.m_strFilePath = strFilePath;
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceUi, BackendCommandOrderingGlobal, strFilePath);
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::ExecuteSaveAppStateCommand(bool bAutoSave, LPCTSTR pszReason)
{
	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandPersistence;
	backendCommand.m_persistenceCommand.m_eType = PersistenceCommandSaveAppState;
	backendCommand.m_persistenceCommand.m_bAutoSave = bAutoSave;
	if (pszReason != NULL)
		backendCommand.m_persistenceCommand.m_strReason = pszReason;
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourcePersistence, BackendCommandOrderingPersistence, _T("persistence:app-state"));
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::ExecuteSaveStatsCommand(LPCTSTR pszReason)
{
	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandPersistence;
	backendCommand.m_persistenceCommand.m_eType = PersistenceCommandSaveStats;
	if (pszReason != NULL)
		backendCommand.m_persistenceCommand.m_strReason = pszReason;
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourcePersistence, BackendCommandOrderingPersistence, _T("persistence:stats"));
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::ExecuteSavePersistenceFileCommand(EPersistenceCommandType eCommand, LPCTSTR pszReason)
{
	if (!IsPersistenceCommandTypeValid(eCommand))
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandPersistence;
	backendCommand.m_persistenceCommand.m_eType = eCommand;
	backendCommand.m_persistenceCommand.m_bAutoSave = false;
	if (pszReason != NULL)
		backendCommand.m_persistenceCommand.m_strReason = pszReason;
	CString strOrderingKey;
	strOrderingKey.Format(_T("persistence:%u"), static_cast<UINT>(eCommand));
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourcePersistence, BackendCommandOrderingPersistence, strOrderingKey);
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::ExecuteSharedFilesCommand(UINT uAction, const CStringArray &astrItemHashes, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	if (uAction == 0)
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandSharedFiles;
	backendCommand.m_sharedFilesCommand.m_eType = GetSharedFilesCommandTypeForAction(uAction);
	backendCommand.m_sharedFilesCommand.m_uAction = uAction;
	CopyStringArray(astrItemHashes, backendCommand.m_sharedFilesCommand.m_astrItemHashes);
	PrepareBackendCommandEnvelope(backendCommand, eSource, eScope, pszOrderingKey != NULL ? pszOrderingKey : (LPCTSTR)_T("shared-files"));
	EnqueueBackendCommand(backendCommand);
}



bool CemuleApp::QueueClientSearchAnswerNetworkCommand(const BYTE *pPacket, uint32 nSize, CUpDownClient &sender, LPCTSTR pszDirectory)
{
	if (pPacket == NULL || nSize < sizeof(uint32) || searchlist == NULL)
		return false;

	const uint32 uSearchID = sender.GetSearchID();
	if (uSearchID == 0)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandClientSearchAnswer;
	backendCommand.m_networkPacketCommand.m_eParseDomain = NetworkParseSearchAnswer;
	backendCommand.m_networkPacketCommand.m_uClientRuntimeID = sender.GetRuntimeID();
	backendCommand.m_networkPacketCommand.m_lClientRuntimeGeneration = sender.GetRuntimeGeneration();
	memcpy(backendCommand.m_networkPacketCommand.m_abyClientUserHash, sender.GetUserHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyClientUserHash));
	backendCommand.m_networkPacketCommand.m_nClientIP = sender.GetConnectIP().ToUInt32(false);
	backendCommand.m_networkPacketCommand.m_nClientUserPort = sender.GetUserPort();
	backendCommand.m_networkPacketCommand.m_uSearchID = uSearchID;
	backendCommand.m_networkPacketCommand.m_lSearchGeneration = searchlist->GetSearchAnswerParseGeneration(uSearchID);
	backendCommand.m_networkPacketCommand.m_strClientHash = md4str(sender.GetUserHash());
	backendCommand.m_networkPacketCommand.m_strSenderName = sender.GetUserName();
	backendCommand.m_networkPacketCommand.m_strDirectory = pszDirectory != NULL ? pszDirectory : _T("");
	backendCommand.m_networkPacketCommand.m_nClientID = sender.GetIP().ToUInt32(false);
	backendCommand.m_networkPacketCommand.m_nClientPort = sender.GetUserPort();
	backendCommand.m_networkPacketCommand.m_nClientServerIP = sender.GetServerIP();
	backendCommand.m_networkPacketCommand.m_nClientServerPort = sender.GetServerPort();
	backendCommand.m_networkPacketCommand.m_bOptUTF8 = sender.GetUnicodeSupport() != UTF8strNone;
	backendCommand.m_networkPacketCommand.m_bClientResponse = true;
	backendCommand.m_networkPacketCommand.m_bPreviewSupport = sender.GetPreviewSupport();
	backendCommand.m_networkPacketCommand.m_bSupportsLargeFiles = sender.SupportsLargeFiles();
	backendCommand.m_networkPacketCommand.m_bDoSpamRating = true;
	backendCommand.m_networkPacketCommand.m_bUseKadReloadThrottle = false;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceNetworkClient, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}

bool CemuleApp::QueueServerSearchAnswerNetworkCommand(const BYTE *pPacket, uint32 nSize, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort)
{
	if (pPacket == NULL || nSize < sizeof(uint32) || searchlist == NULL)
		return false;

	const uint32 uSearchID = searchlist->GetCurrentED2KSearchID();
	if (uSearchID == 0)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandServerSearchAnswer;
	backendCommand.m_networkPacketCommand.m_eParseDomain = NetworkParseSearchAnswer;
	backendCommand.m_networkPacketCommand.m_uSearchID = uSearchID;
	backendCommand.m_networkPacketCommand.m_lSearchGeneration = searchlist->GetSearchAnswerParseGeneration(uSearchID);
	backendCommand.m_networkPacketCommand.m_nClientServerIP = nServerIP;
	backendCommand.m_networkPacketCommand.m_nClientServerPort = nServerPort;
	backendCommand.m_networkPacketCommand.m_bOptUTF8 = bOptUTF8;
	backendCommand.m_networkPacketCommand.m_bClientResponse = false;
	backendCommand.m_networkPacketCommand.m_bSupportsLargeFiles = true;
	backendCommand.m_networkPacketCommand.m_bDoSpamRating = true;
	backendCommand.m_networkPacketCommand.m_bUseKadReloadThrottle = false;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceNetworkServer, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}
bool CemuleApp::QueueServerUdpSearchAnswerNetworkCommand(const BYTE *pPacket, uint32 nSize, bool bOptUTF8, uint32 nServerIP, uint16 nServerPort)
{
	if (pPacket == NULL || nSize < sizeof(uint32) || searchlist == NULL)
		return false;

	const uint32 uSearchID = searchlist->GetCurrentED2KSearchID();
	if (uSearchID == 0)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandServerUdpSearchAnswer;
	backendCommand.m_networkPacketCommand.m_eParseDomain = NetworkParseServerUdp;
	backendCommand.m_networkPacketCommand.m_uSearchID = uSearchID;
	backendCommand.m_networkPacketCommand.m_lSearchGeneration = searchlist->GetSearchAnswerParseGeneration(uSearchID);
	backendCommand.m_networkPacketCommand.m_nClientServerIP = nServerIP;
	backendCommand.m_networkPacketCommand.m_nClientServerPort = nServerPort;
	backendCommand.m_networkPacketCommand.m_uProtocol = OP_EDONKEYPROT;
	backendCommand.m_networkPacketCommand.m_uOpcode = OP_GLOBSEARCHRES;
	backendCommand.m_networkPacketCommand.m_bOptUTF8 = bOptUTF8;
	backendCommand.m_networkPacketCommand.m_bClientResponse = false;
	backendCommand.m_networkPacketCommand.m_bSupportsLargeFiles = true;
	backendCommand.m_networkPacketCommand.m_bDoSpamRating = true;
	backendCommand.m_networkPacketCommand.m_bUseKadReloadThrottle = false;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceNetworkUdp, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}

bool CemuleApp::QueueKadPacketNetworkCommand(const BYTE *pPacket, uint32 nSize, uint32 nIP, uint16 nUDPPort, bool bValidReceiverKey, uint32 nSenderUDPKey)
{
	if (pPacket == NULL || nSize < 2)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandKadPacket;
	backendCommand.m_networkPacketCommand.m_eParseDomain = NetworkParseKad;
	backendCommand.m_networkPacketCommand.m_nClientServerIP = nIP;
	backendCommand.m_networkPacketCommand.m_nClientServerPort = nUDPPort;
	backendCommand.m_networkPacketCommand.m_uProtocol = pPacket[0];
	backendCommand.m_networkPacketCommand.m_uOpcode = pPacket[1];
	backendCommand.m_networkPacketCommand.m_uTransactionID = nSize >= 6 ? PeekUInt32(pPacket + 2) : 0;
	backendCommand.m_networkPacketCommand.m_uContactID = nIP;
	backendCommand.m_networkPacketCommand.m_lSessionGeneration = 0;
	backendCommand.m_networkPacketCommand.m_bValidReceiverKey = bValidReceiverKey;
	backendCommand.m_networkPacketCommand.m_uSenderUDPKey = nSenderUDPKey;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceNetworkKad, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}


bool CemuleApp::QueueDownloadFileStatusNetworkCommand(CUpDownClient *pClient, CPartFile *pFile, const BYTE *pPacket, uint32 nSize, ULONGLONG uPacketPosition, bool bUdpPacket)
{
	if (pClient == NULL || pPacket == NULL || nSize == 0)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandDownloadFileStatus;
	backendCommand.m_networkPacketCommand.m_eParseDomain = bUdpPacket ? NetworkParseClientUdp : NetworkParseClientTcp;
	backendCommand.m_networkPacketCommand.m_uClientRuntimeID = pClient->GetRuntimeID();
	backendCommand.m_networkPacketCommand.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	memcpy(backendCommand.m_networkPacketCommand.m_abyClientUserHash, pClient->GetUserHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyClientUserHash));
	backendCommand.m_networkPacketCommand.m_nClientIP = pClient->GetConnectIP().ToUInt32(false);
	backendCommand.m_networkPacketCommand.m_nClientUserPort = pClient->GetUserPort();
	backendCommand.m_networkPacketCommand.m_bUdpPacket = bUdpPacket;
	backendCommand.m_networkPacketCommand.m_uPacketPosition = uPacketPosition;
	if (pFile != NULL) {
		memcpy(backendCommand.m_networkPacketCommand.m_abyFileHash, pFile->GetFileHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash));
		backendCommand.m_networkPacketCommand.m_uFileRuntimeID = pFile->GetRuntimeID();
	} else if (nSize >= sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash))
		memcpy(backendCommand.m_networkPacketCommand.m_abyFileHash, pPacket, sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash));
	if (IsZeroHash(backendCommand.m_networkPacketCommand.m_abyFileHash))
		return false;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, bUdpPacket ? BackendCommandSourceNetworkUdp : BackendCommandSourceNetworkClient, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}

bool CemuleApp::QueueDownloadHashSetNetworkCommand(CUpDownClient *pClient, const BYTE *pPacket, uint32 nSize, bool bFileIdentifiers)
{
	if (pClient == NULL || pPacket == NULL || nSize == 0)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandDownloadHashSet;
	backendCommand.m_networkPacketCommand.m_eParseDomain = NetworkParseClientTcp;
	backendCommand.m_networkPacketCommand.m_uClientRuntimeID = pClient->GetRuntimeID();
	backendCommand.m_networkPacketCommand.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	memcpy(backendCommand.m_networkPacketCommand.m_abyClientUserHash, pClient->GetUserHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyClientUserHash));
	backendCommand.m_networkPacketCommand.m_nClientIP = pClient->GetConnectIP().ToUInt32(false);
	backendCommand.m_networkPacketCommand.m_nClientUserPort = pClient->GetUserPort();
	backendCommand.m_networkPacketCommand.m_bFileIdentifiers = bFileIdentifiers;
	CPartFile *pReqFile = pClient->GetRequestFile();
	if (pReqFile != NULL) {
		memcpy(backendCommand.m_networkPacketCommand.m_abyFileHash, pReqFile->GetFileHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash));
		backendCommand.m_networkPacketCommand.m_uFileRuntimeID = pReqFile->GetRuntimeID();
	} else if (!bFileIdentifiers && nSize >= sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash))
		memcpy(backendCommand.m_networkPacketCommand.m_abyFileHash, pPacket, sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash));
	if (IsZeroHash(backendCommand.m_networkPacketCommand.m_abyFileHash))
		return false;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceNetworkClient, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}

bool CemuleApp::QueueDownloadFoundSourcesNetworkCommand(CPartFile *pFile, const BYTE *pPacket, uint32 nSize, ULONGLONG uPacketPosition, uint32 nServerIP, uint16 nServerPort, bool bWithObfuscationAndHash)
{
	if (pFile == NULL || pPacket == NULL || nSize == 0)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandDownloadFoundSources;
	backendCommand.m_networkPacketCommand.m_eParseDomain = NetworkParseServerTcp;
	backendCommand.m_networkPacketCommand.m_uPacketPosition = uPacketPosition;
	backendCommand.m_networkPacketCommand.m_nClientServerIP = nServerIP;
	backendCommand.m_networkPacketCommand.m_nClientServerPort = nServerPort;
	backendCommand.m_networkPacketCommand.m_bWithObfuscationAndHash = bWithObfuscationAndHash;
	memcpy(backendCommand.m_networkPacketCommand.m_abyFileHash, pFile->GetFileHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash));
	backendCommand.m_networkPacketCommand.m_uFileRuntimeID = pFile->GetRuntimeID();
	if (IsZeroHash(backendCommand.m_networkPacketCommand.m_abyFileHash))
		return false;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceNetworkServer, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}

bool CemuleApp::QueueDownloadSourceExchangeNetworkCommand(CUpDownClient *pClient, CPartFile *pFile, const BYTE *pPacket, uint32 nSize, ULONGLONG uPacketPosition, uint8 uSourceExchangeVersion, bool bSourceExchange2)
{
	if (pClient == NULL || pFile == NULL || pPacket == NULL || nSize == 0)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandDownloadSourceExchange;
	backendCommand.m_networkPacketCommand.m_eParseDomain = NetworkParseClientTcp;
	backendCommand.m_networkPacketCommand.m_uClientRuntimeID = pClient->GetRuntimeID();
	backendCommand.m_networkPacketCommand.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	memcpy(backendCommand.m_networkPacketCommand.m_abyClientUserHash, pClient->GetUserHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyClientUserHash));
	backendCommand.m_networkPacketCommand.m_nClientIP = pClient->GetConnectIP().ToUInt32(false);
	backendCommand.m_networkPacketCommand.m_nClientUserPort = pClient->GetUserPort();
	backendCommand.m_networkPacketCommand.m_uPacketPosition = uPacketPosition;
	backendCommand.m_networkPacketCommand.m_uSourceExchangeVersion = uSourceExchangeVersion;
	backendCommand.m_networkPacketCommand.m_bSourceExchange2 = bSourceExchange2;
	memcpy(backendCommand.m_networkPacketCommand.m_abyFileHash, pFile->GetFileHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash));
	backendCommand.m_networkPacketCommand.m_uFileRuntimeID = pFile->GetRuntimeID();
	if (IsZeroHash(backendCommand.m_networkPacketCommand.m_abyFileHash))
		return false;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceNetworkClient, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}

bool CemuleApp::QueueDownloadBlockReceiveNetworkCommand(CUpDownClient *pClient, CPartFile *pFile, const BYTE *pPacket, uint32 nSize, bool bCompressedBlock, bool bI64Offsets)
{
	if (pClient == NULL || pFile == NULL || pPacket == NULL || nSize == 0)
		return false;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandNetworkPacket;
	backendCommand.m_networkPacketCommand.m_eType = NetworkPacketCommandDownloadBlockReceive;
	backendCommand.m_networkPacketCommand.m_eParseDomain = bCompressedBlock ? NetworkParseCompressedBlock : NetworkParseClientTcp;
	backendCommand.m_networkPacketCommand.m_uClientRuntimeID = pClient->GetRuntimeID();
	backendCommand.m_networkPacketCommand.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	memcpy(backendCommand.m_networkPacketCommand.m_abyClientUserHash, pClient->GetUserHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyClientUserHash));
	backendCommand.m_networkPacketCommand.m_nClientIP = pClient->GetConnectIP().ToUInt32(false);
	backendCommand.m_networkPacketCommand.m_nClientUserPort = pClient->GetUserPort();
	backendCommand.m_networkPacketCommand.m_bCompressedBlock = bCompressedBlock;
	backendCommand.m_networkPacketCommand.m_bI64Offsets = bI64Offsets;
	memcpy(backendCommand.m_networkPacketCommand.m_abyFileHash, pFile->GetFileHash(), sizeof(backendCommand.m_networkPacketCommand.m_abyFileHash));
	backendCommand.m_networkPacketCommand.m_uFileRuntimeID = pFile->GetRuntimeID();
	if (IsZeroHash(backendCommand.m_networkPacketCommand.m_abyFileHash))
		return false;
	backendCommand.m_networkPacketCommand.m_packet.assign(pPacket, pPacket + nSize);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceNetworkClient, GetNetworkPacketOrderingScope(backendCommand.m_networkPacketCommand), BuildNetworkPacketOrderingKey(backendCommand.m_networkPacketCommand));
	return EnqueueNetworkPacketBackendCommand(backendCommand);
}
void CemuleApp::ExecuteDownloadListRemoveCommand(const CStringArray &astrItemHashes, bool bAddToCanceledMet, bool bDeleteCompletedFile, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey, bool bPreferUiChunkedRemove)
{
	if (astrItemHashes.GetSize() == 0)
		return;
	if (bPreferUiChunkedRemove && IsUiThread()) {
		CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
		if (pDownloadList != NULL && ::IsWindow(pDownloadList->GetSafeHwnd())) {
			pDownloadList->StartChunkedRemoveDownloadsFromCommand(astrItemHashes, bAddToCanceledMet, bDeleteCompletedFile);
			return;
		}
	}
	if ((!bPreferUiChunkedRemove || astrItemHashes.GetSize() >= static_cast<INT_PTR>(kLargeDownloadBatchThreshold)) && !HasBackendCommandThreadSignalTarget())
		StartBackendCommandThread();

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandDownload;
	backendCommand.m_downloadCommand.m_eType = DownloadCommandRemoveItems;
	backendCommand.m_downloadCommand.m_bAddToCanceledMet = bAddToCanceledMet;
	backendCommand.m_downloadCommand.m_bDeleteCompletedFile = bDeleteCompletedFile;
	CopyStringArray(astrItemHashes, backendCommand.m_downloadCommand.m_astrItemHashes);
	PrepareBackendCommandEnvelope(backendCommand, eSource, eScope, pszOrderingKey != NULL ? pszOrderingKey : (LPCTSTR)_T("download-list:remove"));
	const bool bQueued = EnqueueBackendCommand(backendCommand);
	if (bQueued && IsUiThread()) {
		const UINT uOverlayTotal = astrItemHashes.GetSize() > static_cast<INT_PTR>(UINT_MAX) ? UINT_MAX : static_cast<UINT>(astrItemHashes.GetSize());
		UpdateBackendDownloadCommandOverlays(true, 0, uOverlayTotal, backendCommand.m_uSequence, backendCommand.m_uCorrelationId);
	}
}

void CemuleApp::ExecuteDownloadListRemoveHashCommand(LPCTSTR pszItemHash, bool bAddToCanceledMet, bool bDeleteCompletedFile, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	CStringArray astrItemHashes;
	if (pszItemHash != NULL && pszItemHash[0] != _T('\0'))
		astrItemHashes.Add(pszItemHash);
	ExecuteDownloadListRemoveCommand(astrItemHashes, bAddToCanceledMet, bDeleteCompletedFile, eSource, eScope, pszOrderingKey);
}

void CemuleApp::ExecuteDownloadListStateCommand(const CStringArray &astrItemHashes, UINT uAction, int iActionValue, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	ExecuteDownloadListStateTextCommand(astrItemHashes, uAction, iActionValue, NULL, eSource, eScope, pszOrderingKey);
}

void CemuleApp::ExecuteDownloadListStateTextCommand(const CStringArray &astrItemHashes, UINT uAction, int iActionValue, LPCTSTR pszActionValue, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	if (astrItemHashes.GetSize() == 0)
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandDownload;
	backendCommand.m_downloadCommand.m_eType = DownloadCommandChangeState;
	backendCommand.m_downloadCommand.m_uAction = uAction;
	backendCommand.m_downloadCommand.m_iActionValue = iActionValue;
	if (pszActionValue != NULL)
		backendCommand.m_downloadCommand.m_strActionValue = pszActionValue;
	CopyStringArray(astrItemHashes, backendCommand.m_downloadCommand.m_astrItemHashes);
	PrepareBackendCommandEnvelope(backendCommand, eSource, eScope, pszOrderingKey != NULL ? pszOrderingKey : (LPCTSTR)_T("download-list:state"));
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::ExecuteDownloadListStateHashCommand(LPCTSTR pszItemHash, UINT uAction, int iActionValue, LPCTSTR pszActionValue, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	CStringArray astrItemHashes;
	if (pszItemHash != NULL && pszItemHash[0] != _T('\0'))
		astrItemHashes.Add(pszItemHash);
	ExecuteDownloadListStateTextCommand(astrItemHashes, uAction, iActionValue, pszActionValue, eSource, eScope, pszOrderingKey);
}

void CemuleApp::ExecuteWebServerDownloadActionCommand(LPCTSTR pszItemHash, LPCTSTR pszAction, int iActionValue, LPCTSTR pszActionValue)
{
	if (pszItemHash == NULL || pszItemHash[0] == _T('\0') || pszAction == NULL || pszAction[0] == _T('\0'))
		return;

	CString strOrderingKey;
	strOrderingKey.Format(_T("webserver:download:%s"), pszItemHash);
	if (_tcscmp(pszAction, _T("cancel")) == 0) {
		ExecuteDownloadListRemoveHashCommand(pszItemHash, false, false, BackendCommandSourceWebServer, BackendCommandOrderingWebRequest, strOrderingKey);
		return;
	}

	UINT uAction = 0;
	if (_tcscmp(pszAction, _T("stop")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStateStop);
	else if (_tcscmp(pszAction, _T("pause")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStatePause);
	else if (_tcscmp(pszAction, _T("resume")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStateResume);
	else if (_tcscmp(pszAction, _T("priolow")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStatePriorityLow);
	else if (_tcscmp(pszAction, _T("prionormal")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStatePriorityNormal);
	else if (_tcscmp(pszAction, _T("priohigh")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStatePriorityHigh);
	else if (_tcscmp(pszAction, _T("prioauto")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStatePriorityAuto);
	else if (_tcscmp(pszAction, _T("setcat")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStateSetCategory);
	else if (_tcscmp(pszAction, _T("rename")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStateSetFileName);
	else if (_tcscmp(pszAction, _T("getflc")) == 0)
		uAction = static_cast<UINT>(BackendDownloadStateTogglePreviewPriority);
	else
		return;

	ExecuteDownloadListStateHashCommand(pszItemHash, uAction, iActionValue, pszActionValue, BackendCommandSourceWebServer, BackendCommandOrderingWebRequest, strOrderingKey);
}

void CemuleApp::ExecuteWebServerClearCompletedCommand(LPCTSTR pszItemHash, int iCategory)
{
	CString strOrderingKey;
	if (pszItemHash != NULL && pszItemHash[0] != _T('\0')) {
		strOrderingKey.Format(_T("webserver:clear-completed:%s"), pszItemHash);
		ExecuteDownloadListStateHashCommand(pszItemHash, static_cast<UINT>(BackendDownloadStateClearCompleted), -1, NULL, BackendCommandSourceWebServer, BackendCommandOrderingWebRequest, strOrderingKey);
		return;
	}

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandDownload;
	backendCommand.m_downloadCommand.m_eType = DownloadCommandChangeState;
	backendCommand.m_downloadCommand.m_uAction = static_cast<UINT>(BackendDownloadStateClearCompleted);
	backendCommand.m_downloadCommand.m_iCat = iCategory;
	strOrderingKey.Format(_T("webserver:clear-completed:%d"), iCategory);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceWebServer, BackendCommandOrderingWebRequest, strOrderingKey);
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::ExecuteWebServerCategoryPriorityCommand(int iCategory, uint8 uPriority)
{
	if (iCategory < 0)
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandDownload;
	backendCommand.m_downloadCommand.m_eType = DownloadCommandChangeState;
	backendCommand.m_downloadCommand.m_uAction = static_cast<UINT>(BackendDownloadStateSetCategoryPriority);
	backendCommand.m_downloadCommand.m_iCat = iCategory;
	backendCommand.m_downloadCommand.m_iActionValue = static_cast<int>(uPriority);

	CString strOrderingKey;
	strOrderingKey.Format(_T("webserver:catprio:%d"), iCategory);
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceWebServer, BackendCommandOrderingWebRequest, strOrderingKey);
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::QueueDownloadListCommandEvent(EApplicationEventType eType, UINT uAction, UINT uProcessed, UINT uFailed, UINT uStale, UINT uTotal, uint64 uSequence, uint64 uCorrelationId,
	EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey, uint64 uCancellationToken)
{
	SApplicationEvent event;
	event.m_eType = eType;
	SetApplicationEventBackendEnvelope(event, BackendCommandDownload, eSource, eScope, pszOrderingKey != NULL ? pszOrderingKey : (LPCTSTR)_T("download-list"), uSequence, uCorrelationId);
	event.m_uAction = uAction;
	event.m_uProcessed = uProcessed;
	event.m_uFailed = uFailed;
	event.m_uStale = uStale;
	event.m_uTotal = uTotal;
	event.m_uCancellationToken = uCancellationToken;
	QueueApplicationEvent(event);
}

void CemuleApp::QueueDownloadListCommandFailureEvent(EApplicationEventType eType, UINT uAction, LPCTSTR pszStage, LPCTSTR pszFilePath, DWORD dwLastError, uint64 uSequence, uint64 uCorrelationId,
	EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey, uint64 uCancellationToken)
{
	SApplicationEvent event;
	event.m_eType = eType;
	SetApplicationEventBackendEnvelope(event, BackendCommandDownload, eSource, eScope, pszOrderingKey != NULL ? pszOrderingKey : (LPCTSTR)_T("download-list"), uSequence, uCorrelationId);
	event.m_eBackendCommandFailureKind = BackendCommandFailureApplyFailed;
	event.m_uAction = uAction;
	event.m_dwLastError = dwLastError;
	if (pszStage != NULL)
		event.m_strMessage = pszStage;
	if (pszFilePath != NULL)
		event.m_strFilePath = pszFilePath;
	event.m_uCancellationToken = uCancellationToken;
	QueueApplicationEvent(event);
}

void CemuleApp::QueueCollectionImportFailureEvent(LPCTSTR pszFilePath, LPCTSTR pszStage, DWORD dwLastError)
{
	SApplicationEvent event;
	event.m_eType = ApplicationEventCollectionImportFailed;
	SetApplicationEventBackendEnvelope(event, BackendCommandCollection, BackendCommandSourceUi, BackendCommandOrderingGlobal, pszFilePath != NULL ? pszFilePath : _T("collection"), 0, 0);
	event.m_eBackendCommandFailureKind = BackendCommandFailureApplyFailed;
	event.m_dwLastError = dwLastError;
	if (pszFilePath != NULL)
		event.m_strFilePath = pszFilePath;
	if (pszStage != NULL)
		event.m_strMessage = pszStage;
	QueueApplicationEvent(event);
}

void CemuleApp::QueuePersistenceCommandEvent(EApplicationEventType eType, EPersistenceCommandType eCommand, bool bAutoSave, LPCTSTR pszStage, DWORD dwLastError, uint64 uSequence, uint64 uCorrelationId)
{
	CString strOrderingKey;
	strOrderingKey.Format(_T("persistence:%u"), static_cast<UINT>(eCommand));
	SApplicationEvent event;
	event.m_eType = eType;
	SetApplicationEventBackendEnvelope(event, BackendCommandPersistence, BackendCommandSourcePersistence, BackendCommandOrderingPersistence, strOrderingKey, uSequence, uCorrelationId);
	event.m_ePersistenceCommandType = eCommand;
	event.m_bAutoSave = bAutoSave;
	event.m_dwLastError = dwLastError;
	if (eType == ApplicationEventPersistenceFailed)
		event.m_eBackendCommandFailureKind = BackendCommandFailureApplyFailed;
	if (pszStage != NULL)
		event.m_strMessage = pszStage;
	QueueApplicationEvent(event);
}

bool CemuleApp::QueuePersistenceWorkRequest(LPCTSTR pszReason)
{
	return QueuePersistenceWorkRequest(PersistenceCommandSaveKnownFiles, pszReason);
}

bool CemuleApp::QueuePersistenceWorkRequest(EPersistenceCommandType eCommand, LPCTSTR pszReason)
{
	CString strStartupMetadataSaveReason;
	if (!IsStartupMetadataSaveAllowed(eCommand, &strStartupMetadataSaveReason))
		return false; // Keep the pending save job for the timer retry path.

	SBackendCommand command;
	command.m_eType = BackendCommandPersistence;
	command.m_eSource = BackendCommandSourcePersistence;
	command.m_eOrderingScope = BackendCommandOrderingPersistence;
	command.m_strOrderingKey.Format(_T("persistence-work:%u"), static_cast<UINT>(eCommand));
	command.m_persistenceCommand.m_eType = eCommand;
	command.m_persistenceCommand.m_bWorkRequest = true;
	if (pszReason != NULL)
		command.m_persistenceCommand.m_strReason = pszReason;
	return EnqueueBackendCommand(command);
}

void CemuleApp::QueueSharedFilesCommandEvent(EApplicationEventType eType, UINT uAction, const std::vector<CString> &vecItemHashes, LPCTSTR pszStage, DWORD dwLastError, uint64 uSequence, uint64 uCorrelationId)
{
	SApplicationEvent event;
	event.m_eType = eType;
	SetApplicationEventBackendEnvelope(event, BackendCommandSharedFiles, BackendCommandSourceSharedFilesOwner, BackendCommandOrderingSharedFiles, _T("shared-files"), uSequence, uCorrelationId);
	event.m_eSharedFilesCommandType = GetSharedFilesCommandTypeForAction(uAction);
	event.m_uAction = uAction;
	event.m_vecItemHashes = vecItemHashes;
	event.m_dwLastError = dwLastError;
	if (eType == ApplicationEventSharedFilesCommandFailed)
		event.m_eBackendCommandFailureKind = BackendCommandFailureApplyFailed;
	if (pszStage != NULL)
		event.m_strMessage = pszStage;
	QueueApplicationEvent(event);
}


void CemuleApp::QueueSharedFilesListChangedEvent(LPCTSTR pszStage, uint64 uSequence, uint64 uCorrelationId)
{
	SApplicationEvent event;
	event.m_eType = ApplicationEventSharedFilesListChanged;
	SetApplicationEventBackendEnvelope(event, BackendCommandSharedFiles, BackendCommandSourceSharedFilesOwner, BackendCommandOrderingSharedFiles, _T("shared-files"), uSequence, uCorrelationId);
	event.m_strMessage = pszStage != NULL ? pszStage : _T("");
	QueueApplicationEvent(event);
}

void CemuleApp::QueueDownloadListChangedEvent(LPCTSTR pszStage, EBackendCommandSource eSource)
{
	if (IsClosing())
		return;

	SApplicationEvent event;
	event.m_eType = ApplicationEventDownloadListChanged;
	SetApplicationEventBackendEnvelope(event, BackendCommandDownload, eSource, BackendCommandOrderingDownloadList, _T("download-list"), 0, 0);
	event.m_strMessage = pszStage != NULL ? pszStage : _T("");
	QueueApplicationEvent(event);
}

void CemuleApp::QueueBulkOperationOverlayRefreshEvent(LPCTSTR pszStage)
{
	if (IsClosing())
		return;

	SApplicationEvent event;
	event.m_eType = ApplicationEventBulkOperationOverlayRefresh;
	SetApplicationEventBackendEnvelope(event, BackendCommandDownload, BackendCommandSourceDownloadModel, BackendCommandOrderingDownloadList, _T("bulk-operation-overlay"), 0, 0);
	event.m_strMessage = pszStage != NULL ? pszStage : _T("");
	QueueApplicationEvent(event);
}

void CemuleApp::QueueDownloadListRowsRemovedEvent(const std::vector<CString>& vecFileHashes, uint64 uSequence, uint64 uCorrelationId)
{
	if (IsClosing() || vecFileHashes.empty())
		return;

	SApplicationEvent event;
	event.m_eType = ApplicationEventDownloadListRowsRemoved;
	event.m_eDownloadCommandType = DownloadCommandRemoveItems;
	SetApplicationEventBackendEnvelope(event, BackendCommandDownload, BackendCommandSourceUi, BackendCommandOrderingDownloadList, _T("download-list"), uSequence, uCorrelationId);
	event.m_eBackendCommandFamily = BackendCommandFamilyDownloadRemoveItems;
	event.m_vecItemHashes = vecFileHashes;
	QueueApplicationEvent(event);
}

void CemuleApp::QueueDownloadListDeletedCompletedRowsRemovedEvent(const std::vector<CString>& vecFileHashes)
{
	if (IsClosing() || vecFileHashes.empty())
		return;

	SApplicationEvent event;
	event.m_eType = ApplicationEventDownloadListDeletedCompletedRowsRemoved;
	SetApplicationEventBackendEnvelope(event, BackendCommandDownload, BackendCommandSourceUi, BackendCommandOrderingDownloadList, _T("download-list-deleted-completed"), 0, 0);
	event.m_vecItemHashes = vecFileHashes;
	QueueApplicationEvent(event);
}

bool CemuleApp::QueueImportPartWorkItem(SImportPartWorkItem *pItem)
{
	if (pItem == NULL)
		return false;
	if (IsClosing()) {
		ReleaseImportPartWorkItem(pItem, true);
		return false;
	}

	{
		CSingleLock lock(&m_importPartWorkQueueLock, TRUE);
		m_importPartWorkItems.AddTail(pItem);
	}

	if (PostBackendCommandMessage())
		return true;

	bool bRemoved = false;
	{
		CSingleLock lock(&m_importPartWorkQueueLock, TRUE);
		POSITION pos = m_importPartWorkItems.GetHeadPosition();
		while (pos != NULL) {
			POSITION posCurrent = pos;
			SImportPartWorkItem *pQueuedItem = m_importPartWorkItems.GetNext(pos);
			if (pQueuedItem == pItem) {
				m_importPartWorkItems.RemoveAt(posCurrent);
				bRemoved = true;
				break;
			}
		}
	}
	if (bRemoved)
		ReleaseImportPartWorkItem(pItem, true);
	return !bRemoved;
}

bool CemuleApp::QueueImportPartWrite(ImportPart_Struct *pImportPart)
{
	if (pImportPart == NULL)
		return false;
	SImportPartWorkItem *pItem = new SImportPartWorkItem;
	pItem->m_eType = ImportPartWorkWrite;
	pItem->m_pImportPart = pImportPart;
	pItem->m_pContext = pImportPart->pContext;
	return QueueImportPartWorkItem(pItem);
}

bool CemuleApp::QueueImportPartProgress(ImportOperationContext *pContext, WPARAM uProgress)
{
	if (pContext == NULL)
		return false;

	{
		CSingleLock lock(&m_importPartWorkQueueLock, TRUE);
		POSITION pos = m_importPartWorkItems.GetHeadPosition();
		while (pos != NULL) {
			SImportPartWorkItem *pQueuedItem = m_importPartWorkItems.GetNext(pos);
			if (pQueuedItem != NULL && pQueuedItem->m_eType == ImportPartWorkProgress && pQueuedItem->m_pContext == pContext) {
				pQueuedItem->m_uProgress = uProgress;
				return true;
			}
		}
	}

	SImportPartWorkItem *pItem = new SImportPartWorkItem;
	pItem->m_eType = ImportPartWorkProgress;
	pItem->m_pContext = AcquireImportOperationContext(pContext);
	pItem->m_uProgress = uProgress;
	return QueueImportPartWorkItem(pItem);
}

bool CemuleApp::QueueImportPartFinished(ImportOperationContext *pContext, bool bAborted)
{
	if (pContext == NULL)
		return false;
	SImportPartWorkItem *pItem = new SImportPartWorkItem;
	pItem->m_eType = ImportPartWorkFinished;
	pItem->m_pContext = AcquireImportOperationContext(pContext);
	pItem->m_bAborted = bAborted;
	return QueueImportPartWorkItem(pItem);
}

void CemuleApp::QueueSharedFilesCommandStatusEvent(EApplicationEventType eType, UINT uAction, UINT uProcessed, UINT uFailed, UINT uStale, UINT uTotal, uint64 uSequence, uint64 uCorrelationId)
{
	SApplicationEvent event;
	event.m_eType = eType;
	SetApplicationEventBackendEnvelope(event, BackendCommandSharedFiles, BackendCommandSourceSharedFilesOwner, BackendCommandOrderingSharedFiles, _T("shared-files"), uSequence, uCorrelationId);
	event.m_eSharedFilesCommandType = GetSharedFilesCommandTypeForAction(uAction);
	event.m_uAction = uAction;
	event.m_uProcessed = uProcessed;
	event.m_uFailed = uFailed;
	event.m_uStale = uStale;
	event.m_uTotal = uTotal;
	QueueApplicationEvent(event);
}

void CemuleApp::QueueSharedFilesCommandFailureEvent(UINT uAction, LPCTSTR pszStage, LPCTSTR pszFilePath, DWORD dwLastError, uint64 uSequence, uint64 uCorrelationId)
{
	SApplicationEvent event;
	event.m_eType = ApplicationEventSharedFilesCommandItemFailed;
	SetApplicationEventBackendEnvelope(event, BackendCommandSharedFiles, BackendCommandSourceSharedFilesOwner, BackendCommandOrderingSharedFiles, _T("shared-files"), uSequence, uCorrelationId);
	event.m_eBackendCommandFailureKind = BackendCommandFailureApplyFailed;
	event.m_eSharedFilesCommandType = GetSharedFilesCommandTypeForAction(uAction);
	event.m_uAction = uAction;
	event.m_dwLastError = dwLastError;
	if (pszStage != NULL)
		event.m_strMessage = pszStage;
	if (pszFilePath != NULL)
		event.m_strFilePath = pszFilePath;
	QueueApplicationEvent(event);
}
void CemuleApp::QueueAsyncDiskWriteResultEvent(LPCTSTR pszName, LONG lGeneration, LPCTSTR pszResult, LPCTSTR pszReason, LPCTSTR pszTempPath, LPCTSTR pszFinalPath, bool bShutdownFallback, DWORD dwLastError)
{
	CString strOrderingKey(_T("async-disk"));
	if (pszName != NULL && pszName[0] != _T('\0')) {
		strOrderingKey += _T(":");
		strOrderingKey += pszName;
	}
	SApplicationEvent event;
	event.m_eType = ApplicationEventAsyncDiskWriteResult;
	SetApplicationEventBackendEnvelope(event, BackendCommandPersistence, BackendCommandSourcePersistence, BackendCommandOrderingPersistence, strOrderingKey, 0, 0);
	event.m_lAsyncGeneration = lGeneration;
	event.m_bAsyncShutdownFallback = bShutdownFallback;
	event.m_dwLastError = dwLastError;
	if (pszName != NULL)
		event.m_strAsyncName = pszName;
	if (pszResult != NULL)
		event.m_strAsyncResult = pszResult;
	if (pszReason != NULL)
		event.m_strAsyncReason = pszReason;
	if (pszTempPath != NULL)
		event.m_strAsyncTempPath = pszTempPath;
	if (pszFinalPath != NULL)
		event.m_strFilePath = pszFinalPath;
	QueueApplicationEvent(event);
}

void CemuleApp::QueuePartFileOwnerStateEvent(EApplicationEventType eType, LPCTSTR pszFileHash, DWORD uFileRuntimeID, LPCTSTR pszStage, DWORD dwLastError)
{
	if (eType != ApplicationEventPartFileOwnerStateChanged && eType != ApplicationEventPartFileDiskWriteRequested && eType != ApplicationEventPartFileOwnerFailed)
		return;
	if (eType != ApplicationEventPartFileOwnerFailed && emuledlg != NULL && emuledlg->IsStartupLoadingDialogVisible()) {
		const bool bDisplayInfoChanged = pszStage != NULL && _tcscmp(pszStage, _T("display-info-changed")) == 0;
		if (!bDisplayInfoChanged)
			return;
	}

	CString strOrderingKey(_T("file"));
	if (pszFileHash != NULL && pszFileHash[0] != _T('\0')) {
		strOrderingKey = _T("file:");
		strOrderingKey += pszFileHash;
	}

	SApplicationEvent event;
	event.m_eType = eType;
	SetApplicationEventBackendEnvelope(event, BackendCommandDownload, BackendCommandSourceDiskIo, BackendCommandOrderingFileHash, strOrderingKey, 0, 0);
	if (eType == ApplicationEventPartFileDiskWriteRequested)
		event.m_eBackendCommandFamily = BackendCommandFamilyDownloadPartMetSnapshotWrite;
	else if (pszStage != NULL && _tcsncmp(pszStage, _T("file-completion"), 15) == 0)
		event.m_eBackendCommandFamily = BackendCommandFamilyDownloadFileCompletion;
	else
		event.m_eBackendCommandFamily = BackendCommandFamilyDownloadCompletePart;
	event.m_uAction = uFileRuntimeID;
	event.m_dwLastError = dwLastError;
	if (pszFileHash != NULL)
		event.m_strFileHash = pszFileHash;
	if (pszStage != NULL)
		event.m_strMessage = pszStage;
	if (eType == ApplicationEventPartFileOwnerFailed)
		event.m_eBackendCommandFailureKind = BackendCommandFailureApplyFailed;
	QueueApplicationEvent(event);
}

void CemuleApp::QueueClientRowUpdateEvent(DWORD uClientRuntimeID, LONG lRuntimeGeneration, LPCTSTR pszStage)
{
	if (uClientRuntimeID == 0 || lRuntimeGeneration == 0)
		return;

	SApplicationEvent event;
	event.m_eType = ApplicationEventClientRowUpdateRequested;
	event.m_uClientRuntimeID = uClientRuntimeID;
	event.m_lClientRuntimeGeneration = lRuntimeGeneration;
	event.m_strMessage = pszStage != NULL ? pszStage : _T("");
	event.m_eBackendCommandSource = BackendCommandSourceTimer;
	event.m_eBackendCommandOrderingScope = BackendCommandOrderingClient;
	event.m_strBackendCommandOrderingKey.Format(_T("client:%lu"), uClientRuntimeID);
	QueueApplicationEvent(event);
}

void CemuleApp::QueueKadConnectionStateChangedEvent(LPCTSTR pszStage)
{
	SApplicationEvent event;
	event.m_eType = ApplicationEventKadConnectionStateChanged;
	SetApplicationEventBackendEnvelope(event, BackendCommandNetworkPacket, BackendCommandSourceNetworkKad, BackendCommandOrderingKad, _T("kad:connection-state"), 0, 0);
	event.m_eBackendCommandFamily = BackendCommandFamilyNetworkKadPacket;
	event.m_strMessage = pszStage != NULL ? pszStage : _T("");
	QueueApplicationEvent(event);
}

void CemuleApp::QueueKadUiStatusRefreshEvent(UINT uStatusFlags, LPCTSTR pszStage)
{
	if (uStatusFlags == 0)
		return;

	SApplicationEvent event;
	event.m_eType = ApplicationEventKadUiStatusRefresh;
	SetApplicationEventBackendEnvelope(event, BackendCommandNetworkPacket, BackendCommandSourceNetworkKad, BackendCommandOrderingKad, _T("kad:ui-status"), 0, 0);
	event.m_eBackendCommandFamily = BackendCommandFamilyNetworkKadPacket;
	event.m_uAction = uStatusFlags;
	event.m_strMessage = pszStage != NULL ? pszStage : _T("");
	QueueApplicationEvent(event);
}

bool CemuleApp::QueueKadSearchCancelUiEvent(uint32 uSearchID, LPCTSTR pszStage)
{
	if (uSearchID == 0)
		return false;

	CString strOrderingKey;
	strOrderingKey.Format(_T("kad:search-cancel:%u"), uSearchID);

	SApplicationEvent event;
	event.m_eType = ApplicationEventKadSearchCancelUiRequested;
	SetApplicationEventBackendEnvelope(event, BackendCommandNetworkPacket, BackendCommandSourceNetworkKad, BackendCommandOrderingKad, strOrderingKey, 0, 0);
	event.m_eBackendCommandFamily = BackendCommandFamilyNetworkKadPacket;
	event.m_uSearchID = uSearchID;
	event.m_strMessage = pszStage != NULL ? pszStage : _T("");
	return QueueApplicationEvent(event);
}

void CemuleApp::QueueClientChatMessageEvent(CUpDownClient *pClient, LPCTSTR pszMessage)
{
	if (pClient == NULL)
		return;
	SApplicationEvent event;
	event.m_eType = ApplicationEventClientChatMessage;
	event.m_eBackendCommandType = BackendCommandUpload;
	event.m_eBackendCommandSource = BackendCommandSourceNetworkClient;
	event.m_eBackendCommandOrderingScope = BackendCommandOrderingClient;
	event.m_uClientRuntimeID = pClient->GetRuntimeID();
	event.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	event.m_strMessage = pszMessage != NULL ? pszMessage : _T("");
	event.m_strBackendCommandOrderingKey.Format(_T("client:%lu/chat"), event.m_uClientRuntimeID);
	QueueApplicationEvent(event);
}

void CemuleApp::QueueClientChatCloseEvent(CUpDownClient *pClient, LPCTSTR pszStage)
{
	if (pClient == NULL)
		return;
	SApplicationEvent event;
	event.m_eType = ApplicationEventClientChatCloseRequested;
	event.m_eBackendCommandType = BackendCommandUpload;
	event.m_eBackendCommandSource = BackendCommandSourceNetworkClient;
	event.m_eBackendCommandOrderingScope = BackendCommandOrderingClient;
	event.m_uClientRuntimeID = pClient->GetRuntimeID();
	event.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	event.m_strMessage = pszStage != NULL ? pszStage : _T("chat-close");
	event.m_strBackendCommandOrderingKey.Format(_T("client:%lu/chat"), event.m_uClientRuntimeID);
	QueueApplicationEvent(event);
}

void CemuleApp::QueueClientCaptchaRequestEvent(CUpDownClient *pClient, HBITMAP hCaptcha)
{
	if (pClient == NULL)
		return;
	SApplicationEvent event;
	event.m_eType = ApplicationEventClientCaptchaRequested;
	event.m_eBackendCommandType = BackendCommandUpload;
	event.m_eBackendCommandSource = BackendCommandSourceNetworkClient;
	event.m_eBackendCommandOrderingScope = BackendCommandOrderingClient;
	event.m_uClientRuntimeID = pClient->GetRuntimeID();
	event.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	event.m_hClientBitmap = hCaptcha;
	event.m_strBackendCommandOrderingKey.Format(_T("client:%lu/chat"), event.m_uClientRuntimeID);
	bool bEventOwnedByQueue = false;
	if (!QueueApplicationEvent(event, &bEventOwnedByQueue) && !bEventOwnedByQueue && hCaptcha != NULL)
		::DeleteObject(hCaptcha);
}

void CemuleApp::QueueClientCaptchaResultEvent(CUpDownClient *pClient, LPCTSTR pszResult)
{
	if (pClient == NULL)
		return;
	SApplicationEvent event;
	event.m_eType = ApplicationEventClientCaptchaResult;
	event.m_eBackendCommandType = BackendCommandUpload;
	event.m_eBackendCommandSource = BackendCommandSourceNetworkClient;
	event.m_eBackendCommandOrderingScope = BackendCommandOrderingClient;
	event.m_uClientRuntimeID = pClient->GetRuntimeID();
	event.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	event.m_strMessage = pszResult != NULL ? pszResult : _T("");
	event.m_strBackendCommandOrderingKey.Format(_T("client:%lu/chat"), event.m_uClientRuntimeID);
	QueueApplicationEvent(event);
}

void CemuleApp::QueueClientChatConnectingResultEvent(CUpDownClient *pClient, bool bSuccess)
{
	if (pClient == NULL)
		return;
	SApplicationEvent event;
	event.m_eType = ApplicationEventClientChatConnectingResult;
	event.m_eBackendCommandType = BackendCommandUpload;
	event.m_eBackendCommandSource = BackendCommandSourceNetworkClient;
	event.m_eBackendCommandOrderingScope = BackendCommandOrderingClient;
	event.m_uClientRuntimeID = pClient->GetRuntimeID();
	event.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	event.m_iActionValue = bSuccess ? 1 : 0;
	event.m_strBackendCommandOrderingKey.Format(_T("client:%lu/chat"), event.m_uClientRuntimeID);
	QueueApplicationEvent(event);
}

void CemuleApp::QueueClientChatConnectionProgressEvent(CUpDownClient *pClient, LPCTSTR pszProgressDesc, bool bNoTimeStamp)
{
	if (pClient == NULL)
		return;
	SApplicationEvent event;
	event.m_eType = ApplicationEventClientChatConnectionProgress;
	event.m_eBackendCommandType = BackendCommandUpload;
	event.m_eBackendCommandSource = BackendCommandSourceNetworkClient;
	event.m_eBackendCommandOrderingScope = BackendCommandOrderingClient;
	event.m_uClientRuntimeID = pClient->GetRuntimeID();
	event.m_lClientRuntimeGeneration = pClient->GetRuntimeGeneration();
	event.m_iActionValue = bNoTimeStamp ? 1 : 0;
	event.m_strMessage = pszProgressDesc != NULL ? pszProgressDesc : _T("");
	event.m_strBackendCommandOrderingKey.Format(_T("client:%lu/chat"), event.m_uClientRuntimeID);
	QueueApplicationEvent(event);
}

void CemuleApp::QueueSearchPacketParseEvent(EApplicationEventType eType, uint32 nSearchID, UINT uProcessed, UINT uFailed, UINT uTotal, LPCTSTR pszStage)
{
	if (IsClosing())
		return;

	CString strOrderingKey;
	strOrderingKey.Format(_T("search:%u"), nSearchID);
	SApplicationEvent event;
	event.m_eType = eType;
	SetApplicationEventBackendEnvelope(event, BackendCommandSearch, BackendCommandSourceUnknown, BackendCommandOrderingSearch, strOrderingKey, 0, 0);
	event.m_uSearchID = nSearchID;
	event.m_lSearchGeneration = searchlist != NULL ? searchlist->GetSearchAnswerParseGeneration(nSearchID) : 0;
	event.m_uProcessed = uProcessed;
	event.m_uFailed = uFailed;
	event.m_uTotal = uTotal;
	if (eType == ApplicationEventSearchPacketParseFailed)
		event.m_eBackendCommandFailureKind = BackendCommandFailureApplyFailed;
	if (pszStage != NULL)
		event.m_strMessage = pszStage;
	QueueApplicationEvent(event);
}

void CemuleApp::QueueNetworkParserFailureEvent(ENetworkParseDomain eDomain, LPCTSTR pszStage, DWORD dwLastError)
{
	if (IsClosing())
		return;

	const LPCTSTR pszDomain = GetNetworkParseDomainName(eDomain);
	SApplicationEvent event;
	event.m_eType = ApplicationEventCommandFailed;
	SetApplicationEventBackendEnvelope(event, BackendCommandNetworkPacket, BackendCommandSourceUnknown, BackendCommandOrderingGlobal, pszDomain, 0, 0);
	event.m_eBackendCommandFailureKind = BackendCommandFailureInvalidPayload;
	event.m_dwLastError = dwLastError;
	event.m_strMessage.Format(_T("Network parser failure. domain=%s stage=%s error=%lu"), pszDomain, pszStage != NULL ? pszStage : _T("unknown"), dwLastError);
	QueueApplicationEvent(event);
}

void CemuleApp::QueueLocalEd2kSearchEndEvent(uint32 nSearchID, UINT uCount, bool bMoreResultsAvailable)
{
	if (IsClosing())
		return;

	CString strOrderingKey;
	strOrderingKey.Format(_T("search:%u"), nSearchID);
	SApplicationEvent event;
	event.m_eType = ApplicationEventLocalEd2kSearchEnd;
	SetApplicationEventBackendEnvelope(event, BackendCommandSearch, BackendCommandSourceUi, BackendCommandOrderingSearch, strOrderingKey, 0, 0);
	event.m_uSearchID = nSearchID;
	event.m_lSearchGeneration = searchlist != NULL ? searchlist->GetSearchAnswerParseGeneration(nSearchID) : 0;
	event.m_uTotal = uCount;
	event.m_bMoreResultsAvailable = bMoreResultsAvailable;
	QueueApplicationEvent(event);
}

void CemuleApp::QueueSearchActivityChangedEvent(uint32 nSearchID)
{
	if (IsClosing())
		return;

	CString strOrderingKey;
	if (nSearchID != 0)
		strOrderingKey.Format(_T("search:%u"), nSearchID);
	else
		strOrderingKey = _T("search:activity");

	SApplicationEvent event;
	event.m_eType = ApplicationEventSearchPacketParseProgress;
	SetApplicationEventBackendEnvelope(event, BackendCommandSearch, BackendCommandSourceUnknown, BackendCommandOrderingSearch, strOrderingKey, 0, 0);
	event.m_uSearchID = nSearchID;
	event.m_lSearchGeneration = nSearchID != 0 && searchlist != NULL ? searchlist->GetSearchAnswerParseGeneration(nSearchID) : 0;
	event.m_strMessage = _T("search-activity-changed");
	QueueApplicationEvent(event);
}

bool CemuleApp::QueueSearchIngestProcessing()
{
	if (IsClosing())
		return false;

	::InterlockedIncrement(&m_lSearchIngestProcessingPending);

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandSearch;
	backendCommand.m_searchCommand.m_eType = SearchCommandIngestApply;
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceTimer, BackendCommandOrderingSearch, _T("search:ingest-apply"));
	if (!EnqueueBackendCommand(backendCommand)) {
		const LONG lRemaining = ::InterlockedDecrement(&m_lSearchIngestProcessingPending);
		if (lRemaining < 0)
			::InterlockedExchange(&m_lSearchIngestProcessingPending, 0);
		AddDebugLogLine(DLP_HIGH, false, _T("Search ingest apply command could not be queued. async=%u\n"), static_cast<UINT>(UseAsyncBackendCommandExecution()));
		return false;
	}

	return true;
}

void CemuleApp::QueueSearchResultsChangedEvent(uint32 nSearchID, const CString &strClientHash, bool bUseKadReloadThrottle)
{
	if (IsClosing())
		return;

	CString strOrderingKey;
	strOrderingKey.Format(_T("search:%u"), nSearchID);
	SApplicationEvent event;
	event.m_eType = ApplicationEventSearchResultsChanged;
	SetApplicationEventBackendEnvelope(event, BackendCommandSearch, BackendCommandSourceSearchIngest, BackendCommandOrderingSearch, strOrderingKey, 0, 0);
	event.m_eSearchCommandType = SearchCommandIngestApply;
	event.m_uSearchID = nSearchID;
	event.m_lSearchGeneration = searchlist != NULL ? searchlist->GetSearchAnswerParseGeneration(nSearchID) : 0;
	event.m_strMessage = strClientHash;
	event.m_bUseKadReloadThrottle = bUseKadReloadThrottle;
	QueueApplicationEvent(event);
}

void CemuleApp::QueueUploadClientRowsChanged(const CUpDownClient* pClient, UINT uTargetFlags)
{
	if (pClient == NULL || pClient->GetRuntimeID() == 0 || uTargetFlags == 0 || IsClosing())
		return;

	SUploadCommand command;
	command.m_eType = UploadCommandClientRowsChanged;
	command.m_uRuntimeID = pClient->GetRuntimeID();
	command.m_lRuntimeGeneration = pClient->GetRuntimeGeneration();
	command.m_uTargetFlags = uTargetFlags;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandUpload;
	CopyUploadCommand(command, backendCommand.m_uploadCommand);
	CString strOrderingKey;
	strOrderingKey.Format(_T("client:%lu"), command.m_uRuntimeID);
	PrepareBackendCommandEnvelope(backendCommand, IsUiThread() ? BackendCommandSourceUi : BackendCommandSourceUploadModel, BackendCommandOrderingClient, strOrderingKey);
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::QueueUploadClientUiRemove(const CUpDownClient* pClient, UINT uTargetFlags, LPCTSTR pszStage)
{
	if (pClient == NULL || pClient->GetRuntimeID() == 0 || uTargetFlags == 0 || IsClosing())
		return;

	SUploadCommand command;
	command.m_eType = UploadCommandClientRowsRemoved;
	command.m_uRuntimeID = pClient->GetRuntimeID();
	command.m_lRuntimeGeneration = pClient->GetRuntimeGeneration();
	command.m_uTargetFlags = uTargetFlags;
	command.m_strStage = pszStage != NULL ? pszStage : _T("upload-client-rows-removed");

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandUpload;
	CopyUploadCommand(command, backendCommand.m_uploadCommand);
	CString strOrderingKey;
	strOrderingKey.Format(_T("client:%lu"), command.m_uRuntimeID);
	PrepareBackendCommandEnvelope(backendCommand, IsUiThread() ? BackendCommandSourceUi : BackendCommandSourceUploadModel, BackendCommandOrderingClient, strOrderingKey);
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::QueueUploadListChangedEvent(UINT uTargetFlags, LPCTSTR pszStage, EBackendCommandSource eSource)
{
	if (uTargetFlags == 0 || IsClosing())
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandUpload;
	backendCommand.m_uploadCommand.m_eType = ((uTargetFlags & UploadClientUiTargetQueueList) != 0 && (uTargetFlags & UploadClientUiTargetUploadList) == 0) ? UploadCommandQueueListChanged : UploadCommandUploadListChanged;
	backendCommand.m_uploadCommand.m_uTargetFlags = uTargetFlags;
	backendCommand.m_uploadCommand.m_strStage = pszStage != NULL ? pszStage : _T("upload-list-changed");
	if (eSource == BackendCommandSourceUnknown)
		eSource = IsUiThread() ? BackendCommandSourceUi : BackendCommandSourceUploadModel;
	PrepareBackendCommandEnvelope(backendCommand, eSource, BackendCommandOrderingUploadList, _T("upload:list-changed"));
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::QueueUploadBandwidthSnapshotEvent(LPCTSTR pszStage)
{
	if (IsClosing())
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandUpload;
	backendCommand.m_uploadCommand.m_eType = UploadCommandBandwidthSnapshotChanged;
	backendCommand.m_uploadCommand.m_uTargetFlags = UploadClientUiTargetUploadList;
	backendCommand.m_uploadCommand.m_strStage = pszStage != NULL ? pszStage : _T("upload-bandwidth-snapshot");
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceTimer, BackendCommandOrderingUploadList, _T("upload:bandwidth"));
	EnqueueBackendCommand(backendCommand);
}

void CemuleApp::QueueUploadDiskIoResultEvent(const CUpDownClient* pClient, LPCTSTR pszStage, DWORD dwLastError)
{
	if (pClient == NULL || pClient->GetRuntimeID() == 0 || IsClosing())
		return;

	SBackendCommand backendCommand;
	backendCommand.m_eType = BackendCommandUpload;
	backendCommand.m_uploadCommand.m_eType = UploadCommandDiskIoResult;
	backendCommand.m_uploadCommand.m_uRuntimeID = pClient->GetRuntimeID();
	backendCommand.m_uploadCommand.m_lRuntimeGeneration = pClient->GetRuntimeGeneration();
	backendCommand.m_uploadCommand.m_uTargetFlags = UploadClientUiTargetUploadList;
	backendCommand.m_uploadCommand.m_strStage = pszStage != NULL ? pszStage : _T("upload-disk-io-result");
	PrepareBackendCommandEnvelope(backendCommand, BackendCommandSourceDiskIo, BackendCommandOrderingClient, _T("upload:disk-io"));
	SApplicationEvent event;
	event.m_eType = ApplicationEventUploadDiskIoResult;
	CopyBackendCommandEnvelopeToEvent(backendCommand, event);
	event.m_eUploadCommandType = backendCommand.m_uploadCommand.m_eType;
	event.m_uClientRuntimeID = backendCommand.m_uploadCommand.m_uRuntimeID;
	event.m_lClientRuntimeGeneration = backendCommand.m_uploadCommand.m_lRuntimeGeneration;
	event.m_uUploadTargetFlags = backendCommand.m_uploadCommand.m_uTargetFlags;
	event.m_strMessage = backendCommand.m_uploadCommand.m_strStage;
	event.m_dwLastError = dwLastError;
	QueueApplicationEvent(event);
}

void CemuleApp::SetBackendOwnerThreadId(DWORD dwThreadId)
{
	m_dwBackendOwnerThreadId = dwThreadId;
}

void CemuleApp::SetNetworkParserOwnerThreadId(DWORD dwThreadId)
{
	m_dwNetworkParserOwnerThreadId = dwThreadId;
}

bool CemuleApp::IsUiThread() const
{
	return g_uMainThreadId == 0 || ::GetCurrentThreadId() == g_uMainThreadId;
}

bool CemuleApp::IsBackendOwnerThread() const
{
	if (m_dwBackendOwnerThreadId != 0)
		return ::GetCurrentThreadId() == m_dwBackendOwnerThreadId;
	return !UseAsyncBackendCommandExecution() && IsUiThread();
}

bool CemuleApp::IsNetworkParserThread() const
{
	return m_dwNetworkParserOwnerThreadId != 0 && ::GetCurrentThreadId() == m_dwNetworkParserOwnerThreadId;
}

bool CemuleApp::IsPersistenceWorkerThread() const
{
	const DWORD dwPersistenceThreadId = m_dwWorkerTopologyThreadIds[static_cast<int>(WorkerTopologyPersistence)];
	return dwPersistenceThreadId != 0 && ::GetCurrentThreadId() == dwPersistenceThreadId;
}

bool CemuleApp::IsModelMutationAllowed(EModelMutationDomain eDomain) const
{
	if (IsUiThread())
		return true;
	if (!IsBackendOwnerThread())
		return false;

	switch (eDomain) {
		case ModelMutationSearchList:
		case ModelMutationSearchFile:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAddFileLinks)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilySearchIngestApply)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkClientSearchAnswer)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkServerSearchAnswer)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkServerUdpSearchAnswer);
		case ModelMutationUploadQueue:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyUploadClientRowsChanged)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyUploadQueueListChanged)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyUploadListChanged)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyUploadBandwidthSnapshot);
		case ModelMutationDownloadQueue:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAddFileLinks)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadRemoveItems)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadChangeState)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadChangeStateOwnerSafe)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadFoundSources)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadBlockRequest)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadBlockReceive)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadCorruptedBlock)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadCompletePart)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadFileCompletion)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAichVerification);
		case ModelMutationPartFile:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAddFileLinks)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadRemoveItems)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadChangeStateOwnerSafe)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadFileStatus)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadHashSet)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadFoundSources)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadSourceExchange)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadBlockRequest)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadBlockReceive)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadCorruptedBlock)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadCompletePart)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadFileCompletion)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAichVerification)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadPartMetSnapshotWrite);
		case ModelMutationClientList:
		case ModelMutationUpDownClient:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAddFileLinks)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadRemoveItems)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadChangeStateOwnerSafe)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadFileStatus)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadHashSet)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadFoundSources)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkDownloadSourceExchange)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadBlockRequest)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadBlockReceive)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadCorruptedBlock)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadFileCompletion)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAichVerification);
		case ModelMutationKad:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyNetworkKadPacket)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadChangeStateOwnerSafe)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadFileCompletion);
		case ModelMutationSharedFiles:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadRemoveItems)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadCompletePart)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadFileCompletion)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAichVerification);
		case ModelMutationKnownFiles:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadRemoveItems)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadFileCompletion);
		case ModelMutationPreferences:
			return IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadFileCompletion)
				|| IsBackendCommandFamilyReadyForBackendOwnerThread(BackendCommandFamilyDownloadAichVerification);
		case ModelMutationFriendList:
		case ModelMutationServerList:
		case ModelMutationWebServer:
			return false;
	}
	return false;
}

bool CemuleApp::IsNetworkParseAllowed(ENetworkParseDomain /*eDomain*/) const
{
	return IsNetworkParserThread();
}

LPCTSTR CemuleApp::GetModelMutationDomainName(EModelMutationDomain eDomain) const
{
	switch (eDomain) {
		case ModelMutationDownloadQueue:	return _T("CDownloadQueue");
		case ModelMutationSearchList:	return _T("CSearchList");
		case ModelMutationUploadQueue:	return _T("CUploadQueue");
		case ModelMutationClientList:	return _T("CClientList");
		case ModelMutationPartFile:	return _T("CPartFile");
		case ModelMutationSearchFile:	return _T("CSearchFile");
		case ModelMutationUpDownClient:	return _T("CUpDownClient");
		case ModelMutationSharedFiles:	return _T("CSharedFileList");
		case ModelMutationKnownFiles:	return _T("CKnownFileList");
		case ModelMutationPreferences:	return _T("CPreferences");
		case ModelMutationFriendList:	return _T("CFriendList");
		case ModelMutationServerList:	return _T("CServerList");
		case ModelMutationKad:	return _T("Kademlia");
		case ModelMutationWebServer:	return _T("CWebServer");
	}
	return _T("UnknownDomain");
}

bool CemuleApp::GuardModelMutation(EModelMutationDomain eDomain, LPCTSTR pszEntryPoint)
{
	if (IsModelMutationAllowed(eDomain))
		return true;

	const DWORD dwCurrentThreadId = ::GetCurrentThreadId();
	const LPCTSTR pszDomain = GetModelMutationDomainName(eDomain);
	const LPCTSTR pszSafeEntryPoint = (pszEntryPoint != NULL) ? pszEntryPoint : _T("unknown");
	AddDebugLogLine(DLP_HIGH, false, _T("Model mutation owner violation. domain=%s entry=%s current=%lu ui=%u backend=%lu closing=%u\n"), pszDomain, pszSafeEntryPoint, dwCurrentThreadId, g_uMainThreadId, m_dwBackendOwnerThreadId, static_cast<UINT>(IsClosing()));

	if (!IsClosing() && ::InterlockedCompareExchange(&m_lBackendCommandDispatching, 0, 0) == 0) {
		SApplicationEvent event;
		event.m_eType = ApplicationEventCommandFailed;
		SetApplicationEventBackendEnvelope(event, GetBackendCommandTypeForModelMutationDomain(eDomain), BackendCommandSourceUnknown, GetBackendCommandOrderingScopeForModelMutationDomain(eDomain), pszDomain, 0, 0);
		event.m_eBackendCommandFailureKind = BackendCommandFailureOwnerGuard;
		event.m_strMessage.Format(_T("Model mutation owner violation. domain=%s entry=%s current=%lu ui=%u backend=%lu"), pszDomain, pszSafeEntryPoint, dwCurrentThreadId, g_uMainThreadId, m_dwBackendOwnerThreadId);
		QueueApplicationEvent(event);
	}
	return false;
}

LPCTSTR CemuleApp::GetNetworkParseDomainName(ENetworkParseDomain eDomain) const
{
	switch (eDomain) {
		case NetworkParseSearchAnswer:	return _T("SearchAnswerParser");
		case NetworkParseClientTcp:	return _T("ClientTcpParser");
		case NetworkParseClientUdp:	return _T("ClientUdpParser");
		case NetworkParseServerTcp:	return _T("ServerTcpParser");
		case NetworkParseServerUdp:	return _T("ServerUdpParser");
		case NetworkParseKad:	return _T("KadParser");
		case NetworkParseWebServer:	return _T("WebServerParser");
		case NetworkParseHttp:	return _T("HttpParser");
		case NetworkParseCompressedBlock:	return _T("CompressedBlockParser");
	}
	return _T("UnknownNetworkParser");
}

bool CemuleApp::GuardNetworkParse(ENetworkParseDomain eDomain, LPCTSTR pszEntryPoint)
{
	if (IsNetworkParseAllowed(eDomain))
		return true;

	const DWORD dwCurrentThreadId = ::GetCurrentThreadId();
	const LPCTSTR pszDomain = GetNetworkParseDomainName(eDomain);
	const LPCTSTR pszSafeEntryPoint = (pszEntryPoint != NULL) ? pszEntryPoint : _T("unknown");
	AddDebugLogLine(DLP_HIGH, false, _T("Network parser owner violation. domain=%s entry=%s current=%lu ui=%u networkParser=%lu closing=%u\n"), pszDomain, pszSafeEntryPoint, dwCurrentThreadId, g_uMainThreadId, m_dwNetworkParserOwnerThreadId, static_cast<UINT>(IsClosing()));
	if (!IsClosing()) {
		SApplicationEvent event;
		event.m_eType = ApplicationEventCommandFailed;
		SetApplicationEventBackendEnvelope(event, BackendCommandNetworkPacket, BackendCommandSourceUnknown, BackendCommandOrderingSearch, pszDomain, 0, 0);
		event.m_eBackendCommandFailureKind = BackendCommandFailureOwnerGuard;
		event.m_strMessage.Format(_T("Network parser owner violation. domain=%s entry=%s current=%lu ui=%u networkParser=%lu"), pszDomain, pszSafeEntryPoint, dwCurrentThreadId, g_uMainThreadId, m_dwNetworkParserOwnerThreadId);
		QueueApplicationEvent(event);
	}
	return false;
}


CemuleApp::EBackendLifecycleState CemuleApp::GetBackendLifecycleState() const
{
	return static_cast<EBackendLifecycleState>(m_lBackendLifecycleState);
}

LPCTSTR CemuleApp::GetBackendLifecycleStateName(EBackendLifecycleState eState) const
{
	switch (eState) {
		case BackendLifecycleStarting:	return _T("starting");
		case BackendLifecycleRunning:	return _T("running");
		case BackendLifecycleStoppingInput:	return _T("stopping-input");
		case BackendLifecycleDrainingParser:	return _T("draining-parser");
		case BackendLifecycleDrainingBackendOwner:	return _T("draining-backend-owner");
		case BackendLifecycleDrainingDiskIo:	return _T("draining-disk-io");
		case BackendLifecycleStoppingNetwork:	return _T("stopping-network");
		case BackendLifecycleStoppingUiUpdates:	return _T("stopping-ui-updates");
		case BackendLifecycleStopped:	return _T("stopped");
	}
	return _T("unknown");
}

bool CemuleApp::IsBackendLifecycleStopping() const
{
	return GetBackendLifecycleState() >= BackendLifecycleStoppingInput;
}

void CemuleApp::SetBackendLifecyclePhase(EBackendLifecycleState eState, LPCTSTR pszReason)
{
	const EBackendLifecycleState eCurrent = GetBackendLifecycleState();
	if (eCurrent == eState)
		return;
	if (eCurrent == BackendLifecycleStopped && eState != BackendLifecycleStopped)
		return;
	if (eState < eCurrent && eCurrent >= BackendLifecycleStoppingInput) {
		AddDebugLogLine(DLP_LOW, false, _T("Backend lifecycle ignored backward transition. current=%s requested=%s reason=%s\n"),
			GetBackendLifecycleStateName(eCurrent), GetBackendLifecycleStateName(eState), pszReason != NULL ? pszReason : _T(""));
		return;
	}

	::InterlockedExchange(&m_lBackendLifecycleState, static_cast<LONG>(eState));
	if (eState == BackendLifecycleRunning && m_app_state == APP_STATE_STARTING)
		m_app_state = APP_STATE_RUNNING;
	if (eState >= BackendLifecycleStoppingInput && m_app_state != APP_STATE_DONE)
		m_app_state = APP_STATE_SHUTTINGDOWN;
	if (eState == BackendLifecycleStopped)
		m_app_state = APP_STATE_DONE;
	AddDebugLogLine(DLP_LOW, false, _T("Backend lifecycle transition. from=%s to=%s reason=%s\n"),
		GetBackendLifecycleStateName(eCurrent), GetBackendLifecycleStateName(eState), pszReason != NULL ? pszReason : _T(""));
}

void CemuleApp::BeginBackendShutdownLifecycle(LPCTSTR pszReason)
{
	if (m_app_state != APP_STATE_DONE)
		m_app_state = APP_STATE_SHUTTINGDOWN;
	CancelStartupCriticalLoads(pszReason != NULL ? pszReason : _T("shutdown"));
	SetBackendLifecyclePhase(BackendLifecycleStoppingInput, pszReason != NULL ? pszReason : _T("shutdown"));
}

void CemuleApp::PrepareBackendShutdownForDiskIo(LPCTSTR pszReason)
{
	if (GetBackendLifecycleState() >= BackendLifecycleDrainingDiskIo)
		return;

	BeginBackendShutdownLifecycle(pszReason != NULL ? pszReason : _T("shutdown-disk-io"));
	if (sharedfiles != NULL)
		sharedfiles->ShutdownSearchThreadForExit();
	StopDirWatchTP();
	SetBackendLifecyclePhase(BackendLifecycleDrainingParser, _T("parser-drain"));
	CancelWorkerTopology(_T("shutdown-input"));
	DrainWorkerTopology(750);
	StopWorkerTopology(_T("shutdown-drain"));
	if (searchlist != NULL)
		searchlist->ShutdownSearchProcessingForLifecycle();
	ClearSearchIngestProcessing();

	SetBackendLifecyclePhase(BackendLifecycleDrainingBackendOwner, _T("backend-owner-drain"));
	DrainBackendWorkForShutdown();
	StopBackendCommandThread();
	if (m_pBackendCommandThread == NULL)
		ClearBackendWorkQueues();
	else
		AddDebugLogLine(DLP_LOW, false, _T("Backend command queue cleanup skipped because worker thread is still running.\n"));

	SetBackendLifecyclePhase(BackendLifecycleDrainingDiskIo, _T("disk-io-drain"));
}

LPCTSTR CemuleApp::GetWorkerTopologyRoleName(EWorkerTopologyRole eRole) const
{
	const SWorkerTopologySpec* pSpec = GetWorkerTopologySpec(eRole);
	return pSpec != NULL ? pSpec->m_pszName : _T("unknown");
}

LPCTSTR CemuleApp::GetWorkerTopologyStateName(EWorkerTopologyState eState) const
{
	switch (eState) {
		case WorkerTopologyStopped:	return _T("stopped");
		case WorkerTopologyStarting:	return _T("starting");
		case WorkerTopologyRunning:	return _T("running");
		case WorkerTopologyDraining:	return _T("draining");
		case WorkerTopologyStopping:	return _T("stopping");
		case WorkerTopologyQuarantined:	return _T("quarantined");
	}
	return _T("unknown");
}

CemuleApp::EWorkerTopologyState CemuleApp::GetWorkerTopologyState(EWorkerTopologyRole eRole) const
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return WorkerTopologyStopped;
	return static_cast<EWorkerTopologyState>(m_alWorkerTopologyStates[static_cast<int>(eRole)]);
}


bool CemuleApp::IsStartupMetadataDomainValid(EStartupMetadataDomain eDomain) const
{
	return eDomain >= StartupMetadataDownloads && eDomain < StartupMetadataDomainCount;
}

CemuleApp::EWorkerTopologyRole CemuleApp::GetStartupMetadataWorkerRole(EStartupMetadataDomain eDomain) const
{
	switch (eDomain) {
		case StartupMetadataDownloads:
			return WorkerTopologyStartupLoadDownloads;
		case StartupMetadataKnownFiles:
			return WorkerTopologyStartupLoadPrimary;
		case StartupMetadataClientHistory:
			return WorkerTopologyStartupLoadSecondary;
		case StartupMetadataStoredSearches:
			return WorkerTopologyStartupLoadSearches;
		case StartupMetadataKnown2Index:
			return WorkerTopologyStartupLoadKnown2;
		case StartupMetadataSharedRules:
			return WorkerTopologyStartupLoadPrimary;
		case StartupMetadataDomainCount:
			break;
	}
	return WorkerTopologyStartupLoadSecondary;
}

LPCTSTR CemuleApp::GetStartupMetadataDomainName(EStartupMetadataDomain eDomain) const
{
	switch (eDomain) {
		case StartupMetadataDownloads: return _T("downloads");
		case StartupMetadataKnownFiles: return _T("known-files");
		case StartupMetadataClientHistory: return _T("client-history");
		case StartupMetadataKnown2Index: return _T("known2-index");
		case StartupMetadataStoredSearches: return _T("stored-searches");
		case StartupMetadataSharedRules: return _T("shared-files");
		case StartupMetadataDomainCount: break;
	}
	return _T("unknown");
}

LPCTSTR CemuleApp::GetStartupMetadataStateName(EStartupMetadataState eState) const
{
	switch (eState) {
		case StartupMetadataStateNotStarted: return _T("not-started");
		case StartupMetadataStateLoading: return _T("loading");
		case StartupMetadataStateApplying: return _T("applying");
		case StartupMetadataStateReady: return _T("ready");
		case StartupMetadataStateSkipped: return _T("skipped");
		case StartupMetadataStateFailed: return _T("failed");
		case StartupMetadataStateCancelled: return _T("cancelled");
	}
	return _T("unknown");
}

CemuleApp::SStartupMetadataLoadState CemuleApp::GetStartupMetadataLoadState(EStartupMetadataDomain eDomain) const
{
	SStartupMetadataLoadState state;
	if (!IsStartupMetadataDomainValid(eDomain))
		return state;
	CSingleLock lock(const_cast<CCriticalSection*>(&m_startupMetadataStateLock), TRUE);
	state = m_startupMetadataStates[static_cast<int>(eDomain)];
	return state;
}

void CemuleApp::PublishStartupMetadataLoadProgress(EStartupMetadataDomain eDomain, LONG lGeneration, uint64 uCancellationToken, LPCTSTR pszStage, UINT uDone, UINT uTotal)
{
	if (!IsStartupMetadataDomainValid(eDomain))
		return;
	bool bChanged = false;
	{
		CSingleLock lock(&m_startupMetadataStateLock, TRUE);
		SStartupMetadataLoadState &state = m_startupMetadataStates[static_cast<int>(eDomain)];
		if (lGeneration != 0 && state.m_lGeneration != lGeneration)
			return;
		if (uCancellationToken != 0 && state.m_uCancellationToken != uCancellationToken)
			return;
		if (state.IsTerminal())
			return;
		if (uTotal > 0 && uDone > uTotal)
			uDone = uTotal;
		const CString strStage(pszStage != NULL ? pszStage : _T(""));
		if (state.m_uProgressDone != uDone || state.m_uProgressTotal != uTotal || state.m_strProgressStage != strStage)
			bChanged = true;
		state.m_uProgressDone = uDone;
		state.m_uProgressTotal = uTotal;
		state.m_strProgressStage = strStage;
	}
	if (bChanged && emuledlg != NULL)
		emuledlg->PostStartupOverlayRefresh();
}

LONG CemuleApp::BeginStartupMetadataLoad(EStartupMetadataDomain eDomain, uint64 *puCancellationToken, LPCTSTR pszReason)
{
	if (puCancellationToken != NULL)
		*puCancellationToken = 0;
	if (!IsStartupMetadataDomainValid(eDomain))
		return 0;

	LONG lGeneration = 0;
	uint64 uCancellationToken = 0;
	{
		CSingleLock lock(&m_startupMetadataStateLock, TRUE);
		SStartupMetadataLoadState &state = m_startupMetadataStates[static_cast<int>(eDomain)];
		lGeneration = state.m_lGeneration + 1;
		if (lGeneration == 0)
			lGeneration = 1;
		uCancellationToken = ++m_uNextStartupMetadataCancellationToken;
		if (uCancellationToken == 0)
			uCancellationToken = ++m_uNextStartupMetadataCancellationToken;
		state.m_eState = StartupMetadataStateLoading;
		state.m_lGeneration = lGeneration;
		state.m_uCancellationToken = uCancellationToken;
		state.m_dwLastError = 0;
		state.m_bCancelRequested = false;
		state.m_uProgressDone = 0;
		state.m_uProgressTotal = 0;
		state.m_strProgressStage.Empty();
		state.m_strReason = pszReason != NULL ? pszReason : _T("");
	}
	if (puCancellationToken != NULL)
		*puCancellationToken = uCancellationToken;
	QueueStartupMetadataStateChangedEvent(eDomain, StartupMetadataStateLoading, lGeneration, uCancellationToken, 0, pszReason);
	return lGeneration;
}

void CemuleApp::SetStartupMetadataStateApplying(EStartupMetadataDomain eDomain, LONG lGeneration, uint64 uCancellationToken, LPCTSTR pszReason)
{
	SetStartupMetadataState(eDomain, StartupMetadataStateApplying, lGeneration, uCancellationToken, 0, false, pszReason);
}

void CemuleApp::CompleteStartupMetadataLoad(EStartupMetadataDomain eDomain, LONG lGeneration, uint64 uCancellationToken, bool bSuccess, DWORD dwLastError, LPCTSTR pszReason)
{
	SetStartupMetadataState(eDomain, bSuccess ? StartupMetadataStateReady : StartupMetadataStateFailed, lGeneration, uCancellationToken, dwLastError, false, pszReason);
}

void CemuleApp::SkipStartupMetadataLoad(EStartupMetadataDomain eDomain, LPCTSTR pszReason)
{
	SStartupMetadataLoadState state = GetStartupMetadataLoadState(eDomain);
	if (state.IsTerminal())
		return;
	uint64 uToken = 0;
	const LONG lGeneration = BeginStartupMetadataLoad(eDomain, &uToken, pszReason != NULL ? pszReason : _T("startup-metadata-skipped"));
	SetStartupMetadataState(eDomain, StartupMetadataStateSkipped, lGeneration, uToken, 0, false, pszReason != NULL ? pszReason : _T("startup-metadata-skipped"));
}

void CemuleApp::CancelStartupMetadataLoads(LPCTSTR pszReason)
{
	for (int i = 0; i < StartupMetadataDomainCount; ++i) {
		EStartupMetadataDomain eDomain = static_cast<EStartupMetadataDomain>(i);
		LONG lGeneration = 0;
		uint64 uCancellationToken = 0;
		bool bCancelled = false;
		{
			CSingleLock lock(&m_startupMetadataStateLock, TRUE);
			SStartupMetadataLoadState &state = m_startupMetadataStates[i];
			if (state.m_eState != StartupMetadataStateLoading && state.m_eState != StartupMetadataStateApplying)
				continue;
			state.m_eState = StartupMetadataStateCancelled;
			state.m_bCancelRequested = true;
			state.m_dwLastError = ERROR_CANCELLED;
			state.m_strReason = pszReason != NULL ? pszReason : _T("cancelled");
			state.m_uCancellationToken = ++m_uNextStartupMetadataCancellationToken;
			if (state.m_uCancellationToken == 0)
				state.m_uCancellationToken = ++m_uNextStartupMetadataCancellationToken;
			lGeneration = state.m_lGeneration;
			uCancellationToken = state.m_uCancellationToken;
			bCancelled = true;
		}
		if (bCancelled)
			QueueStartupMetadataStateChangedEvent(eDomain, StartupMetadataStateCancelled, lGeneration, uCancellationToken, ERROR_CANCELLED, pszReason);
	}
}

void CemuleApp::CancelStartupCriticalLoads(LPCTSTR pszReason)
{
	const LPCTSTR pszCancelReason = pszReason != NULL ? pszReason : _T("startup-critical-cancel");
	if (downloadqueue != NULL)
		downloadqueue->CancelStartupLoad();
	if (searchlist != NULL)
		searchlist->CancelStartupLoad();
	CancelStartupMetadataLoads(pszCancelReason);
}

bool CemuleApp::IsStartupMetadataLoadCancelled(EStartupMetadataDomain eDomain, LONG lGeneration, uint64 uCancellationToken) const
{
	if (!IsStartupMetadataDomainValid(eDomain) || lGeneration == 0 || uCancellationToken == 0)
		return true;
	CSingleLock lock(const_cast<CCriticalSection*>(&m_startupMetadataStateLock), TRUE);
	const SStartupMetadataLoadState &state = m_startupMetadataStates[static_cast<int>(eDomain)];
	return state.m_bCancelRequested || state.m_lGeneration != lGeneration || state.m_uCancellationToken != uCancellationToken || state.m_eState == StartupMetadataStateCancelled;
}

bool CemuleApp::IsStartupMetadataDomainReady(EStartupMetadataDomain eDomain) const
{
	if (!IsStartupMetadataDomainValid(eDomain))
		return false;
	CSingleLock lock(const_cast<CCriticalSection*>(&m_startupMetadataStateLock), TRUE);
	const EStartupMetadataState eState = m_startupMetadataStates[static_cast<int>(eDomain)].m_eState;
	return eState == StartupMetadataStateReady || eState == StartupMetadataStateSkipped;
}

bool CemuleApp::KnownFilesReady() const
{
	return IsStartupMetadataDomainReady(StartupMetadataKnownFiles);
}

bool CemuleApp::ClientHistoryReady() const
{
	return IsStartupMetadataDomainReady(StartupMetadataClientHistory);
}

bool CemuleApp::Known2IndexReady() const
{
	return IsStartupMetadataDomainReady(StartupMetadataKnown2Index);
}

bool CemuleApp::SharedFilesReady() const
{
	return IsStartupMetadataDomainReady(StartupMetadataSharedRules) && sharedfiles != NULL && sharedfiles->IsStartupScanComplete() && !sharedfiles->IsReloading();
}

bool CemuleApp::StartupCriticalMetadataReady() const
{
	return IsStartupMetadataDomainReady(StartupMetadataDownloads) && KnownFilesReady() && ClientHistoryReady() && IsStartupMetadataDomainReady(StartupMetadataSharedRules) && (!thePrefs.IsStoringSearchesEnabled() || IsStartupMetadataDomainReady(StartupMetadataStoredSearches));
}

bool CemuleApp::StartupCriticalMetadataLoadsTerminal() const
{
	const bool bStoredSearchesRequired = thePrefs.IsStoringSearchesEnabled();
	CSingleLock lock(const_cast<CCriticalSection*>(&m_startupMetadataStateLock), TRUE);
	return m_startupMetadataStates[static_cast<int>(StartupMetadataDownloads)].IsTerminal()
		&& m_startupMetadataStates[static_cast<int>(StartupMetadataKnownFiles)].IsTerminal()
		&& m_startupMetadataStates[static_cast<int>(StartupMetadataClientHistory)].IsTerminal()
		&& m_startupMetadataStates[static_cast<int>(StartupMetadataSharedRules)].IsTerminal()
		&& (!bStoredSearchesRequired || m_startupMetadataStates[static_cast<int>(StartupMetadataStoredSearches)].IsTerminal());
}

bool CemuleApp::AllStartupMetadataReady() const
{
	return StartupCriticalMetadataReady() && Known2IndexReady() && SharedFilesReady();
}

bool CemuleApp::BeginStartupCriticalLoads()
{
	bool bQueued = true;
	if (downloadqueue != NULL && !IsStartupMetadataDomainReady(StartupMetadataDownloads))
		bQueued = BeginStartupDownloadsLoad() && bQueued;
	else if (downloadqueue == NULL && !GetStartupMetadataLoadState(StartupMetadataDownloads).IsTerminal()) {
		uint64 uToken = 0;
		const LONG lGeneration = BeginStartupMetadataLoad(StartupMetadataDownloads, &uToken, _T("downloads-unavailable"));
		CompleteStartupMetadataLoad(StartupMetadataDownloads, lGeneration, uToken, false, ERROR_INVALID_HANDLE, _T("downloads-unavailable"));
		bQueued = false;
	}
	if (knownfiles != NULL && !KnownFilesReady())
		bQueued = BeginStartupKnownFilesLoad() && bQueued;
	else if (knownfiles == NULL && !GetStartupMetadataLoadState(StartupMetadataKnownFiles).IsTerminal()) {
		uint64 uToken = 0;
		const LONG lGeneration = BeginStartupMetadataLoad(StartupMetadataKnownFiles, &uToken, _T("known-files-unavailable"));
		CompleteStartupMetadataLoad(StartupMetadataKnownFiles, lGeneration, uToken, false, ERROR_INVALID_HANDLE, _T("known-files-unavailable"));
		bQueued = false;
	}
	if (thePrefs.GetClientHistory()) {
		if (clientlist != NULL && !ClientHistoryReady())
			bQueued = BeginStartupClientHistoryLoad() && bQueued;
		else if (clientlist == NULL && !GetStartupMetadataLoadState(StartupMetadataClientHistory).IsTerminal()) {
			uint64 uToken = 0;
			const LONG lGeneration = BeginStartupMetadataLoad(StartupMetadataClientHistory, &uToken, _T("client-history-unavailable"));
			CompleteStartupMetadataLoad(StartupMetadataClientHistory, lGeneration, uToken, false, ERROR_INVALID_HANDLE, _T("client-history-unavailable"));
			bQueued = false;
		}
	}
	else
		SkipStartupMetadataLoad(StartupMetadataClientHistory, _T("client-history-disabled"));
	if (!IsStartupMetadataDomainReady(StartupMetadataSharedRules) && !GetStartupMetadataLoadState(StartupMetadataSharedRules).IsTerminal())
		bQueued = BeginStartupSharedCacheLoad() && bQueued;
	if (thePrefs.IsStoringSearchesEnabled()) {
		const SStartupMetadataLoadState storedSearchesState = GetStartupMetadataLoadState(StartupMetadataStoredSearches);
		if (searchlist != NULL && !storedSearchesState.IsTerminal() && !searchlist->IsStartupLoadActive() && !searchlist->IsStartupLoadCompleted()) {
			const bool bStoredSearchDependenciesReady = IsStartupMetadataDomainReady(StartupMetadataDownloads) && KnownFilesReady() && IsStartupMetadataDomainReady(StartupMetadataSharedRules);
			if (bStoredSearchDependenciesReady)
				searchlist->BeginStartupLoad();
			else {
				const SStartupMetadataLoadState downloadsState = GetStartupMetadataLoadState(StartupMetadataDownloads);
				const SStartupMetadataLoadState knownFilesState = GetStartupMetadataLoadState(StartupMetadataKnownFiles);
				const SStartupMetadataLoadState sharedRulesState = GetStartupMetadataLoadState(StartupMetadataSharedRules);
				if ((downloadsState.IsTerminal() && !downloadsState.IsReady()) || (knownFilesState.IsTerminal() && !knownFilesState.IsReady()) || (sharedRulesState.IsTerminal() && !sharedRulesState.IsReady())) {
					uint64 uToken = 0;
					const LONG lGeneration = BeginStartupMetadataLoad(StartupMetadataStoredSearches, &uToken, _T("stored-searches-dependency-unavailable"));
					CompleteStartupMetadataLoad(StartupMetadataStoredSearches, lGeneration, uToken, false, ERROR_NOT_READY, _T("stored-searches-dependency-unavailable"));
					searchlist->CancelStartupLoad();
					bQueued = false;
				}
				else
					bQueued = false;
			}
		}
		else if (searchlist == NULL && !storedSearchesState.IsTerminal()) {
			uint64 uToken = 0;
			const LONG lGeneration = BeginStartupMetadataLoad(StartupMetadataStoredSearches, &uToken, _T("stored-searches-unavailable"));
			CompleteStartupMetadataLoad(StartupMetadataStoredSearches, lGeneration, uToken, false, ERROR_INVALID_HANDLE, _T("stored-searches-unavailable"));
			bQueued = false;
		}
		else if (searchlist != NULL && searchlist->IsStartupLoadCompleted() && !storedSearchesState.IsTerminal()) {
			uint64 uToken = 0;
			const LONG lGeneration = BeginStartupMetadataLoad(StartupMetadataStoredSearches, &uToken, _T("stored-searches-completed-without-metadata"));
			CompleteStartupMetadataLoad(StartupMetadataStoredSearches, lGeneration, uToken, false, ERROR_NOT_READY, _T("stored-searches-completed-without-metadata"));
			bQueued = false;
		}
	}
	else
		SkipStartupMetadataLoad(StartupMetadataStoredSearches, _T("stored-searches-disabled"));
	return bQueued;
}

bool CemuleApp::BeginStartupDownloadsLoad()
{
	if (downloadqueue == NULL)
		return false;
	const SStartupMetadataLoadState currentState = GetStartupMetadataLoadState(StartupMetadataDownloads);
	if (currentState.m_eState == StartupMetadataStateLoading || currentState.m_eState == StartupMetadataStateApplying || currentState.IsTerminal())
		return currentState.m_eState != StartupMetadataStateFailed && currentState.m_eState != StartupMetadataStateCancelled;

	downloadqueue->BeginStartupLoad();
	uint64 uDownloadsToken = 0;
	const LONG lDownloadsGeneration = BeginStartupMetadataLoad(StartupMetadataDownloads, &uDownloadsToken, _T("async-downloads-load"));
	const EWorkerTopologyRole eStartupRole = GetStartupMetadataWorkerRole(StartupMetadataDownloads);
	if (GetWorkerTopologyState(eStartupRole) == WorkerTopologyStopped && !StartStartupLoadWorker(StartupMetadataDownloads)) {
		CompleteStartupMetadataLoad(StartupMetadataDownloads, lDownloadsGeneration, uDownloadsToken, false, ::GetLastError(), _T("async-downloads-load-start-worker"));
		downloadqueue->CancelStartupLoad();
		return false;
	}

	SWorkerTopologyItem item;
	item.m_eRole = eStartupRole;
	item.m_eType = WorkerTopologyItemStartupMetadataLoad;
	item.m_strStage = _T("startup-downloads-load");
	item.m_strCoalesceKey = _T("startup-downloads-load");
	item.m_lWorkerGeneration = lDownloadsGeneration;
	item.m_uCorrelationId = uDownloadsToken;
	if (!QueueStartupLoadWorkerItem(StartupMetadataDownloads, item)) {
		CompleteStartupMetadataLoad(StartupMetadataDownloads, lDownloadsGeneration, uDownloadsToken, false, ::GetLastError(), _T("async-downloads-load-queue"));
		downloadqueue->CancelStartupLoad();
		return false;
	}

	return true;
}

bool CemuleApp::ProcessStartupDownloadsLoadWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_strStage != _T("startup-downloads-load"))
		return false;

	const LONG lGeneration = item.m_lWorkerGeneration;
	const uint64 uDownloadsToken = item.m_uCorrelationId;
	if (IsStartupMetadataLoadCancelled(StartupMetadataDownloads, lGeneration, uDownloadsToken))
		return false;

	CDownloadQueue::SStartupDownloadLoadResult *pResult = new CDownloadQueue::SStartupDownloadLoadResult();
	pResult->lGeneration = lGeneration;
	pResult->uCancellationToken = uDownloadsToken;
	pResult->bSuccess = downloadqueue != NULL && downloadqueue->LoadStartupPartFilesForWorker(*pResult);
	if (!pResult->bSuccess && pResult->dwLastError == 0)
		pResult->dwLastError = ERROR_READ_FAULT;
	if (pResult->strStage.IsEmpty())
		pResult->strStage = pResult->bSuccess ? _T("load-completed") : _T("load-failed");

	if (IsStartupMetadataLoadCancelled(StartupMetadataDownloads, lGeneration, uDownloadsToken)) {
		CDownloadQueue::DeleteStartupDownloadLoadResult(pResult);
		return false;
	}

	if (emuledlg != NULL && ::IsWindow(emuledlg->m_hWnd) && ::PostMessage(emuledlg->m_hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_DOWNLOADS_LOAD_READY, 0, reinterpret_cast<LPARAM>(pResult)))
		return true;

	const DWORD dwPostError = ::GetLastError();
	CDownloadQueue::DeleteStartupDownloadLoadResult(pResult);
	CompleteStartupMetadataLoad(StartupMetadataDownloads, lGeneration, uDownloadsToken, false, dwPostError, _T("async-downloads-load-post"));
	return false;
}

bool CemuleApp::BeginStartupKnownFilesLoad()
{
	if (knownfiles == NULL)
		return false;
	const SStartupMetadataLoadState currentState = GetStartupMetadataLoadState(StartupMetadataKnownFiles);
	if (currentState.m_eState == StartupMetadataStateLoading || currentState.m_eState == StartupMetadataStateApplying || currentState.IsTerminal())
		return currentState.m_eState != StartupMetadataStateFailed && currentState.m_eState != StartupMetadataStateCancelled;

	uint64 uKnownFilesToken = 0;
	const LONG lKnownFilesGeneration = BeginStartupMetadataLoad(StartupMetadataKnownFiles, &uKnownFilesToken, _T("async-known-files-load"));
	const EWorkerTopologyRole eStartupRole = GetStartupMetadataWorkerRole(StartupMetadataKnownFiles);
	if (GetWorkerTopologyState(eStartupRole) == WorkerTopologyStopped && !StartStartupLoadWorker(StartupMetadataKnownFiles)) {
		CompleteStartupMetadataLoad(StartupMetadataKnownFiles, lKnownFilesGeneration, uKnownFilesToken, false, ::GetLastError(), _T("async-known-files-load-start-worker"));
		if (sharedfiles != NULL)
			sharedfiles->StartDeferredStartupScanAfterKnownFilesFailure();
		return false;
	}

	SWorkerTopologyItem item;
	item.m_eRole = eStartupRole;
	item.m_eType = WorkerTopologyItemStartupMetadataLoad;
	item.m_strStage = _T("startup-known-files-load");
	item.m_strCoalesceKey = _T("startup-known-files-load");
	item.m_lWorkerGeneration = lKnownFilesGeneration;
	item.m_uCorrelationId = uKnownFilesToken;
	if (!QueueStartupLoadWorkerItem(StartupMetadataKnownFiles, item)) {
		CompleteStartupMetadataLoad(StartupMetadataKnownFiles, lKnownFilesGeneration, uKnownFilesToken, false, ::GetLastError(), _T("async-known-files-load-queue"));
		if (sharedfiles != NULL)
			sharedfiles->StartDeferredStartupScanAfterKnownFilesFailure();
		return false;
	}

	return true;
}

bool CemuleApp::ProcessStartupKnownFilesLoadWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_strStage != _T("startup-known-files-load"))
		return false;

	const LONG lGeneration = item.m_lWorkerGeneration;
	const uint64 uKnownFilesToken = item.m_uCorrelationId;
	if (IsStartupMetadataLoadCancelled(StartupMetadataKnownFiles, lGeneration, uKnownFilesToken))
		return false;

	SStartupKnownFilesLoadResult *pResult = new SStartupKnownFilesLoadResult();
	pResult->lGeneration = lGeneration;
	pResult->uCancellationToken = uKnownFilesToken;
	pResult->pKnownRecords = new CStartupKnownFilesRecords();
	pResult->pCancelledRecords = new CStartupCancelledFilesRecords();
	pResult->bSuccess = knownfiles != NULL && knownfiles->LoadStartupKnownFilesRecords(*pResult->pKnownRecords, *pResult->pCancelledRecords, pResult->dwCancelledFilesSeed, lGeneration, uKnownFilesToken);
	pResult->dwLastError = pResult->bSuccess ? 0 : ERROR_READ_FAULT;
	pResult->strStage = pResult->bSuccess ? _T("load-completed") : _T("load-failed");

	if (IsStartupMetadataLoadCancelled(StartupMetadataKnownFiles, lGeneration, uKnownFilesToken)) {
		CKnownFileList::DeleteStartupKnownFilesRecords(pResult->pKnownRecords, pResult->pCancelledRecords);
		CKnownFileList::DeleteStartupKnownFilesParsedFiles(pResult->vecParsedKnownFiles);
		delete pResult;
		return false;
	}

	if (emuledlg != NULL && ::IsWindow(emuledlg->m_hWnd) && ::PostMessage(emuledlg->m_hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_KNOWNFILES_LOAD_READY, 0, reinterpret_cast<LPARAM>(pResult)))
		return true;

	const DWORD dwPostError = ::GetLastError();
	CKnownFileList::DeleteStartupKnownFilesRecords(pResult->pKnownRecords, pResult->pCancelledRecords);
	CKnownFileList::DeleteStartupKnownFilesParsedFiles(pResult->vecParsedKnownFiles);
	delete pResult;
	CompleteStartupMetadataLoad(StartupMetadataKnownFiles, lGeneration, uKnownFilesToken, false, dwPostError, _T("async-known-files-load-post"));
	return false;
}

bool CemuleApp::BeginStartupClientHistoryLoad()
{
	if (clientlist == NULL)
		return false;
	if (!thePrefs.GetClientHistory()) {
		SkipStartupMetadataLoad(StartupMetadataClientHistory, _T("client-history-disabled"));
		return true;
	}
	const SStartupMetadataLoadState currentState = GetStartupMetadataLoadState(StartupMetadataClientHistory);
	if (currentState.m_eState == StartupMetadataStateLoading || currentState.m_eState == StartupMetadataStateApplying || currentState.IsTerminal())
		return currentState.m_eState != StartupMetadataStateFailed && currentState.m_eState != StartupMetadataStateCancelled;

	uint64 uClientHistoryToken = 0;
	const LONG lClientHistoryGeneration = BeginStartupMetadataLoad(StartupMetadataClientHistory, &uClientHistoryToken, _T("async-client-history-load"));
	const EWorkerTopologyRole eStartupRole = GetStartupMetadataWorkerRole(StartupMetadataClientHistory);
	if (GetWorkerTopologyState(eStartupRole) == WorkerTopologyStopped && !StartStartupLoadWorker(StartupMetadataClientHistory)) {
		CompleteStartupMetadataLoad(StartupMetadataClientHistory, lClientHistoryGeneration, uClientHistoryToken, false, ::GetLastError(), _T("async-client-history-load-start-worker"));
		return false;
	}

	SWorkerTopologyItem item;
	item.m_eRole = eStartupRole;
	item.m_eType = WorkerTopologyItemStartupMetadataLoad;
	item.m_strStage = _T("startup-client-history-load");
	item.m_strCoalesceKey = _T("startup-client-history-load");
	item.m_lWorkerGeneration = lClientHistoryGeneration;
	item.m_uCorrelationId = uClientHistoryToken;
	if (!QueueStartupLoadWorkerItem(StartupMetadataClientHistory, item)) {
		CompleteStartupMetadataLoad(StartupMetadataClientHistory, lClientHistoryGeneration, uClientHistoryToken, false, ::GetLastError(), _T("async-client-history-load-queue"));
		return false;
	}

	return true;
}

bool CemuleApp::ProcessStartupClientHistoryLoadWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_strStage != _T("startup-client-history-load"))
		return false;

	const LONG lGeneration = item.m_lWorkerGeneration;
	const uint64 uClientHistoryToken = item.m_uCorrelationId;
	if (IsStartupMetadataLoadCancelled(StartupMetadataClientHistory, lGeneration, uClientHistoryToken))
		return false;

	SStartupClientHistoryLoadResult *pResult = new SStartupClientHistoryLoadResult();
	pResult->lGeneration = lGeneration;
	pResult->uCancellationToken = uClientHistoryToken;
	pResult->pRecords = new CClientList::CStartupClientHistoryRecords();
	if (pResult->pRecords == NULL) {
		pResult->bSuccess = false;
		pResult->dwLastError = ERROR_OUTOFMEMORY;
		pResult->strStage = _T("alloc-failed");
	}
	else {
		pResult->bSuccess = clientlist != NULL && clientlist->LoadStartupClientHistoryRecords(*pResult->pRecords, lGeneration, uClientHistoryToken);
		pResult->dwLastError = pResult->bSuccess ? 0 : ERROR_INVALID_DATA;
		pResult->strStage = pResult->bSuccess ? _T("load-completed") : _T("load-failed");
		if (pResult->bSuccess)
			PublishStartupMetadataLoadProgress(StartupMetadataClientHistory, lGeneration, uClientHistoryToken, _T("read-client-history"), ClampStartupTelemetryToUInt(pResult->pRecords->size()), ClampStartupTelemetryToUInt(pResult->pRecords->size()));
	}
	if (!pResult->bSuccess) {
		CClientList::DeleteStartupClientHistoryRecords(pResult->pRecords);
		pResult->pRecords = NULL;
	}

	if (IsStartupMetadataLoadCancelled(StartupMetadataClientHistory, lGeneration, uClientHistoryToken)) {
		CClientList::DeleteStartupClientHistoryRecords(pResult->pRecords);
		pResult->pRecords = NULL;
		delete pResult;
		return false;
	}

	if (emuledlg != NULL && ::IsWindow(emuledlg->m_hWnd) && ::PostMessage(emuledlg->m_hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_CLIENTHISTORY_LOAD_READY, 0, reinterpret_cast<LPARAM>(pResult)))
		return true;

	const DWORD dwPostError = ::GetLastError();
	CClientList::DeleteStartupClientHistoryRecords(pResult->pRecords);
	pResult->pRecords = NULL;
	delete pResult;
	CompleteStartupMetadataLoad(StartupMetadataClientHistory, lGeneration, uClientHistoryToken, false, dwPostError, _T("async-client-history-load-post"));
	return false;
}

bool CemuleApp::BeginStartupStoredSearchesLoad()
{
	if (searchlist == NULL)
		return false;
	if (!thePrefs.IsStoringSearchesEnabled()) {
		SkipStartupMetadataLoad(StartupMetadataStoredSearches, _T("stored-searches-disabled"));
		return true;
	}
	const SStartupMetadataLoadState currentState = GetStartupMetadataLoadState(StartupMetadataStoredSearches);
	if (currentState.m_eState == StartupMetadataStateLoading || currentState.m_eState == StartupMetadataStateApplying || currentState.IsTerminal())
		return currentState.m_eState != StartupMetadataStateFailed && currentState.m_eState != StartupMetadataStateCancelled;

	uint64 uStoredSearchesToken = 0;
	const LONG lStoredSearchesGeneration = BeginStartupMetadataLoad(StartupMetadataStoredSearches, &uStoredSearchesToken, _T("async-stored-searches-load"));
	const EWorkerTopologyRole eStartupRole = GetStartupMetadataWorkerRole(StartupMetadataStoredSearches);
	if (GetWorkerTopologyState(eStartupRole) == WorkerTopologyStopped && !StartStartupLoadWorker(StartupMetadataStoredSearches)) {
		CompleteStartupMetadataLoad(StartupMetadataStoredSearches, lStoredSearchesGeneration, uStoredSearchesToken, false, ::GetLastError(), _T("async-stored-searches-load-start-worker"));
		return false;
	}

	SWorkerTopologyItem item;
	item.m_eRole = eStartupRole;
	item.m_eType = WorkerTopologyItemStartupMetadataLoad;
	item.m_strStage = _T("startup-stored-searches-load");
	item.m_strCoalesceKey = _T("startup-stored-searches-load");
	item.m_lWorkerGeneration = lStoredSearchesGeneration;
	item.m_uCorrelationId = uStoredSearchesToken;
	if (!QueueStartupLoadWorkerItem(StartupMetadataStoredSearches, item)) {
		CompleteStartupMetadataLoad(StartupMetadataStoredSearches, lStoredSearchesGeneration, uStoredSearchesToken, false, ::GetLastError(), _T("async-stored-searches-load-queue"));
		return false;
	}

	return true;
}

bool CemuleApp::BeginStartupSharedCacheLoad()
{
	const SStartupMetadataLoadState currentState = GetStartupMetadataLoadState(StartupMetadataSharedRules);
	if (currentState.m_eState == StartupMetadataStateLoading || currentState.m_eState == StartupMetadataStateApplying || currentState.IsTerminal())
		return currentState.m_eState != StartupMetadataStateFailed && currentState.m_eState != StartupMetadataStateCancelled;

	if (sharedfiles == NULL) {
		uint64 uSharedCacheToken = 0;
		const LONG lSharedCacheGeneration = BeginStartupMetadataLoad(StartupMetadataSharedRules, &uSharedCacheToken, _T("shared-cache-unavailable"));
		CompleteStartupMetadataLoad(StartupMetadataSharedRules, lSharedCacheGeneration, uSharedCacheToken, false, ERROR_INVALID_HANDLE, _T("shared-cache-unavailable"));
		return false;
	}

	uint64 uSharedCacheToken = 0;
	const LONG lSharedCacheGeneration = BeginStartupMetadataLoad(StartupMetadataSharedRules, &uSharedCacheToken, _T("shared-cache-worker-start"));
	const EWorkerTopologyRole eStartupRole = GetStartupMetadataWorkerRole(StartupMetadataSharedRules);
	if (GetWorkerTopologyState(eStartupRole) == WorkerTopologyStopped && !StartStartupLoadWorker(StartupMetadataSharedRules)) {
		CompleteStartupMetadataLoad(StartupMetadataSharedRules, lSharedCacheGeneration, uSharedCacheToken, false, ::GetLastError(), _T("shared-cache-worker-start-failed"));
		return false;
	}

	SWorkerTopologyItem item;
	item.m_eRole = eStartupRole;
	item.m_eType = WorkerTopologyItemStartupMetadataLoad;
	item.m_strStage = _T("startup-shared-cache-load");
	item.m_strCoalesceKey = _T("startup-shared-cache-load");
	item.m_lWorkerGeneration = lSharedCacheGeneration;
	item.m_uCorrelationId = uSharedCacheToken;
	if (!QueueStartupLoadWorkerItem(StartupMetadataSharedRules, item)) {
		CompleteStartupMetadataLoad(StartupMetadataSharedRules, lSharedCacheGeneration, uSharedCacheToken, false, ::GetLastError(), _T("shared-cache-worker-queue-failed"));
		return false;
	}

	return true;
}

bool CemuleApp::BeginStartupKnown2IndexLoad()
{
	const SStartupMetadataLoadState currentState = GetStartupMetadataLoadState(StartupMetadataKnown2Index);
	if (currentState.m_eState == StartupMetadataStateLoading || currentState.m_eState == StartupMetadataStateApplying || currentState.IsTerminal())
		return currentState.m_eState != StartupMetadataStateFailed && currentState.m_eState != StartupMetadataStateCancelled;

	if (!KnownFilesReady()) {
		uint64 uKnown2Token = 0;
		const LONG lKnown2Generation = BeginStartupMetadataLoad(StartupMetadataKnown2Index, &uKnown2Token, _T("aich-sync-known-files-unavailable"));
		CompleteStartupMetadataLoad(StartupMetadataKnown2Index, lKnown2Generation, uKnown2Token, false, ERROR_NOT_READY, _T("aich-sync-known-files-unavailable"));
		return false;
	}
	if (!SharedFilesReady())
		return true;

	uint64 uKnown2Token = 0;
	const LONG lKnown2Generation = BeginStartupMetadataLoad(StartupMetadataKnown2Index, &uKnown2Token, _T("aich-sync-worker-start"));
	const EWorkerTopologyRole eStartupRole = GetStartupMetadataWorkerRole(StartupMetadataKnown2Index);
	if (GetWorkerTopologyState(eStartupRole) == WorkerTopologyStopped && !StartStartupLoadWorker(StartupMetadataKnown2Index)) {
		CompleteStartupMetadataLoad(StartupMetadataKnown2Index, lKnown2Generation, uKnown2Token, false, ::GetLastError(), _T("aich-sync-worker-start-failed"));
		return false;
	}

	SWorkerTopologyItem item;
	item.m_eRole = eStartupRole;
	item.m_eType = WorkerTopologyItemStartupMetadataLoad;
	item.m_strStage = _T("startup-known2-index-load");
	item.m_strCoalesceKey = _T("startup-known2-index-load");
	item.m_lWorkerGeneration = lKnown2Generation;
	item.m_uCorrelationId = uKnown2Token;
	if (!QueueStartupLoadWorkerItem(StartupMetadataKnown2Index, item)) {
		CompleteStartupMetadataLoad(StartupMetadataKnown2Index, lKnown2Generation, uKnown2Token, false, ::GetLastError(), _T("aich-sync-worker-queue-failed"));
		return false;
	}

	return true;
}

bool CemuleApp::ProcessStartupKnown2IndexLoadWorkerItem(const SWorkerTopologyItem &item)
{
	static const UINT uDeferredAICHBuildSlice = 4;

	if (item.m_strStage == _T("startup-known2-deferred-aich-build")) {
		if (IsClosing())
			return true;

		if (!item.m_vecPayload.empty()) {
			const int iHashCount = static_cast<int>(item.m_vecPayload.size() / MDX_DIGEST_SIZE);
			int iNextHash = max(0, min(item.m_iPayloadCursor, iHashCount));
			UINT uProcessed = 0;
			while (iNextHash < iHashCount && uProcessed < uDeferredAICHBuildSlice && !IsClosing()) {
				const uchar *pucFileHash = reinterpret_cast<const uchar*>(&item.m_vecPayload[static_cast<size_t>(iNextHash) * MDX_DIGEST_SIZE]);
				CAICHSyncThread::BuildStartupDeferredAICHHashset(pucFileHash);
				++iNextHash;
				++uProcessed;
			}

			const INT_PTR iPendingAICHHashsets = max(0, iHashCount - iNextHash);
			if (iPendingAICHHashsets > 0 && uProcessed > 0 && !IsClosing()) {
				SWorkerTopologyItem nextItem = item;
				nextItem.m_iPayloadCursor = iNextHash;
				if (!QueueStartupLoadWorkerItem(StartupMetadataKnown2Index, nextItem))
					AddDebugLogLine(DLP_LOW, false, _T("Failed to queue next deferred AICH hashset creation slice. pending=%Id processed=%u\n"), iPendingAICHHashsets, uProcessed);
			} else if (iPendingAICHHashsets > 0) {
				AddDebugLogLine(DLP_LOW, false, _T("Deferred AICH hashset creation paused because current payload slice made no progress. pending=%Id processed=%u\n"), iPendingAICHHashsets, uProcessed);
			}
			return true;
		}

		INT_PTR iPendingAICHHashsets = 0;
		INT_PTR iProcessedAICHHashsets = 0;
		const bool bSuccess = CAICHSyncThread::RunStartupSync(true, &iPendingAICHHashsets, uDeferredAICHBuildSlice, &iProcessedAICHHashsets);
		if (bSuccess && iPendingAICHHashsets > 0 && iProcessedAICHHashsets > 0 && !IsClosing()) {
			SWorkerTopologyItem nextItem = item;
			if (!QueueStartupLoadWorkerItem(StartupMetadataKnown2Index, nextItem))
				AddDebugLogLine(DLP_LOW, false, _T("Failed to queue next deferred AICH hashset creation slice. pending=%Id processed=%Id\n"), iPendingAICHHashsets, iProcessedAICHHashsets);
		} else if (bSuccess && iPendingAICHHashsets > 0) {
			AddDebugLogLine(DLP_LOW, false, _T("Deferred AICH hashset creation paused because current slice made no progress. pending=%Id processed=%Id\n"), iPendingAICHHashsets, iProcessedAICHHashsets);
		}
		return true;
	}

	if (item.m_strStage != _T("startup-known2-index-load"))
		return false;
	if (IsStartupMetadataLoadCancelled(StartupMetadataKnown2Index, item.m_lWorkerGeneration, item.m_uCorrelationId))
		return false;
	INT_PTR iPendingAICHHashsets = 0;
	std::vector<BYTE> vecDeferredAICHFileHashes;
	PublishStartupMetadataLoadProgress(StartupMetadataKnown2Index, item.m_lWorkerGeneration, item.m_uCorrelationId, _T("scan-known2-index"), 0, 1);
	const bool bSuccess = CAICHSyncThread::RunStartupSync(false, &iPendingAICHHashsets, 0, NULL, &vecDeferredAICHFileHashes);
	PublishStartupMetadataLoadProgress(StartupMetadataKnown2Index, item.m_lWorkerGeneration, item.m_uCorrelationId, _T("scan-known2-index"), 1, 1);
	LPCTSTR pszCompletionReason = bSuccess ? (iPendingAICHHashsets > 0 ? _T("aich-index-completed-deferred-hash") : _T("aich-sync-completed")) : _T("aich-sync-failed");
	CompleteStartupMetadataLoad(StartupMetadataKnown2Index, item.m_lWorkerGeneration, item.m_uCorrelationId, bSuccess, bSuccess ? 0 : ERROR_READ_FAULT, pszCompletionReason);
	if (bSuccess && iPendingAICHHashsets > 0 && !IsClosing()) {
		SWorkerTopologyItem deferredItem;
		deferredItem.m_eRole = item.m_eRole;
		deferredItem.m_eType = WorkerTopologyItemStartupMetadataLoad;
		deferredItem.m_strStage = _T("startup-known2-deferred-aich-build");
		deferredItem.m_strCoalesceKey = _T("startup-known2-deferred-aich-build");
		deferredItem.m_lWorkerGeneration = item.m_lWorkerGeneration;
		deferredItem.m_uCorrelationId = item.m_uCorrelationId;
		deferredItem.m_iPayloadCursor = 0;
		deferredItem.m_vecPayload.swap(vecDeferredAICHFileHashes);
		if (!QueueStartupLoadWorkerItem(StartupMetadataKnown2Index, deferredItem))
			AddDebugLogLine(DLP_LOW, false, _T("Failed to queue deferred AICH hashset creation after startup. pending=%Id payload=%Iu\n"), iPendingAICHHashsets, deferredItem.m_vecPayload.size() / MDX_DIGEST_SIZE);
	}
	if (emuledlg != NULL)
		emuledlg->PostStartupOverlayRefresh();
	return true;
}

bool CemuleApp::ProcessStartupSharedCacheLoadWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_strStage != _T("startup-shared-cache-load"))
		return false;

	const LONG lGeneration = item.m_lWorkerGeneration;
	const uint64 uSharedCacheToken = item.m_uCorrelationId;
	if (IsStartupMetadataLoadCancelled(StartupMetadataSharedRules, lGeneration, uSharedCacheToken))
		return false;

	PublishStartupMetadataLoadProgress(StartupMetadataSharedRules, lGeneration, uSharedCacheToken, _T("read-shared-cache"), 0, 1000);
	const bool bLoaded = sharedfiles != NULL && sharedfiles->LoadSharedCacheForStartup(lGeneration, uSharedCacheToken);
	if (IsStartupMetadataLoadCancelled(StartupMetadataSharedRules, lGeneration, uSharedCacheToken))
		return false;

	CompleteStartupMetadataLoad(StartupMetadataSharedRules, lGeneration, uSharedCacheToken, true, 0, bLoaded ? _T("shared-cache-loaded") : _T("shared-cache-empty-or-invalid"));
	if (!bLoaded)
		AddDebugLogLine(DLP_LOW, false, _T("Shared cache was not loaded or contained no valid cache data. Startup will continue with normal shared file scanning.\n"));
	if (emuledlg != NULL)
		emuledlg->PostStartupOverlayRefresh();
	return true;
}

bool CemuleApp::ProcessStartupStoredSearchesLoadWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_strStage != _T("startup-stored-searches-load"))
		return false;

	const LONG lGeneration = item.m_lWorkerGeneration;
	const uint64 uStoredSearchesToken = item.m_uCorrelationId;
	if (IsStartupMetadataLoadCancelled(StartupMetadataStoredSearches, lGeneration, uStoredSearchesToken))
		return false;

	CSearchList::SStartupStoredSearchesLoadResult *pResult = new CSearchList::SStartupStoredSearchesLoadResult();
	pResult->lGeneration = lGeneration;
	pResult->uCancellationToken = uStoredSearchesToken;
	pResult->bSuccess = searchlist != NULL && searchlist->LoadStartupStoredSearchesForWorker(*pResult);
	if (!pResult->bSuccess && pResult->dwLastError == 0)
		pResult->dwLastError = ERROR_READ_FAULT;
	if (pResult->strStage.IsEmpty())
		pResult->strStage = pResult->bSuccess ? _T("stage-completed") : _T("load-failed");

	if (IsStartupMetadataLoadCancelled(StartupMetadataStoredSearches, lGeneration, uStoredSearchesToken)) {
		CSearchList::DeleteStartupStoredSearchesLoadResult(pResult);
		return false;
	}

	if (emuledlg != NULL && ::IsWindow(emuledlg->m_hWnd) && ::PostMessage(emuledlg->m_hWnd, CemuleDlg::UWM_EMULEAI_STARTUP_STOREDSEARCHES_LOAD_READY, 0, reinterpret_cast<LPARAM>(pResult)))
		return true;

	const DWORD dwPostError = ::GetLastError();
	CSearchList::DeleteStartupStoredSearchesLoadResult(pResult);
	CompleteStartupMetadataLoad(StartupMetadataStoredSearches, lGeneration, uStoredSearchesToken, false, dwPostError, _T("async-stored-searches-load-post"));
	return false;
}

void CemuleApp::SetStartupMetadataState(EStartupMetadataDomain eDomain, EStartupMetadataState eState, LONG lGeneration, uint64 uCancellationToken, DWORD dwLastError, bool bCancelRequested, LPCTSTR pszReason)
{
	if (!IsStartupMetadataDomainValid(eDomain))
		return;
	bool bChanged = false;
	{
		CSingleLock lock(&m_startupMetadataStateLock, TRUE);
		SStartupMetadataLoadState &state = m_startupMetadataStates[static_cast<int>(eDomain)];
		if (lGeneration != 0 && state.m_lGeneration != lGeneration)
			return;
		if (uCancellationToken != 0 && state.m_uCancellationToken != uCancellationToken)
			return;
		if (state.m_eState != eState || state.m_dwLastError != dwLastError || state.m_bCancelRequested != bCancelRequested || state.m_strReason != (pszReason != NULL ? pszReason : _T("")))
			bChanged = true;
		state.m_eState = eState;
		if (lGeneration != 0)
			state.m_lGeneration = lGeneration;
		if (uCancellationToken != 0)
			state.m_uCancellationToken = uCancellationToken;
		state.m_dwLastError = dwLastError;
		state.m_bCancelRequested = bCancelRequested;
		if (state.IsTerminal()) {
			state.m_uProgressDone = 1;
			state.m_uProgressTotal = 1;
			state.m_strProgressStage.Empty();
		}
		state.m_strReason = pszReason != NULL ? pszReason : _T("");
	}
	if (bChanged)
		QueueStartupMetadataStateChangedEvent(eDomain, eState, lGeneration, uCancellationToken, dwLastError, pszReason);
}

void CemuleApp::QueueStartupMetadataStateChangedEvent(EStartupMetadataDomain eDomain, EStartupMetadataState eState, LONG lGeneration, uint64 uCancellationToken, DWORD dwLastError, LPCTSTR pszReason)
{
	AddDebugLogLine(DLP_LOW, false, _T("Startup metadata state changed. domain=%s state=%s generation=%ld token=%I64u error=%lu reason=%s\n"),
		GetStartupMetadataDomainName(eDomain), GetStartupMetadataStateName(eState), lGeneration, uCancellationToken, dwLastError, pszReason != NULL ? pszReason : _T(""));
	if (emuledlg == NULL || !::IsWindow(emuledlg->m_hWnd))
		return;

	SApplicationEvent event;
	event.m_eType = ApplicationEventStartupMetadataStateChanged;
	event.m_eBackendCommandType = BackendCommandPersistence;
	event.m_eBackendCommandSource = BackendCommandSourcePersistence;
	event.m_eBackendCommandOrderingScope = BackendCommandOrderingPersistence;
	event.m_eBackendCommandFailurePolicy = BackendCommandFailurePolicyReport;
	event.m_strBackendCommandOrderingKey.Format(_T("startup-metadata:%u"), static_cast<UINT>(eDomain));
	event.m_eStartupMetadataDomain = eDomain;
	event.m_eStartupMetadataState = eState;
	event.m_lStartupMetadataGeneration = lGeneration;
	event.m_lAsyncGeneration = lGeneration;
	event.m_uCancellationToken = uCancellationToken;
	event.m_uCorrelationId = uCancellationToken;
	event.m_dwLastError = dwLastError;
	event.m_strMessage = pszReason != NULL ? pszReason : _T("");
	QueueApplicationEvent(event);
}

bool CemuleApp::IsStartupMetadataSaveAllowed(EPersistenceCommandType eCommand, CString *pstrReason) const
{
	if (eCommand == PersistenceCommandSaveKnownFiles || eCommand == PersistenceCommandSaveSharedFiles) {
		if (KnownFilesReady() && SharedFilesReady())
			return true;
		if (pstrReason != NULL)
			*pstrReason = KnownFilesReady() ? _T("startup-metadata-not-ready:shared-files") : _T("startup-metadata-not-ready:known-files");
		return false;
	}

	EStartupMetadataDomain eDomain = StartupMetadataDomainCount;
	switch (eCommand) {
		case PersistenceCommandSaveClientHistory:
			eDomain = StartupMetadataClientHistory;
			break;
		case PersistenceCommandSaveSearchStore:
			eDomain = StartupMetadataStoredSearches;
			break;
		default:
			return true;
	}

	if (IsStartupMetadataDomainReady(eDomain))
		return true;
	if (pstrReason != NULL)
		pstrReason->Format(_T("startup-metadata-not-ready:%s"), GetStartupMetadataDomainName(eDomain));
	return false;
}

bool CemuleApp::RejectStartupMetadataPersistenceCommand(const SBackendCommand &command, LPCTSTR pszStage, DWORD dwLastError)
{
	const LPCTSTR pszSafeStage = pszStage != NULL ? pszStage : _T("startup-metadata-not-ready");
	const DWORD dwSafeError = dwLastError != 0 ? dwLastError : ERROR_NOT_READY;
	AddDebugLogLine(DLP_LOW, false, _T("Skipping persistence save before startup metadata domain is ready. command=%u auto=%u reason=%s stage=%s\n"),
		static_cast<UINT>(command.m_persistenceCommand.m_eType), command.m_persistenceCommand.m_bAutoSave ? 1U : 0U, (LPCTSTR)command.m_persistenceCommand.m_strReason, pszSafeStage);
	QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, pszSafeStage, dwSafeError, command.m_uSequence, command.m_uCorrelationId);
	return false;
}

bool CemuleApp::StartWorkerTopology(LPCTSTR pszReason)
{
	bool bStarted = true;
	for (int i = 0; i < WorkerTopologyRoleCount; ++i) {
		if (!StartWorkerTopologyRole(static_cast<EWorkerTopologyRole>(i), pszReason))
			bStarted = false;
	}
	return bStarted;
}

void CemuleApp::StopWorkerTopology(LPCTSTR pszReason)
{
	for (int i = WorkerTopologyRoleCount - 1; i >= 0; --i)
		StopWorkerTopologyRole(static_cast<EWorkerTopologyRole>(i), 2000, pszReason);
}

bool CemuleApp::DrainWorkerTopology(DWORD dwTimeoutMs)
{
	bool bDrained = true;
	for (int i = 0; i < WorkerTopologyRoleCount; ++i) {
		const EWorkerTopologyRole eRole = static_cast<EWorkerTopologyRole>(i);
		if (ShouldDrainWorkerTopologyRole(eRole) && !DrainWorkerTopologyRole(eRole, dwTimeoutMs))
			bDrained = false;
	}
	return bDrained;
}

void CemuleApp::CancelWorkerTopology(LPCTSTR pszReason)
{
	for (int i = 0; i < WorkerTopologyRoleCount; ++i)
		CancelWorkerTopologyRole(static_cast<EWorkerTopologyRole>(i), pszReason);
}

bool CemuleApp::StartPersistenceWorker()
{
	return StartWorkerTopologyRole(WorkerTopologyPersistence, _T("persistence-api"));
}

void CemuleApp::StopPersistenceWorker()
{
	StopWorkerTopologyRole(WorkerTopologyPersistence, 2000, _T("persistence-api"));
}

bool CemuleApp::DrainPersistenceWorker(DWORD dwTimeoutMs)
{
	return DrainWorkerTopologyRole(WorkerTopologyPersistence, dwTimeoutMs);
}

void CemuleApp::CancelPersistenceWorker()
{
	CancelWorkerTopologyRole(WorkerTopologyPersistence, _T("persistence-api"));
}

bool CemuleApp::QueuePersistenceWorkerItem(const SWorkerTopologyItem &item)
{
	return QueueWorkerTopologyItem(WorkerTopologyPersistence, item);
}

bool CemuleApp::StartStartupLoadWorker(EStartupMetadataDomain eDomain)
{
	const EWorkerTopologyRole eRole = GetStartupMetadataWorkerRole(eDomain);
	return IsWorkerTopologyRoleValid(eRole) && StartWorkerTopologyRole(eRole, _T("startup-load-api"));
}

bool CemuleApp::QueueStartupLoadWorkerItem(EStartupMetadataDomain eDomain, const SWorkerTopologyItem &item)
{
	const EWorkerTopologyRole eRole = GetStartupMetadataWorkerRole(eDomain);
	return IsWorkerTopologyRoleValid(eRole) && QueueWorkerTopologyItem(eRole, item);
}

bool CemuleApp::StartNetworkUtilityWorker()
{
	return StartWorkerTopologyRole(WorkerTopologyNetworkUtility, _T("network-utility-api"));
}

void CemuleApp::StopNetworkUtilityWorker()
{
	StopWorkerTopologyRole(WorkerTopologyNetworkUtility, 2000, _T("network-utility-api"));
}

bool CemuleApp::DrainNetworkUtilityWorker(DWORD dwTimeoutMs)
{
	return DrainWorkerTopologyRole(WorkerTopologyNetworkUtility, dwTimeoutMs);
}

void CemuleApp::CancelNetworkUtilityWorker()
{
	CancelWorkerTopologyRole(WorkerTopologyNetworkUtility, _T("network-utility-api"));
}

bool CemuleApp::QueueNetworkUtilityWorkerItem(const SWorkerTopologyItem &item)
{
	return QueueWorkerTopologyItem(WorkerTopologyNetworkUtility, item);
}

bool CemuleApp::StartNetworkParseCpuWorker()
{
	return StartWorkerTopologyRole(WorkerTopologyNetworkParseCpu, _T("network-parse-cpu-api"));
}

void CemuleApp::StopNetworkParseCpuWorker()
{
	StopWorkerTopologyRole(WorkerTopologyNetworkParseCpu, 2000, _T("network-parse-cpu-api"));
}

bool CemuleApp::DrainNetworkParseCpuWorker(DWORD dwTimeoutMs)
{
	return DrainWorkerTopologyRole(WorkerTopologyNetworkParseCpu, dwTimeoutMs);
}

void CemuleApp::CancelNetworkParseCpuWorker()
{
	CancelWorkerTopologyRole(WorkerTopologyNetworkParseCpu, _T("network-parse-cpu-api"));
}

bool CemuleApp::QueueNetworkParseCpuWorkerItem(const SWorkerTopologyItem &item)
{
	return QueueWorkerTopologyItem(WorkerTopologyNetworkParseCpu, item);
}

bool CemuleApp::QueueCollectionImportWorkerJob(HWND hNotifyWnd, const CString &strPath)
{
	if (strPath.IsEmpty() || IsClosing())
		return false;

	if (GetWorkerTopologyState(WorkerTopologyNetworkParseCpu) == WorkerTopologyStopped && !StartNetworkParseCpuWorker()) {
		QueueCollectionImportFailureEvent(strPath, _T("network-parse-cpu-start"), ::GetLastError());
		return false;
	}

	SWorkerTopologyItem item;
	item.m_eRole = WorkerTopologyNetworkParseCpu;
	item.m_eType = WorkerTopologyItemNetworkParseCpu;
	item.m_dwCreatedTick = ::GetTickCount();
	item.m_dwDueTick = item.m_dwCreatedTick;
	item.m_hNotifyWnd = hNotifyWnd;
	item.m_strStage = _T("collection-import");
	item.m_strPayload = strPath;
	if (QueueNetworkParseCpuWorkerItem(item))
		return true;

	QueueCollectionImportFailureEvent(strPath, _T("network-parse-cpu-queue"), ERROR_INVALID_HANDLE);
	return false;
}

bool CemuleApp::QueueCollectionImportResult(HWND hNotifyWnd, SCollectionImportResult *pResult)
{
	if (hNotifyWnd == NULL || pResult == NULL || IsClosing()) {
		::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return false;
	}

	pResult->hNotifyWnd = hNotifyWnd;
	CSingleLock lock(&m_collectionImportResultLock, TRUE);
	if (!::IsWindow(hNotifyWnd)) {
		::SetLastError(ERROR_INVALID_WINDOW_HANDLE);
		return false;
	}

	m_collectionImportResults.AddTail(pResult);
	if (::PostMessage(hNotifyWnd, CemuleDlg::UWM_EMULEAI_COLLECTION_IMPORT_READY, 0, 0))
		return true;

	for (POSITION pos = m_collectionImportResults.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SCollectionImportResult *pQueuedResult = m_collectionImportResults.GetNext(pos);
		if (pQueuedResult == pResult) {
			m_collectionImportResults.RemoveAt(posCurrent);
			break;
		}
	}
	return false;
}

SCollectionImportResult* CemuleApp::PopCollectionImportResult(HWND hNotifyWnd)
{
	if (hNotifyWnd == NULL)
		return NULL;

	CSingleLock lock(&m_collectionImportResultLock, TRUE);
	for (POSITION pos = m_collectionImportResults.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SCollectionImportResult *pResult = m_collectionImportResults.GetNext(pos);
		if (pResult != NULL && pResult->hNotifyWnd == hNotifyWnd) {
			m_collectionImportResults.RemoveAt(posCurrent);
			pResult->hNotifyWnd = NULL;
			return pResult;
		}
	}
	return NULL;
}

void CemuleApp::ClearCollectionImportResults(HWND hNotifyWnd)
{
	if (hNotifyWnd == NULL)
		return;

	CSingleLock lock(&m_collectionImportResultLock, TRUE);
	for (POSITION pos = m_collectionImportResults.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SCollectionImportResult *pResult = m_collectionImportResults.GetNext(pos);
		if (pResult != NULL && pResult->hNotifyWnd == hNotifyWnd) {
			m_collectionImportResults.RemoveAt(posCurrent);
			delete pResult->pCollection;
			delete pResult;
		}
	}
}

bool CemuleApp::BeginSharedFilesFileSystemReload(HWND hNotifyWnd, LONG lGeneration, uint64 *puReloadToken)
{
	if (puReloadToken != NULL)
		*puReloadToken = 0;
	if (hNotifyWnd == NULL || lGeneration <= 0 || IsClosing())
		return false;

	CSingleLock lock(&m_sharedFilesFileSystemReloadStateLock, TRUE);
	uint64 uExistingToken = 0;
	if (m_sharedFilesFileSystemReloadTokens.Lookup(hNotifyWnd, uExistingToken) && uExistingToken != 0)
		return false;

	uint64 uReloadToken = ++m_uNextSharedFilesFileSystemReloadToken;
	if (uReloadToken == 0)
		uReloadToken = ++m_uNextSharedFilesFileSystemReloadToken;
	m_sharedFilesFileSystemReloadGenerations.SetAt(hNotifyWnd, lGeneration);
	m_sharedFilesFileSystemReloadTokens.SetAt(hNotifyWnd, uReloadToken);
	if (puReloadToken != NULL)
		*puReloadToken = uReloadToken;
	return true;
}

bool CemuleApp::IsSharedFilesFileSystemReloadActive(HWND hNotifyWnd)
{
	if (hNotifyWnd == NULL)
		return false;

	CSingleLock lock(&m_sharedFilesFileSystemReloadStateLock, TRUE);
	uint64 uReloadToken = 0;
	return m_sharedFilesFileSystemReloadTokens.Lookup(hNotifyWnd, uReloadToken) && uReloadToken != 0;
}

bool CemuleApp::CompleteSharedFilesFileSystemReload(HWND hNotifyWnd, LONG lGeneration, uint64 uReloadToken)
{
	if (hNotifyWnd == NULL || lGeneration <= 0 || uReloadToken == 0)
		return false;

	CSingleLock lock(&m_sharedFilesFileSystemReloadStateLock, TRUE);
	LONG lActiveGeneration = 0;
	uint64 uActiveToken = 0;
	if (!m_sharedFilesFileSystemReloadGenerations.Lookup(hNotifyWnd, lActiveGeneration) || !m_sharedFilesFileSystemReloadTokens.Lookup(hNotifyWnd, uActiveToken))
		return false;
	if (lActiveGeneration != lGeneration || uActiveToken != uReloadToken)
		return false;
	m_sharedFilesFileSystemReloadGenerations.RemoveKey(hNotifyWnd);
	m_sharedFilesFileSystemReloadTokens.RemoveKey(hNotifyWnd);
	return true;
}

void CemuleApp::CancelSharedFilesFileSystemReload(HWND hNotifyWnd)
{
	if (hNotifyWnd == NULL)
		return;

	CSingleLock lock(&m_sharedFilesFileSystemReloadStateLock, TRUE);
	m_sharedFilesFileSystemReloadGenerations.RemoveKey(hNotifyWnd);
	m_sharedFilesFileSystemReloadTokens.RemoveKey(hNotifyWnd);
}

bool CemuleApp::QueueSharedFilesFileSystemReloadWorkerJob(HWND hNotifyWnd, LONG lGeneration, uint64 uReloadToken, const CString &strDirectory)
{
	if (hNotifyWnd == NULL || lGeneration <= 0 || uReloadToken == 0 || strDirectory.IsEmpty() || IsClosing())
		return false;

	if (GetWorkerTopologyState(WorkerTopologyPersistence) == WorkerTopologyStopped && !StartPersistenceWorker())
		return false;

	SWorkerTopologyItem item;
	item.m_eRole = WorkerTopologyPersistence;
	item.m_eType = WorkerTopologyItemFileSystemReload;
	item.m_dwCreatedTick = ::GetTickCount();
	item.m_dwDueTick = item.m_dwCreatedTick;
	item.m_hNotifyWnd = hNotifyWnd;
	item.m_lWorkerGeneration = lGeneration;
	item.m_uCorrelationId = uReloadToken;
	item.m_strStage = _T("shared-files-filesystem-reload");
	item.m_strPayload = strDirectory;
	item.m_strCoalesceKey = _T("shared-files-filesystem-reload");
	return QueuePersistenceWorkerItem(item);
}

bool CemuleApp::IsWorkerTopologyRoleValid(EWorkerTopologyRole eRole) const
{
	return eRole >= WorkerTopologyBackendCommand && eRole < WorkerTopologyRoleCount && GetWorkerTopologySpec(eRole) != NULL;
}

bool CemuleApp::IsStartupLoadWorkerRole(EWorkerTopologyRole eRole) const
{
	switch (eRole) {
		case WorkerTopologyStartupLoadPrimary:
		case WorkerTopologyStartupLoadSecondary:
		case WorkerTopologyStartupLoadSearches:
		case WorkerTopologyStartupLoadDownloads:
		case WorkerTopologyStartupLoadKnown2:
			return true;
		default:
			return false;
	}
}

bool CemuleApp::AllStartupMetadataLoadsTerminal() const
{
	CSingleLock lock(const_cast<CCriticalSection*>(&m_startupMetadataStateLock), TRUE);
	for (int i = 0; i < StartupMetadataDomainCount; ++i) {
		if (!m_startupMetadataStates[i].IsTerminal())
			return false;
	}
	return true;
}

void CemuleApp::StopStartupLoadWorkersIfIdle(LPCTSTR pszReason)
{
	if (IsClosing() || !AllStartupMetadataLoadsTerminal())
		return;

	static const EWorkerTopologyRole s_startupRoles[] =
	{
		WorkerTopologyStartupLoadPrimary,
		WorkerTopologyStartupLoadSecondary,
		WorkerTopologyStartupLoadSearches,
		WorkerTopologyStartupLoadDownloads,
		WorkerTopologyStartupLoadKnown2
	};

	for (int i = 0; i < static_cast<int>(_countof(s_startupRoles)); ++i) {
		const EWorkerTopologyRole eRole = s_startupRoles[i];
		if (GetWorkerTopologyState(eRole) != WorkerTopologyStopped && !IsWorkerTopologyRoleIdle(eRole))
			return;
	}

	const LPCTSTR pszSafeReason = pszReason != NULL ? pszReason : _T("startup-load-workers-idle");
	for (int i = static_cast<int>(_countof(s_startupRoles)) - 1; i >= 0; --i) {
		const EWorkerTopologyRole eRole = s_startupRoles[i];
		if (GetWorkerTopologyState(eRole) != WorkerTopologyStopped)
			StopWorkerTopologyRole(eRole, 250, pszSafeReason);
	}
}

bool CemuleApp::IsWorkerTopologyThreadRole(EWorkerTopologyRole eRole) const
{
	const SWorkerTopologySpec* pSpec = GetWorkerTopologySpec(eRole);
	return pSpec != NULL && pSpec->m_bDedicatedThread;
}

UINT CemuleApp::GetWorkerTopologyQueueLimit(EWorkerTopologyRole eRole) const
{
	const SWorkerTopologySpec* pSpec = GetWorkerTopologySpec(eRole);
	return pSpec != NULL ? pSpec->m_uQueueLimit : 0;
}

bool CemuleApp::ShouldCoalesceWorkerTopologyRole(EWorkerTopologyRole eRole) const
{
	const SWorkerTopologySpec* pSpec = GetWorkerTopologySpec(eRole);
	return pSpec != NULL && pSpec->m_bCoalesceByKey;
}

bool CemuleApp::ShouldDropOldestWorkerTopologyItem(EWorkerTopologyRole eRole) const
{
	const SWorkerTopologySpec* pSpec = GetWorkerTopologySpec(eRole);
	return pSpec != NULL && pSpec->m_bDropOldestOnPressure;
}

bool CemuleApp::ShouldDrainWorkerTopologyRole(EWorkerTopologyRole eRole) const
{
	const SWorkerTopologySpec* pSpec = GetWorkerTopologySpec(eRole);
	return pSpec != NULL && pSpec->m_bDrainOnShutdown;
}

bool CemuleApp::ShouldCancelWorkerTopologyRole(EWorkerTopologyRole eRole) const
{
	const SWorkerTopologySpec* pSpec = GetWorkerTopologySpec(eRole);
	return pSpec != NULL && pSpec->m_bCancelOnShutdown;
}

bool CemuleApp::ShouldAcceptWorkerTopologyItem(EWorkerTopologyRole eRole) const
{
	if (!IsWorkerTopologyRoleValid(eRole) || !IsWorkerTopologyThreadRole(eRole))
		return false;
	const int iRole = static_cast<int>(eRole);
	const EWorkerTopologyState eState = GetWorkerTopologyState(eRole);
	return m_bWorkerTopologyAccepting[iRole] && !IsBackendLifecycleStopping() && !IsClosing() && eState != WorkerTopologyStopping && eState != WorkerTopologyQuarantined;
}

bool CemuleApp::CleanupStoppedWorkerTopologyRoleLocked(EWorkerTopologyRole eRole)
{
	if (!IsWorkerTopologyRoleValid(eRole) || !IsWorkerTopologyThreadRole(eRole))
		return false;

	const int iRole = static_cast<int>(eRole);
	CWinThread* pThread = m_pWorkerTopologyThreads[iRole];
	HANDLE hThread = pThread != NULL ? pThread->m_hThread : NULL;
	if (hThread != NULL && ::WaitForSingleObject(hThread, 0) == WAIT_TIMEOUT)
		return false;

	if (pThread != NULL) {
		delete pThread;
		m_pWorkerTopologyThreads[iRole] = NULL;
	}
	if (m_hWorkerTopologyEvents[iRole] != NULL) {
		::CloseHandle(m_hWorkerTopologyEvents[iRole]);
		m_hWorkerTopologyEvents[iRole] = NULL;
	}
	if (m_hWorkerTopologyStopEvents[iRole] != NULL) {
		::CloseHandle(m_hWorkerTopologyStopEvents[iRole]);
		m_hWorkerTopologyStopEvents[iRole] = NULL;
	}
	m_dwWorkerTopologyThreadIds[iRole] = 0;
	m_bWorkerTopologyAccepting[iRole] = false;
	::InterlockedExchange(&m_alWorkerTopologyInFlight[iRole], 0);
	m_workerTopologyQueues[iRole].RemoveAll();
	::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyStopped);
	return true;
}

bool CemuleApp::StartWorkerTopologyRole(EWorkerTopologyRole eRole, LPCTSTR pszReason)
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return false;

	const int iRole = static_cast<int>(eRole);
	if (!IsWorkerTopologyThreadRole(eRole)) {
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		m_bWorkerTopologyAccepting[iRole] = true;
		::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyRunning);
		return true;
	}

	{
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		if (m_pWorkerTopologyThreads[iRole] != NULL) {
			if (CleanupStoppedWorkerTopologyRoleLocked(eRole)) {
				// The previous bounded stop completed after timeout. Start a fresh worker below.
			} else if (GetWorkerTopologyState(eRole) == WorkerTopologyStopping || GetWorkerTopologyState(eRole) == WorkerTopologyQuarantined) {
				AddDebugLogLine(DLP_LOW, false, _T("Worker topology role %s start ignored while previous stop is still pending. reason=%s\n"), GetWorkerTopologyRoleName(eRole), pszReason != NULL ? pszReason : _T(""));
				return false;
			} else {
				m_bWorkerTopologyAccepting[iRole] = true;
				::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyRunning);
				return true;
			}
		}
	}

	HANDLE hQueueEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	HANDLE hStopEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	if (hQueueEvent == NULL || hStopEvent == NULL) {
		if (hQueueEvent != NULL)
			::CloseHandle(hQueueEvent);
		if (hStopEvent != NULL)
			::CloseHandle(hStopEvent);
		AddDebugLogLine(DLP_HIGH, false, _T("Worker topology role %s could not create events. reason=%s\n"), GetWorkerTopologyRoleName(eRole), pszReason != NULL ? pszReason : _T(""));
		return false;
	}

	SWorkerTopologyThreadParam* pParam = new SWorkerTopologyThreadParam();
	pParam->m_pApp = this;
	pParam->m_eRole = eRole;

	CWinThread* pThread = AfxBeginThread(WorkerTopologyThreadProc, pParam, THREAD_PRIORITY_BELOW_NORMAL, 0, CREATE_SUSPENDED, NULL);
	if (pThread == NULL) {
		delete pParam;
		::CloseHandle(hQueueEvent);
		::CloseHandle(hStopEvent);
		AddDebugLogLine(DLP_HIGH, false, _T("Worker topology role %s could not start. reason=%s\n"), GetWorkerTopologyRoleName(eRole), pszReason != NULL ? pszReason : _T(""));
		return false;
	}

	pThread->m_bAutoDelete = FALSE;
	{
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		m_pWorkerTopologyThreads[iRole] = pThread;
		m_hWorkerTopologyEvents[iRole] = hQueueEvent;
		m_hWorkerTopologyStopEvents[iRole] = hStopEvent;
		m_bWorkerTopologyAccepting[iRole] = true;
		::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyStarting);
	}
	pThread->ResumeThread();
	AddDebugLogLine(DLP_LOW, false, _T("Worker topology role %s started. reason=%s\n"), GetWorkerTopologyRoleName(eRole), pszReason != NULL ? pszReason : _T(""));
	return true;
}

void CemuleApp::StopWorkerTopologyRole(EWorkerTopologyRole eRole, DWORD dwTimeoutMs, LPCTSTR pszReason)
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return;

	const int iRole = static_cast<int>(eRole);
	if (!IsWorkerTopologyThreadRole(eRole)) {
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		m_bWorkerTopologyAccepting[iRole] = false;
		m_workerTopologyQueues[iRole].RemoveAll();
		::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyStopped);
		return;
	}

	CWinThread* pThread = NULL;
	HANDLE hThread = NULL;
	HANDLE hQueueEvent = NULL;
	HANDLE hStopEvent = NULL;
	{
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		m_bWorkerTopologyAccepting[iRole] = false;
		::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyStopping);
		if (ShouldCancelWorkerTopologyRole(eRole))
			m_workerTopologyQueues[iRole].RemoveAll();
		pThread = m_pWorkerTopologyThreads[iRole];
		hThread = pThread != NULL ? pThread->m_hThread : NULL;
		hQueueEvent = m_hWorkerTopologyEvents[iRole];
		hStopEvent = m_hWorkerTopologyStopEvents[iRole];
	}

	if (hStopEvent != NULL)
		::SetEvent(hStopEvent);
	if (hQueueEvent != NULL)
		::SetEvent(hQueueEvent);

	DWORD dwWait = WAIT_OBJECT_0;
	if (hThread != NULL)
		dwWait = ::WaitForSingleObject(hThread, dwTimeoutMs);

	if (dwWait != WAIT_OBJECT_0) {
		AddDebugLogLine(DLP_HIGH, false, _T("Worker topology role %s stop timed out. wait=%lu reason=%s inFlight=%ld queueEmpty=%u\n"), GetWorkerTopologyRoleName(eRole), dwWait, pszReason != NULL ? pszReason : _T(""), ::InterlockedCompareExchange(&m_alWorkerTopologyInFlight[iRole], 0, 0), IsWorkerTopologyQueueEmpty(eRole) ? 1U : 0U);
		const DWORD dwEscalationTimeout = dwTimeoutMs > 1000 ? dwTimeoutMs : 1000;
		const DWORD dwEscalationStarted = ::GetTickCount();
		for (;;) {
			if (hStopEvent != NULL)
				::SetEvent(hStopEvent);
			if (hQueueEvent != NULL)
				::SetEvent(hQueueEvent);

			const DWORD dwElapsed = ::GetTickCount() - dwEscalationStarted;
			if (dwElapsed >= dwEscalationTimeout)
				break;
			const DWORD dwRemaining = dwEscalationTimeout - dwElapsed;
			const DWORD dwWaitSlice = dwRemaining < 250 ? dwRemaining : 250;
			dwWait = hThread != NULL ? ::WaitForSingleObject(hThread, dwWaitSlice) : WAIT_OBJECT_0;
			if (dwWait == WAIT_OBJECT_0)
				break;
		}

		if (dwWait != WAIT_OBJECT_0) {
			CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
			m_bWorkerTopologyAccepting[iRole] = false;
			if (ShouldCancelWorkerTopologyRole(eRole))
				m_workerTopologyQueues[iRole].RemoveAll();
			::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyQuarantined);
			AddDebugLogLine(DLP_HIGH, false, _T("Worker topology role %s stop quarantined after bounded wait. reason=%s inFlight=%ld queueEmpty=%u\n"), GetWorkerTopologyRoleName(eRole), pszReason != NULL ? pszReason : _T(""), ::InterlockedCompareExchange(&m_alWorkerTopologyInFlight[iRole], 0, 0), IsWorkerTopologyQueueEmpty(eRole) ? 1U : 0U);
			return;
		}
	}

	{
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		CleanupStoppedWorkerTopologyRoleLocked(eRole);
	}
	AddDebugLogLine(DLP_LOW, false, _T("Worker topology role %s stopped. reason=%s\n"), GetWorkerTopologyRoleName(eRole), pszReason != NULL ? pszReason : _T(""));
}

bool CemuleApp::DrainWorkerTopologyRole(EWorkerTopologyRole eRole, DWORD dwTimeoutMs)
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return true;

	const DWORD dwStarted = ::GetTickCount();
	if (IsWorkerTopologyThreadRole(eRole)) {
		const int iRole = static_cast<int>(eRole);
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyDraining);
		if (m_hWorkerTopologyEvents[iRole] != NULL)
			::SetEvent(m_hWorkerTopologyEvents[iRole]);
	}

	for (;;) {
		if (IsWorkerTopologyRoleIdle(eRole))
			return true;
		if (dwTimeoutMs == 0)
			return false;
		const DWORD dwElapsed = ::GetTickCount() - dwStarted;
		if (dwElapsed >= dwTimeoutMs)
			break;
		::Sleep(10);
	}

	INT_PTR iPendingItems = 0;
	LONG lInFlight = 0;
	{
		const int iRole = static_cast<int>(eRole);
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		iPendingItems = m_workerTopologyQueues[iRole].GetCount();
		lInFlight = ::InterlockedCompareExchange(&m_alWorkerTopologyInFlight[iRole], 0, 0);
	}
	AddDebugLogLine(DLP_HIGH, false, _T("Worker topology role %s drain timed out. timeout=%lu pending=%Id inFlight=%ld\n"), GetWorkerTopologyRoleName(eRole), dwTimeoutMs, iPendingItems, lInFlight);
	return false;
}

void CemuleApp::CancelWorkerTopologyRole(EWorkerTopologyRole eRole, LPCTSTR pszReason)
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return;

	const int iRole = static_cast<int>(eRole);
	{
		CSingleLock seqLock(&m_workerTopologySequenceLock, TRUE);
		++m_uWorkerTopologyCancellationTokens[iRole];
	}
	CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
	m_bWorkerTopologyAccepting[iRole] = false;
	if (ShouldCancelWorkerTopologyRole(eRole) || !ShouldDrainWorkerTopologyRole(eRole))
		m_workerTopologyQueues[iRole].RemoveAll();
	if (GetWorkerTopologyState(eRole) == WorkerTopologyRunning)
		::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyStopping);
	AddDebugLogLine(DLP_LOW, false, _T("Worker topology role %s canceled. reason=%s\n"), GetWorkerTopologyRoleName(eRole), pszReason != NULL ? pszReason : _T(""));
}

bool CemuleApp::QueueWorkerTopologyItem(EWorkerTopologyRole eRole, const SWorkerTopologyItem &item)
{
	if (!IsWorkerTopologyRoleValid(eRole) || !IsWorkerTopologyThreadRole(eRole))
		return false;

	const int iRole = static_cast<int>(eRole);
	SWorkerTopologyItem queuedItem(item);
	queuedItem.m_eRole = eRole;
	if (queuedItem.m_dwCreatedTick == 0)
		queuedItem.m_dwCreatedTick = ::GetTickCount();

	{
		CSingleLock seqLock(&m_workerTopologySequenceLock, TRUE);
		queuedItem.m_uSequence = ++m_uNextWorkerTopologySequence;
		if (queuedItem.m_uCancellationToken == 0)
			queuedItem.m_uCancellationToken = m_uWorkerTopologyCancellationTokens[iRole];
	}

	{
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		if (!ShouldAcceptWorkerTopologyItem(eRole))
			return false;

		if (ShouldCoalesceWorkerTopologyRole(eRole))
			CoalesceWorkerTopologyItemLocked(eRole, queuedItem);

		const UINT uLimit = GetWorkerTopologyQueueLimit(eRole);
		if (uLimit == 0)
			return false;

		while (m_workerTopologyQueues[iRole].GetCount() >= static_cast<INT_PTR>(uLimit)) {
			if (!ShouldDropOldestWorkerTopologyItem(eRole))
				return false;
			m_workerTopologyQueues[iRole].RemoveHead();
		}

		m_workerTopologyQueues[iRole].AddTail(queuedItem);
		if (m_hWorkerTopologyEvents[iRole] != NULL)
			::SetEvent(m_hWorkerTopologyEvents[iRole]);
	}

	return true;
}

bool CemuleApp::PopWorkerTopologyItem(EWorkerTopologyRole eRole, SWorkerTopologyItem &item)
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return false;

	const int iRole = static_cast<int>(eRole);
	CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
	if (m_workerTopologyQueues[iRole].IsEmpty())
		return false;
	item = m_workerTopologyQueues[iRole].RemoveHead();
	return true;
}

void CemuleApp::ClearWorkerTopologyQueue(EWorkerTopologyRole eRole)
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return;

	const int iRole = static_cast<int>(eRole);
	CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
	m_workerTopologyQueues[iRole].RemoveAll();
}

bool CemuleApp::IsWorkerTopologyQueueEmpty(EWorkerTopologyRole eRole)
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return true;

	const int iRole = static_cast<int>(eRole);
	CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
	return m_workerTopologyQueues[iRole].IsEmpty() != FALSE;
}

bool CemuleApp::IsWorkerTopologyRoleIdle(EWorkerTopologyRole eRole)
{
	if (!IsWorkerTopologyRoleValid(eRole))
		return true;

	const int iRole = static_cast<int>(eRole);
	CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
	const LONG lInFlight = ::InterlockedCompareExchange(&m_alWorkerTopologyInFlight[iRole], 0, 0);
	return m_workerTopologyQueues[iRole].IsEmpty() != FALSE && lInFlight == 0;
}

bool CemuleApp::CoalesceWorkerTopologyItemLocked(EWorkerTopologyRole eRole, const SWorkerTopologyItem &item)
{
	if (item.m_strCoalesceKey.IsEmpty())
		return false;

	const int iRole = static_cast<int>(eRole);
	POSITION pos = m_workerTopologyQueues[iRole].GetHeadPosition();
	while (pos != NULL) {
		POSITION posCurrent = pos;
		const SWorkerTopologyItem &oldItem = m_workerTopologyQueues[iRole].GetNext(pos);
		if (oldItem.m_eType == item.m_eType && oldItem.m_strCoalesceKey == item.m_strCoalesceKey) {
			m_workerTopologyQueues[iRole].RemoveAt(posCurrent);
			return true;
		}
	}
	return false;
}

UINT AFX_CDECL CemuleApp::WorkerTopologyThreadProc(LPVOID pParam)
{
	SWorkerTopologyThreadParam* pThreadParam = static_cast<SWorkerTopologyThreadParam*>(pParam);
	CemuleApp* pApp = pThreadParam != NULL ? pThreadParam->m_pApp : NULL;
	const EWorkerTopologyRole eRole = pThreadParam != NULL ? pThreadParam->m_eRole : WorkerTopologyBackendCommand;
	delete pThreadParam;
	return pApp != NULL ? pApp->RunWorkerTopologyThread(eRole) : 0;
}

UINT CemuleApp::RunWorkerTopologyThread(EWorkerTopologyRole eRole)
{
	if (!IsWorkerTopologyRoleValid(eRole) || !IsWorkerTopologyThreadRole(eRole))
		return 0;

	DbgSetThreadName("%s", GetWorkerTopologyRoleThreadName(eRole));
	if (eRole == WorkerTopologyNetworkParseCpu)
		SetNetworkParserOwnerThreadId(::GetCurrentThreadId());
	const int iRole = static_cast<int>(eRole);
	{
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		m_dwWorkerTopologyThreadIds[iRole] = ::GetCurrentThreadId();
		::InterlockedExchange(&m_alWorkerTopologyStates[iRole], WorkerTopologyRunning);
	}

	HANDLE handles[2] = { m_hWorkerTopologyStopEvents[iRole], m_hWorkerTopologyEvents[iRole] };
	AddDebugLogLine(DLP_VERYLOW, false, _T("Worker topology role %s thread started. thread=%lu\n"), GetWorkerTopologyRoleName(eRole), m_dwWorkerTopologyThreadIds[iRole]);

	bool bUnhandledException = false;
	try {
		for (;;) {
			const DWORD dwWait = ::WaitForMultipleObjects(2, handles, FALSE, INFINITE);
			if (dwWait == WAIT_OBJECT_0)
				break;
			if (dwWait != WAIT_OBJECT_0 + 1)
				break;

			SWorkerTopologyItem item;
			while (PopWorkerTopologyItem(eRole, item))
				ProcessWorkerTopologyItem(item);
		}

		if (ShouldDrainWorkerTopologyRole(eRole)) {
			SWorkerTopologyItem item;
			while (PopWorkerTopologyItem(eRole, item))
				ProcessWorkerTopologyItem(item);
		}
		else {
			ClearWorkerTopologyQueue(eRole);
		}
	}
	catch (CException *ex) {
		TCHAR szError[512] = _T("");
		if (ex != NULL) {
			ex->GetErrorMessage(szError, _countof(szError));
			ex->Delete();
		}
		bUnhandledException = true;
		AddDebugLogLine(DLP_HIGH, false, _T("Worker topology role %s thread stopped after MFC exception. error=%s\n"), GetWorkerTopologyRoleName(eRole), szError);
	}
	catch (...) {
		bUnhandledException = true;
		AddDebugLogLine(DLP_HIGH, false, _T("Worker topology role %s thread stopped after unknown exception.\n"), GetWorkerTopologyRoleName(eRole));
	}

	{
		CSingleLock lock(&m_workerTopologyQueueLocks[iRole], TRUE);
		m_bWorkerTopologyAccepting[iRole] = false;
		m_dwWorkerTopologyThreadIds[iRole] = 0;
		::InterlockedExchange(&m_alWorkerTopologyStates[iRole], bUnhandledException ? WorkerTopologyQuarantined : WorkerTopologyStopped);
	}
	if (eRole == WorkerTopologyNetworkParseCpu)
		SetNetworkParserOwnerThreadId(0);
	AddDebugLogLine(DLP_VERYLOW, false, _T("Worker topology role %s thread stopped.\n"), GetWorkerTopologyRoleName(eRole));
	return 0;
}

void CemuleApp::ProcessWorkerTopologyItem(const SWorkerTopologyItem &item)
{
	uint64 uCancellationToken = 0;
	if (!IsWorkerTopologyRoleValid(item.m_eRole))
		return;
	{
		CSingleLock seqLock(&m_workerTopologySequenceLock, TRUE);
		uCancellationToken = m_uWorkerTopologyCancellationTokens[static_cast<int>(item.m_eRole)];
	}
	if (item.m_uCancellationToken < uCancellationToken && ShouldCancelWorkerTopologyRole(item.m_eRole))
		return;
	if ((IsBackendLifecycleStopping() || IsClosing()) && !ShouldDrainWorkerTopologyRole(item.m_eRole))
		return;

	CScopedInterlockedCounter scopedWorkerInFlight(m_alWorkerTopologyInFlight[static_cast<int>(item.m_eRole)]);
	try {
		switch (item.m_eRole) {
			case WorkerTopologyNetworkParseCpu:
				ProcessNetworkParseCpuWorkerItem(item);
				break;
			case WorkerTopologyNetworkUtility:
				ProcessNetworkUtilityWorkerItem(item);
				break;
			case WorkerTopologyPersistence:
				ProcessPersistenceWorkerItem(item);
				break;
			case WorkerTopologyStartupLoadPrimary:
			case WorkerTopologyStartupLoadSecondary:
			case WorkerTopologyStartupLoadSearches:
			case WorkerTopologyStartupLoadDownloads:
			case WorkerTopologyStartupLoadKnown2:
				ProcessStartupLoadWorkerItem(item);
				break;
			default:
				AddDebugLogLine(DLP_HIGH, false, _T("Worker topology item dropped for unsupported role %s. type=%u\n"), GetWorkerTopologyRoleName(item.m_eRole), static_cast<UINT>(item.m_eType));
				break;
		}
	}
	catch (CException *ex) {
		TCHAR szError[512] = _T("");
		if (ex != NULL) {
			ex->GetErrorMessage(szError, _countof(szError));
			ex->Delete();
		}
		AddDebugLogLine(DLP_HIGH, false, _T("Worker topology item failed with MFC exception. role=%s type=%u stage=%s error=%s\n"), GetWorkerTopologyRoleName(item.m_eRole), static_cast<UINT>(item.m_eType), (LPCTSTR)item.m_strStage, szError);
	}
	catch (...) {
		AddDebugLogLine(DLP_HIGH, false, _T("Worker topology item failed with unknown exception. role=%s type=%u stage=%s\n"), GetWorkerTopologyRoleName(item.m_eRole), static_cast<UINT>(item.m_eType), (LPCTSTR)item.m_strStage);
	}
}

bool CemuleApp::WaitWorkerTopologyItemDueTime(EWorkerTopologyRole eRole, DWORD dwDueTick)
{
	if (dwDueTick == 0)
		return true;

	while (static_cast<LONG>(dwDueTick - ::GetTickCount()) > 0) {
		const DWORD dwDelay = min(static_cast<DWORD>(dwDueTick - ::GetTickCount()), static_cast<DWORD>(50));
		HANDLE hStopEvent = IsWorkerTopologyRoleValid(eRole) ? m_hWorkerTopologyStopEvents[static_cast<int>(eRole)] : NULL;
		if (hStopEvent != NULL && ::WaitForSingleObject(hStopEvent, dwDelay) == WAIT_OBJECT_0)
			return false;
		if (hStopEvent == NULL)
			::Sleep(dwDelay);
	}
	return true;
}

bool CemuleApp::ProcessNetworkParseCpuWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_eType != WorkerTopologyItemNetworkParseCpu)
		return false;

	if (item.m_strStage == _T("search-answer-parse")) {
		if (searchlist == NULL)
			return false;
		searchlist->ProcessNetworkParseCpuWorkerJobs();
		return true;
	}

	if (item.m_strStage == _T("download-link-parse")) {
		ProcessDownloadLinkParseJobsOnParserThread();
		return true;
	}

	if (item.m_strStage == _T("collection-import"))
		return ProcessCollectionImportWorkerItem(item);

	AddDebugLogLine(DLP_LOW, false, _T("Network parse CPU worker item dropped. stage=%s type=%u\n"), (LPCTSTR)item.m_strStage, static_cast<UINT>(item.m_eType));
	return false;
}

bool CemuleApp::ProcessCollectionImportWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_strPayload.IsEmpty())
		return false;

	SCollectionImportResult *pResult = new SCollectionImportResult();
	pResult->strPath = item.m_strPayload;
	CCollection *pCollection = new CCollection();
	const int iSlash = item.m_strPayload.ReverseFind(_T('\\'));
	const CString strFileName = (iSlash >= 0) ? item.m_strPayload.Mid(iSlash + 1) : item.m_strPayload;
	if (pCollection->InitCollectionFromFile(item.m_strPayload, strFileName)) {
		pResult->pCollection = pCollection;
		pResult->bSuccess = true;
	} else {
		pResult->strStage = _T("parse-failed");
		pResult->dwLastError = ERROR_INVALID_DATA;
		delete pCollection;
	}

	DWORD dwPostError = ERROR_INVALID_WINDOW_HANDLE;
	if (item.m_hNotifyWnd != NULL && ::IsWindow(item.m_hNotifyWnd)) {
		if (QueueCollectionImportResult(item.m_hNotifyWnd, pResult))
			return true;
		dwPostError = ::GetLastError();
		if (dwPostError == ERROR_SUCCESS)
			dwPostError = ERROR_INVALID_WINDOW_HANDLE;
	}

	if (!IsClosing())
		QueueCollectionImportFailureEvent(pResult->strPath, _T("post-result"), dwPostError);
	delete pResult->pCollection;
	delete pResult;
	return false;
}


bool CemuleApp::ProcessNetworkUtilityWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_eType != WorkerTopologyItemNetworkUtility)
		return false;
	if (item.m_strStage != _T("connection-check"))
		return false;
	if (!WaitWorkerTopologyItemDueTime(WorkerTopologyNetworkUtility, item.m_dwDueTick))
		return false;
	if (IsNetworkActivityBlockedByBind())
		return false;
	if (ConChecker == NULL)
		return false;

	const uint8 uConnectionState = ConChecker->RunWorkerCheck(item.m_lWorkerGeneration, item.m_strPayload);
	if (uConnectionState == CONSTATE_NULL)
		return false;

	ConChecker->ApplyWorkerResult(item.m_lWorkerGeneration, uConnectionState);
	return true;
}

bool CemuleApp::ProcessStartupLoadWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_eType != WorkerTopologyItemStartupMetadataLoad)
		return false;

	if (ProcessStartupDownloadsLoadWorkerItem(item))
		return true;
	if (ProcessStartupKnownFilesLoadWorkerItem(item))
		return true;
	if (ProcessStartupClientHistoryLoadWorkerItem(item))
		return true;
	if (ProcessStartupStoredSearchesLoadWorkerItem(item))
		return true;
	if (ProcessStartupKnown2IndexLoadWorkerItem(item))
		return true;
	if (ProcessStartupSharedCacheLoadWorkerItem(item))
		return true;

	AddDebugLogLine(DLP_LOW, false, _T("Startup load worker item dropped. stage=%s role=%s type=%u\n"), (LPCTSTR)item.m_strStage, GetWorkerTopologyRoleName(item.m_eRole), static_cast<UINT>(item.m_eType));
	return false;
}

bool CemuleApp::ProcessPersistenceWorkerItem(const SWorkerTopologyItem &item)
{
	if (item.m_eType == WorkerTopologyItemFileSystemReload && item.m_strStage == _T("shared-files-filesystem-reload")) {
		const bool bProcessed = CSharedFilesCtrl::ProcessFileSystemReloadWorkerItem(item);
		if (!bProcessed)
			CompleteSharedFilesFileSystemReload(item.m_hNotifyWnd, item.m_lWorkerGeneration, item.m_uCorrelationId);
		return bProcessed;
	}

	if (item.m_eType != WorkerTopologyItemPersistenceSave)
		return false;

	if (item.m_strStage == _T("persistence-command")) {
		if (!WaitWorkerTopologyItemDueTime(WorkerTopologyPersistence, item.m_dwDueTick))
			return false;

		SBackendCommand command;
		bool bProcessed = false;
		while (PopPersistenceBackendCommand(command)) {
			if (!QueuePersistenceCommandOwnerEvent(command))
				QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, _T("persistence-owner-event-queue"), ERROR_INVALID_HANDLE, command.m_uSequence, command.m_uCorrelationId);
			bProcessed = true;
		}
		return bProcessed;
	}

	if (item.m_strStage == _T("backup")) {
		if (!WaitWorkerTopologyItemDueTime(WorkerTopologyPersistence, item.m_dwDueTick)) {
			::InterlockedExchange(&m_lBackupWorkerActive, 0);
			return false;
		}
		try {
			QueueLogLine(true, GetResString(_T("BACKUP_STARTED")));
			BackupMain();
		} catch (CException *ex) {
			if (ex != NULL)
				ex->Delete();
			AddDebugLogLine(DLP_HIGH, false, _T("Persistence worker backup failed with MFC exception.\n"));
		} catch (...) {
			AddDebugLogLine(DLP_HIGH, false, _T("Persistence worker backup failed with unknown exception.\n"));
		}
		::InterlockedExchange(&m_lBackupWorkerActive, 0);
		return true;
	}

	if (item.m_strStage == _T("downloads-overview-export")) {
		if (!WaitWorkerTopologyItemDueTime(WorkerTopologyPersistence, item.m_dwDueTick))
			return false;
		return downloadqueue != NULL && downloadqueue->ProcessQueuedPartMetFilesOverviewExport();
	}

	AddDebugLogLine(DLP_LOW, false, _T("Persistence worker item dropped. stage=%s type=%u\n"), (LPCTSTR)item.m_strStage, static_cast<UINT>(item.m_eType));
	return false;
}

bool CemuleApp::UseBackendCommandDispatcher() const
{
	return true;
}

bool CemuleApp::UseAsyncBackendCommandExecution() const
{
	return UseBackendCommandDispatcher() && !IsBackendLifecycleStopping() && !IsClosing() && m_pBackendCommandThread != NULL && m_dwBackendCommandThreadId != 0;
}

bool CemuleApp::UseAsyncBackendCommandExecution(const SBackendCommand &command) const
{
	return UseAsyncBackendCommandExecution() && IsBackendCommandEligibleForBackendOwnerThread(command);
}

bool CemuleApp::CanStartAsyncBackendOwnerExecutor() const
{
	if (!UseBackendCommandDispatcher() || IsBackendLifecycleStopping() || IsClosing())
		return false;
	if (!IsBackendCommandReadinessRegistryComplete()) {
		if (thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_HIGH, false, _T("Backend owner async disabled by incomplete readiness registry.\n"));
		return false;
	}

	bool bHasReadyFamily = false;
	for (int i = 0; i < static_cast<int>(sizeof(g_backendCommandReadiness) / sizeof(g_backendCommandReadiness[0])); ++i) {
		const SBackendCommandReadiness &readiness = g_backendCommandReadiness[i];
		if (!IsBackendCommandReadinessConsistent(readiness)) {
			if (thePrefs.GetLogUiResponsivenessEvents())
				AddDebugLogLine(DLP_HIGH, false, _T("Backend owner async disabled by readiness contract mismatch. family=%s readiness=%s reason=%s\n"),
					GetBackendCommandFamilyName(readiness.m_eFamily), GetBackendCommandReadinessName(readiness.m_eReadiness), readiness.m_pszReason != NULL ? readiness.m_pszReason : _T(""));
			return false;
		}
		if (readiness.m_eReadiness == BackendCommandReadinessBackendOwnerReady && readiness.m_bEnableBackendOwnerDispatch)
			bHasReadyFamily = true;
	}
	return bHasReadyFamily;
}

LPCTSTR CemuleApp::GetBackendCommandSourceName(EBackendCommandSource eSource) const
{
	switch (eSource) {
		case BackendCommandSourceUnknown: return _T("unknown");
		case BackendCommandSourceUi: return _T("ui");
		case BackendCommandSourceNetworkClient: return _T("network-client");
		case BackendCommandSourceNetworkServer: return _T("network-server");
		case BackendCommandSourceNetworkUdp: return _T("network-udp");
		case BackendCommandSourceNetworkKad: return _T("network-kad");
		case BackendCommandSourceWebServer: return _T("webserver");
		case BackendCommandSourceTimer: return _T("timer");
		case BackendCommandSourcePersistence: return _T("persistence");
		case BackendCommandSourceSharedFilesOwner: return _T("shared-files-owner");
		case BackendCommandSourceDiskIo: return _T("disk-io");
		case BackendCommandSourceSearchIngest: return _T("search-ingest");
		case BackendCommandSourceDownloadModel: return _T("download-model");
		case BackendCommandSourceUploadModel: return _T("upload-model");
		case BackendCommandSourceShutdown: return _T("shutdown");
	}
	return _T("unknown");
}

LPCTSTR CemuleApp::GetBackendCommandOrderingScopeName(EBackendCommandOrderingScope eScope) const
{
	switch (eScope) {
		case BackendCommandOrderingGlobal: return _T("global");
		case BackendCommandOrderingClient: return _T("client");
		case BackendCommandOrderingSearch: return _T("search");
		case BackendCommandOrderingFileHash: return _T("file-hash");
		case BackendCommandOrderingDownloadList: return _T("download-list");
		case BackendCommandOrderingSharedFiles: return _T("shared-files");
		case BackendCommandOrderingPersistence: return _T("persistence");
		case BackendCommandOrderingUploadList: return _T("upload-list");
		case BackendCommandOrderingServer: return _T("server");
		case BackendCommandOrderingKad: return _T("kad");
		case BackendCommandOrderingWebRequest: return _T("web-request");
		case BackendCommandOrderingDiskIo: return _T("disk-io");
	}
	return _T("global");
}

LPCTSTR CemuleApp::GetBackendCommandFailureName(EBackendCommandFailureKind eFailure) const
{
	switch (eFailure) {
		case BackendCommandFailureNone: return _T("none");
		case BackendCommandFailureInvalidPayload: return _T("invalid-payload");
		case BackendCommandFailureStaleTarget: return _T("stale-target");
		case BackendCommandFailureOwnerGuard: return _T("owner-guard");
		case BackendCommandFailureShutdown: return _T("shutdown");
		case BackendCommandFailureDispatcherUnavailable: return _T("dispatcher-unavailable");
		case BackendCommandFailureApplyFailed: return _T("apply-failed");
		case BackendCommandFailureException: return _T("exception");
	}
	return _T("unknown");
}

LPCTSTR CemuleApp::GetBackendCommandFailurePolicyName(EBackendCommandFailurePolicy ePolicy) const
{
	switch (ePolicy) {
		case BackendCommandFailurePolicyUnknown: return _T("unknown");
		case BackendCommandFailurePolicyReport: return _T("report");
		case BackendCommandFailurePolicyDropStale: return _T("drop-stale");
		case BackendCommandFailurePolicyReportAndDropStale: return _T("report-and-drop-stale");
	}
	return _T("unknown");
}

LPCTSTR CemuleApp::GetBackendCommandFamilyName(EBackendCommandFamily eFamily) const
{
	const SBackendCommandContract *pContract = FindBackendCommandContract(eFamily);
	return pContract != NULL ? pContract->m_pszName : _T("unknown");
}

CemuleApp::EBackendCommandFamily CemuleApp::GetBackendCommandFamily(const SBackendCommand &command) const
{
	switch (command.m_eType) {
		case BackendCommandDownload:
			switch (command.m_downloadCommand.m_eType) {
				case DownloadCommandAddFileLinks: return BackendCommandFamilyDownloadAddFileLinks;
				case DownloadCommandProcessLinks: return BackendCommandFamilyDownloadProcessLinks;
				case DownloadCommandRemoveItems: return BackendCommandFamilyDownloadRemoveItems;
				case DownloadCommandChangeState: return IsBackendDownloadChangeStateOwnerSafeAction(command.m_downloadCommand.m_uAction) ? BackendCommandFamilyDownloadChangeStateOwnerSafe : BackendCommandFamilyDownloadChangeState;
			}
			break;
		case BackendCommandUpload:
			switch (command.m_uploadCommand.m_eType) {
				case UploadCommandClientRowsChanged:
				case UploadCommandClientRowsRemoved:
					return BackendCommandFamilyUploadClientRowsChanged;
				case UploadCommandQueueListChanged:
					return BackendCommandFamilyUploadQueueListChanged;
				case UploadCommandUploadListChanged:
					return BackendCommandFamilyUploadListChanged;
				case UploadCommandBandwidthSnapshotChanged:
					return BackendCommandFamilyUploadBandwidthSnapshot;
				case UploadCommandDiskIoResult:
					return BackendCommandFamilyUploadDiskIoResult;
			}
			break;
		case BackendCommandSearch:
			switch (command.m_searchCommand.m_eType) {
				case SearchCommandStart: return BackendCommandFamilySearchStart;
				case SearchCommandCancel: return BackendCommandFamilySearchCancel;
				case SearchCommandIngestApply: return BackendCommandFamilySearchIngestApply;
				case SearchCommandKnownTypeRefresh: return BackendCommandFamilySearchKnownTypeRefresh;
			}
			break;
		case BackendCommandCollection:
			return BackendCommandFamilyCollectionImport;
		case BackendCommandPersistence:
			switch (command.m_persistenceCommand.m_eType) {
				case PersistenceCommandSaveAppState: return BackendCommandFamilyPersistenceSaveAppState;
				case PersistenceCommandSaveStats: return BackendCommandFamilyPersistenceSaveStats;
				case PersistenceCommandSaveKnownFiles: return BackendCommandFamilyPersistenceSaveKnownFiles;
				case PersistenceCommandSavePreferences: return BackendCommandFamilyPersistenceSavePreferences;
				case PersistenceCommandSaveFriends: return BackendCommandFamilyPersistenceSaveFriends;
				case PersistenceCommandSaveClientCredits: return BackendCommandFamilyPersistenceSaveClientCredits;
				case PersistenceCommandSaveServerList: return BackendCommandFamilyPersistenceSaveServerList;
				case PersistenceCommandSaveClientHistory: return BackendCommandFamilyPersistenceSaveClientHistory;
				case PersistenceCommandSaveSearchStore: return BackendCommandFamilyPersistenceSaveSearchStore;
				case PersistenceCommandSaveSearchSpam: return BackendCommandFamilyPersistenceSaveSearchSpam;
				case PersistenceCommandSaveSharedFiles: return BackendCommandFamilyPersistenceSaveSharedFiles;
				case PersistenceCommandSaveKadNodes: return BackendCommandFamilyPersistenceSaveKadNodes;
			}
			break;
		case BackendCommandSharedFiles:
			switch (command.m_sharedFilesCommand.m_eType) {
				case SharedFilesCommandMenuAction: return BackendCommandFamilySharedFilesMenuAction;
				case SharedFilesCommandSelectionAction: return BackendCommandFamilySharedFilesSelectionAction;
				case SharedFilesCommandReload: return BackendCommandFamilySharedFilesReload;
				case SharedFilesCommandBulkDelete: return BackendCommandFamilySharedFilesBulkDelete;
				case SharedFilesCommandBulkCancelDownloads: return BackendCommandFamilySharedFilesBulkCancelDownloads;
				case SharedFilesCommandBulkUnshare: return BackendCommandFamilySharedFilesBulkUnshare;
				case SharedFilesCommandBulkHistoryRemove: return BackendCommandFamilySharedFilesBulkHistoryRemove;
				case SharedFilesCommandBulkMetadataUpdate: return BackendCommandFamilySharedFilesBulkMetadataUpdate;
				case SharedFilesCommandBulkPriority: return BackendCommandFamilySharedFilesBulkPriority;
				case SharedFilesCommandCreateCollection: return BackendCommandFamilySharedFilesCreateCollection;
				case SharedFilesCommandToggleShareStatus: return BackendCommandFamilySharedFilesToggleShareStatus;
			}
			break;
		case BackendCommandNetworkPacket:
			switch (command.m_networkPacketCommand.m_eType) {
				case NetworkPacketCommandClientSearchAnswer: return BackendCommandFamilyNetworkClientSearchAnswer;
				case NetworkPacketCommandServerSearchAnswer: return BackendCommandFamilyNetworkServerSearchAnswer;
				case NetworkPacketCommandDownloadFileStatus: return BackendCommandFamilyNetworkDownloadFileStatus;
				case NetworkPacketCommandDownloadHashSet: return BackendCommandFamilyNetworkDownloadHashSet;
				case NetworkPacketCommandDownloadFoundSources: return BackendCommandFamilyNetworkDownloadFoundSources;
				case NetworkPacketCommandDownloadSourceExchange: return BackendCommandFamilyNetworkDownloadSourceExchange;
				case NetworkPacketCommandDownloadBlockReceive: return BackendCommandFamilyDownloadBlockReceive;
				case NetworkPacketCommandServerUdpSearchAnswer: return BackendCommandFamilyNetworkServerUdpSearchAnswer;
				case NetworkPacketCommandKadPacket: return BackendCommandFamilyNetworkKadPacket;
			}
			break;
	}
	return BackendCommandFamilyUnknown;
}

const CemuleApp::SBackendCommandContract* CemuleApp::GetBackendCommandContract(const SBackendCommand &command) const
{
	return FindBackendCommandContract(command.m_eFamily != BackendCommandFamilyUnknown ? command.m_eFamily : GetBackendCommandFamily(command));
}

LPCTSTR CemuleApp::GetBackendExecutorDomainName(EBackendExecutorDomain eExecutor) const
{
	switch (eExecutor) {
		case BackendExecutorUi: return _T("ui");
		case BackendExecutorNetworkParser: return _T("network-parser");
		case BackendExecutorBackendOwner: return _T("backend-owner");
		case BackendExecutorDiskIo: return _T("disk-io");
	}
	return _T("unknown");
}

LPCTSTR CemuleApp::GetBackendCommandApplyModeName(EBackendCommandApplyMode eMode) const
{
	switch (eMode) {
		case BackendCommandApplyUiCompatibilityOnly: return _T("ui-compatibility-only");
		case BackendCommandApplyBackendOwnerSafe: return _T("backend-owner-safe");
	}
	return _T("unknown");
}

LPCTSTR CemuleApp::GetBackendCommandReadinessName(EBackendCommandReadiness eReadiness) const
{
	switch (eReadiness) {
		case BackendCommandReadinessBlocked: return _T("blocked");
		case BackendCommandReadinessUiCompatibilityOnly: return _T("ui-compatibility-only");
		case BackendCommandReadinessBackendOwnerReady: return _T("backend-owner-ready");
	}
	return _T("unknown");
}

bool CemuleApp::IsBackendCommandFamilyReadyForBackendOwnerThread(EBackendCommandFamily eFamily) const
{
	const SBackendCommandReadiness *pReadiness = FindBackendCommandReadiness(eFamily);
	return pReadiness != NULL && IsBackendCommandReadinessConsistent(*pReadiness) && pReadiness->m_eReadiness == BackendCommandReadinessBackendOwnerReady && pReadiness->m_bEnableBackendOwnerDispatch;
}

LPCTSTR CemuleApp::GetBackendCommandReadinessReason(EBackendCommandFamily eFamily) const
{
	const SBackendCommandReadiness *pReadiness = FindBackendCommandReadiness(eFamily);
	return pReadiness != NULL && pReadiness->m_pszReason != NULL ? pReadiness->m_pszReason : _T("missing-readiness");
}

CemuleApp::EApplicationEventDispatchDomain CemuleApp::GetApplicationEventDispatchDomain(EApplicationEventType eType) const
{
	switch (eType) {
		case ApplicationEventCommandFailed:
		case ApplicationEventCollectionImportFailed:
		case ApplicationEventAsyncDiskWriteResult:
		case ApplicationEventUploadDiskIoResult:
		case ApplicationEventPersistenceProgress:
		case ApplicationEventPersistenceCompleted:
		case ApplicationEventPersistenceFailed:
		case ApplicationEventStartupMetadataStateChanged:
		case ApplicationEventDownloadBatchProgress:
		case ApplicationEventDownloadBatchCompleted:
		case ApplicationEventDownloadStateProgress:
		case ApplicationEventDownloadStateCompleted:
		case ApplicationEventDownloadRemoveProgress:
		case ApplicationEventDownloadRemoveDiskCleanupCompleted:
		case ApplicationEventDownloadRemoveCompleted:
		case ApplicationEventSearchPacketParseProgress:
		case ApplicationEventSearchPacketParseCompleted:
		case ApplicationEventSearchPacketParseFailed:
		case ApplicationEventSharedFilesCommandProgress:
		case ApplicationEventSharedFilesCommandCompleted:
		case ApplicationEventSharedFilesCommandFailed:
		case ApplicationEventSharedFilesCommandItemFailed:
		case ApplicationEventDownloadStateItemFailed:
		case ApplicationEventDownloadRemoveItemFailed:
			return ApplicationEventDispatchBackendResult;
		case ApplicationEventPartFileOwnerStateChanged:
		case ApplicationEventPartFileDiskWriteRequested:
		case ApplicationEventPartFileOwnerFailed:
			return ApplicationEventDispatchBackendResult;
		case ApplicationEventUploadClientRowsChanged:
		case ApplicationEventUploadClientRowsRemoved:
		case ApplicationEventClientRowUpdateRequested:
		case ApplicationEventDownloadListRowsRemoved:
		case ApplicationEventDownloadListDeletedCompletedRowsRemoved:
		case ApplicationEventDownloadListChanged:
		case ApplicationEventBulkOperationOverlayRefresh:
		case ApplicationEventSharedFilesListChanged:
		case ApplicationEventUploadQueueListChanged:
		case ApplicationEventUploadListChanged:
		case ApplicationEventUploadBandwidthSnapshotChanged:
		case ApplicationEventSearchResultsChanged:
		case ApplicationEventLocalEd2kSearchEnd:
		case ApplicationEventClientChatMessage:
		case ApplicationEventClientChatCloseRequested:
		case ApplicationEventClientCaptchaRequested:
		case ApplicationEventClientCaptchaResult:
		case ApplicationEventClientChatConnectingResult:
		case ApplicationEventClientChatConnectionProgress:
		case ApplicationEventKadConnectionStateChanged:
		case ApplicationEventKadUiStatusRefresh:
		case ApplicationEventKadSearchCancelUiRequested:
			return ApplicationEventDispatchUiNotification;
		case ApplicationEventSearchStartRequested:
		case ApplicationEventSearchCancelRequested:
		case ApplicationEventCollectionImportRequested:
		case ApplicationEventPersistenceRequested:
		case ApplicationEventPersistenceWorkRequested:
		case ApplicationEventSharedFilesCommandRequested:
		case ApplicationEventDownloadProcessLinkRequested:
		case ApplicationEventDownloadRemoveRequested:
		case ApplicationEventDownloadStateRequested:
			return ApplicationEventDispatchUiCommandBridge;
	}
	return ApplicationEventDispatchTelemetry;
}

LPCTSTR CemuleApp::GetApplicationEventDispatchDomainName(EApplicationEventDispatchDomain eDomain) const
{
	switch (eDomain) {
		case ApplicationEventDispatchTelemetry: return _T("telemetry");
		case ApplicationEventDispatchUiNotification: return _T("ui-notification");
		case ApplicationEventDispatchUiCommandBridge: return _T("ui-command-bridge");
		case ApplicationEventDispatchBackendResult: return _T("backend-result");
	}
	return _T("unknown");
}

bool CemuleApp::IsApplicationEventDispatchAllowed(const SApplicationEvent &) const
{
	return IsUiThread();
}

CemuleApp::EBackendExecutorDomain CemuleApp::GetBackendCommandExecutorDomain(EBackendCommandType eType) const
{
	switch (eType) {
		case BackendCommandPersistence:
			return BackendExecutorDiskIo;
		case BackendCommandDownload:
		case BackendCommandUpload:
		case BackendCommandSearch:
		case BackendCommandCollection:
		case BackendCommandSharedFiles:
		case BackendCommandNetworkPacket:
			return BackendExecutorBackendOwner;
	}
	return BackendExecutorBackendOwner;
}

bool CemuleApp::IsBackendCommandApplyThreadAllowed() const
{
	if (UseAsyncBackendCommandExecution() || HasBackendCommandThreadSignalTarget())
		return IsUiThread() || (m_dwBackendCommandThreadId != 0 && ::GetCurrentThreadId() == m_dwBackendCommandThreadId);
	return IsUiThread();
}

bool CemuleApp::IsBackendCommandEligibleForBackendOwnerThread(const SBackendCommand &command) const
{
	const SBackendCommandContract *pContract = GetBackendCommandContract(command);
	const SBackendCommandReadiness *pReadiness = FindBackendCommandReadiness(command.m_eFamily != BackendCommandFamilyUnknown ? command.m_eFamily : GetBackendCommandFamily(command));
	if (pContract == NULL || pReadiness == NULL)
		return false;
	if (pContract->m_eExecutorDomain != BackendExecutorBackendOwner || pContract->m_eApplyMode != BackendCommandApplyBackendOwnerSafe)
		return false;
	if (pReadiness->m_eFamily != pContract->m_eFamily || !IsBackendCommandReadinessConsistent(*pReadiness))
		return false;
	return pReadiness->m_eReadiness == BackendCommandReadinessBackendOwnerReady && pReadiness->m_bEnableBackendOwnerDispatch;
}

void CemuleApp::PrepareBackendCommandEnvelope(SBackendCommand &command, EBackendCommandSource eSource, EBackendCommandOrderingScope eScope, LPCTSTR pszOrderingKey)
{
	command.m_eSource = eSource;
	command.m_eOrderingScope = eScope;
	command.m_strOrderingKey = pszOrderingKey != NULL ? pszOrderingKey : _T("");
}

CString CemuleApp::BuildBackendCommandOrderingKey(const SBackendCommand &command) const
{
	CString strKey;
	switch (command.m_eType) {
		case BackendCommandDownload:
			strKey = _T("download-list");
			break;
		case BackendCommandUpload:
			strKey.Format(_T("client:%lu"), command.m_uploadCommand.m_uRuntimeID);
			break;
		case BackendCommandSearch:
			strKey.Format(_T("search:%u"), command.m_searchCommand.m_uSearchID);
			break;
		case BackendCommandCollection:
			strKey = command.m_collectionCommand.m_strFilePath;
			break;
		case BackendCommandPersistence:
			strKey.Format(_T("persistence:%u"), static_cast<UINT>(command.m_persistenceCommand.m_eType));
			break;
		case BackendCommandSharedFiles:
			strKey = _T("shared-files");
			break;
		case BackendCommandNetworkPacket:
			strKey = BuildNetworkPacketOrderingKey(command.m_networkPacketCommand);
			break;
	}
	if (strKey.IsEmpty())
		strKey = _T("global");
	return strKey;
}

void CemuleApp::EnsureBackendCommandEnvelope(SBackendCommand &command) const
{
	if (command.m_dwCreatedTick == 0)
		command.m_dwCreatedTick = ::GetTickCount();

	if (command.m_eType == BackendCommandDownload && (command.m_downloadCommand.m_eType == DownloadCommandRemoveItems || command.m_downloadCommand.m_eType == DownloadCommandChangeState))
		CanonicalizeDownloadItemHashes(command.m_downloadCommand.m_astrItemHashes);

	command.m_eFamily = GetBackendCommandFamily(command);
	const SBackendCommandContract *pContract = FindBackendCommandContract(command.m_eFamily);

	if (command.m_eSource == BackendCommandSourceUnknown) {
		if (command.m_eType == BackendCommandNetworkPacket) {
			switch (command.m_networkPacketCommand.m_eType) {
				case NetworkPacketCommandClientSearchAnswer:
				case NetworkPacketCommandDownloadHashSet:
				case NetworkPacketCommandDownloadSourceExchange:
				case NetworkPacketCommandDownloadBlockReceive:
					command.m_eSource = BackendCommandSourceNetworkClient;
					break;
				case NetworkPacketCommandDownloadFileStatus:
					command.m_eSource = command.m_networkPacketCommand.m_bUdpPacket ? BackendCommandSourceNetworkUdp : BackendCommandSourceNetworkClient;
					break;
				case NetworkPacketCommandServerSearchAnswer:
				case NetworkPacketCommandDownloadFoundSources:
					command.m_eSource = BackendCommandSourceNetworkServer;
					break;
				case NetworkPacketCommandServerUdpSearchAnswer:
					command.m_eSource = BackendCommandSourceNetworkUdp;
					break;
				case NetworkPacketCommandKadPacket:
					command.m_eSource = BackendCommandSourceNetworkKad;
					break;
			}
		} else if (command.m_eType == BackendCommandPersistence)
			command.m_eSource = BackendCommandSourcePersistence;
		else if (pContract != NULL)
			command.m_eSource = pContract->m_eDefaultSource;
		else
			command.m_eSource = BackendCommandSourceUi;
	}

	if (command.m_eOrderingScope == BackendCommandOrderingGlobal) {
		switch (command.m_eType) {
			case BackendCommandDownload:
				command.m_eOrderingScope = BackendCommandOrderingDownloadList;
				break;
			case BackendCommandUpload:
				command.m_eOrderingScope = BackendCommandOrderingClient;
				break;
			case BackendCommandSearch:
				command.m_eOrderingScope = BackendCommandOrderingSearch;
				break;
			case BackendCommandPersistence:
				command.m_eOrderingScope = BackendCommandOrderingPersistence;
				break;
			case BackendCommandSharedFiles:
				command.m_eOrderingScope = BackendCommandOrderingSharedFiles;
				break;
			case BackendCommandNetworkPacket:
				command.m_eOrderingScope = GetNetworkPacketOrderingScope(command.m_networkPacketCommand);
				break;
			case BackendCommandCollection:
				if (pContract != NULL)
					command.m_eOrderingScope = pContract->m_eDefaultOrderingScope;
				break;
		}
	}

	if (command.m_strOrderingKey.IsEmpty())
		command.m_strOrderingKey = BuildBackendCommandOrderingKey(command);

	if (command.m_lGenerationGuard == 0) {
		const LONG lGenerationGuard = static_cast<LONG>(GetBackendLifecycleState()) + 1;
		command.m_lGenerationGuard = lGenerationGuard != 0 ? lGenerationGuard : 1;
	}

	if (pContract != NULL) {
		if (command.m_eFailurePolicy == BackendCommandFailurePolicyUnknown)
			command.m_eFailurePolicy = pContract->m_eFailurePolicy;
		command.m_bCancelable = pContract->m_bCancelable;
		command.m_bDropIfStale = pContract->m_bDropIfStale;
	} else if (command.m_eFailurePolicy == BackendCommandFailurePolicyUnknown)
		command.m_eFailurePolicy = BackendCommandFailurePolicyReport;
}

void CemuleApp::AssignBackendCommandCancellationToken(SBackendCommand &command)
{
	if (command.m_uCancellationToken != 0)
		return;

	CSingleLock lock(&m_backendCommandQueueLock, TRUE);
	command.m_uCancellationToken = ++m_uNextBackendCommandCancellationToken;
	if (command.m_uCancellationToken == 0)
		command.m_uCancellationToken = ++m_uNextBackendCommandCancellationToken;
}

bool CemuleApp::ValidateBackendCommandContract(const SBackendCommand &command, CString *pstrStage) const
{
	const SBackendCommandContract *pContract = FindBackendCommandContract(command.m_eFamily != BackendCommandFamilyUnknown ? command.m_eFamily : GetBackendCommandFamily(command));
	if (pContract == NULL) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-family");
		return false;
	}

	if (command.m_eType != pContract->m_eType) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-type");
		return false;
	}

	if (command.m_eSource == BackendCommandSourceUnknown || (pContract->m_dwAllowedSourceMask & BACKEND_COMMAND_SOURCE_MASK(command.m_eSource)) == 0) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-source");
		return false;
	}

	if (command.m_eOrderingScope < BackendCommandOrderingGlobal || command.m_eOrderingScope > BackendCommandOrderingDiskIo || (pContract->m_dwAllowedScopeMask & BACKEND_COMMAND_SCOPE_MASK(command.m_eOrderingScope)) == 0) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-scope");
		return false;
	}

	if (command.m_strOrderingKey.IsEmpty()) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-ordering-key");
		return false;
	}

	if (command.m_lGenerationGuard == 0) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-generation");
		return false;
	}

	if (command.m_uCancellationToken == 0) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-cancellation-token");
		return false;
	}

	if (command.m_eFailurePolicy == BackendCommandFailurePolicyUnknown) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-failure-policy");
		return false;
	}

	if (pContract->m_eExecutorDomain < BackendExecutorUi || pContract->m_eExecutorDomain > BackendExecutorDiskIo) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-executor");
		return false;
	}

	if (pContract->m_eApplyMode != BackendCommandApplyUiCompatibilityOnly && pContract->m_eApplyMode != BackendCommandApplyBackendOwnerSafe) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-contract-apply-mode");
		return false;
	}

	if (command.m_eType == BackendCommandNetworkPacket && IsDownloadNetworkPacketCommand(command.m_networkPacketCommand.m_eType)) {
		if (command.m_eOrderingScope != BackendCommandOrderingFileHash || IsZeroHash(command.m_networkPacketCommand.m_abyFileHash) || command.m_strOrderingKey.Left(5) != _T("file:")) {
			if (pstrStage != NULL)
				*pstrStage = _T("backend-contract-download-file-hash");
			return false;
		}
	}

	return true;
}

bool CemuleApp::ValidateBackendCommandEnvelope(const SBackendCommand &command, CString *pstrStage) const
{
	if (!ValidateBackendCommandContract(command, pstrStage))
		return false;

	if (command.m_eSource == BackendCommandSourceUnknown) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-envelope-source");
		return false;
	}

	if (command.m_strOrderingKey.IsEmpty()) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-envelope-ordering-key");
		return false;
	}

	if (command.m_eOrderingScope < BackendCommandOrderingGlobal || command.m_eOrderingScope > BackendCommandOrderingDiskIo) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-envelope-ordering-scope");
		return false;
	}

	switch (command.m_eType) {
		case BackendCommandDownload:
		{
			const bool bEmptyDownloadStateCommand = command.m_downloadCommand.m_eType == DownloadCommandChangeState
				&& (command.m_downloadCommand.m_uAction == static_cast<UINT>(BackendDownloadStateClearCompleted)
					|| command.m_downloadCommand.m_uAction == static_cast<UINT>(BackendDownloadStateSetCategoryPriority));
			if ((command.m_downloadCommand.m_eType == DownloadCommandRemoveItems || command.m_downloadCommand.m_eType == DownloadCommandChangeState) && command.m_downloadCommand.m_astrItemHashes.GetSize() == 0 && !bEmptyDownloadStateCommand) {
				if (pstrStage != NULL)
					*pstrStage = _T("backend-envelope-download-items");
				return false;
			}
			if ((command.m_downloadCommand.m_eType == DownloadCommandAddFileLinks || command.m_downloadCommand.m_eType == DownloadCommandProcessLinks) && IsDownloadCommandLinkPayloadEmpty(command.m_downloadCommand)) {
				if (pstrStage != NULL)
					*pstrStage = _T("backend-envelope-download-links");
				return false;
			}
			break;
		}
		case BackendCommandSearch:
			if (command.m_searchCommand.m_eType == SearchCommandCancel && command.m_searchCommand.m_uSearchID == 0) {
				if (pstrStage != NULL)
					*pstrStage = _T("backend-envelope-search-id");
				return false;
			}
			break;
		case BackendCommandCollection:
			if (command.m_collectionCommand.m_strFilePath.IsEmpty()) {
				if (pstrStage != NULL)
					*pstrStage = _T("backend-envelope-collection-path");
				return false;
			}
			break;
		case BackendCommandNetworkPacket:
			return ValidateNetworkPacketCommandSnapshot(command.m_networkPacketCommand, pstrStage);
		case BackendCommandUpload:
			if ((command.m_uploadCommand.m_eType == UploadCommandClientRowsChanged || command.m_uploadCommand.m_eType == UploadCommandClientRowsRemoved) && (command.m_uploadCommand.m_uRuntimeID == 0 || command.m_uploadCommand.m_lRuntimeGeneration == 0 || command.m_uploadCommand.m_uTargetFlags == 0)) {
				if (pstrStage != NULL)
					*pstrStage = _T("backend-envelope-upload-target");
				return false;
			}
			break;
		case BackendCommandPersistence:
			break;
		case BackendCommandSharedFiles:
			if (!IsSharedFilesCommandTypeValid(command.m_sharedFilesCommand.m_eType) || command.m_sharedFilesCommand.m_uAction == 0) {
				if (pstrStage != NULL)
					*pstrStage = _T("backend-envelope-shared-files");
				return false;
			}
			break;
		default:
			if (pstrStage != NULL)
				*pstrStage = _T("backend-envelope-command-type");
			return false;
	}

	return true;
}

bool CemuleApp::ShouldAcceptBackendCommand(const SBackendCommand &command) const
{
	if (!UseBackendCommandDispatcher())
		return false;
	if (!IsBackendLifecycleStopping() && !IsClosing())
		return true;
	if (IsClosing())
		return false;

	AddDebugLogLine(DLP_LOW, false, _T("Backend command rejected during shutdown. lifecycle=%s command=%u source=%s scope=%s key=%s sequence=%I64u correlation=%I64u\n"),
		GetBackendLifecycleStateName(GetBackendLifecycleState()), static_cast<UINT>(command.m_eType), GetBackendCommandSourceName(command.m_eSource),
		GetBackendCommandOrderingScopeName(command.m_eOrderingScope), (LPCTSTR)command.m_strOrderingKey, command.m_uSequence, command.m_uCorrelationId);
	return false;
}

bool CemuleApp::ShouldAcceptApplicationEvent(const SApplicationEvent &event) const
{
	if (!IsBackendLifecycleStopping() && !IsClosing())
		return true;
	if (IsClosing())
		return false;

	AddDebugLogLine(DLP_LOW, false, _T("Application event rejected during shutdown. lifecycle=%s event=%u command=%u source=%s scope=%s key=%s sequence=%I64u correlation=%I64u\n"),
		GetBackendLifecycleStateName(GetBackendLifecycleState()), static_cast<UINT>(event.m_eType), static_cast<UINT>(event.m_eBackendCommandType),
		GetBackendCommandSourceName(event.m_eBackendCommandSource), GetBackendCommandOrderingScopeName(event.m_eBackendCommandOrderingScope),
		(LPCTSTR)event.m_strBackendCommandOrderingKey, event.m_uSequence, event.m_uCorrelationId);
	return false;
}

bool CemuleApp::IsBackendCommandDrainingForShutdown() const
{
	return GetBackendLifecycleState() == BackendLifecycleDrainingBackendOwner;
}

bool CemuleApp::QueuePersistenceCommandOwnerEvent(const SBackendCommand &command)
{
	if (command.m_eType != BackendCommandPersistence)
		return false;

	SApplicationEvent event;
	event.m_eType = command.m_persistenceCommand.m_bWorkRequest ? ApplicationEventPersistenceWorkRequested : ApplicationEventPersistenceRequested;
	CopyBackendCommandEnvelopeToEvent(command, event);
	event.m_ePersistenceCommandType = command.m_persistenceCommand.m_eType;
	event.m_bAutoSave = command.m_persistenceCommand.m_bAutoSave;
	event.m_strMessage = command.m_persistenceCommand.m_strReason;
	return QueueApplicationEvent(event);
}

bool CemuleApp::QueuePersistenceBackendCommand(const SBackendCommand &command, DWORD dwDelayMs)
{
	if (command.m_eType != BackendCommandPersistence || IsClosing())
		return false;

	if (GetWorkerTopologyState(WorkerTopologyPersistence) == WorkerTopologyStopped && !StartPersistenceWorker())
		return false;

	SBackendCommand *pQueuedCommand = new SBackendCommand();
	CopyBackendCommand(command, *pQueuedCommand);
	const uint64 uQueuedSequence = pQueuedCommand->m_uSequence;
	const uint64 uQueuedCorrelationId = pQueuedCommand->m_uCorrelationId;
	const EPersistenceCommandType eQueuedPersistenceType = pQueuedCommand->m_persistenceCommand.m_eType;
	bool bQueuedNewCommand = false;
	bool bCoalescedCommand = false;
	{
		CSingleLock lock(&m_persistenceCommandQueueLock, TRUE);
		for (POSITION pos = m_persistenceCommandQueue.GetHeadPosition(); pos != NULL;) {
			SBackendCommand *pExistingCommand = m_persistenceCommandQueue.GetNext(pos);
			if (pExistingCommand == NULL || pExistingCommand->m_eFamily != command.m_eFamily || pExistingCommand->m_eOrderingScope != command.m_eOrderingScope || pExistingCommand->m_strOrderingKey.Compare(command.m_strOrderingKey) != 0)
				continue;
			if (pExistingCommand->m_persistenceCommand.m_bAutoSave != command.m_persistenceCommand.m_bAutoSave || pExistingCommand->m_persistenceCommand.m_bWorkRequest != command.m_persistenceCommand.m_bWorkRequest)
				continue;
			CopyBackendCommand(*pQueuedCommand, *pExistingCommand);
			bCoalescedCommand = true;
			break;
		}

		if (bCoalescedCommand) {
			delete pQueuedCommand;
			pQueuedCommand = NULL;
		}
		else {
			static const INT_PTR s_iPersistenceCommandQueueLimit = 32;
			if (m_persistenceCommandQueue.GetCount() >= s_iPersistenceCommandQueueLimit) {
				AddDebugLogLine(DLP_HIGH, false, _T("Persistence command queue rejected command under pressure. family=%s key=%s count=%Id limit=%Id\n"), GetBackendCommandFamilyName(command.m_eFamily), (LPCTSTR)command.m_strOrderingKey, m_persistenceCommandQueue.GetCount(), s_iPersistenceCommandQueueLimit);
				delete pQueuedCommand;
				return false;
			}
			m_persistenceCommandQueue.AddTail(pQueuedCommand);
			pQueuedCommand = NULL;
			bQueuedNewCommand = true;
		}
	}

	SWorkerTopologyItem item;
	item.m_eRole = WorkerTopologyPersistence;
	item.m_eType = WorkerTopologyItemPersistenceSave;
	item.m_dwCreatedTick = ::GetTickCount();
	item.m_dwDueTick = item.m_dwCreatedTick + dwDelayMs;
	item.m_strStage = _T("persistence-command");
	item.m_strCoalesceKey = _T("persistence-command");
	if (QueuePersistenceWorkerItem(item))
		return true;

	if (bQueuedNewCommand) {
		CSingleLock lock(&m_persistenceCommandQueueLock, TRUE);
		for (POSITION pos = m_persistenceCommandQueue.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			SBackendCommand *pQueued = m_persistenceCommandQueue.GetNext(pos);
			if (pQueued != NULL && pQueued->m_uSequence == uQueuedSequence && pQueued->m_uCorrelationId == uQueuedCorrelationId && pQueued->m_persistenceCommand.m_eType == eQueuedPersistenceType) {
				m_persistenceCommandQueue.RemoveAt(posCurrent);
				delete pQueued;
				break;
			}
		}
	}

	if (bCoalescedCommand) {
		AddDebugLogLine(DLP_HIGH, false, _T("Persistence command coalesced while worker item could not be queued. family=%s key=%s\n"), GetBackendCommandFamilyName(command.m_eFamily), (LPCTSTR)command.m_strOrderingKey);
		return true;
	}
	delete pQueuedCommand;
	return false;
}
bool CemuleApp::PopPersistenceBackendCommand(SBackendCommand &command)
{
	CSingleLock lock(&m_persistenceCommandQueueLock, TRUE);
	if (m_persistenceCommandQueue.IsEmpty())
		return false;
	SBackendCommand *pCommand = m_persistenceCommandQueue.RemoveHead();
	if (pCommand == NULL)
		return false;
	CopyBackendCommand(*pCommand, command);
	delete pCommand;
	return true;
}
void CemuleApp::ClearPersistenceBackendCommandQueue()
{
	CSingleLock lock(&m_persistenceCommandQueueLock, TRUE);
	while (!m_persistenceCommandQueue.IsEmpty())
		delete m_persistenceCommandQueue.RemoveHead();
}


bool CemuleApp::HasBackendWorkForShutdown()
{
	if (HasBackendContinuationWork())
		return true;

	{
		CSingleLock lock(&m_backendCommandQueueLock, TRUE);
		if (!m_backendCommandQueue.IsEmpty())
			return true;
	}
	{
		CSingleLock lock(&m_persistenceCommandQueueLock, TRUE);
		if (!m_persistenceCommandQueue.IsEmpty())
			return true;
	}
	if (!IsWorkerTopologyRoleIdle(WorkerTopologyPersistence))
		return true;
	return false;
}

void CemuleApp::DrainBackendWorkForShutdown()
{
	const DWORD dwStart = ::GetTickCount();
	const DWORD dwTimeoutMs = 5000;
	while (HasBackendWorkForShutdown()) {
		if (UseAsyncBackendCommandExecution() && m_dwBackendCommandThreadId != 0)
			SignalBackendCommandThread();
		if (GetWorkerTopologyState(WorkerTopologyPersistence) != WorkerTopologyStopped) {
			const int iPersistenceRole = static_cast<int>(WorkerTopologyPersistence);
			CSingleLock lock(&m_workerTopologyQueueLocks[iPersistenceRole], TRUE);
			if (m_hWorkerTopologyEvents[iPersistenceRole] != NULL)
				::SetEvent(m_hWorkerTopologyEvents[iPersistenceRole]);
		}

		if (IsUiThread())
			ProcessBackendCommandsOnCurrentThread();

		if (!HasBackendWorkForShutdown())
			break;

		const DWORD dwElapsed = static_cast<DWORD>(::GetTickCount() - dwStart);
		if (dwElapsed >= dwTimeoutMs) {
			AddDebugLogLine(DLP_HIGH, false, _T("Backend shutdown drain timed out. elapsed=%lu pending=%u\n"), dwElapsed, HasBackendWorkForShutdown() ? 1U : 0U);
			break;
		}

		::Sleep(10);
	}
}


bool CemuleApp::IsBackendCommandDroppableUnderPressure(const SBackendCommand &command) const
{
	if (command.m_eFamily == BackendCommandFamilyUploadClientRowsChanged || command.m_eFamily == BackendCommandFamilyUploadQueueListChanged || command.m_eFamily == BackendCommandFamilyUploadListChanged || command.m_eFamily == BackendCommandFamilyUploadBandwidthSnapshot || command.m_eFamily == BackendCommandFamilyUploadDiskIoResult)
		return true;
	return command.m_eFamily == BackendCommandFamilyNetworkClientSearchAnswer || command.m_eFamily == BackendCommandFamilyNetworkServerSearchAnswer || command.m_eFamily == BackendCommandFamilyNetworkServerUdpSearchAnswer;
}

bool CemuleApp::CoalesceBackendCommandLocked(const SBackendCommand &command)
{
	if (command.m_eFamily == BackendCommandFamilyUnknown)
		return false;

	for (POSITION pos = m_backendCommandQueue.GetHeadPosition(); pos != NULL;) {
		SBackendCommand *pQueuedCommand = m_backendCommandQueue.GetNext(pos);
		if (pQueuedCommand == NULL || pQueuedCommand->m_eFamily != command.m_eFamily || pQueuedCommand->m_eOrderingScope != command.m_eOrderingScope || pQueuedCommand->m_strOrderingKey.Compare(command.m_strOrderingKey) != 0)
			continue;

		if (command.m_eFamily == BackendCommandFamilyUploadClientRowsChanged || command.m_eFamily == BackendCommandFamilyUploadDiskIoResult) {
			if (pQueuedCommand->m_uploadCommand.m_uRuntimeID != command.m_uploadCommand.m_uRuntimeID || pQueuedCommand->m_uploadCommand.m_lRuntimeGeneration != command.m_uploadCommand.m_lRuntimeGeneration)
				continue;
			SBackendCommand mergedCommand;
			CopyBackendCommand(command, mergedCommand);
			mergedCommand.m_uploadCommand.m_uTargetFlags |= pQueuedCommand->m_uploadCommand.m_uTargetFlags;
			CopyBackendCommand(mergedCommand, *pQueuedCommand);
			return true;
		}

		if (command.m_eFamily == BackendCommandFamilyUploadQueueListChanged
			|| command.m_eFamily == BackendCommandFamilyUploadListChanged
			|| command.m_eFamily == BackendCommandFamilyUploadBandwidthSnapshot
			|| command.m_eFamily == BackendCommandFamilySearchCancel
			|| command.m_eFamily == BackendCommandFamilySearchIngestApply
			|| command.m_eFamily == BackendCommandFamilySearchKnownTypeRefresh) {
			CopyBackendCommand(command, *pQueuedCommand);
			return true;
		}

		if (command.m_eType == BackendCommandPersistence && command.m_eFamily == pQueuedCommand->m_eFamily) {
			CopyBackendCommand(command, *pQueuedCommand);
			return true;
		}
	}
	return false;
}

bool CemuleApp::TrimBackendCommandQueueForPressureLocked(const SBackendCommand &command, INT_PTR iTargetCount)
{
	while (m_backendCommandQueue.GetCount() >= iTargetCount) {
		POSITION posRemove = NULL;
		for (POSITION pos = m_backendCommandQueue.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			SBackendCommand *pQueuedCommand = m_backendCommandQueue.GetNext(pos);
			if (pQueuedCommand != NULL && IsBackendCommandDroppableUnderPressure(*pQueuedCommand)) {
				posRemove = posCurrent;
				break;
			}
		}
		if (posRemove == NULL)
			return false;
		SBackendCommand *pDroppedCommand = m_backendCommandQueue.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Backend command queue pressure dropped queued low-priority command. family=%s source=%s scope=%s key=%s count=%Id target=%Id\n"),
			pDroppedCommand != NULL ? GetBackendCommandFamilyName(pDroppedCommand->m_eFamily) : _T("unknown"), pDroppedCommand != NULL ? GetBackendCommandSourceName(pDroppedCommand->m_eSource) : _T("unknown"),
			pDroppedCommand != NULL ? GetBackendCommandOrderingScopeName(pDroppedCommand->m_eOrderingScope) : _T("unknown"), pDroppedCommand != NULL ? (LPCTSTR)pDroppedCommand->m_strOrderingKey : (LPCTSTR)_T(""), m_backendCommandQueue.GetCount(), iTargetCount);
		m_backendCommandQueue.RemoveAt(posRemove);
		delete pDroppedCommand;
	}
	return true;
}

bool CemuleApp::IsApplicationEventDroppableUnderPressure(const SApplicationEvent &event) const
{
	return event.m_eType == ApplicationEventSearchResultsChanged || event.m_eType == ApplicationEventSearchPacketParseProgress || event.m_eType == ApplicationEventDownloadBatchProgress ||
		event.m_eType == ApplicationEventDownloadRemoveProgress || event.m_eType == ApplicationEventDownloadStateProgress || event.m_eType == ApplicationEventSharedFilesCommandProgress ||
		event.m_eType == ApplicationEventDownloadListChanged || event.m_eType == ApplicationEventSharedFilesListChanged || event.m_eType == ApplicationEventUploadQueueListChanged || event.m_eType == ApplicationEventUploadListChanged ||
		event.m_eType == ApplicationEventUploadBandwidthSnapshotChanged || event.m_eType == ApplicationEventUploadClientRowsChanged || event.m_eType == ApplicationEventClientRowUpdateRequested ||
		event.m_eType == ApplicationEventKadUiStatusRefresh;
}

bool CemuleApp::TrimApplicationEventQueueForPressureLocked(const SApplicationEvent &event, INT_PTR iTargetCount)
{
	while (m_applicationEventQueue.GetCount() >= iTargetCount) {
		POSITION posRemove = NULL;
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			const SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (IsApplicationEventDroppableUnderPressure(queuedEvent)) {
				posRemove = posCurrent;
				break;
			}
		}
		if (posRemove == NULL)
			return false;
		const SApplicationEvent &droppedEvent = m_applicationEventQueue.GetAt(posRemove);
		AddDebugLogLine(DLP_HIGH, false, _T("Application event queue pressure dropped queued low-priority event. queued=%u incoming=%u count=%Id target=%Id\n"), static_cast<UINT>(droppedEvent.m_eType), static_cast<UINT>(event.m_eType), m_applicationEventQueue.GetCount(), iTargetCount);
		if (droppedEvent.m_eType == ApplicationEventClientCaptchaRequested && droppedEvent.m_hClientBitmap != NULL)
			::DeleteObject(droppedEvent.m_hClientBitmap);
		m_applicationEventQueue.RemoveAt(posRemove);
	}
	return true;
}
bool CemuleApp::StartBackendCommandThread()
{
	if (!CanStartAsyncBackendOwnerExecutor())
		return true;
	if (IsBackendLifecycleStopping() || IsClosing())
		return false;
	if (m_pBackendCommandThread != NULL)
		return true;

	m_hBackendCommandEvent = ::CreateEvent(NULL, FALSE, FALSE, NULL);
	m_hBackendCommandStopEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);
	if (m_hBackendCommandEvent == NULL || m_hBackendCommandStopEvent == NULL) {
		AddDebugLogLine(DLP_HIGH, false, _T("Backend command thread events could not be created. error=%lu\n"), ::GetLastError());
		StopBackendCommandThread();
		return false;
	}

	m_pBackendCommandThread = AfxBeginThread(BackendCommandThreadProc, this, THREAD_PRIORITY_NORMAL, 0, CREATE_SUSPENDED, NULL);
	if (m_pBackendCommandThread == NULL) {
		AddDebugLogLine(DLP_HIGH, false, _T("Backend command thread could not be created. error=%lu\n"), ::GetLastError());
		StopBackendCommandThread();
		return false;
	}
	m_pBackendCommandThread->m_bAutoDelete = FALSE;
	m_pBackendCommandThread->ResumeThread();
	return true;
}

void CemuleApp::StopBackendCommandThread()
{
	if (m_hBackendCommandStopEvent != NULL)
		::SetEvent(m_hBackendCommandStopEvent);
	if (m_hBackendCommandEvent != NULL)
		::SetEvent(m_hBackendCommandEvent);

	if (m_pBackendCommandThread != NULL) {
		HANDLE hThread = m_pBackendCommandThread->m_hThread;
		DWORD dwWait = (hThread != NULL) ? ::WaitForSingleObject(hThread, 5000) : WAIT_OBJECT_0;
		if (dwWait != WAIT_OBJECT_0) {
			INT_PTR iPendingCommands = 0;
			{
				CSingleLock lock(&m_backendCommandQueueLock, TRUE);
				iPendingCommands = m_backendCommandQueue.GetCount();
			}
			AddDebugLogLine(DLP_HIGH, false, _T("Backend command thread did not stop within timeout. wait=%lu pending=%Id\n"), dwWait, iPendingCommands);
			// Do not continue teardown while the backend command worker can still access model state.
			for (;;) {
				if (m_hBackendCommandStopEvent != NULL)
					::SetEvent(m_hBackendCommandStopEvent);
				if (m_hBackendCommandEvent != NULL)
					::SetEvent(m_hBackendCommandEvent);
				dwWait = (hThread != NULL) ? ::WaitForSingleObject(hThread, 1000) : WAIT_OBJECT_0;
				if (dwWait == WAIT_OBJECT_0)
					break;
				INT_PTR iPendingCommandsLoop = 0;
				{
					CSingleLock lock(&m_backendCommandQueueLock, TRUE);
					iPendingCommandsLoop = m_backendCommandQueue.GetCount();
				}
				AddDebugLogLine(DLP_VERYLOW, false, _T("Waiting for backend command thread to stop before teardown. wait=%lu pending=%Id\n"), dwWait, iPendingCommandsLoop);
			}
		}
		delete m_pBackendCommandThread;
		m_pBackendCommandThread = NULL;
	}

	if (m_hBackendCommandEvent != NULL) {
		::CloseHandle(m_hBackendCommandEvent);
		m_hBackendCommandEvent = NULL;
	}
	if (m_hBackendCommandStopEvent != NULL) {
		::CloseHandle(m_hBackendCommandStopEvent);
		m_hBackendCommandStopEvent = NULL;
	}
	m_dwBackendCommandThreadId = 0;
}

bool CemuleApp::SignalBackendCommandThread()
{
	return m_hBackendCommandEvent != NULL && ::SetEvent(m_hBackendCommandEvent) != FALSE;
}

UINT AFX_CDECL CemuleApp::BackendCommandThreadProc(LPVOID pParam)
{
	CemuleApp *pApp = reinterpret_cast<CemuleApp*>(pParam);
	return pApp != NULL ? pApp->RunBackendCommandThread() : 0;
}

UINT CemuleApp::RunBackendCommandThread()
{
	DbgSetThreadName("BackendCommandWorker");
	m_dwBackendCommandThreadId = ::GetCurrentThreadId();
	const DWORD dwPreviousBackendOwnerThreadId = m_dwBackendOwnerThreadId;
	SetBackendOwnerThreadId(m_dwBackendCommandThreadId);
	AddDebugLogLine(DLP_VERYLOW, false, _T("Backend command thread started. thread=%lu\n"), m_dwBackendCommandThreadId);
	HANDLE ahWait[2] = { m_hBackendCommandStopEvent, m_hBackendCommandEvent };
	for (;;) {
		const DWORD dwWait = ::WaitForMultipleObjects(2, ahWait, FALSE, INFINITE);
		if (dwWait == WAIT_OBJECT_0)
			break;
		if (dwWait == WAIT_OBJECT_0 + 1) {
			if (IsClosing() && !IsBackendCommandDrainingForShutdown())
				break;
			ProcessBackendCommandsOnCurrentThread();
		}
		else
			break;
	}
	AddDebugLogLine(DLP_VERYLOW, false, _T("Backend command thread stopped. thread=%lu\n"), m_dwBackendCommandThreadId);
	SetBackendOwnerThreadId(dwPreviousBackendOwnerThreadId);
	m_dwBackendCommandThreadId = 0;
	return 0;
}

bool CemuleApp::EnqueueBackendCommand(const SBackendCommand &command)
{
	SBackendCommand preparedCommand;
	CopyBackendCommand(command, preparedCommand);
	EnsureBackendCommandEnvelope(preparedCommand);
	AssignBackendCommandCancellationToken(preparedCommand);
	CString strValidationStage;
	if (!ValidateBackendCommandEnvelope(preparedCommand, &strValidationStage)) {
		QueueBackendCommandFailedEventEx(preparedCommand, BackendCommandFailureInvalidPayload, strValidationStage, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!ShouldAcceptBackendCommand(preparedCommand))
		return false;

	CScopedBackendCommandPtr scopedCommand(new SBackendCommand());
	CopyBackendCommand(preparedCommand, *scopedCommand.Get());
	bool bQueued = false;
	bool bRejectedByPressure = false;
	{
		CSingleLock lock(&m_backendCommandQueueLock, TRUE);
		scopedCommand.Get()->m_uSequence = ++m_uNextBackendCommandSequence;
		if (scopedCommand.Get()->m_uCorrelationId == 0)
			scopedCommand.Get()->m_uCorrelationId = scopedCommand.Get()->m_uSequence;
		CopyBackendCommand(*scopedCommand.Get(), preparedCommand);
		if (CoalesceBackendCommandLocked(*scopedCommand.Get()))
			bQueued = true;
		else if (m_backendCommandQueue.GetCount() >= 2048 && !TrimBackendCommandQueueForPressureLocked(*scopedCommand.Get(), 2048))
			bRejectedByPressure = m_backendCommandQueue.GetCount() >= 4096 || IsBackendCommandDroppableUnderPressure(*scopedCommand.Get());
		if (!bQueued && !bRejectedByPressure) {
			m_backendCommandQueue.AddTail(scopedCommand.Get());
			scopedCommand.Release();
			bQueued = true;
		}
	}
	if (bRejectedByPressure) {
		AddDebugLogLine(DLP_HIGH, false, _T("Backend command rejected by queue pressure. family=%s command=%u source=%s scope=%s key=%s\n"), GetBackendCommandFamilyName(preparedCommand.m_eFamily), static_cast<UINT>(preparedCommand.m_eType), GetBackendCommandSourceName(preparedCommand.m_eSource), GetBackendCommandOrderingScopeName(preparedCommand.m_eOrderingScope), (LPCTSTR)preparedCommand.m_strOrderingKey);
		if (!IsBackendCommandDroppableUnderPressure(preparedCommand))
			QueueBackendCommandFailedEventEx(preparedCommand, BackendCommandFailureDispatcherUnavailable, _T("backend-command-backpressure"), ERROR_NOT_ENOUGH_QUOTA);
		return false;
	}

	const bool bOwnerEligible = IsBackendCommandEligibleForBackendOwnerThread(preparedCommand);
	const bool bPreferBackendOwner = bOwnerEligible && (UseAsyncBackendCommandExecution(preparedCommand) || HasBackendCommandThreadSignalTarget());
	const bool bPosted = bPreferBackendOwner ? SignalBackendCommandThread() : PostBackendCommandUiMessage();
	if (!bPosted) {
		const bool bOnUiThread = g_uMainThreadId != 0 && ::GetCurrentThreadId() == g_uMainThreadId;
		const bool bBackendDispatchingOnUi = bOnUiThread && ::InterlockedCompareExchange(&m_lBackendCommandDispatching, 0, 0) != 0;
		if (bOnUiThread && !bBackendDispatchingOnUi)
			ProcessBackendCommands();
		else
			AddDebugLogLine(DLP_LOW, false, _T("Backend command queued without an available dispatcher. family=%s command=%u source=%s scope=%s key=%s async=%u owner=%u dispatching=%u\n"),
				GetBackendCommandFamilyName(preparedCommand.m_eFamily), static_cast<UINT>(preparedCommand.m_eType), GetBackendCommandSourceName(preparedCommand.m_eSource), GetBackendCommandOrderingScopeName(preparedCommand.m_eOrderingScope),
				(LPCTSTR)preparedCommand.m_strOrderingKey, static_cast<UINT>(UseAsyncBackendCommandExecution()), bPreferBackendOwner ? 1U : 0U, bBackendDispatchingOnUi ? 1U : 0U);
	}
	return true;
}

bool CemuleApp::HasBackendCommandThreadSignalTarget() const
{
	return m_hBackendCommandEvent != NULL && m_pBackendCommandThread != NULL && !IsBackendLifecycleStopping() && !IsClosing();
}

void CemuleApp::SetActiveDownloadAddOperationProgress(UINT uDone, UINT uTotal, bool bActive)
{
	CSingleLock lock(&m_activeDownloadAddOperationLock, TRUE);
	if (bActive && uTotal >= BULK_OPERATION_MIN_ITEMS) {
		m_bActiveDownloadAddOperation = true;
		m_bActiveDownloadAddSavingToDisk = false;
		m_uActiveDownloadAddDone = min(uDone, uTotal);
		m_uActiveDownloadAddTotal = uTotal;
		return;
	}

	const bool bPreserveDiskProgress = !bActive && uTotal >= BULK_OPERATION_MIN_ITEMS && m_bActiveDownloadAddSavingToDisk;
	if (bPreserveDiskProgress)
		m_bActiveDownloadAddOperation = false;
	else {
		m_bActiveDownloadAddOperation = false;
		m_bActiveDownloadAddSavingToDisk = false;
		m_uActiveDownloadAddDone = 0;
		m_uActiveDownloadAddTotal = 0;
	}
}

void CemuleApp::ClearActiveDownloadAddOperationProgress()
{
	CSingleLock lock(&m_activeDownloadAddOperationLock, TRUE);
	m_bActiveDownloadAddOperation = false;
	if (!m_bActiveDownloadAddSavingToDisk) {
		m_uActiveDownloadAddDone = 0;
		m_uActiveDownloadAddTotal = 0;
	}
}

void CemuleApp::SetActiveDownloadAddDiskProgress(UINT uDone, UINT uTotal, bool bActive)
{
	CSingleLock lock(&m_activeDownloadAddOperationLock, TRUE);
	if (bActive && uTotal >= BULK_OPERATION_MIN_ITEMS) {
		m_bActiveDownloadAddOperation = false;
		m_bActiveDownloadAddSavingToDisk = true;
		m_uActiveDownloadAddDone = min(uDone, uTotal);
		m_uActiveDownloadAddTotal = uTotal;
		return;
	}

	if (m_bActiveDownloadAddSavingToDisk) {
		m_bActiveDownloadAddSavingToDisk = false;
		m_uActiveDownloadAddDone = 0;
		m_uActiveDownloadAddTotal = 0;
	}
}

bool CemuleApp::GetActiveDownloadAddOperationProgress(UINT &uDone, UINT &uTotal, bool *pbSavingToDisk) const
{
	if (pbSavingToDisk != NULL)
		*pbSavingToDisk = false;

	CSingleLock lock(&m_activeDownloadAddOperationLock, TRUE);
	if ((m_bActiveDownloadAddOperation || m_bActiveDownloadAddSavingToDisk) && m_uActiveDownloadAddTotal >= BULK_OPERATION_MIN_ITEMS) {
		uDone = min(m_uActiveDownloadAddDone, m_uActiveDownloadAddTotal);
		uTotal = m_uActiveDownloadAddTotal;
		if (pbSavingToDisk != NULL)
			*pbSavingToDisk = m_bActiveDownloadAddSavingToDisk;
		return true;
	}

	uDone = 0;
	uTotal = 0;
	return false;
}

bool CemuleApp::GetActiveBackendDownloadListOperationProgress(bool &bRemove, UINT &uDone, UINT &uTotal)
{
	bRemove = false;
	uDone = 0;
	uTotal = 0;
	if (m_backendDownloadListJobs.IsEmpty()) {
		if (GetActiveDownloadAddOperationProgress(uDone, uTotal))
			return true;
		if (m_chunkedDownloadJobs.IsEmpty())
			return false;
		const SChunkedDownloadJob *pAddJob = m_chunkedDownloadJobs.GetHead();
		if (pAddJob == NULL || GetChunkedDownloadJobItemCount(*pAddJob) == 0)
			return false;
		bRemove = false;
		uTotal = GetChunkedDownloadJobItemCount(*pAddJob);
		uDone = pAddJob->m_uProcessed + pAddJob->m_uFailed;
		if (uDone > uTotal)
			uDone = uTotal;
		return true;
	}

	const SBackendDownloadListJob *pJob = m_backendDownloadListJobs.GetHead();
	if (pJob == NULL || pJob->m_vecItemHashes.empty())
		return false;

	bRemove = pJob->m_eType == DownloadCommandRemoveItems;
	uTotal = static_cast<UINT>(pJob->m_vecItemHashes.size());
	uDone = pJob->m_uProcessed + pJob->m_uFailed + pJob->m_uStale;
	if (uDone > uTotal)
		uDone = uTotal;
	return true;
}

void CemuleApp::CancelBackendDownloadListOperations()
{
	if (m_backendDownloadListJobs.IsEmpty()) {
		CancelBackendDownloadAddOperations();
		return;
	}

	bool bRemove = true;
	UINT uDone = 0;
	UINT uTotal = 0;
	if (!GetActiveBackendDownloadListOperationProgress(bRemove, uDone, uTotal)) {
		CancelBackendDownloadAddOperations();
		return;
	}
	ClearBackendDownloadListJobs();
	QueueDownloadListCommandEvent(bRemove ? ApplicationEventDownloadRemoveCompleted : ApplicationEventDownloadStateCompleted, 0, uDone, 0, 0, uTotal, 0, 0);
}

void CemuleApp::CancelBackendDownloadAddOperations()
{
	m_bChunkedDownloadMessagePending = false;
	const bool bHadParseJobs = !m_chunkedDownloadParseJobs.IsEmpty();
	UINT uProcessed = 0;
	UINT uFailed = 0;
	UINT uTotal = 0;
	EDownloadCommandType eDownloadCommandType = DownloadCommandAddFileLinks;
	while (!m_chunkedDownloadJobs.IsEmpty()) {
		SChunkedDownloadJob *pJob = m_chunkedDownloadJobs.RemoveHead();
		if (pJob != NULL) {
			EndChunkedDownloadJobBulkAdd(*pJob);
			eDownloadCommandType = pJob->m_command.m_eType;
			uProcessed += pJob->m_uProcessed;
			uFailed += pJob->m_uFailed;
			uTotal += GetChunkedDownloadJobItemCount(*pJob);
			delete pJob;
		}
	}
	ClearChunkedDownloadParseJobs();
	ClearActiveDownloadAddOperationProgress();

	if (bHadParseJobs || uProcessed != 0 || uFailed != 0 || uTotal != 0) {
		SApplicationEvent event;
		event.m_eType = ApplicationEventDownloadBatchCompleted;
		SetApplicationEventBackendEnvelope(event, BackendCommandDownload, BackendCommandSourceUi, BackendCommandOrderingDownloadList, _T("download:add"), 0, 0);
		event.m_eDownloadCommandType = eDownloadCommandType;
		event.m_uProcessed = uProcessed;
		event.m_uFailed = uFailed;
		event.m_uTotal = uTotal;
		QueueApplicationEvent(event);
	}
}

bool CemuleApp::QueueBackendContinuationProcessing()
{
	if (HasBackendCommandThreadSignalTarget())
		return SignalBackendCommandThread();
	return PostBackendCommandMessage();
}

void CemuleApp::ProcessBackendCommands()
{
	if (UseAsyncBackendCommandExecution()) {
		const DWORD dwCurrentThreadId = ::GetCurrentThreadId();
		if (dwCurrentThreadId != m_dwBackendCommandThreadId && (g_uMainThreadId == 0 || dwCurrentThreadId != g_uMainThreadId)) {
			SignalBackendCommandThread();
			return;
		}
	} else if (g_uMainThreadId != 0 && ::GetCurrentThreadId() != g_uMainThreadId) {
		PostBackendCommandMessage();
		return;
	}

	ProcessBackendCommandsOnCurrentThread();
}

bool CemuleApp::HasBackendContinuationWork() const
{
	const SBackendDownloadListJob *pJob = !m_backendDownloadListJobs.IsEmpty() ? m_backendDownloadListJobs.GetHead() : NULL;
	const SChunkedDownloadJob *pAddJob = !m_chunkedDownloadJobs.IsEmpty() ? m_chunkedDownloadJobs.GetHead() : NULL;
	bool bImportPartWorkPending = false;
	{
		CSingleLock lock(const_cast<CCriticalSection*>(&m_importPartWorkQueueLock), TRUE);
		bImportPartWorkPending = !m_importPartWorkItems.IsEmpty();
	}
	const bool bSearchKnownTypeRefreshPending = searchlist != NULL && searchlist->HasKnownTypeRefreshWork();
	const bool bDeferredDownloadValidatorPending = downloadqueue != NULL && downloadqueue->HasDeferredDownloadValidatorAdds();
	const bool bBulkAddDiskProgressPending = downloadqueue != NULL && downloadqueue->HasBulkAddDiskFinalizationProgressUpdate();
	return IsBackendDownloadListJobRunnable(pJob) || bImportPartWorkPending || bDeferredDownloadValidatorPending || bBulkAddDiskProgressPending || bSearchKnownTypeRefreshPending || !m_chunkedDownloadParseJobs.IsEmpty() || IsChunkedDownloadJobRunnable(pAddJob);
}

bool CemuleApp::ProcessBackendContinuationSlice(DWORD dwSliceStart, bool *pbYieldRequested)
{
	bool bProcessed = false;

	const bool bBackendOwnerThread = (UseAsyncBackendCommandExecution() || HasBackendCommandThreadSignalTarget()) && m_dwBackendCommandThreadId != 0 && ::GetCurrentThreadId() == m_dwBackendCommandThreadId;
	const bool bCanProcessDeferredDownloadValidatorAdds = bBackendOwnerThread || (!UseAsyncBackendCommandExecution() && !HasBackendCommandThreadSignalTarget());
	if (!IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch) && !m_backendDownloadListJobs.IsEmpty()) {
		const SBackendDownloadListJob *pJob = m_backendDownloadListJobs.GetHead();
		if (CanProcessBackendDownloadListJobOnCurrentThread(pJob)) {
			ProcessBackendDownloadListJobsOnCurrentThread();
			bProcessed = true;
		}
	}

	if (!IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch) && CanProcessImportPartWorkItemsOnCurrentThread()) {
		ProcessImportPartWorkItemsOnCurrentThread(dwSliceStart);
		bProcessed = true;
	}

	if ((!UseAsyncBackendCommandExecution() || IsUiThread()) && !IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch) && searchlist != NULL && searchlist->HasKnownTypeRefreshWork()) {
		bProcessed = searchlist->ProcessKnownTypeRefreshWork(dwSliceStart) || bProcessed;
	}

	if (!bBackendOwnerThread && !IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch) && !m_chunkedDownloadParseJobs.IsEmpty()) {
		ProcessChunkedDownloadParseJobs();
		bProcessed = true;
	}

	if (!IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch) && !m_chunkedDownloadJobs.IsEmpty()) {
		const SChunkedDownloadJob *pAddJob = m_chunkedDownloadJobs.GetHead();
		if (ShouldUseChunkedDownloadUiTimer(pAddJob)) {
			PostChunkedDownloadJobMessage();
			bProcessed = true;
			if (pbYieldRequested != NULL)
				*pbYieldRequested = true;
		}
		else if (CanProcessChunkedDownloadJobOnCurrentThread(pAddJob)) {
			bool bYieldAfterAdd = false;
			ProcessChunkedDownloadJobs(&bYieldAfterAdd);
			if (bYieldAfterAdd && pbYieldRequested != NULL)
				*pbYieldRequested = true;
			bProcessed = true;
		}
	}

	if (m_chunkedDownloadJobs.IsEmpty() && bCanProcessDeferredDownloadValidatorAdds && !IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch) && downloadqueue != NULL && downloadqueue->HasDeferredDownloadValidatorAdds()) {
		const bool bProcessedValidatorAdd = downloadqueue->ProcessDeferredDownloadValidatorAdds(false);
		bProcessed = bProcessedValidatorAdd || bProcessed;
		if (bProcessedValidatorAdd && IsUiThread() && pbYieldRequested != NULL)
			*pbYieldRequested = true;
	}

	if (bCanProcessDeferredDownloadValidatorAdds && !IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch) && downloadqueue != NULL && downloadqueue->HasBulkAddDiskFinalizationProgressUpdate()) {
		downloadqueue->ProcessBulkAddDiskFinalizationProgressUpdate();
		bProcessed = true;
	}

	return bProcessed;
}

void CemuleApp::ProcessBackendCommandsOnCurrentThread()
{
	if (IsUiThread())
		::InterlockedExchange(&m_lBackendCommandMessagePending, 0);
	if (!IsBackendCommandApplyThreadAllowed()) {
		static volatile LONG s_lLastBackendWrongThreadTraceTick = 0;
		const DWORD dwNow = ::GetTickCount();
		if (ShouldTraceRateLimited(s_lLastBackendWrongThreadTraceTick, dwNow, 5000) && thePrefs.GetLogUiResponsivenessEvents()) {
			AddDebugLogLine(DLP_LOW, false, _T("Backend command dispatcher refused wrong-thread apply. current=%lu backend=%lu ui=%u async=%u\n"),
				::GetCurrentThreadId(), m_dwBackendCommandThreadId, g_uMainThreadId, static_cast<UINT>(UseAsyncBackendCommandExecution()));
		}
		if (UseAsyncBackendCommandExecution())
			SignalBackendCommandThread();
		else
			PostBackendCommandMessage();
		return;
	}
	if (IsClosing() && !IsBackendCommandDrainingForShutdown()) {
		ClearBackendWorkQueues();
		return;
	}

	if (::InterlockedCompareExchange(&m_lBackendCommandDispatching, 1, 0) != 0) {
		const DWORD dwNow = ::GetTickCount();
		if (ShouldTraceRateLimited(m_lBackendCommandReentryTraceTick, dwNow, 5000) && thePrefs.GetLogUiResponsivenessEvents()) {
			AddDebugLogLine(DLP_LOW, false, _T("Backend command dispatcher re-entry deferred. current=%lu backend=%lu ui=%u async=%u\n"),
				::GetCurrentThreadId(), m_dwBackendCommandThreadId, g_uMainThreadId, static_cast<UINT>(UseAsyncBackendCommandExecution()));
		}
		if (UseAsyncBackendCommandExecution() && IsUiThread()) {
			SignalBackendCommandThread();
			return;
		}
		PostBackendCommandMessage();
		return;
	}

	CScopedInterlockedFlag scopedBackendCommandDispatch(m_lBackendCommandDispatching);
	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	bool bPreferContinuation = HasBackendContinuationWork();

	for (;;) {
		bool bProcessed = false;

		bool bYieldRequested = false;
		if (bPreferContinuation && HasBackendContinuationWork() && !IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch))
			bProcessed = ProcessBackendContinuationSlice(dwSliceStart, &bYieldRequested) || bProcessed;
		if (bYieldRequested)
			break;

		SBackendCommand *pCommand = NULL;
		bool bWakeUiLane = false;
		bool bWakeBackendLane = false;
		{
			CSingleLock lock(&m_backendCommandQueueLock, TRUE);
			if (!m_backendCommandQueue.IsEmpty()) {
				SBackendCommand *pHeadCommand = m_backendCommandQueue.GetHead();
				const bool bAsyncExecution = UseAsyncBackendCommandExecution();
				const bool bHasBackendSignalTarget = HasBackendCommandThreadSignalTarget();
				const bool bOnBackendOwnerThread = (bAsyncExecution || bHasBackendSignalTarget) && m_dwBackendCommandThreadId != 0 && ::GetCurrentThreadId() == m_dwBackendCommandThreadId;
				const bool bOnUiThread = IsUiThread();
				const bool bOwnerEligible = pHeadCommand != NULL && IsBackendCommandEligibleForBackendOwnerThread(*pHeadCommand);
				if ((bAsyncExecution || bHasBackendSignalTarget) && bOnBackendOwnerThread && !bOwnerEligible)
					bWakeUiLane = true;
				else if ((bAsyncExecution || bHasBackendSignalTarget) && bOnUiThread && bOwnerEligible)
					bWakeBackendLane = true;
				else
					pCommand = m_backendCommandQueue.RemoveHead();
			}
		}

		if (bWakeUiLane) {
			PostBackendCommandUiMessage();
			break;
		}
		if (bWakeBackendLane) {
			SignalBackendCommandThread();
			break;
		}

		if (pCommand != NULL) {
			try {
				ExecuteBackendCommand(*pCommand);
			} catch (CException *ex) {
				ex->Delete();
				QueueBackendCommandFailedEventEx(*pCommand, BackendCommandFailureException, _T("backend-command-exception"), ::GetLastError());
			} catch (...) {
				QueueBackendCommandFailedEventEx(*pCommand, BackendCommandFailureException, _T("backend-command-unknown-exception"), ::GetLastError());
			}
			delete pCommand;
			++uProcessedInSlice;
			bProcessed = true;
		}

		if (!bPreferContinuation && HasBackendContinuationWork() && !IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch))
			bProcessed = ProcessBackendContinuationSlice(dwSliceStart, &bYieldRequested) || bProcessed;
		if (bYieldRequested)
			break;

		bPreferContinuation = !bPreferContinuation;

		if (!bProcessed)
			break;
		if (uProcessedInSlice != 0 && IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch))
			break;
		if (uProcessedInSlice == 0 && IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch))
			break;
	}

	DWORD dwSliceElapsed = 0;
	bool bHasMoreCommands = false;
	bool bHeadCommandNeedsUiLane = false;
	INT_PTR iRemainingCommands = 0;
	{
		CSingleLock lock(&m_backendCommandQueueLock, TRUE);
		bHasMoreCommands = !m_backendCommandQueue.IsEmpty();
		iRemainingCommands = m_backendCommandQueue.GetCount();
		if (bHasMoreCommands && UseAsyncBackendCommandExecution()) {
			const SBackendCommand *pHeadCommand = m_backendCommandQueue.GetHead();
			bHeadCommandNeedsUiLane = pHeadCommand != NULL && !IsBackendCommandEligibleForBackendOwnerThread(*pHeadCommand);
		}
	}
	if (IsTimeBudgetHardExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch, &dwSliceElapsed))
		TraceTimeBudgetSlice(TimeBudgetBackendCommandDispatch, _T("ProcessBackendCommands"), dwSliceElapsed, uProcessedInSlice, iRemainingCommands);

	if (HasBackendContinuationWork() || bHasMoreCommands) {
		const SBackendDownloadListJob *pJob = !m_backendDownloadListJobs.IsEmpty() ? m_backendDownloadListJobs.GetHead() : NULL;
		const SChunkedDownloadJob *pAddJob = !m_chunkedDownloadJobs.IsEmpty() ? m_chunkedDownloadJobs.GetHead() : NULL;
		const bool bSearchKnownTypeRefreshPending = searchlist != NULL && searchlist->HasKnownTypeRefreshWork();
		const bool bHasUiCompatibilityContinuation = BackendDownloadListJobNeedsUiCompatibility(pJob) || bSearchKnownTypeRefreshPending || !m_chunkedDownloadParseJobs.IsEmpty() || ChunkedDownloadJobNeedsUiCompatibility(pAddJob);
		const bool bHasBackendOwnerAddContinuation = pAddJob != NULL && pAddJob->m_bBackendOwnerSafe && HasBackendCommandThreadSignalTarget();
		const bool bHasBackendOwnerDownloadQueueContinuation = downloadqueue != NULL && downloadqueue->HasBulkAddDiskFinalizationProgressUpdate() && HasBackendCommandThreadSignalTarget();
		bool bPosted = false;
		if (ShouldUseChunkedDownloadUiTimer(pAddJob)) {
			PostChunkedDownloadJobMessage();
			bPosted = m_bChunkedDownloadMessagePending;
			if (bHasMoreCommands || bHasUiCompatibilityContinuation || bHeadCommandNeedsUiLane)
				bPosted = ((UseAsyncBackendCommandExecution() && (bHasUiCompatibilityContinuation || bHeadCommandNeedsUiLane)) ? PostBackendCommandUiMessage() : PostBackendCommandMessage()) || bPosted;
		}
		else
			bPosted = (bHasBackendOwnerAddContinuation || bHasBackendOwnerDownloadQueueContinuation) ? SignalBackendCommandThread() : ((UseAsyncBackendCommandExecution() && (bHasUiCompatibilityContinuation || bHeadCommandNeedsUiLane)) ? PostBackendCommandUiMessage() : PostBackendCommandMessage());
		if (!bPosted) {
			if (IsBackendCommandDrainingForShutdown())
				AddDebugLogLine(DLP_LOW, false, _T("Backend command dispatcher kept shutdown drain work pending. commands=%Id continuation=%u uiCompat=%u\n"),
					iRemainingCommands, HasBackendContinuationWork() ? 1U : 0U, bHasUiCompatibilityContinuation ? 1U : 0U);
			else if (IsBackendLifecycleStopping() || IsClosing())
				ClearBackendWorkQueues();
			else
				AddDebugLogLine(DLP_LOW, false, _T("Backend command dispatcher kept pending work because no dispatch target is available. commands=%Id continuation=%u uiCompat=%u\n"),
					iRemainingCommands, HasBackendContinuationWork() ? 1U : 0U, bHasUiCompatibilityContinuation ? 1U : 0U);
		}
	}
}

bool CemuleApp::IsBackendCommandAllowedForCurrentApplyMode(const SBackendCommand &command, CString *pstrStage) const
{
	if (!UseAsyncBackendCommandExecution() || IsUiThread())
		return true;
	if (!IsBackendOwnerThread()) {
		if (pstrStage != NULL)
			*pstrStage = _T("backend-owner-thread");
		return false;
	}
	if (!IsBackendCommandEligibleForBackendOwnerThread(command)) {
		const SBackendCommandContract *pContract = GetBackendCommandContract(command);
		if (pstrStage != NULL)
			pstrStage->Format(_T("backend-owner-readiness:%s:%s"), pContract != NULL ? GetBackendCommandApplyModeName(pContract->m_eApplyMode) : _T("missing-contract"), GetBackendCommandReadinessReason(command.m_eFamily));
		AddDebugLogLine(DLP_HIGH, false, _T("Backend owner rejected non-ready command. family=%s source=%s scope=%s key=%s sequence=%I64u correlation=%I64u\n"),
			GetBackendCommandFamilyName(command.m_eFamily), GetBackendCommandSourceName(command.m_eSource), GetBackendCommandOrderingScopeName(command.m_eOrderingScope),
			(LPCTSTR)command.m_strOrderingKey, command.m_uSequence, command.m_uCorrelationId);
		return false;
	}
	return true;
}

void CemuleApp::ExecuteBackendCommand(const SBackendCommand &command)
{
	if (UseAsyncBackendCommandExecution())
		ExecuteBackendCommandBackendOwnerApply(command);
	else
		ExecuteBackendCommandUiCompatibilityApply(command);
}

void CemuleApp::ExecuteBackendCommandBackendOwnerApply(const SBackendCommand &command)
{
	CString strStage;
	if (!IsBackendCommandAllowedForCurrentApplyMode(command, &strStage)) {
		QueueBackendCommandFailedEventEx(command, BackendCommandFailureOwnerGuard, strStage, ERROR_ACCESS_DENIED);
		return;
	}
	ExecuteBackendCommandUiCompatibilityApply(command);
}

void CemuleApp::ExecuteBackendCommandUiCompatibilityApply(const SBackendCommand &command)
{
	if (command.m_eType == BackendCommandNetworkPacket) {
		ExecuteNetworkPacketCommand(command);
		return;
	}

	if (command.m_eType == BackendCommandDownload) {
		if (command.m_downloadCommand.m_eType == DownloadCommandRemoveItems || command.m_downloadCommand.m_eType == DownloadCommandChangeState) {
			if (command.m_downloadCommand.m_astrItemHashes.GetSize() == 0) {
				if (command.m_downloadCommand.m_eType == DownloadCommandChangeState && command.m_downloadCommand.m_uAction == static_cast<UINT>(BackendDownloadStateClearCompleted)) {
					if (!GuardModelMutation(ModelMutationDownloadQueue, _T("CemuleApp::ExecuteBackendCommandUiCompatibilityApply::ClearCompleted"))) {
						QueueBackendCommandFailedEventEx(command, BackendCommandFailureOwnerGuard, _T("download-clear-completed-owner-guard"), ERROR_ACCESS_DENIED);
						return;
					}
					if (downloadqueue == NULL) {
						QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("download-queue-unavailable"), ERROR_INVALID_HANDLE);
						return;
					}

					SBackendCommand materializedCommand;
					CopyBackendCommand(command, materializedCommand);
					downloadqueue->CollectCompletedFileHashes(materializedCommand.m_downloadCommand.m_astrItemHashes, command.m_downloadCommand.m_iCat);
					if (materializedCommand.m_downloadCommand.m_astrItemHashes.GetSize() == 0) {
						QueueDownloadListCommandEvent(ApplicationEventDownloadStateCompleted, command.m_downloadCommand.m_uAction, 0, 0, 0, 0,
							command.m_uSequence, command.m_uCorrelationId, command.m_eSource, command.m_eOrderingScope, command.m_strOrderingKey, command.m_uCancellationToken);
						return;
					}
					QueueBackendDownloadListJob(materializedCommand);
					return;
				}
				if (command.m_downloadCommand.m_eType == DownloadCommandChangeState && command.m_downloadCommand.m_uAction == static_cast<UINT>(BackendDownloadStateSetCategoryPriority)) {
					if (!GuardModelMutation(ModelMutationDownloadQueue, _T("CemuleApp::ExecuteBackendCommandUiCompatibilityApply::CategoryPriority"))) {
						QueueBackendCommandFailedEventEx(command, BackendCommandFailureOwnerGuard, _T("download-category-priority-owner-guard"), ERROR_ACCESS_DENIED);
						return;
					}
					if (downloadqueue == NULL) {
						QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("download-queue-unavailable"), ERROR_INVALID_HANDLE);
						return;
					}
					downloadqueue->SetCatPrio(static_cast<UINT>(command.m_downloadCommand.m_iCat), static_cast<uint8>(command.m_downloadCommand.m_iActionValue));
					QueueDownloadListCommandEvent(ApplicationEventDownloadStateCompleted, command.m_downloadCommand.m_uAction, 1, 0, 0, 1,
						command.m_uSequence, command.m_uCorrelationId, command.m_eSource, command.m_eOrderingScope, command.m_strOrderingKey, command.m_uCancellationToken);
					return;
				}
				QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("download-list-empty"), ERROR_INVALID_PARAMETER);
				return;
			}
			QueueBackendDownloadListJob(command);
			return;
		}

		if (!command.m_downloadCommand.m_strRawLinks.IsEmpty())
			QueueChunkedDownloadParseJob(command);
		else if (command.m_downloadCommand.m_astrLinks.GetSize() == 0 && GetDownloadCommandSnapshotCount(command.m_downloadCommand) == 0)
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("download-links-empty"), ERROR_INVALID_PARAMETER);
		else
			QueueChunkedDownloadJob(command);
		return;
	}

	if (command.m_eType == BackendCommandSearch) {
		if (command.m_searchCommand.m_eType == SearchCommandIngestApply) {
			ProcessSearchIngestJobsOnCurrentThread();
			return;
		}
		if (command.m_searchCommand.m_eType == SearchCommandKnownTypeRefresh) {
			const bool bQueued = searchlist != NULL && searchlist->QueueKnownTypeRefreshForAllSearches(command.m_searchCommand.m_bStartupRefresh);
			if (!bQueued && command.m_searchCommand.m_bStartupRefresh && emuledlg != NULL)
				emuledlg->NotifyStartupSearchKnownTypesRefreshCompleted(false);
			return;
		}

		SApplicationEvent event;
		CopyBackendCommandEnvelopeToEvent(command, event);
		event.m_eSearchCommandType = command.m_searchCommand.m_eType;
		event.m_uSearchID = command.m_searchCommand.m_uSearchID;
		event.m_searchParams = command.m_searchCommand.m_searchParams;
		event.m_eType = (command.m_searchCommand.m_eType == SearchCommandCancel) ? ApplicationEventSearchCancelRequested : ApplicationEventSearchStartRequested;
		QueueApplicationEvent(event);
		return;
	}

	if (command.m_eType == BackendCommandCollection) {
		if (command.m_collectionCommand.m_strFilePath.IsEmpty()) {
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("collection-path-empty"), ERROR_INVALID_PARAMETER);
			return;
		}

		SApplicationEvent event;
		event.m_eType = ApplicationEventCollectionImportRequested;
		CopyBackendCommandEnvelopeToEvent(command, event);
		event.m_strFilePath = command.m_collectionCommand.m_strFilePath;
		QueueApplicationEvent(event);
		return;
	}

	if (command.m_eType == BackendCommandSharedFiles) {
		if (!IsSharedFilesCommandTypeValid(command.m_sharedFilesCommand.m_eType) || command.m_sharedFilesCommand.m_uAction == 0) {
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("shared-files-invalid-payload"), ERROR_INVALID_PARAMETER);
			return;
		}

		SApplicationEvent event;
		event.m_eType = ApplicationEventSharedFilesCommandRequested;
		CopyBackendCommandEnvelopeToEvent(command, event);
		event.m_eSharedFilesCommandType = command.m_sharedFilesCommand.m_eType;
		event.m_uAction = command.m_sharedFilesCommand.m_uAction;
		for (INT_PTR i = 0; i < command.m_sharedFilesCommand.m_astrItemHashes.GetSize(); ++i)
			event.m_vecItemHashes.push_back(command.m_sharedFilesCommand.m_astrItemHashes.GetAt(i));
		QueueApplicationEvent(event);
		return;
	}

	if (command.m_eType == BackendCommandPersistence) {
		if (IsBackendCommandDrainingForShutdown() && IsUiThread())
			ExecutePersistenceCommandOnCurrentThread(command);
		else if (!QueuePersistenceBackendCommand(command))
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureDispatcherUnavailable, _T("persistence-worker-unavailable"), ERROR_INVALID_HANDLE);
		return;
	}

	if (command.m_eType != BackendCommandUpload) {
		QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("backend-command-unknown-type"), ERROR_INVALID_FUNCTION);
		return;
	}

	SUploadCommand uploadCommand;
	CopyUploadCommand(command.m_uploadCommand, uploadCommand);
	if (uploadCommand.m_eType == UploadCommandQueueListChanged || uploadCommand.m_eType == UploadCommandUploadListChanged || uploadCommand.m_eType == UploadCommandBandwidthSnapshotChanged) {
		if (!GuardModelMutation(ModelMutationUploadQueue, _T("CemuleApp::ExecuteBackendCommandUiCompatibilityApply::UploadQueueUiState"))) {
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureOwnerGuard, _T("upload-list-owner-guard"), ERROR_ACCESS_DENIED);
			return;
		}
		if (uploadqueue != NULL) {
			uploadCommand.m_uWaitingCount = static_cast<UINT>(uploadqueue->GetWaitingUserCount());
			uploadCommand.m_uUploadingCount = static_cast<UINT>(uploadqueue->GetUploadQueueLength());
			uploadCommand.m_uActiveUploadCount = static_cast<UINT>(uploadqueue->GetActiveUploadsCount());
			if (uploadCommand.m_eType == UploadCommandBandwidthSnapshotChanged) {
				uploadCommand.m_uDataRate = uploadqueue->GetDatarate();
				uploadCommand.m_uToNetworkDataRate = uploadqueue->GetToNetworkDatarate();
			}
		}
	}

	SApplicationEvent event;
	CopyBackendCommandEnvelopeToEvent(command, event);
	event.m_eUploadCommandType = uploadCommand.m_eType;
	event.m_uClientRuntimeID = uploadCommand.m_uRuntimeID;
	event.m_lClientRuntimeGeneration = uploadCommand.m_lRuntimeGeneration;
	event.m_uUploadTargetFlags = uploadCommand.m_uTargetFlags;
	event.m_uUploadWaitingCount = uploadCommand.m_uWaitingCount;
	event.m_uUploadUploadingCount = uploadCommand.m_uUploadingCount;
	event.m_uUploadActiveCount = uploadCommand.m_uActiveUploadCount;
	event.m_uUploadDataRate = uploadCommand.m_uDataRate;
	event.m_uUploadToNetworkDataRate = uploadCommand.m_uToNetworkDataRate;
	event.m_strMessage = uploadCommand.m_strStage;

	switch (uploadCommand.m_eType) {
		case UploadCommandClientRowsChanged:
		case UploadCommandClientRowsRemoved:
			if (uploadCommand.m_uRuntimeID == 0 || uploadCommand.m_lRuntimeGeneration == 0 || uploadCommand.m_uTargetFlags == 0) {
				QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("upload-client-rows-invalid-payload"), ERROR_INVALID_PARAMETER);
				return;
			}
			event.m_eType = (uploadCommand.m_eType == UploadCommandClientRowsRemoved) ? ApplicationEventUploadClientRowsRemoved : ApplicationEventUploadClientRowsChanged;
			break;
		case UploadCommandQueueListChanged:
			event.m_eType = ApplicationEventUploadQueueListChanged;
			break;
		case UploadCommandUploadListChanged:
			event.m_eType = ApplicationEventUploadListChanged;
			break;
		case UploadCommandBandwidthSnapshotChanged:
			event.m_eType = ApplicationEventUploadBandwidthSnapshotChanged;
			break;
		case UploadCommandDiskIoResult:
			if (uploadCommand.m_uRuntimeID == 0 || uploadCommand.m_lRuntimeGeneration == 0) {
				QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("upload-disk-io-invalid-payload"), ERROR_INVALID_PARAMETER);
				return;
			}
			event.m_eType = ApplicationEventUploadDiskIoResult;
			break;
		default:
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("upload-invalid-payload"), ERROR_INVALID_PARAMETER);
			return;
	}
	QueueApplicationEvent(event);
}

bool CemuleApp::ValidateNetworkPacketCommandSnapshot(const SNetworkPacketCommand &command, CString *pstrStage) const
{
	LPCTSTR pszStage = NULL;
	if (command.m_packet.empty())
		pszStage = _T("network-packet-empty");
	else if (command.m_uPacketPosition > static_cast<ULONGLONG>(command.m_packet.size()))
		pszStage = _T("network-packet-position");
	else {
		switch (command.m_eType) {
			case NetworkPacketCommandClientSearchAnswer:
				if (command.m_uSearchID == 0)
					pszStage = _T("network-packet-search-id");
				else if (command.m_uClientRuntimeID == 0 || command.m_lClientRuntimeGeneration == 0)
					pszStage = _T("network-packet-client-runtime");
				else if (IsZeroHash(command.m_abyClientUserHash))
					pszStage = _T("network-packet-client-hash");
				break;
			case NetworkPacketCommandServerSearchAnswer:
			case NetworkPacketCommandServerUdpSearchAnswer:
				if (command.m_uSearchID == 0)
					pszStage = _T("network-packet-search-id");
				break;
			case NetworkPacketCommandDownloadFileStatus:
			case NetworkPacketCommandDownloadSourceExchange:
				if (command.m_uClientRuntimeID == 0)
					pszStage = _T("network-packet-client-id");
				else if (command.m_lClientRuntimeGeneration == 0)
					pszStage = _T("network-packet-client-generation");
				else if (IsZeroHash(command.m_abyClientUserHash))
					pszStage = _T("network-packet-client-hash");
				else if (IsZeroHash(command.m_abyFileHash))
					pszStage = _T("network-packet-file-hash");
				break;
			case NetworkPacketCommandDownloadBlockReceive:
				if (command.m_uClientRuntimeID == 0)
					pszStage = _T("network-packet-client-id");
				else if (command.m_lClientRuntimeGeneration == 0)
					pszStage = _T("network-packet-client-generation");
				else if (IsZeroHash(command.m_abyClientUserHash))
					pszStage = _T("network-packet-client-hash");
				else if (IsZeroHash(command.m_abyFileHash))
					pszStage = _T("network-packet-file-hash");
				else {
					const size_t uMinimumSize = 16 + (command.m_bI64Offsets ? 8 : 4) + (command.m_bCompressedBlock ? 4 : (command.m_bI64Offsets ? 8 : 4));
					if (command.m_packet.size() < uMinimumSize)
						pszStage = _T("network-packet-block-size");
				}
				break;
			case NetworkPacketCommandDownloadHashSet:
				if (command.m_uClientRuntimeID == 0)
					pszStage = _T("network-packet-client-id");
				else if (command.m_lClientRuntimeGeneration == 0)
					pszStage = _T("network-packet-client-generation");
				else if (IsZeroHash(command.m_abyClientUserHash))
					pszStage = _T("network-packet-client-hash");
				else if (!command.m_bFileIdentifiers && IsZeroHash(command.m_abyFileHash))
					pszStage = _T("network-packet-file-hash");
				break;
			case NetworkPacketCommandDownloadFoundSources:
				if (IsZeroHash(command.m_abyFileHash))
					pszStage = _T("network-packet-file-hash");
				break;
			case NetworkPacketCommandKadPacket:
				if (command.m_packet.size() < 2)
					pszStage = _T("network-packet-kad-size");
				else if (command.m_nClientServerIP == 0 || command.m_nClientServerPort == 0)
					pszStage = _T("network-packet-kad-endpoint");
				break;
			default:
				pszStage = _T("network-packet-type");
				break;
		}
	}

	if (pstrStage != NULL)
		*pstrStage = pszStage != NULL ? pszStage : _T("");
	return pszStage == NULL;
}

bool CemuleApp::EnqueueNetworkPacketBackendCommand(SBackendCommand &command)
{
	command.m_eType = BackendCommandNetworkPacket;
	EnsureBackendCommandEnvelope(command);

	CString strStage;
	if (!ValidateNetworkPacketCommandSnapshot(command.m_networkPacketCommand, &strStage)) {
		QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, strStage, ERROR_INVALID_PARAMETER);
		return false;
	}
	return EnqueueBackendCommand(command);
}

void CemuleApp::ExecuteNetworkPacketCommand(const SBackendCommand &command)
{
	CString strStage;
	if (!ValidateNetworkPacketCommandSnapshot(command.m_networkPacketCommand, &strStage)) {
		QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, strStage, ERROR_INVALID_PARAMETER);
		return;
	}

	CString strFailureStage(_T("network-packet-stale-target"));
	EBackendCommandFailureKind eFailureKind = BackendCommandFailureStaleTarget;
	DWORD dwFailureLastError = ERROR_NOT_FOUND;
	bool bApplied = false;
	switch (command.m_networkPacketCommand.m_eType) {
		case NetworkPacketCommandClientSearchAnswer:
			bApplied = ApplyClientSearchAnswerNetworkCommand(command.m_networkPacketCommand, &strFailureStage, &eFailureKind, &dwFailureLastError);
			break;
		case NetworkPacketCommandServerSearchAnswer:
		case NetworkPacketCommandServerUdpSearchAnswer:
			bApplied = ApplyServerSearchAnswerNetworkCommand(command.m_networkPacketCommand, &strFailureStage, &eFailureKind, &dwFailureLastError);
			break;
		case NetworkPacketCommandDownloadFileStatus:
			bApplied = ApplyDownloadFileStatusNetworkCommand(command.m_networkPacketCommand, &strFailureStage, &eFailureKind, &dwFailureLastError);
			break;
		case NetworkPacketCommandDownloadHashSet:
			bApplied = ApplyDownloadHashSetNetworkCommand(command.m_networkPacketCommand, &strFailureStage, &eFailureKind, &dwFailureLastError);
			break;
		case NetworkPacketCommandDownloadFoundSources:
			bApplied = ApplyDownloadFoundSourcesNetworkCommand(command.m_networkPacketCommand, &strFailureStage, &eFailureKind, &dwFailureLastError);
			break;
		case NetworkPacketCommandDownloadSourceExchange:
			bApplied = ApplyDownloadSourceExchangeNetworkCommand(command.m_networkPacketCommand, &strFailureStage, &eFailureKind, &dwFailureLastError);
			break;
		case NetworkPacketCommandDownloadBlockReceive:
			bApplied = ApplyDownloadBlockReceiveNetworkCommand(command.m_networkPacketCommand, &strFailureStage, &eFailureKind, &dwFailureLastError);
			break;
		case NetworkPacketCommandKadPacket:
			bApplied = ApplyKadPacketNetworkCommand(command.m_networkPacketCommand, &strFailureStage, &eFailureKind, &dwFailureLastError);
			break;
		default:
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("network-packet-type"), ERROR_INVALID_PARAMETER);
			return;
	}
	if (!bApplied) {
		AddDebugLogLine(DLP_HIGH, false, _T("Network packet command dropped. packetCommand=%u failure=%s stage=%s error=%lu source=%s scope=%s key=%s sequence=%I64u correlation=%I64u\n"),
			static_cast<UINT>(command.m_networkPacketCommand.m_eType), GetBackendCommandFailureName(eFailureKind), (LPCTSTR)strFailureStage, dwFailureLastError,
			GetBackendCommandSourceName(command.m_eSource), GetBackendCommandOrderingScopeName(command.m_eOrderingScope), (LPCTSTR)command.m_strOrderingKey, command.m_uSequence, command.m_uCorrelationId);
		QueueBackendCommandFailedEventEx(command, eFailureKind, strFailureStage, dwFailureLastError);
	}
}

bool CemuleApp::ValidateSearchOwnerCommandTarget(const SNetworkPacketCommand &command, LPCTSTR pszEntryPoint, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (searchlist == NULL || command.m_packet.empty() || command.m_uSearchID == 0) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, searchlist == NULL ? _T("search-list-unavailable") : _T("search-answer-invalid-target"),
			searchlist == NULL ? BackendCommandFailureStaleTarget : BackendCommandFailureInvalidPayload, searchlist == NULL ? ERROR_INVALID_HANDLE : ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationSearchList, pszEntryPoint != NULL ? pszEntryPoint : _T("CemuleApp::ValidateSearchOwnerCommandTarget"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("search-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}
	const LONG lCurrentGeneration = searchlist->GetSearchAnswerParseGeneration(command.m_uSearchID);
	if (command.m_lSearchGeneration != 0 && lCurrentGeneration != 0 && command.m_lSearchGeneration != lCurrentGeneration) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("search-generation-stale"), BackendCommandFailureStaleTarget, ERROR_OPERATION_ABORTED);
		return false;
	}
	return true;
}

bool CemuleApp::ApplyClientSearchAnswerNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (!ValidateSearchOwnerCommandTarget(command, _T("CemuleApp::ApplyClientSearchAnswerNetworkCommand"), pstrFailureStage, peFailureKind, pdwLastError))
		return false;
	searchlist->QueueClientSearchAnswerPacketSnapshot(command.m_packet, command.m_uSearchID, command.m_lSearchGeneration, command.m_strClientHash, command.m_strSenderName,
		command.m_nClientID, command.m_nClientPort, command.m_nClientServerIP, command.m_nClientServerPort, command.m_bOptUTF8, command.m_bPreviewSupport, command.m_bSupportsLargeFiles,
		command.m_strDirectory.IsEmpty() ? NULL : (LPCTSTR)command.m_strDirectory);
	return true;
}

bool CemuleApp::ApplyServerSearchAnswerNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (!ValidateSearchOwnerCommandTarget(command, _T("CemuleApp::ApplyServerSearchAnswerNetworkCommand"), pstrFailureStage, peFailureKind, pdwLastError))
		return false;
	if (command.m_eType == NetworkPacketCommandServerUdpSearchAnswer) {
		CSafeMemFile data(const_cast<BYTE*>(&command.m_packet[0]), static_cast<UINT>(command.m_packet.size()));
		int iLeft;
		do {
			searchlist->ProcessUDPSearchAnswer(data, command.m_bOptUTF8, command.m_nClientServerIP, command.m_nClientServerPort);

			iLeft = static_cast<int>(data.GetLength() - data.GetPosition());
			if (iLeft >= 2) {
				const uint8 protocol = data.ReadUInt8();
				--iLeft;
				if (protocol != OP_EDONKEYPROT) {
					data.Seek(-1, CFile::current);
					++iLeft;
					break;
				}

				const uint8 opcode = data.ReadUInt8();
				--iLeft;
				if (opcode != OP_GLOBSEARCHRES) {
					data.Seek(-2, CFile::current);
					iLeft += 2;
					break;
				}
			}
		} while (iLeft > 0);

		if (iLeft > 0 && thePrefs.GetDebugServerUDPLevel() > 0) {
			Debug(_T("***NOTE: OP_GlobSearchResult contains %d additional bytes\n"), iLeft);
			if (thePrefs.GetDebugServerUDPLevel() > 1)
				DebugHexDump(data);
		}
		return true;
	}
	searchlist->QueueServerSearchAnswerPacketSnapshot(command.m_packet, command.m_uSearchID, command.m_lSearchGeneration, command.m_bOptUTF8, command.m_nClientServerIP, command.m_nClientServerPort);
	return true;
}


bool CemuleApp::ApplyKadPacketNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (command.m_packet.size() < 2 || command.m_nClientServerIP == 0 || command.m_nClientServerPort == 0) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("kad-packet-invalid-target"), BackendCommandFailureInvalidPayload, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationKad, _T("CemuleApp::ApplyKadPacketNetworkCommand"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("kad-packet-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}

	Kademlia::CKademlia::ProcessPacket(&command.m_packet[0], static_cast<uint32>(command.m_packet.size()), command.m_nClientServerIP, command.m_nClientServerPort,
		command.m_bValidReceiverKey, Kademlia::CKadUDPKey(command.m_uSenderUDPKey, GetPublicIPv4()));
	return true;
}

bool CemuleApp::ResolveDownloadNetworkFileTarget(const SNetworkPacketCommand &command, CPartFile **ppFile, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (ppFile != NULL)
		*ppFile = NULL;
	if (downloadqueue == NULL || IsZeroHash(command.m_abyFileHash)) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-file-invalid-target"), BackendCommandFailureInvalidPayload, ERROR_INVALID_PARAMETER);
		return false;
	}

	CPartFile *pFile = downloadqueue->GetFileByID(command.m_abyFileHash);
	if (pFile == NULL) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-file-stale"), BackendCommandFailureStaleTarget, ERROR_NOT_FOUND);
		return false;
	}
	if (command.m_uFileRuntimeID != 0 && pFile->GetRuntimeID() != command.m_uFileRuntimeID) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-file-generation-stale"), BackendCommandFailureStaleTarget, ERROR_NOT_FOUND);
		return false;
	}
	if (ppFile != NULL)
		*ppFile = pFile;
	return true;
}

bool CemuleApp::ApplyDownloadFileStatusNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (clientlist == NULL || downloadqueue == NULL || command.m_uClientRuntimeID == 0 || command.m_packet.empty() || command.m_uPacketPosition > static_cast<ULONGLONG>(command.m_packet.size())) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-file-status-invalid-target"), BackendCommandFailureInvalidPayload, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationUpDownClient, _T("CemuleApp::ApplyDownloadFileStatusNetworkCommand"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-file-status-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}

	CUpDownClient *pClient = clientlist->AcquireTrackedClientByRuntimeIDAndGeneration(command.m_uClientRuntimeID, command.m_lClientRuntimeGeneration);
	if (pClient == NULL) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-client-stale"), BackendCommandFailureStaleTarget, ERROR_NOT_FOUND);
		return false;
	}

	bool bApplied = false;
	try {
		CPartFile *pFile = NULL;
		if (!ResolveDownloadNetworkFileTarget(command, &pFile, pstrFailureStage, peFailureKind, pdwLastError)) {
			pClient->CheckFailedFileIdReqs(command.m_abyFileHash);
		} else {
			CSafeMemFile data(&command.m_packet[0], static_cast<UINT>(command.m_packet.size()));
			data.Seek(static_cast<LONGLONG>(command.m_uPacketPosition), CFile::begin);
			pClient->ProcessFileStatus(command.m_bUdpPacket, data, pFile);
			bApplied = true;
		}
	} catch (CException *ex) {
		ex->Delete();
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-file-status-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download file status network command failed. client=%lu\n"), command.m_uClientRuntimeID);
	} catch (CString strError) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, strError, BackendCommandFailureApplyFailed, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download file status network command failed. client=%lu error=%s\n"), command.m_uClientRuntimeID, (LPCTSTR)strError);
	} catch (...) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-file-status-unknown-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download file status network command failed. client=%lu unknown exception\n"), command.m_uClientRuntimeID);
	}
	pClient->ReleaseRuntimeReference();
	return bApplied;
}

bool CemuleApp::ApplyDownloadHashSetNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (clientlist == NULL || command.m_uClientRuntimeID == 0 || command.m_packet.empty()) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-hashset-invalid-target"), BackendCommandFailureInvalidPayload, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationUpDownClient, _T("CemuleApp::ApplyDownloadHashSetNetworkCommand"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-hashset-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}

	CUpDownClient *pClient = clientlist->AcquireTrackedClientByRuntimeIDAndGeneration(command.m_uClientRuntimeID, command.m_lClientRuntimeGeneration);
	if (pClient == NULL) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-client-stale"), BackendCommandFailureStaleTarget, ERROR_NOT_FOUND);
		return false;
	}

	bool bApplied = false;
	try {
		CPartFile *pReqFile = NULL;
		if (ResolveDownloadNetworkFileTarget(command, &pReqFile, pstrFailureStage, peFailureKind, pdwLastError) && (command.m_bFileIdentifiers || md4equ(command.m_abyFileHash, pReqFile->GetFileHash()))) {
			pClient->ProcessHashSet(&command.m_packet[0], static_cast<uint32>(command.m_packet.size()), command.m_bFileIdentifiers);
			bApplied = true;
		}
	} catch (CException *ex) {
		ex->Delete();
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-hashset-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download hashset network command failed. client=%lu\n"), command.m_uClientRuntimeID);
	} catch (CString strError) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, strError, BackendCommandFailureApplyFailed, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download hashset network command failed. client=%lu error=%s\n"), command.m_uClientRuntimeID, (LPCTSTR)strError);
	} catch (...) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-hashset-unknown-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download hashset network command failed. client=%lu unknown exception\n"), command.m_uClientRuntimeID);
	}
	pClient->ReleaseRuntimeReference();
	return bApplied;
}

bool CemuleApp::ApplyDownloadFoundSourcesNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (downloadqueue == NULL || command.m_packet.empty() || command.m_uPacketPosition > static_cast<ULONGLONG>(command.m_packet.size())) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-found-sources-invalid-target"), BackendCommandFailureInvalidPayload, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationPartFile, _T("CemuleApp::ApplyDownloadFoundSourcesNetworkCommand"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-found-sources-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}

	CPartFile *pFile = NULL;
	if (!ResolveDownloadNetworkFileTarget(command, &pFile, pstrFailureStage, peFailureKind, pdwLastError))
		return false;

	try {
		CSafeMemFile data(&command.m_packet[0], static_cast<UINT>(command.m_packet.size()));
		data.Seek(static_cast<LONGLONG>(command.m_uPacketPosition), CFile::begin);
		pFile->AddSources(&data, command.m_nClientServerIP, command.m_nClientServerPort, command.m_bWithObfuscationAndHash);
		return true;
	} catch (CException *ex) {
		ex->Delete();
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-found-sources-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download found-sources network command failed. file=%s\n"), (LPCTSTR)md4str(command.m_abyFileHash));
	} catch (...) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-found-sources-unknown-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download found-sources network command failed. file=%s unknown exception\n"), (LPCTSTR)md4str(command.m_abyFileHash));
	}
	return false;
}

bool CemuleApp::ApplyDownloadSourceExchangeNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (clientlist == NULL || downloadqueue == NULL || command.m_uClientRuntimeID == 0 || command.m_packet.empty() || command.m_uPacketPosition > static_cast<ULONGLONG>(command.m_packet.size())) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-source-exchange-invalid-target"), BackendCommandFailureInvalidPayload, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationUpDownClient, _T("CemuleApp::ApplyDownloadSourceExchangeNetworkCommand"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-source-exchange-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}
	if (!GuardModelMutation(ModelMutationPartFile, _T("CemuleApp::ApplyDownloadSourceExchangeNetworkCommand"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-source-exchange-file-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}

	CUpDownClient *pClient = clientlist->AcquireTrackedClientByRuntimeIDAndGeneration(command.m_uClientRuntimeID, command.m_lClientRuntimeGeneration);
	if (pClient == NULL) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-client-stale"), BackendCommandFailureStaleTarget, ERROR_NOT_FOUND);
		return false;
	}

	bool bApplied = false;
	try {
		CPartFile *pFile = NULL;
		if (ResolveDownloadNetworkFileTarget(command, &pFile, pstrFailureStage, peFailureKind, pdwLastError)) {
			pClient->SetLastSrcAnswerTime();
			pFile->SetLastAnsweredTime();
			CSafeMemFile data(&command.m_packet[0], static_cast<UINT>(command.m_packet.size()));
			data.Seek(static_cast<LONGLONG>(command.m_uPacketPosition), CFile::begin);
			pFile->AddClientSources(&data, command.m_uSourceExchangeVersion, command.m_bSourceExchange2, pClient);
			bApplied = true;
		}
	} catch (CException *ex) {
		ex->Delete();
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-source-exchange-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download source-exchange network command failed. client=%lu file=%s\n"), command.m_uClientRuntimeID, (LPCTSTR)md4str(command.m_abyFileHash));
	} catch (...) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-source-exchange-unknown-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download source-exchange network command failed. client=%lu file=%s unknown exception\n"), command.m_uClientRuntimeID, (LPCTSTR)md4str(command.m_abyFileHash));
	}
	pClient->ReleaseRuntimeReference();
	return bApplied;
}

bool CemuleApp::ApplyDownloadBlockReceiveNetworkCommand(const SNetworkPacketCommand &command, CString *pstrFailureStage, EBackendCommandFailureKind *peFailureKind, DWORD *pdwLastError)
{
	if (clientlist == NULL || downloadqueue == NULL || command.m_uClientRuntimeID == 0 || command.m_packet.empty()) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-block-receive-invalid-target"), BackendCommandFailureInvalidPayload, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationUpDownClient, _T("CemuleApp::ApplyDownloadBlockReceiveNetworkCommand"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-block-receive-client-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}
	if (!GuardModelMutation(ModelMutationPartFile, _T("CemuleApp::ApplyDownloadBlockReceiveNetworkCommand"))) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-block-receive-file-owner-guard"), BackendCommandFailureOwnerGuard, ERROR_ACCESS_DENIED);
		return false;
	}

	CUpDownClient *pClient = clientlist->AcquireTrackedClientByRuntimeIDAndGeneration(command.m_uClientRuntimeID, command.m_lClientRuntimeGeneration);
	if (pClient == NULL) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-client-stale"), BackendCommandFailureStaleTarget, ERROR_NOT_FOUND);
		return false;
	}

	bool bApplied = false;
	try {
		CPartFile *pFile = NULL;
		if (ResolveDownloadNetworkFileTarget(command, &pFile, pstrFailureStage, peFailureKind, pdwLastError)) {
			if (pClient->GetRequestFile() != pFile) {
				SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-block-receive-request-file-stale"), BackendCommandFailureStaleTarget, ERROR_NOT_FOUND);
			} else if (pFile->IsStopped() || (pFile->GetStatus() != PS_READY && pFile->GetStatus() != PS_EMPTY)) {
				SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-block-receive-file-not-ready"), BackendCommandFailureStaleTarget, ERROR_NOT_READY);
			} else {
				pClient->ProcessBlockPacket(&command.m_packet[0], static_cast<uint32>(command.m_packet.size()), command.m_bCompressedBlock, command.m_bI64Offsets);
				if (!pFile->IsStopped()) {
					EDownloadState eNewDownloadState = DS_CONNECTED;
					const UINT uStatus = pFile->GetStatus();
					if (uStatus == PS_ERROR)
						eNewDownloadState = DS_ONQUEUE;
					else if (uStatus == PS_PAUSED || uStatus == PS_INSUFFICIENT)
						eNewDownloadState = DS_NONE;
					if (eNewDownloadState != DS_CONNECTED) {
						pClient->SendCancelTransfer();
						pClient->SetDownloadState(eNewDownloadState);
					}
				}
				bApplied = true;
			}
		}
	} catch (CException *ex) {
		ex->Delete();
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-block-receive-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download block receive network command failed. client=%lu file=%s\n"), command.m_uClientRuntimeID, (LPCTSTR)md4str(command.m_abyFileHash));
	} catch (CString strError) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, strError, BackendCommandFailureApplyFailed, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download block receive network command failed. client=%lu file=%s error=%s\n"), command.m_uClientRuntimeID, (LPCTSTR)md4str(command.m_abyFileHash), (LPCTSTR)strError);
	} catch (...) {
		SetNetworkPacketApplyFailure(pstrFailureStage, peFailureKind, pdwLastError, _T("download-block-receive-unknown-exception"), BackendCommandFailureException, ::GetLastError());
		AddDebugLogLine(DLP_HIGH, false, _T("Download block receive network command failed. client=%lu file=%s unknown exception\n"), command.m_uClientRuntimeID, (LPCTSTR)md4str(command.m_abyFileHash));
	}
	pClient->ReleaseRuntimeReference();
	return bApplied;
}


bool CemuleApp::GuardPersistenceCommandMutation(const SBackendCommand &command, LPCTSTR pszStage)
{
	const EModelMutationDomain eDomain = GetPersistenceCommandMutationDomain(command.m_persistenceCommand.m_eType);
	if (IsModelMutationAllowed(eDomain))
		return true;

	const LPCTSTR pszDomain = GetModelMutationDomainName(eDomain);
	const LPCTSTR pszSafeStage = pszStage != NULL ? pszStage : _T("persistence-owner-guard");
	AddDebugLogLine(DLP_HIGH, false, _T("Persistence command owner guard failed. command=%u domain=%s stage=%s current=%lu backend=%lu sequence=%I64u correlation=%I64u\n"),
		static_cast<UINT>(command.m_persistenceCommand.m_eType), pszDomain, pszSafeStage, ::GetCurrentThreadId(), m_dwBackendOwnerThreadId, command.m_uSequence, command.m_uCorrelationId);
	QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, pszSafeStage, ERROR_ACCESS_DENIED, command.m_uSequence, command.m_uCorrelationId);
	return false;
}


namespace
{
	bool IsAutoPersistenceInitialLoadComplete(CemuleApp::EPersistenceCommandType eType)
	{
		switch (eType) {
			case CemuleApp::PersistenceCommandSaveSearchStore:
				return !thePrefs.IsStoringSearchesEnabled() || theApp.IsStartupMetadataDomainReady(CemuleApp::StartupMetadataStoredSearches);
			case CemuleApp::PersistenceCommandSaveKnownFiles:
			case CemuleApp::PersistenceCommandSaveSharedFiles:
				return theApp.KnownFilesReady() && theApp.SharedFilesReady();
			case CemuleApp::PersistenceCommandSaveClientHistory:
				return !thePrefs.GetClientHistory() || theApp.ClientHistoryReady();
			default:
				return true;
		}
	}
}

void CemuleApp::ExecutePersistenceCommandOnCurrentThread(const SBackendCommand &command)
{
	if (!IsPersistenceCommandTypeValid(command.m_persistenceCommand.m_eType)) {
		QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("persistence-invalid-payload"), ERROR_INVALID_PARAMETER);
		return;
	}

	if (!GuardPersistenceCommandMutation(command, _T("persistence-owner-guard")))
		return;

	CString strStartupMetadataSaveReason;
	if (!IsStartupMetadataSaveAllowed(command.m_persistenceCommand.m_eType, &strStartupMetadataSaveReason)) {
		if (command.m_persistenceCommand.m_bWorkRequest) {
			if (command.m_persistenceCommand.m_eType == PersistenceCommandSaveKnownFiles && knownfiles != NULL)
				knownfiles->DeferKnownMetSaveJob();
			return;
		}
		AddDebugLogLine(DLP_LOW, false, _T("Skipping persistence save before startup metadata domain is ready. command=%u reason=%s stage=%s\n"), static_cast<UINT>(command.m_persistenceCommand.m_eType), (LPCTSTR)command.m_persistenceCommand.m_strReason, (LPCTSTR)strStartupMetadataSaveReason);
		RejectStartupMetadataPersistenceCommand(command, strStartupMetadataSaveReason, ERROR_BUSY);
		return;
	}

	if (command.m_persistenceCommand.m_bAutoSave && emuledlg != NULL && emuledlg->IsInitializing()) {
		AddDebugLogLine(DLP_LOW, false, _T("Skipping automatic persistence save before startup loads completed. command=%u reason=%s\n"), static_cast<UINT>(command.m_persistenceCommand.m_eType), (LPCTSTR)command.m_persistenceCommand.m_strReason);
		RejectStartupMetadataPersistenceCommand(command, _T("startup-load-incomplete"), ERROR_BUSY);
		return;
	}
	if (command.m_persistenceCommand.m_bAutoSave && !IsAutoPersistenceInitialLoadComplete(command.m_persistenceCommand.m_eType)) {
		AddDebugLogLine(DLP_LOW, false, _T("Skipping automatic persistence save before the owning startup load completed. command=%u reason=%s\n"), static_cast<UINT>(command.m_persistenceCommand.m_eType), (LPCTSTR)command.m_persistenceCommand.m_strReason);
		RejectStartupMetadataPersistenceCommand(command, _T("domain-startup-load-incomplete"), ERROR_BUSY);
		return;
	}
	if (command.m_persistenceCommand.m_bWorkRequest) {
		if (command.m_persistenceCommand.m_eType == PersistenceCommandSaveKnownFiles && knownfiles != NULL) {
			knownfiles->ProcessKnownMetSaveJob();
			return;
		}
		QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, false, _T("unsupported-work-request"), ERROR_INVALID_FUNCTION, command.m_uSequence, command.m_uCorrelationId);
		return;
	}

	AddDebugLogLine(DLP_LOW, false, _T("Persistence save command executing. command=%u auto=%u source=%s scope=%s key=%s sequence=%I64u correlation=%I64u reason=%s\n"),
		static_cast<UINT>(command.m_persistenceCommand.m_eType), command.m_persistenceCommand.m_bAutoSave ? 1U : 0U, GetBackendCommandSourceName(command.m_eSource),
		GetBackendCommandOrderingScopeName(command.m_eOrderingScope), (LPCTSTR)command.m_strOrderingKey, command.m_uSequence, command.m_uCorrelationId, (LPCTSTR)command.m_persistenceCommand.m_strReason);

	try {
		switch (command.m_persistenceCommand.m_eType) {
		case PersistenceCommandSaveAppState:
			if (uploadqueue == NULL) {
				QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, _T("uploadqueue-unavailable"), ERROR_INVALID_HANDLE, command.m_uSequence, command.m_uCorrelationId);
				return;
			}
			uploadqueue->SaveAppState(command.m_persistenceCommand.m_bAutoSave);
			break;
		case PersistenceCommandSaveStats:
			thePrefs.SaveStats();
			break;
		case PersistenceCommandSaveKnownFiles:
			if (knownfiles == NULL) {
				QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, _T("knownfiles-unavailable"), ERROR_INVALID_HANDLE, command.m_uSequence, command.m_uCorrelationId);
				return;
			}
			knownfiles->Save();
			break;
		case PersistenceCommandSavePreferences:
			thePrefs.Save();
			break;
		case PersistenceCommandSaveFriends:
			if (friendlist != NULL)
				friendlist->SaveList();
			break;
		case PersistenceCommandSaveClientCredits:
			if (uploadqueue == NULL) {
				QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, _T("uploadqueue-unavailable"), ERROR_INVALID_HANDLE, command.m_uSequence, command.m_uCorrelationId);
				return;
			}
			uploadqueue->SaveClientCreditList();
			break;
		case PersistenceCommandSaveServerList:
			if (serverlist != NULL && !serverlist->SaveServermetToFile()) {
				QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, _T("servermet-save-failed"), ::GetLastError(), command.m_uSequence, command.m_uCorrelationId);
				return;
			}
			break;
		case PersistenceCommandSaveClientHistory:
			if (clientlist != NULL && thePrefs.GetClientHistory())
				clientlist->SaveList();
			break;
		case PersistenceCommandSaveSearchStore:
			if (searchlist != NULL)
				searchlist->StoreSearches();
			break;
		case PersistenceCommandSaveSearchSpam:
			if (searchlist != NULL)
				searchlist->SaveSpamFilter();
			break;
		case PersistenceCommandSaveSharedFiles:
			if (sharedfiles != NULL)
				sharedfiles->Save();
			break;
		case PersistenceCommandSaveKadNodes:
			if (Kademlia::CKademlia::m_pInstance != NULL && Kademlia::CKademlia::m_pInstance->m_pRoutingZone != NULL)
				Kademlia::CKademlia::m_pInstance->m_pRoutingZone->WriteFile();
			break;
		}
		QueuePersistenceCommandEvent(ApplicationEventPersistenceCompleted, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, command.m_persistenceCommand.m_strReason, 0, command.m_uSequence, command.m_uCorrelationId);
	} catch (CException *ex) {
		TCHAR szCause[512] = { 0 };
		if (ex != NULL) {
			ex->GetErrorMessage(szCause, _countof(szCause));
			ex->Delete();
		}
		QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, szCause[0] != _T('\0') ? szCause : _T("exception"), ::GetLastError(), command.m_uSequence, command.m_uCorrelationId);
	} catch (const CString &strError) {
		QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, strError, ::GetLastError(), command.m_uSequence, command.m_uCorrelationId);
	} catch (...) {
		QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, command.m_persistenceCommand.m_eType, command.m_persistenceCommand.m_bAutoSave, _T("unknown-exception"), ::GetLastError(), command.m_uSequence, command.m_uCorrelationId);
	}
}

void CemuleApp::QueueBackendCommandFailedEvent(const SBackendCommand &command, LPCTSTR pszMessage)
{
	QueueBackendCommandFailedEventEx(command, BackendCommandFailureApplyFailed, pszMessage, 0);
}

void CemuleApp::QueueBackendCommandFailedEventEx(const SBackendCommand &command, EBackendCommandFailureKind eFailure, LPCTSTR pszStage, DWORD dwLastError)
{
	SBackendCommand preparedCommand;
	CopyBackendCommand(command, preparedCommand);
	EnsureBackendCommandEnvelope(preparedCommand);
	AssignBackendCommandCancellationToken(preparedCommand);

	SApplicationEvent event;
	event.m_eType = ApplicationEventCommandFailed;
	CopyBackendCommandEnvelopeToEvent(preparedCommand, event);
	event.m_eBackendCommandFailureKind = eFailure;
	event.m_dwLastError = dwLastError;
	if (pszStage != NULL)
		event.m_strMessage = pszStage;
	QueueApplicationEvent(event);
}

bool CemuleApp::PostBackendCommandUiMessage()
{
	if (IsBackendLifecycleStopping() || IsClosing())
		return false;
	if (emuledlg == NULL)
		return false;

	HWND hWnd = emuledlg->m_hWnd;
	if (hWnd == NULL || !::IsWindow(hWnd)) {
		::InterlockedExchange(&m_lBackendCommandMessagePending, 0);
		AddDebugLogLine(DLP_LOW, false, _T("Backend command UI post skipped because the main window handle is not available. hwnd=%p\n"), hWnd);
		return false;
	}
	if (::InterlockedCompareExchange(&m_lBackendCommandMessagePending, 1, 0) != 0)
		return true;

	const BOOL bPosted = ::PostMessage(hWnd, CemuleDlg::UWM_EMULEAI_PROCESS_BACKEND_COMMANDS, 0, 0);
	if (bPosted == FALSE) {
		::InterlockedExchange(&m_lBackendCommandMessagePending, 0);
		AddDebugLogLine(DLP_HIGH, false, _T("Backend command UI post failed. hwnd=%p error=%lu\n"), hWnd, ::GetLastError());
		return false;
	}
	return true;
}

bool CemuleApp::PostBackendCommandMessage()
{
	if (IsBackendLifecycleStopping() || IsClosing())
		return false;
	if (UseAsyncBackendCommandExecution())
		return SignalBackendCommandThread();
	return PostBackendCommandUiMessage();
}

void CemuleApp::ClearBackendCommandQueue()
{
	::InterlockedExchange(&m_lBackendCommandMessagePending, 0);
	CSingleLock lock(&m_backendCommandQueueLock, TRUE);
	while (!m_backendCommandQueue.IsEmpty())
		delete m_backendCommandQueue.RemoveHead();
}

void CemuleApp::ClearBackendWorkQueues()
{
	ClearDownloadLinkParseQueue();
	ClearPersistenceBackendCommandQueue();
	ClearBackendCommandQueue();
	ClearBackendDownloadListJobs();
	ClearChunkedDownloadParseJobs();
	ClearChunkedDownloadJobs();
	ClearImportPartWorkItems();
	ClearSearchIngestProcessing();
}


void CemuleApp::QueueBackendDownloadListJob(const SBackendCommand &command)
{
	if (IsClosing())
		return;

	SBackendDownloadListJob *pJob = new SBackendDownloadListJob();
	pJob->m_eType = command.m_downloadCommand.m_eType;
	pJob->m_uAction = command.m_downloadCommand.m_uAction;
	pJob->m_iActionValue = command.m_downloadCommand.m_iActionValue;
	pJob->m_strActionValue = command.m_downloadCommand.m_strActionValue;
	pJob->m_bAddToCanceledMet = command.m_downloadCommand.m_bAddToCanceledMet;
	pJob->m_bDeleteCompletedFile = command.m_downloadCommand.m_bDeleteCompletedFile;
	pJob->m_uSequence = command.m_uSequence;
	pJob->m_uCorrelationId = command.m_uCorrelationId;
	pJob->m_uCancellationToken = command.m_uCancellationToken;
	pJob->m_eSource = command.m_eSource;
	pJob->m_eOrderingScope = command.m_eOrderingScope;
	pJob->m_strOrderingKey = command.m_strOrderingKey;
	pJob->m_bBackendOwnerSafe = IsBackendCommandEligibleForBackendOwnerThread(command);
	pJob->m_dwStartedTick = ::GetTickCount();
	pJob->m_dwLastProgressTick = pJob->m_dwStartedTick;

	std::set<CString, CStringNoCaseLess> setQueuedHashes;
	for (INT_PTR i = 0; i < command.m_downloadCommand.m_astrItemHashes.GetSize(); ++i) {
		CString strHash(command.m_downloadCommand.m_astrItemHashes.GetAt(i));
		strHash.Trim();
		SDownloadItemId id;
		if (strHash.IsEmpty() || !strmd4(strHash, id.m_abyFileHash))
			continue;
		const CString strCanonicalHash(md4str(id.m_abyFileHash));
		if (setQueuedHashes.insert(strCanonicalHash).second)
			pJob->m_vecItemHashes.push_back(strCanonicalHash);
	}

	if (pJob->m_vecItemHashes.empty()) {
		delete pJob;
		QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("download-list-no-valid-ids"), ERROR_INVALID_PARAMETER);
		return;
	}
	if (pJob->m_eType == DownloadCommandRemoveItems && downloadqueue != NULL) {
		downloadqueue->BeginBulkRemoveDownloads();
		pJob->m_bBulkRemoveActive = true;
		if (IsUiThread()) {
			CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
			if (pDownloadList != NULL && ::IsWindow(pDownloadList->GetSafeHwnd()) && !pDownloadList->HasActiveChunkedDownloadOperation()) {
				pDownloadList->BeginBackendDownloadRemoveBatch();
				pJob->m_bListUpdateBatchActive = true;
			}
		}
	}

	m_backendDownloadListJobs.AddTail(pJob);
	QueueDownloadListCommandEvent(pJob->m_eType == DownloadCommandRemoveItems ? ApplicationEventDownloadRemoveProgress : ApplicationEventDownloadStateProgress, pJob->m_uAction, 0, 0, 0, static_cast<UINT>(pJob->m_vecItemHashes.size()),
		pJob->m_uSequence, pJob->m_uCorrelationId, pJob->m_eSource, pJob->m_eOrderingScope, pJob->m_strOrderingKey, pJob->m_uCancellationToken);
	if (IsUiThread())
		UpdateBackendDownloadCommandOverlays(pJob->m_eType == DownloadCommandRemoveItems, 0, static_cast<UINT>(pJob->m_vecItemHashes.size()), pJob->m_uSequence, pJob->m_uCorrelationId);
}

void CemuleApp::ProcessBackendDownloadListJobsOnCurrentThread()
{
	if (IsClosing()) {
		ClearBackendDownloadListJobs();
		return;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	while (!m_backendDownloadListJobs.IsEmpty()) {
		SBackendDownloadListJob *pJob = m_backendDownloadListJobs.GetHead();
		if (pJob == NULL) {
			m_backendDownloadListJobs.RemoveHead();
			continue;
		}
		if (!CanProcessBackendDownloadListJobOnCurrentThread(pJob))
			break;

		while (pJob->m_iNextIndex < static_cast<INT_PTR>(pJob->m_vecItemHashes.size())) {
			const CString strHash(pJob->m_vecItemHashes[static_cast<size_t>(pJob->m_iNextIndex++)]);
			ProcessBackendDownloadListJobItem(*pJob, strHash);
			++uProcessedInSlice;

			const DWORD dwNow = ::GetTickCount();
			const ETimeBudgetedSliceKind eSliceKind = (pJob->m_eType == DownloadCommandRemoveItems) ? TimeBudgetDownloadRemove : TimeBudgetDownloadState;
			if (static_cast<DWORD>(dwNow - pJob->m_dwLastProgressTick) >= GetTimeBudgetedProgressTraceMs(eSliceKind)) {
				FlushBackendDownloadRemoveRows(*pJob);
				pJob->m_dwLastProgressTick = dwNow;
				const UINT uProgressProcessed = pJob->m_uProcessed;
				QueueDownloadListCommandEvent(pJob->m_eType == DownloadCommandRemoveItems ? ApplicationEventDownloadRemoveProgress : ApplicationEventDownloadStateProgress, pJob->m_uAction, uProgressProcessed, pJob->m_uFailed, pJob->m_uStale,
					static_cast<UINT>(pJob->m_vecItemHashes.size()), pJob->m_uSequence, pJob->m_uCorrelationId, pJob->m_eSource, pJob->m_eOrderingScope, pJob->m_strOrderingKey, pJob->m_uCancellationToken);
			}

			if (IsTimeBudgetExceeded(dwSliceStart, eSliceKind))
				break;
		}

		if (pJob->m_iNextIndex >= static_cast<INT_PTR>(pJob->m_vecItemHashes.size())) {
			if (FinishBackendDownloadListJob(*pJob))
				delete m_backendDownloadListJobs.RemoveHead();
			else
				break;
		}

		if (uProcessedInSlice != 0 && IsTimeBudgetExceeded(dwSliceStart, TimeBudgetBackendCommandDispatch))
			break;
	}
}

void CemuleApp::ClearBackendDownloadListJobs()
{
	while (!m_backendDownloadListJobs.IsEmpty()) {
		SBackendDownloadListJob *pJob = m_backendDownloadListJobs.RemoveHead();
		if (pJob != NULL) {
			FlushBackendDownloadRemoveRows(*pJob);
			if (pJob->m_bListUpdateBatchActive && IsUiThread()) {
				CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
				if (pDownloadList != NULL && ::IsWindow(pDownloadList->GetSafeHwnd()))
					pDownloadList->EndBackendDownloadRemoveBatch(true);
			}
			if (pJob->m_bBulkRemoveActive && downloadqueue != NULL)
				downloadqueue->EndBulkRemoveDownloads();
		}
		delete pJob;
	}
}

bool CemuleApp::IsSearchIngestProcessingPending() const
{
	return ::InterlockedCompareExchange(const_cast<volatile LONG*>(&m_lSearchIngestProcessingPending), 0, 0) > 0;
}

void CemuleApp::ProcessSearchIngestJobsOnCurrentThread()
{
	const LONG lPending = ::InterlockedExchange(&m_lSearchIngestProcessingPending, 0);
	if (lPending <= 0)
		return;

	if (IsClosing()) {
		ClearSearchIngestProcessing();
		return;
	}

	if (searchlist != NULL) {
		if (!GuardModelMutation(ModelMutationSearchList, _T("CemuleApp::ProcessSearchIngestJobsOnCurrentThread"))) {
			QueueSearchIngestProcessing();
			return;
		}
		searchlist->ProcessChunkedSearchIngestJobs();
	}

}

void CemuleApp::ClearSearchIngestProcessing()
{
	::InterlockedExchange(&m_lSearchIngestProcessingPending, 0);
	if (searchlist != NULL && IsModelMutationAllowed(ModelMutationSearchList))
		searchlist->ClearChunkedSearchIngestJobs();
}

bool CemuleApp::ProcessBackendDownloadListJobItem(SBackendDownloadListJob &job, LPCTSTR pszHash)
{
	if (job.m_eType == DownloadCommandRemoveItems)
		return ProcessBackendDownloadRemoveJobItem(job, pszHash);
	if (job.m_eType == DownloadCommandChangeState)
		return ProcessBackendDownloadStateJobItem(job, pszHash);
	++job.m_uFailed;
	QueueBackendDownloadListFailureEvent(job, pszHash, _T("invalid-action"), NULL, ERROR_INVALID_FUNCTION);
	return false;
}

void CemuleApp::QueueBackendDownloadStartNextCategory(SBackendDownloadListJob &job, UINT uCategory)
{
	if (uCategory == 0)
		return;
	for (std::vector<UINT>::const_iterator it = job.m_vecStartNextCategories.begin(); it != job.m_vecStartNextCategories.end(); ++it) {
		if (*it == uCategory)
			return;
	}
	job.m_vecStartNextCategories.push_back(uCategory);
}

void CemuleApp::QueueBackendDownloadRemoveRows(SBackendDownloadListJob &job, LPCTSTR pszHash)
{
	if (pszHash == NULL || pszHash[0] == _T('\0'))
		return;
	job.m_vecPendingUiRemovedHashes.push_back(pszHash);
	if (job.m_vecPendingUiRemovedHashes.size() >= kBackendDownloadRemoveRowFlushThreshold)
		FlushBackendDownloadRemoveRows(job);
}

void CemuleApp::FlushBackendDownloadRemoveRows(SBackendDownloadListJob &job)
{
	if (job.m_vecPendingUiRemovedHashes.empty())
		return;
	QueueDownloadListRowsRemovedEvent(job.m_vecPendingUiRemovedHashes, job.m_uSequence, job.m_uCorrelationId);
	job.m_vecPendingUiRemovedHashes.clear();
}

bool CemuleApp::ProcessBackendDownloadRemoveJobItem(SBackendDownloadListJob &job, LPCTSTR pszHash)
{
	SDownloadItemId id;
	if (pszHash == NULL || !strmd4(CString(pszHash), id.m_abyFileHash)) {
		++job.m_uFailed;
		QueueBackendDownloadListFailureEvent(job, pszHash, _T("invalid-id"), NULL, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationDownloadQueue, _T("CemuleApp::ProcessBackendDownloadRemoveJobItem"))) {
		++job.m_uFailed;
		QueueBackendDownloadListFailureEvent(job, pszHash, _T("owner-guard"), NULL, ERROR_INVALID_FUNCTION);
		return false;
	}

	CPartFile *pFile = ResolveDownloadFileForCommand(id);
	if (pFile == NULL) {
		++job.m_uStale;
		return false;
	}

	switch (pFile->GetStatus()) {
	case PS_WAITINGFORHASH:
	case PS_HASHING:
	case PS_COMPLETING:
		++job.m_uStale;
		return false;
	case PS_COMPLETE:
		if (job.m_bDeleteCompletedFile) {
			const CString strFilePath = pFile->GetFilePath();
			if (ShellDeleteFile(strFilePath))
				theApp.sharedfiles->RemoveFile(pFile, true);
			else {
				const DWORD dwError = ::GetLastError();
				AddDebugLogLine(DLP_HIGH, false, _T("Backend download remove failed to delete completed file. hash=%s error=%lu path=%s\n"), pszHash, dwError, (LPCTSTR)strFilePath);
				++job.m_uFailed;
				QueueBackendDownloadListFailureEvent(job, pszHash, _T("delete-completed-file"), strFilePath, dwError);
				return false;
			}
		}
		if (IsUiThread()) {
			CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
			if (pDownloadList != NULL)
				pDownloadList->RemoveFile(pFile);
		} else
			QueueBackendDownloadRemoveRows(job, pszHash);
		break;
	default:
		if (pFile->GetCategory() != 0 && downloadqueue != NULL) {
			if (job.m_bBulkRemoveActive)
				QueueBackendDownloadStartNextCategory(job, static_cast<UINT>(pFile->GetCategory()));
			else
				downloadqueue->StartNextFileIfPrefs(pFile->GetCategory());
		}
		{
			const uint64 uDiskCleanupSequence = job.m_bBackendOwnerSafe ? 0 : job.m_uSequence;
			const uint64 uDiskCleanupCorrelationId = job.m_bBackendOwnerSafe ? 0 : job.m_uCorrelationId;
			const bool bQueuedDiskCleanup = pFile->DeletePartFile(job.m_bAddToCanceledMet, uDiskCleanupSequence, uDiskCleanupCorrelationId, !job.m_bBackendOwnerSafe);
			if (bQueuedDiskCleanup && !job.m_bBackendOwnerSafe)
				++job.m_uPendingDiskDeletes;
			if (bQueuedDiskCleanup && job.m_bBackendOwnerSafe && !IsUiThread())
				QueueBackendDownloadRemoveRows(job, pszHash);
		}
		break;
	}

	++job.m_uProcessed;
	return true;
}

bool CemuleApp::ProcessBackendDownloadStateJobItem(SBackendDownloadListJob &job, LPCTSTR pszHash)
{
	SDownloadItemId id;
	if (pszHash == NULL || !strmd4(CString(pszHash), id.m_abyFileHash)) {
		++job.m_uFailed;
		QueueBackendDownloadListFailureEvent(job, pszHash, _T("invalid-id"), NULL, ERROR_INVALID_PARAMETER);
		return false;
	}
	if (!GuardModelMutation(ModelMutationPartFile, _T("CemuleApp::ProcessBackendDownloadStateJobItem"))) {
		++job.m_uFailed;
		QueueBackendDownloadListFailureEvent(job, pszHash, _T("owner-guard"), NULL, ERROR_INVALID_FUNCTION);
		return false;
	}

	CPartFile *pFile = ResolveDownloadFileForCommand(id);
	if (pFile == NULL) {
		++job.m_uStale;
		return false;
	}

	switch (job.m_uAction) {
	case BackendDownloadStatePermissionDefault:
		pFile->SetPermissions(-1);
		break;
	case BackendDownloadStatePermissionNone:
		pFile->SetPermissions(PERM_NOONE);
		break;
	case BackendDownloadStatePermissionFriends:
		pFile->SetPermissions(PERM_FRIENDS);
		break;
	case BackendDownloadStatePermissionAll:
		pFile->SetPermissions(PERM_ALL);
		break;
	case BackendDownloadStatePriorityHigh:
		pFile->SetAutoDownPriority(false);
		pFile->SetDownPriority(PR_HIGH);
		break;
	case BackendDownloadStatePriorityLow:
		pFile->SetAutoDownPriority(false);
		pFile->SetDownPriority(PR_LOW);
		break;
	case BackendDownloadStatePriorityNormal:
		pFile->SetAutoDownPriority(false);
		pFile->SetDownPriority(PR_NORMAL);
		break;
	case BackendDownloadStatePriorityAuto:
		pFile->SetAutoDownPriority(true);
		pFile->SetDownPriority(PR_HIGH);
		break;
	case BackendDownloadStatePause:
		if (pFile->CanPauseFile())
			pFile->PauseFile();
		break;
	case BackendDownloadStateResume:
		if (pFile->CanResumeFile()) {
			if (pFile->GetStatus() == PS_ERROR && pFile->GetCompletionError() && !IsUiThread()) {
				++job.m_uFailed;
				QueueBackendDownloadListFailureEvent(job, pszHash, _T("resume-completion-error-ui-required"), NULL, ERROR_INVALID_FUNCTION);
				return false;
			}
			if (pFile->GetStatus() == PS_INSUFFICIENT)
				pFile->ResumeFileInsufficient();
			else
				pFile->ResumeFile();
		}
		break;
	case BackendDownloadStateStop:
		if (pFile->CanStopFile()) {
			if (IsUiThread()) {
				CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
				if (pDownloadList != NULL)
					pDownloadList->HideSources(pFile);
			}
			pFile->StopFile(false);
		}
		break;
	case BackendDownloadStateSetSourceLimit:
		pFile->SetPrivateMaxSources(job.m_iActionValue);
		pFile->UpdateDisplayedInfo(true);
		break;
	case BackendDownloadStateSetCategory:
		pFile->SetCategory(job.m_iActionValue);
		pFile->UpdateDisplayedInfo(true);
		break;
	case BackendDownloadStateSetPauseOnPreview:
		if (pFile->IsPreviewableFileType() && !pFile->IsReadyForPreview())
			pFile->SetPauseOnPreview(job.m_iActionValue != 0);
		break;
	case BackendDownloadStateToggleAutoRenameToMajorityName:
		if (thePrefs.GetDownloadInspector() > 0 && pFile->GetStatus() != PS_COMPLETE && pFile->GetStatus() != PS_COMPLETING)
			pFile->ToggleAutoRenameToMajorityName();
		break;
	case BackendDownloadStateCleanupFilename:
		if (pFile->IsPartFile()) {
			if (IsUiThread()) {
				CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
				if (pDownloadList != NULL)
					pDownloadList->HideSources(pFile);
			}
			pFile->SetAutoRenameToMajorityName(false);
			pFile->SetFileName(CleanupFilename(pFile->GetFileName()));
		}
		break;
	case BackendDownloadStateClearCompleted:
		if (!pFile->IsPartFile()) {
			if (IsUiThread()) {
				CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
				if (pDownloadList != NULL)
					pDownloadList->RemoveFile(pFile);
			} else
				QueueBackendDownloadRemoveRows(job, pszHash);
		}
		break;
	case BackendDownloadStateSetFileName:
		if (pFile->GetStatus() != PS_COMPLETE && pFile->GetStatus() != PS_COMPLETING && !job.m_strActionValue.IsEmpty() && IsValidEd2kString(job.m_strActionValue)) {
			if (IsUiThread()) {
				CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
				if (pDownloadList != NULL)
					pDownloadList->HideSources(pFile);
			}
			pFile->SetAutoRenameToMajorityName(false);
			pFile->SetFileName(job.m_strActionValue, true);
			pFile->UpdateDisplayedInfo();
			pFile->SavePartFile();
		}
		break;
	case BackendDownloadStateTogglePreviewPriority:
		pFile->SetPreviewPrio(!pFile->GetPreviewPrio());
		break;
	case BackendDownloadStateImportParts:
		if (pFile->GetFileOp() == PFOP_IMPORTPARTS) {
			pFile->ImportPartsFromFile(NULL);
			break;
		}
		if (pFile->m_bMD4HashsetNeeded || job.m_strActionValue.IsEmpty()) {
			++job.m_uFailed;
			QueueBackendDownloadListFailureEvent(job, pszHash, _T("import-parts-invalid-source"), NULL, ERROR_INVALID_PARAMETER);
			return false;
		}
		if (!pFile->ImportPartsFromFile(job.m_strActionValue)) {
			++job.m_uFailed;
			QueueBackendDownloadListFailureEvent(job, pszHash, _T("import-parts-start-failed"), job.m_strActionValue, ::GetLastError());
			return false;
		}
		break;
	default:
		++job.m_uFailed;
		QueueBackendDownloadListFailureEvent(job, pszHash, _T("invalid-state-action"), NULL, ERROR_INVALID_FUNCTION);
		return false;
	}

	++job.m_uProcessed;
	return true;
}

bool CemuleApp::FinishBackendDownloadListJob(SBackendDownloadListJob &job)
{
	FlushBackendDownloadRemoveRows(job);

	if (job.m_bListUpdateBatchActive && IsUiThread()) {
		job.m_bListUpdateBatchActive = false;
		CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
		if (pDownloadList != NULL && ::IsWindow(pDownloadList->GetSafeHwnd()))
			pDownloadList->EndBackendDownloadRemoveBatch(true);
	}

	if (job.m_bBulkRemoveActive && downloadqueue != NULL) {
		job.m_bBulkRemoveActive = false;
		downloadqueue->EndBulkRemoveDownloads();
		for (std::vector<UINT>::const_iterator it = job.m_vecStartNextCategories.begin(); it != job.m_vecStartNextCategories.end(); ++it)
			downloadqueue->StartNextFileIfPrefs(static_cast<int>(*it));
	}

	const bool bRemove = job.m_eType == DownloadCommandRemoveItems;
	if (bRemove && job.m_uPendingDiskDeletes > 0) {
		job.m_bWaitingForDiskCleanup = true;
		const UINT uProgressProcessed = job.m_uProcessed;
		AddDebugLogLine(DLP_VERYLOW, false, _T("Backend download remove waiting for disk cleanup. sequence=%I64u correlation=%I64u processed=%u pendingDisk=%u failed=%u stale=%u\n"), job.m_uSequence, job.m_uCorrelationId, job.m_uProcessed, job.m_uPendingDiskDeletes, job.m_uFailed, job.m_uStale);
		QueueDownloadListCommandEvent(ApplicationEventDownloadRemoveProgress, job.m_uAction, uProgressProcessed, job.m_uFailed, job.m_uStale,
			static_cast<UINT>(job.m_vecItemHashes.size()), job.m_uSequence, job.m_uCorrelationId, job.m_eSource, job.m_eOrderingScope, job.m_strOrderingKey, job.m_uCancellationToken);
		return false;
	}

	job.m_bWaitingForDiskCleanup = false;
	QueueDownloadListCommandEvent(bRemove ? ApplicationEventDownloadRemoveCompleted : ApplicationEventDownloadStateCompleted, job.m_uAction, job.m_uProcessed, job.m_uFailed, job.m_uStale,
		static_cast<UINT>(job.m_vecItemHashes.size()), job.m_uSequence, job.m_uCorrelationId, job.m_eSource, job.m_eOrderingScope, job.m_strOrderingKey, job.m_uCancellationToken);
	return true;
}

bool CemuleApp::CompleteBackendDownloadRemoveDiskCleanup(uint64 uSequence, uint64 uCorrelationId, UINT uCompletedCount, UINT uFailedCount)
{
	const UINT uFinishedCount = uCompletedCount + uFailedCount;
	if (uFinishedCount == 0)
		return false;

	for (POSITION pos = m_backendDownloadListJobs.GetHeadPosition(); pos != NULL;) {
		POSITION posCurrent = pos;
		SBackendDownloadListJob *pJob = m_backendDownloadListJobs.GetNext(pos);
		if (pJob == NULL || pJob->m_eType != DownloadCommandRemoveItems || pJob->m_uPendingDiskDeletes == 0)
			continue;
		if (uSequence != 0 && pJob->m_uSequence != 0 && uSequence != pJob->m_uSequence)
			continue;
		if (uCorrelationId != 0 && pJob->m_uCorrelationId != 0 && uCorrelationId != pJob->m_uCorrelationId)
			continue;

		if (uFinishedCount >= pJob->m_uPendingDiskDeletes)
			pJob->m_uPendingDiskDeletes = 0;
		else
			pJob->m_uPendingDiskDeletes -= uFinishedCount;
		if (uFailedCount != 0) {
			const UINT uFailedFromProcessed = min(uFailedCount, pJob->m_uProcessed);
			pJob->m_uProcessed -= uFailedFromProcessed;
			pJob->m_uFailed += uFailedFromProcessed;
			AddDebugLogLine(DLP_HIGH, false, _T("Backend download remove disk cleanup reported physical delete failure. sequence=%I64u correlation=%I64u failed=%u applied=%u\n"), uSequence, uCorrelationId, uFailedCount, uFailedFromProcessed);
		}

		if (pJob->m_iNextIndex >= static_cast<INT_PTR>(pJob->m_vecItemHashes.size()) && pJob->m_uPendingDiskDeletes == 0) {
			if (FinishBackendDownloadListJob(*pJob)) {
				m_backendDownloadListJobs.RemoveAt(posCurrent);
				delete pJob;
				if (!m_backendDownloadListJobs.IsEmpty()) {
					if (UseAsyncBackendCommandExecution())
						PostBackendCommandUiMessage();
					else
						PostBackendCommandMessage();
				}
			}
		} else {
			const UINT uProgressProcessed = pJob->m_uProcessed;
			QueueDownloadListCommandEvent(ApplicationEventDownloadRemoveProgress, pJob->m_uAction, uProgressProcessed, pJob->m_uFailed, pJob->m_uStale,
				static_cast<UINT>(pJob->m_vecItemHashes.size()), pJob->m_uSequence, pJob->m_uCorrelationId, pJob->m_eSource, pJob->m_eOrderingScope, pJob->m_strOrderingKey, pJob->m_uCancellationToken);
		}
		return true;
	}
	return false;
}

void CemuleApp::QueueBackendDownloadListFailureEvent(const SBackendDownloadListJob &job, LPCTSTR pszHash, LPCTSTR pszStage, LPCTSTR pszFilePath, DWORD dwError)
{
	CString strMessage;
	strMessage.Format(_T("%s hash=%s"), pszStage != NULL ? pszStage : _T("unknown"), pszHash != NULL ? pszHash : _T(""));
	QueueDownloadListCommandFailureEvent(job.m_eType == DownloadCommandRemoveItems ? ApplicationEventDownloadRemoveItemFailed : ApplicationEventDownloadStateItemFailed, job.m_uAction, strMessage, pszFilePath, dwError,
		job.m_uSequence, job.m_uCorrelationId, job.m_eSource, job.m_eOrderingScope, job.m_strOrderingKey, job.m_uCancellationToken);
}

void CemuleApp::QueueChunkedDownloadParseJob(const SBackendCommand &command)
{
	if (command.m_downloadCommand.m_strRawLinks.IsEmpty() || IsClosing())
		return;

	SDownloadLinkParseThreadParam *pParam = new SDownloadLinkParseThreadParam();
	pParam->m_pApp = this;
	CopyBackendCommand(command, pParam->m_command);
	pParam->m_strRawLinks = command.m_downloadCommand.m_strRawLinks;
	pParam->m_strTokenDelimiters = command.m_downloadCommand.m_strTokenDelimiters.IsEmpty() ? CString(_T(" \t\r\n")) : command.m_downloadCommand.m_strTokenDelimiters;
	pParam->m_command.m_downloadCommand.m_strRawLinks.Empty();
	pParam->m_command.m_downloadCommand.m_strTokenDelimiters.Empty();

	bool bQueueWorkerItem = false;
	{
		CSingleLock lock(&m_downloadLinkParseQueueLock, TRUE);
		m_downloadLinkParseQueue.AddTail(pParam);
		if (!m_bDownloadLinkParseWorkerActive) {
			m_bDownloadLinkParseWorkerActive = true;
			bQueueWorkerItem = true;
		}
	}

	if (!bQueueWorkerItem)
		return;

	bool bQueued = false;
	if (GetWorkerTopologyState(WorkerTopologyNetworkParseCpu) != WorkerTopologyStopped || StartNetworkParseCpuWorker()) {
		SWorkerTopologyItem item;
		item.m_eRole = WorkerTopologyNetworkParseCpu;
		item.m_eType = WorkerTopologyItemNetworkParseCpu;
		item.m_dwCreatedTick = ::GetTickCount();
		item.m_dwDueTick = item.m_dwCreatedTick;
		item.m_strStage = _T("download-link-parse");
		item.m_strCoalesceKey = _T("download-link-parse");
		bQueued = QueueNetworkParseCpuWorkerItem(item);
	}
	if (bQueued)
		return;

	AddDebugLogLine(DLP_HIGH, false, _T("Download link parse worker could not be queued. Falling back to backend dispatcher slicing. sequence=%I64u correlation=%I64u\n"), command.m_uSequence, command.m_uCorrelationId);
	CTypedPtrList<CPtrList, SDownloadLinkParseThreadParam*> fallbackQueue;
	{
		CSingleLock lock(&m_downloadLinkParseQueueLock, TRUE);
		m_bDownloadLinkParseWorkerActive = false;
		while (!m_downloadLinkParseQueue.IsEmpty())
			fallbackQueue.AddTail(m_downloadLinkParseQueue.RemoveHead());
	}
	while (!fallbackQueue.IsEmpty()) {
		SDownloadLinkParseThreadParam *pFallback = fallbackQueue.RemoveHead();
		if (pFallback != NULL) {
			QueueChunkedDownloadParseFallbackJob(pFallback->m_command, pFallback->m_strRawLinks, pFallback->m_strTokenDelimiters);
			delete pFallback;
		}
	}
}

void CemuleApp::QueueChunkedDownloadParseFallbackJob(const SBackendCommand &command, LPCTSTR pszRawLinks, LPCTSTR pszTokenDelimiters)
{
	if (pszRawLinks == NULL || pszRawLinks[0] == _T('\0') || IsClosing())
		return;

	SChunkedDownloadParseJob *pJob = new SChunkedDownloadParseJob();
	CopyBackendCommand(command, pJob->m_command);
	pJob->m_strRawLinks = pszRawLinks;
	pJob->m_strTokenDelimiters = (pszTokenDelimiters != NULL && pszTokenDelimiters[0] != _T('\0')) ? pszTokenDelimiters : _T(" \t\r\n");
	pJob->m_iNextParsePos = 0;
	pJob->m_dwStartedTick = ::GetTickCount();
	pJob->m_dwLastProgressTick = pJob->m_dwStartedTick;
	pJob->m_command.m_downloadCommand.m_strRawLinks.Empty();
	pJob->m_command.m_downloadCommand.m_strTokenDelimiters.Empty();
	m_chunkedDownloadParseJobs.AddTail(pJob);
	PostChunkedDownloadParseJobMessage();
}

void CemuleApp::ClearDownloadLinkParseQueue()
{
	CSingleLock lock(&m_downloadLinkParseQueueLock, TRUE);
	while (!m_downloadLinkParseQueue.IsEmpty())
		delete m_downloadLinkParseQueue.RemoveHead();
	m_bDownloadLinkParseWorkerActive = false;
}

void CemuleApp::ProcessDownloadLinkParseJobsOnParserThread()
{
	for (;;) {
		SDownloadLinkParseThreadParam *pParseParam = NULL;
		{
			CSingleLock lock(&m_downloadLinkParseQueueLock, TRUE);
			if (m_downloadLinkParseQueue.IsEmpty()) {
				m_bDownloadLinkParseWorkerActive = false;
				return;
			}
			pParseParam = m_downloadLinkParseQueue.RemoveHead();
		}

		if (pParseParam == NULL)
			continue;

		SBackendCommand command;
		CopyBackendCommand(pParseParam->m_command, command);
		CString strRawLinks(pParseParam->m_strRawLinks);
		CString strTokenDelimiters(pParseParam->m_strTokenDelimiters.IsEmpty() ? CString(_T(" \t\r\n")) : pParseParam->m_strTokenDelimiters);
		delete pParseParam;

		if (IsClosing())
			continue;

		CStringArray astrLinks;
		int iNextParsePos = 0;
		UINT uParsed = 0;
		while (iNextParsePos >= 0) {
			const CString strToken(strRawLinks.Tokenize(strTokenDelimiters, iNextParsePos));
			if (strToken.IsEmpty())
				break;
			AddTrimmedToken(astrLinks, strToken);
			++uParsed;
		}

		if (IsClosing())
			continue;

		if (astrLinks.GetSize() == 0) {
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureInvalidPayload, _T("download-parse-no-links"), ERROR_INVALID_PARAMETER);
			continue;
		}

		SBackendCommand completedCommand;
		CopyBackendCommand(command, completedCommand);
		completedCommand.m_downloadCommand.m_strRawLinks.Empty();
		completedCommand.m_downloadCommand.m_strTokenDelimiters.Empty();
		completedCommand.m_downloadCommand.m_astrLinks.RemoveAll();
		CopyStringArray(astrLinks, completedCommand.m_downloadCommand.m_astrLinks);
		completedCommand.m_uSequence = 0;
		completedCommand.m_uCorrelationId = command.m_uCorrelationId != 0 ? command.m_uCorrelationId : command.m_uSequence;

		AddDebugLogLine(DLP_LOW, false, _T("Download link parse worker completed. parsed=%u links=%d correlation=%I64u\n"), uParsed, completedCommand.m_downloadCommand.m_astrLinks.GetSize(), completedCommand.m_uCorrelationId);
		if (!EnqueueBackendCommand(completedCommand))
			QueueBackendCommandFailedEventEx(command, BackendCommandFailureDispatcherUnavailable, _T("download-parse-backend-dispatcher-unavailable"), ERROR_INVALID_HANDLE);
	}
}

void CemuleApp::PostChunkedDownloadParseJobMessage()
{
	if (IsClosing()) {
		ClearChunkedDownloadParseJobs();
		return;
	}
	if (m_bChunkedDownloadParseMessagePending)
		return;

	m_bChunkedDownloadParseMessagePending = PostBackendCommandUiMessage();
	if (m_bChunkedDownloadParseMessagePending)
		return;

	AddDebugLogLine(DLP_LOW, false, _T("Chunked download parse aborted because the backend continuation dispatcher is unavailable. pending=%d\n"), static_cast<int>(m_chunkedDownloadParseJobs.GetCount()));
	FailChunkedDownloadParseJobs(_T("Download parse dispatcher is unavailable."));
}

void CemuleApp::ProcessChunkedDownloadParseJobs()
{
	m_bChunkedDownloadParseMessagePending = false;
	if (IsClosing()) {
		ClearChunkedDownloadParseJobs();
		return;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	while (!m_chunkedDownloadParseJobs.IsEmpty()) {
		SChunkedDownloadParseJob *pJob = m_chunkedDownloadParseJobs.GetHead();
		if (pJob == NULL) {
			m_chunkedDownloadParseJobs.RemoveHead();
			continue;
		}

		while (pJob->m_iNextParsePos >= 0) {
			const CString strToken(pJob->m_strRawLinks.Tokenize(pJob->m_strTokenDelimiters, pJob->m_iNextParsePos));
			if (strToken.IsEmpty())
				break;
			AddTrimmedToken(pJob->m_command.m_downloadCommand.m_astrLinks, strToken);
			++pJob->m_uParsed;
			++uProcessedInSlice;

			const DWORD dwNow = ::GetTickCount();
			if (static_cast<DWORD>(dwNow - pJob->m_dwLastProgressTick) >= GetTimeBudgetedProgressTraceMs(TimeBudgetDownloadParse)) {
				pJob->m_dwLastProgressTick = dwNow;
				AddDebugLogLine(DLP_VERYLOW, false, _T("Chunked download parse progress. sequence=%I64u correlation=%I64u parsed=%u offset=%d length=%d\n"), pJob->m_command.m_uSequence, pJob->m_command.m_uCorrelationId, pJob->m_uParsed, pJob->m_iNextParsePos, pJob->m_strRawLinks.GetLength());
			}

			if (IsTimeBudgetExceeded(dwSliceStart, TimeBudgetDownloadParse))
				break;
		}

		if (pJob->m_iNextParsePos < 0 || pJob->m_iNextParsePos >= pJob->m_strRawLinks.GetLength()) {
			SBackendCommand completedCommand;
			CopyBackendCommand(pJob->m_command, completedCommand);
			AddDebugLogLine(DLP_LOW, false, _T("Chunked download parse completed. sequence=%I64u correlation=%I64u parsed=%u elapsed=%u\n"), pJob->m_command.m_uSequence, pJob->m_command.m_uCorrelationId, pJob->m_uParsed, static_cast<DWORD>(::GetTickCount() - pJob->m_dwStartedTick));
			delete m_chunkedDownloadParseJobs.RemoveHead();
			if (completedCommand.m_downloadCommand.m_astrLinks.GetSize() == 0)
				QueueBackendCommandFailedEventEx(completedCommand, BackendCommandFailureInvalidPayload, _T("download-parse-no-links"), ERROR_INVALID_PARAMETER);
			else
				QueueChunkedDownloadJob(completedCommand);
		}

		if (uProcessedInSlice != 0 && IsTimeBudgetExceeded(dwSliceStart, TimeBudgetDownloadParse))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (IsTimeBudgetHardExceeded(dwSliceStart, TimeBudgetDownloadParse, &dwSliceElapsed))
		TraceTimeBudgetSlice(TimeBudgetDownloadParse, _T("ProcessChunkedDownloadParseJobs"), dwSliceElapsed, uProcessedInSlice, m_chunkedDownloadParseJobs.GetCount());

	if (!m_chunkedDownloadParseJobs.IsEmpty())
		PostChunkedDownloadParseJobMessage();
}

void CemuleApp::ClearChunkedDownloadParseJobs()
{
	m_bChunkedDownloadParseMessagePending = false;
	while (!m_chunkedDownloadParseJobs.IsEmpty())
		delete m_chunkedDownloadParseJobs.RemoveHead();
}

void CemuleApp::FailChunkedDownloadParseJobs(LPCTSTR pszMessage)
{
	m_bChunkedDownloadParseMessagePending = false;
	while (!m_chunkedDownloadParseJobs.IsEmpty()) {
		SChunkedDownloadParseJob *pJob = m_chunkedDownloadParseJobs.RemoveHead();
		if (pJob != NULL) {
			if (!IsClosing())
				QueueBackendCommandFailedEvent(pJob->m_command, pszMessage != NULL ? pszMessage : _T("Download parse job failed."));
			delete pJob;
		}
	}
}

void CemuleApp::QueueChunkedDownloadJob(const SBackendCommand &command)
{
	const UINT uItemCount = static_cast<UINT>(command.m_downloadCommand.m_astrLinks.GetSize() + static_cast<INT_PTR>(GetDownloadCommandSnapshotCount(command.m_downloadCommand)));
	if (uItemCount == 0 || IsClosing())
		return;

	SChunkedDownloadJob *pJob = new SChunkedDownloadJob();
	pJob->m_command.m_eType = command.m_downloadCommand.m_eType;
	pJob->m_command.m_iCat = command.m_downloadCommand.m_iCat;
	pJob->m_uSequence = command.m_uSequence;
	pJob->m_uCorrelationId = command.m_uCorrelationId;
	pJob->m_uCancellationToken = command.m_uCancellationToken;
	pJob->m_eSource = command.m_eSource;
	pJob->m_eOrderingScope = command.m_eOrderingScope;
	pJob->m_strOrderingKey = command.m_strOrderingKey;
	pJob->m_bBackendOwnerSafe = IsBackendCommandEligibleForBackendOwnerThread(command) || (command.m_downloadCommand.m_pFileSnapshots.get() != NULL && !command.m_downloadCommand.m_pFileSnapshots->empty());
	pJob->m_dwStartedTick = ::GetTickCount();
	pJob->m_dwLastProgressTick = pJob->m_dwStartedTick;
	CopyStringArray(command.m_downloadCommand.m_astrLinks, pJob->m_command.m_astrLinks);
	pJob->m_command.m_pFileSnapshots = command.m_downloadCommand.m_pFileSnapshots;
	m_chunkedDownloadJobs.AddTail(pJob);

	SApplicationEvent event;
	event.m_eType = ApplicationEventDownloadBatchProgress;
	SetApplicationEventBackendEnvelope(event, BackendCommandDownload, pJob->m_eSource, pJob->m_eOrderingScope,
		pJob->m_strOrderingKey.IsEmpty() ? (LPCTSTR)_T("download:add") : (LPCTSTR)pJob->m_strOrderingKey, pJob->m_uSequence, pJob->m_uCorrelationId);
	event.m_eDownloadCommandType = pJob->m_command.m_eType;
	event.m_uProcessed = 0;
	event.m_uFailed = 0;
	event.m_uTotal = GetChunkedDownloadJobItemCount(*pJob);
	event.m_uCancellationToken = pJob->m_uCancellationToken;
	SetActiveDownloadAddOperationProgress(0, event.m_uTotal, true);
	QueueApplicationEvent(event);
	UpdateDownloadAddMirroredOverlay(0, event.m_uTotal);
	if (emuledlg != NULL)
		emuledlg->RefreshActiveBulkOperationOverlays();

	PostChunkedDownloadJobMessage();
}

void CemuleApp::PostChunkedDownloadJobMessage()
{
	if (IsClosing()) {
		ClearChunkedDownloadJobs();
		return;
	}
	if (m_bChunkedDownloadMessagePending)
		return;

	const SChunkedDownloadJob *pJob = !m_chunkedDownloadJobs.IsEmpty() ? m_chunkedDownloadJobs.GetHead() : NULL;
	if (ShouldUseChunkedDownloadUiTimer(pJob) && emuledlg != NULL && ::IsWindow(emuledlg->GetSafeHwnd())) {
		m_bChunkedDownloadMessagePending = emuledlg->StartChunkedDownloadAddTimer();
		if (m_bChunkedDownloadMessagePending)
			return;
	}
	if (pJob != NULL && pJob->m_bBackendOwnerSafe && HasBackendCommandThreadSignalTarget())
		m_bChunkedDownloadMessagePending = SignalBackendCommandThread();
	else
		m_bChunkedDownloadMessagePending = (UseAsyncBackendCommandExecution() && ChunkedDownloadJobNeedsUiCompatibility(pJob)) ? PostBackendCommandUiMessage() : PostBackendCommandMessage();
	if (m_bChunkedDownloadMessagePending)
		return;

	AddDebugLogLine(DLP_LOW, false, _T("Chunked download job aborted because the backend continuation dispatcher is unavailable. pending=%d\n"), static_cast<int>(m_chunkedDownloadJobs.GetCount()));
	FailChunkedDownloadJobs(_T("Download add dispatcher is unavailable."));
}

void CemuleApp::BeginChunkedDownloadJobBulkAdd(SChunkedDownloadJob &job)
{
	if (job.m_bBulkAddActive || downloadqueue == NULL)
		return;
	const bool bSnapshotBulkAdd = job.m_command.m_pFileSnapshots.get() != NULL && !job.m_command.m_pFileSnapshots->empty();
	const UINT uBulkItemCount = GetChunkedDownloadJobItemCount(job);
	const bool bLargeBulkAdd = uBulkItemCount >= kLargeDownloadBatchThreshold;
	const bool bSuppressPerItemListUpdates = uBulkItemCount >= BULK_OPERATION_MIN_ITEMS;
	downloadqueue->BeginBulkAddDownloads(bSuppressPerItemListUpdates, bSnapshotBulkAdd || bLargeBulkAdd);
	job.m_bBulkAddActive = true;
}

void CemuleApp::EndChunkedDownloadJobBulkAdd(SChunkedDownloadJob &job)
{
	if (!job.m_bBulkAddActive)
		return;
	job.m_bBulkAddActive = false;
	if (downloadqueue != NULL)
		downloadqueue->EndBulkAddDownloads();
	if (emuledlg != NULL)
		emuledlg->DeferUiLogFlush(750);
}

void CemuleApp::ProcessChunkedDownloadJobs(bool *pbYieldRequested)
{
	m_bChunkedDownloadMessagePending = false;
	if (IsClosing()) {
		ClearChunkedDownloadJobs();
		return;
	}

	const DWORD dwSliceStart = ::GetTickCount();
	DWORD dwUiSnapshotSliceBudgetMs = 18;
	UINT uMaxUiSnapshotDownloadItemsPerSlice = 1024;
	GetChunkedDownloadUiSnapshotSliceLimits(dwUiSnapshotSliceBudgetMs, uMaxUiSnapshotDownloadItemsPerSlice);
	UINT uProcessedInSlice = 0;
	while (!m_chunkedDownloadJobs.IsEmpty()) {
		SChunkedDownloadJob *pJob = m_chunkedDownloadJobs.GetHead();
		if (pJob == NULL) {
			m_chunkedDownloadJobs.RemoveHead();
			continue;
		}
		if (!CanProcessChunkedDownloadJobOnCurrentThread(pJob))
			break;

		BeginChunkedDownloadJobBulkAdd(*pJob);
		const UINT uJobTotal = GetChunkedDownloadJobItemCount(*pJob);
		const bool bLimitUiDownloadSlice = IsUiThread();
		while (pJob->m_iNextIndex < static_cast<INT_PTR>(uJobTotal)) {
			const INT_PTR iLinkCount = pJob->m_command.m_astrLinks.GetSize();
			bool bProcessed = false;
			if (pJob->m_iNextIndex < iLinkCount) {
				const CString strLink(pJob->m_command.m_astrLinks.GetAt(pJob->m_iNextIndex));
				bProcessed = ProcessChunkedDownloadItem(*pJob, strLink);
			} else {
				const size_t uSnapshotIndex = static_cast<size_t>(pJob->m_iNextIndex - iLinkCount);
				if (pJob->m_command.m_pFileSnapshots.get() != NULL && uSnapshotIndex < pJob->m_command.m_pFileSnapshots->size())
					bProcessed = ProcessChunkedDownloadSnapshotItem(*pJob, (*pJob->m_command.m_pFileSnapshots)[uSnapshotIndex]);
			}
			++pJob->m_iNextIndex;
			if (bProcessed)
				++pJob->m_uProcessed;
			else
				++pJob->m_uFailed;
			++uProcessedInSlice;

			const DWORD dwNow = ::GetTickCount();
			if (static_cast<DWORD>(dwNow - pJob->m_dwLastProgressTick) >= GetTimeBudgetedProgressTraceMs(TimeBudgetDownloadAdd)) {
				pJob->m_dwLastProgressTick = dwNow;
				SApplicationEvent event;
				event.m_eType = ApplicationEventDownloadBatchProgress;
				SetApplicationEventBackendEnvelope(event, BackendCommandDownload, pJob->m_eSource, pJob->m_eOrderingScope,
					pJob->m_strOrderingKey.IsEmpty() ? (LPCTSTR)_T("download:add") : (LPCTSTR)pJob->m_strOrderingKey, pJob->m_uSequence, pJob->m_uCorrelationId);
				event.m_eDownloadCommandType = pJob->m_command.m_eType;
				event.m_uProcessed = pJob->m_uProcessed;
				event.m_uFailed = pJob->m_uFailed;
				event.m_uTotal = uJobTotal;
				event.m_uCancellationToken = pJob->m_uCancellationToken;
				const UINT uProgressDone = pJob->m_uProcessed + pJob->m_uFailed;
				SetActiveDownloadAddOperationProgress(uProgressDone, uJobTotal, true);
				QueueApplicationEvent(event);
				UpdateDownloadAddMirroredOverlay(min(uProgressDone, uJobTotal), uJobTotal);
				if (emuledlg != NULL)
					emuledlg->RefreshActiveBulkOperationOverlays();
			}

			if (bLimitUiDownloadSlice) {
				if ((uProcessedInSlice & 0x07) == 0)
					GetChunkedDownloadUiSnapshotSliceLimits(dwUiSnapshotSliceBudgetMs, uMaxUiSnapshotDownloadItemsPerSlice);
				const DWORD dwUiSliceElapsed = static_cast<DWORD>(::GetTickCount() - dwSliceStart);
				if (uProcessedInSlice >= uMaxUiSnapshotDownloadItemsPerSlice || (uProcessedInSlice != 0 && dwUiSliceElapsed >= dwUiSnapshotSliceBudgetMs))
					break;
			} else if (uProcessedInSlice != 0 && IsTimeBudgetExceeded(dwSliceStart, TimeBudgetDownloadAdd))
				break;
		}

		if (pJob->m_iNextIndex >= static_cast<INT_PTR>(uJobTotal)) {
			EndChunkedDownloadJobBulkAdd(*pJob);
			SApplicationEvent event;
			event.m_eType = ApplicationEventDownloadBatchCompleted;
			SetApplicationEventBackendEnvelope(event, BackendCommandDownload, pJob->m_eSource, pJob->m_eOrderingScope,
				pJob->m_strOrderingKey.IsEmpty() ? (LPCTSTR)_T("download:add") : (LPCTSTR)pJob->m_strOrderingKey, pJob->m_uSequence, pJob->m_uCorrelationId);
			event.m_eDownloadCommandType = pJob->m_command.m_eType;
			event.m_uProcessed = pJob->m_uProcessed;
			event.m_uFailed = pJob->m_uFailed;
			event.m_uTotal = uJobTotal;
			event.m_uCancellationToken = pJob->m_uCancellationToken;
			SetActiveDownloadAddOperationProgress(pJob->m_uProcessed + pJob->m_uFailed, uJobTotal, false);
			QueueApplicationEvent(event);
			UpdateDownloadAddMirroredOverlay(0, 0);
			if (emuledlg != NULL)
				emuledlg->RefreshActiveBulkOperationOverlays();
			delete m_chunkedDownloadJobs.RemoveHead();
		}

		if (bLimitUiDownloadSlice) {
			GetChunkedDownloadUiSnapshotSliceLimits(dwUiSnapshotSliceBudgetMs, uMaxUiSnapshotDownloadItemsPerSlice);
			const DWORD dwUiSliceElapsed = static_cast<DWORD>(::GetTickCount() - dwSliceStart);
			if (uProcessedInSlice >= uMaxUiSnapshotDownloadItemsPerSlice || (uProcessedInSlice != 0 && dwUiSliceElapsed >= dwUiSnapshotSliceBudgetMs)) {
				if (pbYieldRequested != NULL)
					*pbYieldRequested = true;
				break;
			}
		} else if (uProcessedInSlice != 0 && IsTimeBudgetExceeded(dwSliceStart, TimeBudgetDownloadAdd))
			break;
	}

	DWORD dwSliceElapsed = 0;
	if (IsTimeBudgetHardExceeded(dwSliceStart, TimeBudgetDownloadAdd, &dwSliceElapsed))
		TraceTimeBudgetSlice(TimeBudgetDownloadAdd, _T("ProcessChunkedDownloadJobs"), dwSliceElapsed, uProcessedInSlice, m_chunkedDownloadJobs.GetCount());

	if (!m_chunkedDownloadJobs.IsEmpty())
		PostChunkedDownloadJobMessage();
}

void CemuleApp::FailChunkedDownloadJobs(LPCTSTR pszMessage)
{
	m_bChunkedDownloadMessagePending = false;
	SetActiveDownloadAddOperationProgress(0, 0, false);
	while (!m_chunkedDownloadJobs.IsEmpty()) {
		SChunkedDownloadJob *pJob = m_chunkedDownloadJobs.RemoveHead();
		if (pJob != NULL) {
			EndChunkedDownloadJobBulkAdd(*pJob);
			if (!IsClosing()) {
				SBackendCommand command;
				command.m_eType = BackendCommandDownload;
				CopyDownloadCommand(pJob->m_command, command.m_downloadCommand);
				PrepareBackendCommandEnvelope(command, pJob->m_eSource, pJob->m_eOrderingScope, pJob->m_strOrderingKey.IsEmpty() ? (LPCTSTR)_T("download:add") : (LPCTSTR)pJob->m_strOrderingKey);
				command.m_uCancellationToken = pJob->m_uCancellationToken;
				command.m_uSequence = pJob->m_uSequence;
				command.m_uCorrelationId = pJob->m_uCorrelationId;
				QueueBackendCommandFailedEvent(command, pszMessage != NULL ? pszMessage : _T("Download add job failed."));
			}
			delete pJob;
		}
	}
}


void CemuleApp::EnsureApplicationEventEnvelope(SApplicationEvent &event) const
{
	switch (event.m_eType) {
		case ApplicationEventDownloadBatchProgress:
		case ApplicationEventDownloadBatchCompleted:
		case ApplicationEventDownloadStateProgress:
		case ApplicationEventDownloadStateCompleted:
		case ApplicationEventDownloadRemoveProgress:
		case ApplicationEventDownloadRemoveDiskCleanupCompleted:
		case ApplicationEventDownloadRemoveCompleted:
		case ApplicationEventDownloadRemoveItemFailed:
		case ApplicationEventDownloadStateItemFailed:
		case ApplicationEventDownloadProcessLinkRequested:
		case ApplicationEventDownloadRemoveRequested:
		case ApplicationEventDownloadStateRequested:
		case ApplicationEventDownloadListRowsRemoved:
		case ApplicationEventDownloadListDeletedCompletedRowsRemoved:
		case ApplicationEventDownloadListChanged:
			event.m_eBackendCommandType = BackendCommandDownload;
			break;
		case ApplicationEventUploadClientRowsChanged:
		case ApplicationEventUploadClientRowsRemoved:
		case ApplicationEventClientRowUpdateRequested:
		case ApplicationEventUploadQueueListChanged:
		case ApplicationEventUploadListChanged:
		case ApplicationEventUploadBandwidthSnapshotChanged:
		case ApplicationEventUploadDiskIoResult:
		case ApplicationEventClientChatMessage:
		case ApplicationEventClientChatCloseRequested:
		case ApplicationEventClientCaptchaRequested:
		case ApplicationEventClientCaptchaResult:
		case ApplicationEventClientChatConnectingResult:
		case ApplicationEventClientChatConnectionProgress:
			event.m_eBackendCommandType = BackendCommandUpload;
			break;
		case ApplicationEventKadConnectionStateChanged:
		case ApplicationEventKadUiStatusRefresh:
		case ApplicationEventKadSearchCancelUiRequested:
			event.m_eBackendCommandType = BackendCommandNetworkPacket;
			break;
		case ApplicationEventSearchStartRequested:
		case ApplicationEventSearchCancelRequested:
		case ApplicationEventSearchResultsChanged:
		case ApplicationEventSearchPacketParseProgress:
		case ApplicationEventSearchPacketParseCompleted:
		case ApplicationEventSearchPacketParseFailed:
		case ApplicationEventLocalEd2kSearchEnd:
			event.m_eBackendCommandType = BackendCommandSearch;
			break;
		case ApplicationEventCollectionImportRequested:
		case ApplicationEventCollectionImportFailed:
			event.m_eBackendCommandType = BackendCommandCollection;
			break;
		case ApplicationEventAsyncDiskWriteResult:
		case ApplicationEventPersistenceRequested:
		case ApplicationEventPersistenceProgress:
		case ApplicationEventPersistenceCompleted:
		case ApplicationEventPersistenceFailed:
		case ApplicationEventPersistenceWorkRequested:
		case ApplicationEventStartupMetadataStateChanged:
			event.m_eBackendCommandType = BackendCommandPersistence;
			break;
		case ApplicationEventSharedFilesCommandRequested:
		case ApplicationEventSharedFilesListChanged:
		case ApplicationEventSharedFilesCommandProgress:
		case ApplicationEventSharedFilesCommandCompleted:
		case ApplicationEventSharedFilesCommandFailed:
		case ApplicationEventSharedFilesCommandItemFailed:
			event.m_eBackendCommandType = BackendCommandSharedFiles;
			break;
		case ApplicationEventPartFileOwnerStateChanged:
		case ApplicationEventPartFileDiskWriteRequested:
		case ApplicationEventPartFileOwnerFailed:
			event.m_eBackendCommandType = BackendCommandDownload;
			break;
		case ApplicationEventCommandFailed:
			break;
	}

	if (event.m_eBackendCommandSource == BackendCommandSourceUnknown) {
		if (event.m_eType == ApplicationEventSearchResultsChanged || event.m_eType == ApplicationEventSearchPacketParseProgress || event.m_eType == ApplicationEventSearchPacketParseCompleted || event.m_eType == ApplicationEventSearchPacketParseFailed)
			event.m_eBackendCommandSource = BackendCommandSourceNetworkClient;
		else if (event.m_eBackendCommandType == BackendCommandPersistence)
			event.m_eBackendCommandSource = BackendCommandSourcePersistence;
		else if (event.m_eBackendCommandType == BackendCommandNetworkPacket)
			event.m_eBackendCommandSource = BackendCommandSourceNetworkClient;
		else
			event.m_eBackendCommandSource = BackendCommandSourceUi;
	}

	if (event.m_eBackendCommandOrderingScope == BackendCommandOrderingGlobal) {
		switch (event.m_eBackendCommandType) {
			case BackendCommandDownload:
				event.m_eBackendCommandOrderingScope = BackendCommandOrderingDownloadList;
				break;
			case BackendCommandUpload:
				event.m_eBackendCommandOrderingScope = BackendCommandOrderingClient;
				break;
			case BackendCommandSearch:
			case BackendCommandNetworkPacket:
				event.m_eBackendCommandOrderingScope = BackendCommandOrderingSearch;
				break;
			case BackendCommandPersistence:
				event.m_eBackendCommandOrderingScope = BackendCommandOrderingPersistence;
				break;
			case BackendCommandSharedFiles:
				event.m_eBackendCommandOrderingScope = BackendCommandOrderingSharedFiles;
				break;
			case BackendCommandCollection:
				break;
		}
	}

	if (event.m_strBackendCommandOrderingKey.IsEmpty()) {
		switch (event.m_eBackendCommandType) {
			case BackendCommandDownload:
				event.m_strBackendCommandOrderingKey = _T("download-list");
				break;
			case BackendCommandUpload:
				if (event.m_uClientRuntimeID != 0)
					event.m_strBackendCommandOrderingKey.Format(_T("client:%lu"), event.m_uClientRuntimeID);
				else
					event.m_strBackendCommandOrderingKey = _T("client");
				break;
			case BackendCommandSearch:
				if (event.m_uSearchID != 0)
					event.m_strBackendCommandOrderingKey.Format(_T("search:%u"), event.m_uSearchID);
				else
					event.m_strBackendCommandOrderingKey = _T("search");
				break;
			case BackendCommandCollection:
				event.m_strBackendCommandOrderingKey = !event.m_strFilePath.IsEmpty() ? (LPCTSTR)event.m_strFilePath : (LPCTSTR)_T("collection");
				break;
			case BackendCommandPersistence:
				event.m_strBackendCommandOrderingKey.Format(_T("persistence:%u"), static_cast<UINT>(event.m_ePersistenceCommandType));
				break;
			case BackendCommandSharedFiles:
				event.m_strBackendCommandOrderingKey = _T("shared-files");
				break;
			case BackendCommandNetworkPacket:
				event.m_strBackendCommandOrderingKey = _T("network-packet");
				break;
		}
	}

	if (event.m_eBackendCommandFailurePolicy == BackendCommandFailurePolicyUnknown)
		event.m_eBackendCommandFailurePolicy = BackendCommandFailurePolicyReport;
	if (event.m_lBackendCommandGenerationGuard == 0) {
		const LONG lGenerationGuard = static_cast<LONG>(GetBackendLifecycleState()) + 1;
		event.m_lBackendCommandGenerationGuard = lGenerationGuard != 0 ? lGenerationGuard : 1;
	}
	if (event.m_uCancellationToken == 0) {
		if (event.m_uCorrelationId != 0)
			event.m_uCancellationToken = event.m_uCorrelationId;
		else if (event.m_uSequence != 0)
			event.m_uCancellationToken = event.m_uSequence;
		else
			event.m_uCancellationToken = static_cast<uint64>(::GetTickCount()) + 1;
	}

	if (event.m_eBackendCommandFamily == BackendCommandFamilyUnknown && event.m_eType != ApplicationEventCommandFailed) {
		SBackendCommand command;
		command.m_eType = event.m_eBackendCommandType;
		command.m_downloadCommand.m_eType = event.m_eDownloadCommandType;
		command.m_uploadCommand.m_eType = event.m_eUploadCommandType;
		command.m_searchCommand.m_eType = event.m_eSearchCommandType;
		command.m_persistenceCommand.m_eType = event.m_ePersistenceCommandType;
		command.m_sharedFilesCommand.m_eType = event.m_eSharedFilesCommandType;
		event.m_eBackendCommandFamily = GetBackendCommandFamily(command);
	}
}

bool CemuleApp::ValidateApplicationEventEnvelope(const SApplicationEvent &event, CString *pstrStage) const
{
	switch (event.m_eType) {
		case ApplicationEventUploadClientRowsChanged:
		case ApplicationEventUploadClientRowsRemoved:
		case ApplicationEventUploadDiskIoResult:
		case ApplicationEventClientChatMessage:
		case ApplicationEventClientChatCloseRequested:
		case ApplicationEventClientCaptchaRequested:
		case ApplicationEventClientCaptchaResult:
		case ApplicationEventClientChatConnectingResult:
		case ApplicationEventClientChatConnectionProgress:
			if (event.m_uClientRuntimeID == 0 || event.m_lClientRuntimeGeneration == 0) {
				if (pstrStage != NULL)
					*pstrStage = _T("application-event-upload-runtime");
				return false;
			}
			if ((event.m_eType == ApplicationEventUploadClientRowsChanged || event.m_eType == ApplicationEventUploadClientRowsRemoved) && event.m_uUploadTargetFlags == 0) {
				if (pstrStage != NULL)
					*pstrStage = _T("application-event-upload-target");
				return false;
			}
			break;
		case ApplicationEventUploadQueueListChanged:
		case ApplicationEventUploadListChanged:
		case ApplicationEventUploadBandwidthSnapshotChanged:
		case ApplicationEventCommandFailed:
		case ApplicationEventDownloadBatchProgress:
		case ApplicationEventDownloadBatchCompleted:
		case ApplicationEventSearchStartRequested:
		case ApplicationEventSearchCancelRequested:
		case ApplicationEventCollectionImportRequested:
		case ApplicationEventDownloadStateProgress:
		case ApplicationEventDownloadStateCompleted:
		case ApplicationEventDownloadRemoveProgress:
		case ApplicationEventDownloadRemoveDiskCleanupCompleted:
		case ApplicationEventDownloadRemoveCompleted:
		case ApplicationEventDownloadRemoveItemFailed:
		case ApplicationEventDownloadStateItemFailed:
		case ApplicationEventDownloadProcessLinkRequested:
		case ApplicationEventDownloadRemoveRequested:
		case ApplicationEventDownloadStateRequested:
		case ApplicationEventDownloadListRowsRemoved:
		case ApplicationEventDownloadListDeletedCompletedRowsRemoved:
		case ApplicationEventDownloadListChanged:
		case ApplicationEventBulkOperationOverlayRefresh:
		case ApplicationEventSearchResultsChanged:
		case ApplicationEventSearchPacketParseProgress:
		case ApplicationEventSearchPacketParseCompleted:
		case ApplicationEventSearchPacketParseFailed:
		case ApplicationEventLocalEd2kSearchEnd:
		case ApplicationEventCollectionImportFailed:
		case ApplicationEventAsyncDiskWriteResult:
		case ApplicationEventPersistenceRequested:
		case ApplicationEventPersistenceProgress:
		case ApplicationEventPersistenceCompleted:
		case ApplicationEventPersistenceFailed:
		case ApplicationEventPersistenceWorkRequested:
		case ApplicationEventStartupMetadataStateChanged:
		case ApplicationEventKadConnectionStateChanged:
		case ApplicationEventKadUiStatusRefresh:
		case ApplicationEventSharedFilesListChanged:
		case ApplicationEventSharedFilesCommandRequested:
		case ApplicationEventSharedFilesCommandProgress:
		case ApplicationEventSharedFilesCommandCompleted:
		case ApplicationEventSharedFilesCommandFailed:
		case ApplicationEventSharedFilesCommandItemFailed:
			break;
		case ApplicationEventPartFileOwnerStateChanged:
		case ApplicationEventPartFileDiskWriteRequested:
		case ApplicationEventPartFileOwnerFailed:
			break;
		case ApplicationEventKadSearchCancelUiRequested:
			if (event.m_uSearchID == 0) {
				if (pstrStage != NULL)
					*pstrStage = _T("application-event-kad-search-id");
				return false;
			}
			break;
		case ApplicationEventClientRowUpdateRequested:
			if (event.m_uClientRuntimeID == 0 || event.m_lClientRuntimeGeneration == 0) {
				if (pstrStage != NULL)
					*pstrStage = _T("application-event-client-runtime");
				return false;
			}
			break;
		default:
			if (pstrStage != NULL)
				*pstrStage = _T("application-event-type");
			return false;
	}

	if (event.m_eBackendCommandSource == BackendCommandSourceUnknown) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-source");
		return false;
	}

	if (event.m_eBackendCommandOrderingScope < BackendCommandOrderingGlobal || event.m_eBackendCommandOrderingScope > BackendCommandOrderingDiskIo) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-scope");
		return false;
	}

	if (event.m_strBackendCommandOrderingKey.IsEmpty()) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-ordering-key");
		return false;
	}

	if (event.m_lBackendCommandGenerationGuard == 0) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-generation");
		return false;
	}

	if (event.m_uCancellationToken == 0) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-cancellation-token");
		return false;
	}

	if (event.m_eBackendCommandFailurePolicy == BackendCommandFailurePolicyUnknown) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-failure-policy");
		return false;
	}

	if (event.m_eBackendCommandFamily == BackendCommandFamilyUnknown) {
		if (event.m_eType != ApplicationEventCommandFailed) {
			if (pstrStage != NULL)
				*pstrStage = _T("application-event-family");
			return false;
		}
		return true;
	}

	const SBackendCommandContract *pContract = FindBackendCommandContract(event.m_eBackendCommandFamily);
	if (pContract == NULL) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-family-contract");
		return false;
	}

	if (pContract->m_eType != event.m_eBackendCommandType) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-family-type");
		return false;
	}

	const SBackendCommandReadiness *pReadiness = FindBackendCommandReadiness(event.m_eBackendCommandFamily);
	if (pReadiness == NULL || !IsBackendCommandReadinessConsistent(*pReadiness)) {
		if (pstrStage != NULL)
			*pstrStage = _T("application-event-family-readiness");
		return false;
	}

	return true;
}

bool CemuleApp::QueueApplicationEvent(const SApplicationEvent &event)
{
	return QueueApplicationEvent(event, NULL);
}

bool CemuleApp::QueueApplicationEvent(const SApplicationEvent &event, bool *pbEventOwnedByQueue)
{
	SApplicationEvent preparedEvent(event);
	if (pbEventOwnedByQueue != NULL)
		*pbEventOwnedByQueue = false;
	EnsureApplicationEventEnvelope(preparedEvent);
	CString strValidationStage;
	if (!ValidateApplicationEventEnvelope(preparedEvent, &strValidationStage)) {
		AddDebugLogLine(DLP_HIGH, false, _T("Application event rejected by envelope contract. event=%u stage=%s source=%s scope=%s key=%s sequence=%I64u correlation=%I64u token=%I64u\n"),
			static_cast<UINT>(preparedEvent.m_eType), (LPCTSTR)strValidationStage, GetBackendCommandSourceName(preparedEvent.m_eBackendCommandSource),
			GetBackendCommandOrderingScopeName(preparedEvent.m_eBackendCommandOrderingScope), (LPCTSTR)preparedEvent.m_strBackendCommandOrderingKey,
			preparedEvent.m_uSequence, preparedEvent.m_uCorrelationId, preparedEvent.m_uCancellationToken);
		return false;
	}

	if (!ShouldAcceptApplicationEvent(preparedEvent))
		return false;

	bool bQueued = true;
	bool bDroppedByPressure = false;
	bool bEventOwnedByQueue = false;
	{
		CSingleLock lock(&m_applicationEventQueueLock, TRUE);
		if (!CoalesceApplicationEventLocked(preparedEvent)) {
			if (m_applicationEventQueue.GetCount() >= 2048 && !TrimApplicationEventQueueForPressureLocked(preparedEvent, 2048)) {
				if (IsApplicationEventDroppableUnderPressure(preparedEvent))
					bDroppedByPressure = true;
				else if (m_applicationEventQueue.GetCount() >= 4096) {
					bQueued = false;
					bDroppedByPressure = true;
				}
			}
			if (!bDroppedByPressure && bQueued) {
				m_applicationEventQueue.AddTail(preparedEvent);
				bEventOwnedByQueue = true;
			}
		}
	}
	if (pbEventOwnedByQueue != NULL)
		*pbEventOwnedByQueue = bEventOwnedByQueue;

	if (bDroppedByPressure) {
		if (emuledlg == NULL || !emuledlg->IsStartupLoadingDialogVisible())
			AddDebugLogLine(DLP_HIGH, false, _T("Application event dropped by queue pressure. event=%u source=%s scope=%s key=%s\n"), static_cast<UINT>(preparedEvent.m_eType), GetBackendCommandSourceName(preparedEvent.m_eBackendCommandSource), GetBackendCommandOrderingScopeName(preparedEvent.m_eBackendCommandOrderingScope), (LPCTSTR)preparedEvent.m_strBackendCommandOrderingKey);
		return bQueued;
	}

	if (!PostApplicationEventMessage()) {
		const bool bOnUiThread = g_uMainThreadId != 0 && ::GetCurrentThreadId() == g_uMainThreadId;
		const bool bApplicationEventDispatchingOnUi = bOnUiThread && m_bApplicationEventDispatching;
		if (bOnUiThread && !bApplicationEventDispatchingOnUi)
			ProcessApplicationEventsFromUiThread();
		else if (bApplicationEventDispatchingOnUi)
			AddDebugLogLine(DLP_VERYLOW, false, _T("Application event queued for the active UI dispatcher slice. event=%u\n"), static_cast<UINT>(preparedEvent.m_eType));
		else {
			AddDebugLogLine(DLP_LOW, false, _T("Application event queued without an available UI dispatcher. event=%u dispatching=%u\n"), static_cast<UINT>(preparedEvent.m_eType), bApplicationEventDispatchingOnUi ? 1U : 0U);
			ClearApplicationEventQueue();
			bQueued = false;
		}
	}
	return bQueued;
}

bool CemuleApp::PostApplicationEventMessage()
{
	if (IsBackendLifecycleStopping() || IsClosing())
		return false;
	if (emuledlg == NULL || !::IsWindow(emuledlg->m_hWnd))
		return false;
	if (::InterlockedCompareExchange(&m_lApplicationEventMessagePending, 1, 0) != 0)
		return true;

	const BOOL bPosted = emuledlg->PostMessage(CemuleDlg::UWM_EMULEAI_DISPATCH_APPLICATION_EVENT);
	if (bPosted == FALSE) {
		::InterlockedExchange(&m_lApplicationEventMessagePending, 0);
		AddDebugLogLine(DLP_HIGH, false, _T("Application event UI post failed. hwnd=%p error=%lu\n"), emuledlg->m_hWnd, ::GetLastError());
		return false;
	}
	return true;
}

bool CemuleApp::CoalesceApplicationEventLocked(const SApplicationEvent &event)
{
	if (event.m_eType == ApplicationEventSearchResultsChanged) {
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType == event.m_eType && queuedEvent.m_uSearchID == event.m_uSearchID && queuedEvent.m_strMessage == event.m_strMessage) {
				queuedEvent.m_eBackendCommandType = event.m_eBackendCommandType;
				queuedEvent.m_eBackendCommandFamily = event.m_eBackendCommandFamily;
				queuedEvent.m_eBackendCommandSource = event.m_eBackendCommandSource;
				queuedEvent.m_eBackendCommandOrderingScope = event.m_eBackendCommandOrderingScope;
				queuedEvent.m_eBackendCommandFailurePolicy = event.m_eBackendCommandFailurePolicy;
				queuedEvent.m_lBackendCommandGenerationGuard = event.m_lBackendCommandGenerationGuard;
				queuedEvent.m_strBackendCommandOrderingKey = event.m_strBackendCommandOrderingKey;
				queuedEvent.m_uSequence = event.m_uSequence;
				queuedEvent.m_uCorrelationId = event.m_uCorrelationId;
				queuedEvent.m_uCancellationToken = event.m_uCancellationToken;
				queuedEvent.m_lSearchGeneration = event.m_lSearchGeneration;
				queuedEvent.m_bUseKadReloadThrottle = queuedEvent.m_bUseKadReloadThrottle || event.m_bUseKadReloadThrottle;
				queuedEvent.m_strMessage = event.m_strMessage;
				return true;
			}
		}
		return false;
	}

	if (event.m_eType == ApplicationEventDownloadRemoveDiskCleanupCompleted) {
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType == event.m_eType && queuedEvent.m_uSequence == event.m_uSequence && queuedEvent.m_uCorrelationId == event.m_uCorrelationId) {
				queuedEvent.m_uProcessed += event.m_uProcessed;
				queuedEvent.m_uFailed += event.m_uFailed;
				queuedEvent.m_uStale += event.m_uStale;
				queuedEvent.m_uTotal += event.m_uTotal;
				return true;
			}
		}
		return false;
	}

	if (event.m_eType == ApplicationEventDownloadListRowsRemoved || event.m_eType == ApplicationEventDownloadListDeletedCompletedRowsRemoved) {
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType == event.m_eType) {
				queuedEvent.m_vecItemHashes.insert(queuedEvent.m_vecItemHashes.end(), event.m_vecItemHashes.begin(), event.m_vecItemHashes.end());
				return true;
			}
		}
		return false;
	}

	if (event.m_eType == ApplicationEventDownloadListChanged || event.m_eType == ApplicationEventBulkOperationOverlayRefresh) {
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType == event.m_eType) {
				queuedEvent.m_strMessage = event.m_strMessage;
				return true;
			}
		}
		return false;
	}

	if (event.m_eType == ApplicationEventDownloadRemoveCompleted || event.m_eType == ApplicationEventDownloadStateCompleted || event.m_eType == ApplicationEventSharedFilesCommandCompleted) {
		const EApplicationEventType eProgressType = event.m_eType == ApplicationEventDownloadRemoveCompleted ? ApplicationEventDownloadRemoveProgress : (event.m_eType == ApplicationEventDownloadStateCompleted ? ApplicationEventDownloadStateProgress : ApplicationEventSharedFilesCommandProgress);
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			POSITION posCurrent = pos;
			const SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType == eProgressType && queuedEvent.m_uSequence == event.m_uSequence && queuedEvent.m_uCorrelationId == event.m_uCorrelationId && queuedEvent.m_uAction == event.m_uAction && queuedEvent.m_uSearchID == event.m_uSearchID)
				m_applicationEventQueue.RemoveAt(posCurrent);
		}
		return false;
	}

	if (event.m_eType == ApplicationEventDownloadRemoveProgress || event.m_eType == ApplicationEventDownloadStateProgress || event.m_eType == ApplicationEventSharedFilesCommandProgress) {
		const EApplicationEventType eCompletedType = event.m_eType == ApplicationEventDownloadRemoveProgress ? ApplicationEventDownloadRemoveCompleted : (event.m_eType == ApplicationEventDownloadStateProgress ? ApplicationEventDownloadStateCompleted : ApplicationEventSharedFilesCommandCompleted);
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			const SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType == eCompletedType && queuedEvent.m_uSequence == event.m_uSequence && queuedEvent.m_uCorrelationId == event.m_uCorrelationId && queuedEvent.m_uAction == event.m_uAction && queuedEvent.m_uSearchID == event.m_uSearchID)
				return true;
		}
	}

	if (event.m_eType == ApplicationEventSharedFilesCommandProgress || event.m_eType == ApplicationEventDownloadBatchProgress || event.m_eType == ApplicationEventDownloadRemoveProgress ||
		event.m_eType == ApplicationEventDownloadStateProgress || event.m_eType == ApplicationEventSearchPacketParseProgress) {
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType == event.m_eType && queuedEvent.m_uCorrelationId == event.m_uCorrelationId && queuedEvent.m_uAction == event.m_uAction && queuedEvent.m_uSearchID == event.m_uSearchID) {
				queuedEvent.m_eBackendCommandType = event.m_eBackendCommandType;
				queuedEvent.m_eBackendCommandFamily = event.m_eBackendCommandFamily;
				queuedEvent.m_eBackendCommandSource = event.m_eBackendCommandSource;
				queuedEvent.m_eBackendCommandOrderingScope = event.m_eBackendCommandOrderingScope;
				queuedEvent.m_eBackendCommandFailureKind = event.m_eBackendCommandFailureKind;
				queuedEvent.m_eBackendCommandFailurePolicy = event.m_eBackendCommandFailurePolicy;
				queuedEvent.m_lBackendCommandGenerationGuard = event.m_lBackendCommandGenerationGuard;
				queuedEvent.m_strBackendCommandOrderingKey = event.m_strBackendCommandOrderingKey;
				queuedEvent.m_uSequence = event.m_uSequence;
				queuedEvent.m_uCorrelationId = event.m_uCorrelationId;
				queuedEvent.m_uCancellationToken = event.m_uCancellationToken;
				queuedEvent.m_uProcessed = event.m_uProcessed;
				queuedEvent.m_uFailed = event.m_uFailed;
				queuedEvent.m_uStale = event.m_uStale;
				queuedEvent.m_uTotal = event.m_uTotal;
			queuedEvent.m_strMessage = event.m_strMessage;
				return true;
			}
		}
		return false;
	}

	if (event.m_eType == ApplicationEventKadUiStatusRefresh) {
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType != event.m_eType)
				continue;
			queuedEvent.m_eBackendCommandType = event.m_eBackendCommandType;
			queuedEvent.m_eBackendCommandFamily = event.m_eBackendCommandFamily;
			queuedEvent.m_eBackendCommandSource = event.m_eBackendCommandSource;
			queuedEvent.m_eBackendCommandOrderingScope = event.m_eBackendCommandOrderingScope;
			queuedEvent.m_eBackendCommandFailurePolicy = event.m_eBackendCommandFailurePolicy;
			queuedEvent.m_lBackendCommandGenerationGuard = event.m_lBackendCommandGenerationGuard;
			queuedEvent.m_strBackendCommandOrderingKey = event.m_strBackendCommandOrderingKey;
			queuedEvent.m_uSequence = event.m_uSequence;
			queuedEvent.m_uCorrelationId = event.m_uCorrelationId;
			queuedEvent.m_uCancellationToken = event.m_uCancellationToken;
			queuedEvent.m_uAction |= event.m_uAction;
			queuedEvent.m_strMessage = event.m_strMessage;
			return true;
		}
		return false;
	}

	if (event.m_eType == ApplicationEventSharedFilesListChanged) {
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType != event.m_eType)
				continue;
			queuedEvent.m_eBackendCommandType = event.m_eBackendCommandType;
			queuedEvent.m_eBackendCommandFamily = event.m_eBackendCommandFamily;
			queuedEvent.m_eBackendCommandSource = event.m_eBackendCommandSource;
			queuedEvent.m_eBackendCommandOrderingScope = event.m_eBackendCommandOrderingScope;
			queuedEvent.m_eBackendCommandFailurePolicy = event.m_eBackendCommandFailurePolicy;
			queuedEvent.m_lBackendCommandGenerationGuard = event.m_lBackendCommandGenerationGuard;
			queuedEvent.m_strBackendCommandOrderingKey = event.m_strBackendCommandOrderingKey;
			queuedEvent.m_uSequence = event.m_uSequence;
			queuedEvent.m_uCorrelationId = event.m_uCorrelationId;
			queuedEvent.m_uCancellationToken = event.m_uCancellationToken;
			const bool bQueuedUpdateOnly = queuedEvent.m_strMessage == _T("shared-file-updated") || queuedEvent.m_strMessage == _T("shared-metadata-updated");
			const bool bNewUpdateOnly = event.m_strMessage == _T("shared-file-updated") || event.m_strMessage == _T("shared-metadata-updated");
			if (!bNewUpdateOnly || bQueuedUpdateOnly)
				queuedEvent.m_strMessage = event.m_strMessage;
			return true;
		}
		return false;
	}

	if (event.m_eType == ApplicationEventUploadQueueListChanged || event.m_eType == ApplicationEventUploadListChanged || event.m_eType == ApplicationEventUploadBandwidthSnapshotChanged) {
		for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
			SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
			if (queuedEvent.m_eType != event.m_eType)
				continue;
			queuedEvent.m_eBackendCommandType = event.m_eBackendCommandType;
			queuedEvent.m_eBackendCommandFamily = event.m_eBackendCommandFamily;
			queuedEvent.m_eBackendCommandSource = event.m_eBackendCommandSource;
			queuedEvent.m_eBackendCommandOrderingScope = event.m_eBackendCommandOrderingScope;
			queuedEvent.m_eBackendCommandFailurePolicy = event.m_eBackendCommandFailurePolicy;
			queuedEvent.m_lBackendCommandGenerationGuard = event.m_lBackendCommandGenerationGuard;
			queuedEvent.m_strBackendCommandOrderingKey = event.m_strBackendCommandOrderingKey;
			queuedEvent.m_uSequence = event.m_uSequence;
			queuedEvent.m_uCorrelationId = event.m_uCorrelationId;
			queuedEvent.m_uCancellationToken = event.m_uCancellationToken;
			queuedEvent.m_uUploadTargetFlags |= event.m_uUploadTargetFlags;
			queuedEvent.m_uUploadWaitingCount = event.m_uUploadWaitingCount;
			queuedEvent.m_uUploadUploadingCount = event.m_uUploadUploadingCount;
			queuedEvent.m_uUploadActiveCount = event.m_uUploadActiveCount;
			queuedEvent.m_uUploadDataRate = event.m_uUploadDataRate;
			queuedEvent.m_uUploadToNetworkDataRate = event.m_uUploadToNetworkDataRate;
			queuedEvent.m_strMessage = event.m_strMessage;
			return true;
		}
		return false;
	}

	if ((event.m_eType != ApplicationEventUploadClientRowsChanged && event.m_eType != ApplicationEventUploadClientRowsRemoved && event.m_eType != ApplicationEventClientRowUpdateRequested) || event.m_uClientRuntimeID == 0 || event.m_lClientRuntimeGeneration == 0)
		return false;

	for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
		SApplicationEvent &queuedEvent = m_applicationEventQueue.GetNext(pos);
		if (queuedEvent.m_eType != event.m_eType || queuedEvent.m_uClientRuntimeID != event.m_uClientRuntimeID || queuedEvent.m_lClientRuntimeGeneration != event.m_lClientRuntimeGeneration)
			continue;
		queuedEvent.m_eBackendCommandType = event.m_eBackendCommandType;
		queuedEvent.m_eBackendCommandFamily = event.m_eBackendCommandFamily;
		queuedEvent.m_eBackendCommandSource = event.m_eBackendCommandSource;
		queuedEvent.m_eBackendCommandOrderingScope = event.m_eBackendCommandOrderingScope;
		queuedEvent.m_eBackendCommandFailurePolicy = event.m_eBackendCommandFailurePolicy;
		queuedEvent.m_lBackendCommandGenerationGuard = event.m_lBackendCommandGenerationGuard;
		queuedEvent.m_strBackendCommandOrderingKey = event.m_strBackendCommandOrderingKey;
		queuedEvent.m_uSequence = event.m_uSequence;
		queuedEvent.m_uCorrelationId = event.m_uCorrelationId;
		queuedEvent.m_uCancellationToken = event.m_uCancellationToken;
		queuedEvent.m_uUploadTargetFlags |= event.m_uUploadTargetFlags;
		if (!event.m_strMessage.IsEmpty())
			queuedEvent.m_strMessage = event.m_strMessage;
		return true;
	}
	return false;
}

void CemuleApp::ClearApplicationEventQueue()
{
	::InterlockedExchange(&m_lApplicationEventMessagePending, 0);
	CSingleLock lock(&m_applicationEventQueueLock, TRUE);
	for (POSITION pos = m_applicationEventQueue.GetHeadPosition(); pos != NULL;) {
		const SApplicationEvent &event = m_applicationEventQueue.GetNext(pos);
		if (event.m_eType == ApplicationEventClientCaptchaRequested && event.m_hClientBitmap != NULL)
			::DeleteObject(event.m_hClientBitmap);
	}
	m_applicationEventQueue.RemoveAll();
}

void CemuleApp::ProcessApplicationEventsFromUiThread()
{
	if (g_uMainThreadId != 0 && ::GetCurrentThreadId() != g_uMainThreadId) {
		PostApplicationEventMessage();
		return;
	}

	::InterlockedExchange(&m_lApplicationEventMessagePending, 0);
	if (IsClosing()) {
		ClearApplicationEventQueue();
		return;
	}

	if (m_bApplicationEventDispatching) {
		static volatile LONG s_lLastApplicationEventReentryTraceTick = 0;
		const DWORD dwNow = ::GetTickCount();
		if (ShouldTraceRateLimited(s_lLastApplicationEventReentryTraceTick, dwNow, 5000) && thePrefs.GetLogUiResponsivenessEvents())
			AddDebugLogLine(DLP_LOW, false, _T("Application event dispatcher re-entry deferred. current=%lu ui=%u\n"), ::GetCurrentThreadId(), g_uMainThreadId);
		{
			CSingleLock lock(&m_applicationEventQueueLock, TRUE);
			if (!m_applicationEventQueue.IsEmpty())
				::InterlockedExchange(&m_lApplicationEventMessagePending, 1);
		}
		// The active dispatcher owns the queue until it unwinds.
		return;
	}

	CScopedBoolReset scopedApplicationEventDispatch(m_bApplicationEventDispatching, true);
	const DWORD dwSliceStart = ::GetTickCount();
	UINT uProcessedInSlice = 0;
	for (;;) {
		SApplicationEvent event;
		{
			CSingleLock lock(&m_applicationEventQueueLock, TRUE);
			if (m_applicationEventQueue.IsEmpty())
				break;
			event = m_applicationEventQueue.RemoveHead();
		}

		DispatchApplicationEvent(event);

		++uProcessedInSlice;
		const bool bStartupLoadingVisible = emuledlg != NULL && emuledlg->IsStartupLoadingDialogVisible();
		if (bStartupLoadingVisible) {
			const UINT uQueueStatus = HIWORD(::GetQueueStatus(QS_KEY | QS_MOUSE | QS_PAINT | QS_TIMER | QS_POSTMESSAGE));
			if (uProcessedInSlice >= 2 || (uProcessedInSlice != 0 && (uQueueStatus & (QS_KEY | QS_MOUSE | QS_PAINT | QS_TIMER)) != 0))
				break;
		}
		if (uProcessedInSlice != 0 && IsTimeBudgetExceeded(dwSliceStart, TimeBudgetApplicationEventDispatch))
			break;
	}

	DWORD dwSliceElapsed = 0;
	bool bHasMoreEvents = false;
	INT_PTR iRemainingEvents = 0;
	{
		CSingleLock lock(&m_applicationEventQueueLock, TRUE);
		bHasMoreEvents = !m_applicationEventQueue.IsEmpty();
		iRemainingEvents = m_applicationEventQueue.GetCount();
	}
	if (IsTimeBudgetHardExceeded(dwSliceStart, TimeBudgetApplicationEventDispatch, &dwSliceElapsed))
		TraceTimeBudgetSlice(TimeBudgetApplicationEventDispatch, _T("ProcessApplicationEventsFromUiThread"), dwSliceElapsed, uProcessedInSlice, iRemainingEvents);

	if (bHasMoreEvents) {
		::InterlockedExchange(&m_lApplicationEventMessagePending, 0);
		if (!PostApplicationEventMessage())
			ClearApplicationEventQueue();
	}
	else {
		::InterlockedExchange(&m_lApplicationEventMessagePending, 0);
		{
			CSingleLock lock(&m_applicationEventQueueLock, TRUE);
			bHasMoreEvents = !m_applicationEventQueue.IsEmpty();
		}
		if (bHasMoreEvents && !PostApplicationEventMessage())
			ClearApplicationEventQueue();
	}
}

void CemuleApp::DispatchApplicationEvent(const SApplicationEvent &event)
{
	if (g_uMainThreadId != 0 && ::GetCurrentThreadId() != g_uMainThreadId) {
		if (!QueueApplicationEvent(event))
			AddDebugLogLine(DLP_LOW, false, _T("Application event dispatch skipped because UI thread is unavailable. Event=%u\n"), static_cast<UINT>(event.m_eType));
		return;
	}

	if (!IsApplicationEventDispatchAllowed(event)) {
		AddDebugLogLine(DLP_HIGH, false, _T("Application event dispatch blocked by dispatch contract. event=%u domain=%s current=%lu ui=%u sequence=%I64u correlation=%I64u\n"),
			static_cast<UINT>(event.m_eType), GetApplicationEventDispatchDomainName(GetApplicationEventDispatchDomain(event.m_eType)), ::GetCurrentThreadId(), g_uMainThreadId, event.m_uSequence, event.m_uCorrelationId);
		if (event.m_eType == ApplicationEventClientCaptchaRequested && event.m_hClientBitmap != NULL)
			::DeleteObject(event.m_hClientBitmap);
		return;
	}

	switch (GetApplicationEventDispatchDomain(event.m_eType)) {
		case ApplicationEventDispatchTelemetry:
			DispatchTelemetryApplicationEvent(event);
			return;
		case ApplicationEventDispatchUiNotification:
			DispatchUiNotificationApplicationEvent(event);
			return;
		case ApplicationEventDispatchUiCommandBridge:
			DispatchUiCommandBridgeApplicationEvent(event);
			return;
		case ApplicationEventDispatchBackendResult:
			DispatchBackendResultApplicationEvent(event);
			return;
	}

	AddDebugLogLine(DLP_LOW, false, _T("Application event dispatch skipped because the dispatch domain is unknown. event=%u sequence=%I64u correlation=%I64u\n"), static_cast<UINT>(event.m_eType), event.m_uSequence, event.m_uCorrelationId);
}

void CemuleApp::DispatchTelemetryApplicationEvent(const SApplicationEvent &event)
{
	AddDebugLogLine(DLP_VERYLOW, false, _T("Telemetry application event observed. event=%u command=%u family=%s source=%s scope=%s key=%s sequence=%I64u correlation=%I64u token=%I64u\n"),
		static_cast<UINT>(event.m_eType), static_cast<UINT>(event.m_eBackendCommandType), GetBackendCommandFamilyName(event.m_eBackendCommandFamily),
		GetBackendCommandSourceName(event.m_eBackendCommandSource), GetBackendCommandOrderingScopeName(event.m_eBackendCommandOrderingScope),
		(LPCTSTR)event.m_strBackendCommandOrderingKey, event.m_uSequence, event.m_uCorrelationId, event.m_uCancellationToken);
}

void CemuleApp::DispatchBackendResultApplicationEvent(const SApplicationEvent &event)
{
	if (event.m_eType == ApplicationEventCommandFailed) {
		AddDebugLogLine(DLP_HIGH, false, _T("Backend command failed. command=%u sequence=%I64u correlation=%I64u message=%s\n"), static_cast<UINT>(event.m_eBackendCommandType), event.m_uSequence, event.m_uCorrelationId, (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventCollectionImportFailed) {
		AddDebugLogLine(DLP_HIGH, false, _T("Collection import failed. stage=%s error=%lu path=%s\n"), (LPCTSTR)event.m_strMessage, event.m_dwLastError, (LPCTSTR)event.m_strFilePath);
		return;
	}

	if (event.m_eType == ApplicationEventUploadDiskIoResult) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("Upload disk IO result. runtime=%lu generation=%ld stage=%s error=%lu sequence=%I64u correlation=%I64u\n"), event.m_uClientRuntimeID, event.m_lClientRuntimeGeneration, (LPCTSTR)event.m_strMessage, event.m_dwLastError, event.m_uSequence, event.m_uCorrelationId);
		return;
	}

	if (event.m_eType == ApplicationEventAsyncDiskWriteResult) {
		AddDebugLogLine(DLP_LOW, false, _T("[AsyncDiskWriteEvent] name=\"%s\" generation=%ld result=%s reason=%s error=%lu shutdownFallback=%u temp=\"%s\" final=\"%s\"\n"),
			(LPCTSTR)event.m_strAsyncName, event.m_lAsyncGeneration, (LPCTSTR)event.m_strAsyncResult, (LPCTSTR)event.m_strAsyncReason, event.m_dwLastError,
			event.m_bAsyncShutdownFallback ? 1U : 0U, (LPCTSTR)event.m_strAsyncTempPath, (LPCTSTR)event.m_strFilePath);
		if (event.m_strAsyncResult.CompareNoCase(_T("failed")) == 0 || event.m_strAsyncResult.CompareNoCase(_T("warning")) == 0)
			QueueDebugLogLine(false, _T("[AsyncDiskWrite] %s %s: %s (%s)"), (LPCTSTR)event.m_strAsyncName, (LPCTSTR)event.m_strAsyncResult, (LPCTSTR)event.m_strAsyncReason, (LPCTSTR)event.m_strFilePath);
		if (IsServerMetAsyncDiskWriteFailure(event)) {
			const CString strDetail = BuildAsyncDiskWriteFailureDetail(event);
			if (!strDetail.IsEmpty())
				LogError(LOG_STATUSBAR, _T("%s - %s"), (LPCTSTR)GetResString(_T("ERR_SAVESERVERMET2")), (LPCTSTR)EscPercent(strDetail));
			else
				LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)GetResString(_T("ERR_SAVESERVERMET2")));
		}
		return;
	}

	if (event.m_eType == ApplicationEventPartFileOwnerStateChanged || event.m_eType == ApplicationEventPartFileDiskWriteRequested || event.m_eType == ApplicationEventPartFileOwnerFailed) {
		if (event.m_eType == ApplicationEventPartFileOwnerStateChanged && event.m_strMessage == _T("display-info-changed")) {
			RefreshDownloadItemFromOwnerEvent(event.m_strFileHash, true);
			return;
		}
		const bool bStartupLoadingVisible = emuledlg != NULL && emuledlg->IsStartupLoadingDialogVisible();
		if (event.m_eType == ApplicationEventPartFileOwnerFailed)
			AddDebugLogLine(DLP_HIGH, false, _T("Part file owner event. event=%u hash=%s runtime=%lu stage=%s error=%lu\n"), static_cast<UINT>(event.m_eType), (LPCTSTR)event.m_strFileHash, event.m_uAction, (LPCTSTR)event.m_strMessage, event.m_dwLastError);
		else if (!bStartupLoadingVisible)
			AddDebugLogLine(DLP_VERYLOW, false, _T("Part file owner event. event=%u hash=%s runtime=%lu stage=%s error=%lu\n"), static_cast<UINT>(event.m_eType), (LPCTSTR)event.m_strFileHash, event.m_uAction, (LPCTSTR)event.m_strMessage, event.m_dwLastError);
		return;
	}

	if (event.m_eType == ApplicationEventSharedFilesCommandProgress) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("Shared files command progress. action=%u sequence=%I64u correlation=%I64u processed=%u failed=%u stale=%u total=%u\n"), event.m_uAction, event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed, event.m_uStale, event.m_uTotal);
		return;
	}

	if (event.m_eType == ApplicationEventSharedFilesCommandItemFailed) {
		AddDebugLogLine(DLP_HIGH, false, _T("Shared files command item failed. action=%u sequence=%I64u correlation=%I64u error=%lu stage=%s path=%s\n"), event.m_uAction, event.m_uSequence, event.m_uCorrelationId, event.m_dwLastError, (LPCTSTR)event.m_strMessage, (LPCTSTR)event.m_strFilePath);
		return;
	}

	if (event.m_eType == ApplicationEventSharedFilesCommandCompleted) {
		AddDebugLogLine(DLP_LOW, false, _T("Shared files command completed. action=%u sequence=%I64u correlation=%I64u processed=%u failed=%u stale=%u total=%u requested=%u stage=%s\n"), event.m_uAction, event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed, event.m_uStale, event.m_uTotal, static_cast<UINT>(event.m_vecItemHashes.size()), (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventSharedFilesCommandFailed) {
		AddDebugLogLine(DLP_HIGH, false, _T("Shared files command failed. action=%u sequence=%I64u correlation=%I64u error=%lu total=%u stage=%s\n"), event.m_uAction, event.m_uSequence, event.m_uCorrelationId, event.m_dwLastError, static_cast<UINT>(event.m_vecItemHashes.size()), (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventStartupMetadataStateChanged) {
		const bool bStartupLoadingVisible = emuledlg != NULL && emuledlg->IsStartupLoadingDialogVisible();
		if (!bStartupLoadingVisible || event.m_dwLastError != 0) {
			AddDebugLogLine(DLP_LOW, false, _T("Startup metadata event. domain=%s state=%s generation=%ld token=%I64u error=%lu reason=%s\n"),
				GetStartupMetadataDomainName(event.m_eStartupMetadataDomain), GetStartupMetadataStateName(event.m_eStartupMetadataState),
				event.m_lStartupMetadataGeneration, event.m_uCancellationToken, event.m_dwLastError, (LPCTSTR)event.m_strMessage);
		}
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	if (event.m_eType == ApplicationEventPersistenceProgress) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("Persistence command progress. command=%u auto=%u sequence=%I64u correlation=%I64u processed=%u failed=%u total=%u stage=%s\n"), static_cast<UINT>(event.m_ePersistenceCommandType), event.m_bAutoSave ? 1U : 0U, event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed, event.m_uTotal, (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventPersistenceCompleted) {
		AddDebugLogLine(DLP_LOW, false, _T("Persistence command completed. command=%u auto=%u sequence=%I64u correlation=%I64u stage=%s\n"), static_cast<UINT>(event.m_ePersistenceCommandType), event.m_bAutoSave ? 1U : 0U, event.m_uSequence, event.m_uCorrelationId, (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventPersistenceFailed) {
		AddDebugLogLine(DLP_HIGH, false, _T("Persistence command failed. command=%u auto=%u sequence=%I64u correlation=%I64u error=%lu stage=%s\n"), static_cast<UINT>(event.m_ePersistenceCommandType), event.m_bAutoSave ? 1U : 0U, event.m_uSequence, event.m_uCorrelationId, event.m_dwLastError, (LPCTSTR)event.m_strMessage);
		QueueDebugLogLine(false, _T("[Persistence] Save command failed: %s"), (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadBatchProgress) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("Download batch progress. Command=%u sequence=%I64u correlation=%I64u processed=%u failed=%u total=%u\n"), static_cast<UINT>(event.m_eDownloadCommandType), event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed, event.m_uTotal);
		if (event.m_eDownloadCommandType == DownloadCommandAddFileLinks)
			UpdateDownloadAddMirroredOverlay(min(event.m_uProcessed + event.m_uFailed, event.m_uTotal), event.m_uTotal);
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	if (event.m_eType == ApplicationEventDownloadBatchCompleted) {
		AddDebugLogLine(DLP_LOW, false, _T("Download batch completed. Command=%u sequence=%I64u correlation=%I64u processed=%u failed=%u total=%u\n"), static_cast<UINT>(event.m_eDownloadCommandType), event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed, event.m_uTotal);
		if (event.m_eDownloadCommandType == DownloadCommandAddFileLinks)
			UpdateDownloadAddMirroredOverlay(0, 0);
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	if (event.m_eType == ApplicationEventSearchPacketParseProgress) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("Search packet parse progress. search=%u processed=%u failed=%u total=%u stage=%s\n"), event.m_uSearchID, event.m_uProcessed, event.m_uFailed, event.m_uTotal, (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventSearchPacketParseCompleted) {
		AddDebugLogLine(DLP_LOW, false, _T("Search packet parse completed. search=%u processed=%u failed=%u total=%u stage=%s\n"), event.m_uSearchID, event.m_uProcessed, event.m_uFailed, event.m_uTotal, (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventSearchPacketParseFailed) {
		AddDebugLogLine(DLP_HIGH, false, _T("Search packet parse failed. search=%u processed=%u failed=%u total=%u stage=%s\n"), event.m_uSearchID, event.m_uProcessed, event.m_uFailed, event.m_uTotal, (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadStateProgress) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("Download state command progress. sequence=%I64u correlation=%I64u action=%u processed=%u failed=%u stale=%u total=%u\n"), event.m_uSequence, event.m_uCorrelationId, event.m_uAction, event.m_uProcessed, event.m_uFailed, event.m_uStale, event.m_uTotal);
		UpdateBackendDownloadCommandOverlays(false, event.m_uProcessed + event.m_uFailed + event.m_uStale, event.m_uTotal, event.m_uSequence, event.m_uCorrelationId);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadStateCompleted) {
		AddDebugLogLine(DLP_LOW, false, _T("Download state command completed. sequence=%I64u correlation=%I64u action=%u processed=%u failed=%u stale=%u total=%u\n"), event.m_uSequence, event.m_uCorrelationId, event.m_uAction, event.m_uProcessed, event.m_uFailed, event.m_uStale, event.m_uTotal);
		HideBackendDownloadCommandOverlays(false, event.m_uSequence, event.m_uCorrelationId);
		RefreshDownloadListAfterCommand(event.m_uAction);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadStateItemFailed) {
		AddDebugLogLine(DLP_HIGH, false, _T("Download state item failed. sequence=%I64u correlation=%I64u action=%u error=%lu stage=%s path=%s\n"), event.m_uSequence, event.m_uCorrelationId, event.m_uAction, event.m_dwLastError, (LPCTSTR)event.m_strMessage, (LPCTSTR)event.m_strFilePath);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadRemoveDiskCleanupCompleted) {
		AddDebugLogLine(event.m_uFailed != 0 ? DLP_HIGH : DLP_VERYLOW, false, _T("Download remove disk cleanup completed. sequence=%I64u correlation=%I64u completed=%u failed=%u\n"), event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed);
		if (!CompleteBackendDownloadRemoveDiskCleanup(event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed)) {
			CDownloadListCtrl *pDownloadList = GetDownloadListCtrlForCommandBridge();
			if (pDownloadList != NULL)
				pDownloadList->CompleteChunkedRemoveDownloadDiskCleanup(event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed);
		}
		return;
	}

	if (event.m_eType == ApplicationEventDownloadRemoveProgress) {
		AddDebugLogLine(DLP_VERYLOW, false, _T("Download remove command progress. sequence=%I64u correlation=%I64u processed=%u failed=%u stale=%u total=%u\n"), event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed, event.m_uStale, event.m_uTotal);
		UpdateBackendDownloadCommandOverlays(true, event.m_uProcessed + event.m_uFailed + event.m_uStale, event.m_uTotal, event.m_uSequence, event.m_uCorrelationId);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadRemoveCompleted) {
		AddDebugLogLine(DLP_LOW, false, _T("Download remove command completed. sequence=%I64u correlation=%I64u processed=%u failed=%u stale=%u total=%u\n"), event.m_uSequence, event.m_uCorrelationId, event.m_uProcessed, event.m_uFailed, event.m_uStale, event.m_uTotal);
		HideBackendDownloadCommandOverlays(true, event.m_uSequence, event.m_uCorrelationId);
		if (emuledlg != NULL && emuledlg->transferwnd != NULL)
			emuledlg->transferwnd->UpdateCatTabTitles();
		const bool bSharedFilesAlreadyUpdated = event.m_eBackendCommandSource == BackendCommandSourceUi && event.m_strBackendCommandOrderingKey == _T("download-list:chunked-remove");
		if (!bSharedFilesAlreadyUpdated) {
			CSharedFilesCtrl *pSharedFilesCtrl = GetSharedFilesCtrlForCommandBridge();
			if (pSharedFilesCtrl != NULL && ::IsWindow(pSharedFilesCtrl->GetSafeHwnd()))
				pSharedFilesCtrl->ReloadListFromApplicationEvent(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
		}
		return;
	}

	if (event.m_eType == ApplicationEventDownloadRemoveItemFailed) {
		AddDebugLogLine(DLP_HIGH, false, _T("Download remove item failed. sequence=%I64u correlation=%I64u error=%lu stage=%s path=%s\n"), event.m_uSequence, event.m_uCorrelationId, event.m_dwLastError, (LPCTSTR)event.m_strMessage, (LPCTSTR)event.m_strFilePath);
		return;
	}

	AddDebugLogLine(DLP_LOW, false, _T("Backend result application event ignored by result dispatcher. event=%u sequence=%I64u correlation=%I64u\n"), static_cast<UINT>(event.m_eType), event.m_uSequence, event.m_uCorrelationId);
}

// LegacyUiBridgeOnly: Keep until phase 31 removes UI adapter apply paths.
void CemuleApp::DispatchUiCommandBridgeApplicationEvent(const SApplicationEvent &event)
{
	if (event.m_eType == ApplicationEventSharedFilesCommandRequested) {
		AddDebugLogLine(DLP_LOW, false, _T("Shared files command requested. command=%u action=%u sequence=%I64u correlation=%I64u total=%u\n"), static_cast<UINT>(event.m_eSharedFilesCommandType), event.m_uAction, event.m_uSequence, event.m_uCorrelationId, static_cast<UINT>(event.m_vecItemHashes.size()));
		if (emuledlg != NULL && emuledlg->sharedfileswnd != NULL && ::IsWindow(emuledlg->sharedfileswnd->m_hWnd)) {
			if (emuledlg->sharedfileswnd->sharedfilesctrl.ExecuteSharedFilesCommandFromEvent(event.m_uAction, event.m_vecItemHashes, event.m_uSequence, event.m_uCorrelationId))
				QueueSharedFilesCommandEvent(ApplicationEventSharedFilesCommandCompleted, event.m_uAction, event.m_vecItemHashes, _T("completed"), 0, event.m_uSequence, event.m_uCorrelationId);
		} else {
			QueueSharedFilesCommandEvent(ApplicationEventSharedFilesCommandFailed, event.m_uAction, event.m_vecItemHashes, _T("shared-files-window-unavailable"), ERROR_INVALID_HANDLE, event.m_uSequence, event.m_uCorrelationId);
		}
		return;
	}

	if (event.m_eType == ApplicationEventPersistenceRequested || event.m_eType == ApplicationEventPersistenceWorkRequested) {
		const bool bWorkRequest = event.m_eType == ApplicationEventPersistenceWorkRequested;
		AddDebugLogLine(DLP_LOW, false, _T("Persistence requested event executing on UI owner lane. command=%u auto=%u work=%u sequence=%I64u correlation=%I64u reason=%s\n"), static_cast<UINT>(event.m_ePersistenceCommandType), event.m_bAutoSave ? 1U : 0U, bWorkRequest ? 1U : 0U, event.m_uSequence, event.m_uCorrelationId, (LPCTSTR)event.m_strMessage);
		if (!IsPersistenceCommandTypeValid(event.m_ePersistenceCommandType)) {
			QueuePersistenceCommandEvent(ApplicationEventPersistenceFailed, event.m_ePersistenceCommandType, event.m_bAutoSave, _T("invalid-command"), ERROR_INVALID_PARAMETER, event.m_uSequence, event.m_uCorrelationId);
			return;
		}

		SBackendCommand command;
		command.m_eType = BackendCommandPersistence;
		command.m_eFamily = event.m_eBackendCommandFamily;
		command.m_eSource = event.m_eBackendCommandSource != BackendCommandSourceUnknown ? event.m_eBackendCommandSource : BackendCommandSourcePersistence;
		command.m_eOrderingScope = event.m_eBackendCommandOrderingScope != BackendCommandOrderingGlobal ? event.m_eBackendCommandOrderingScope : BackendCommandOrderingPersistence;
		command.m_eFailurePolicy = event.m_eBackendCommandFailurePolicy;
		command.m_lGenerationGuard = event.m_lBackendCommandGenerationGuard;
		command.m_strOrderingKey = event.m_strBackendCommandOrderingKey.IsEmpty() ? CString(bWorkRequest ? _T("persistence-work") : _T("persistence")) : event.m_strBackendCommandOrderingKey;
		command.m_persistenceCommand.m_eType = event.m_ePersistenceCommandType;
		command.m_persistenceCommand.m_bAutoSave = !bWorkRequest && event.m_bAutoSave;
		command.m_persistenceCommand.m_bWorkRequest = bWorkRequest;
		command.m_persistenceCommand.m_strReason = event.m_strMessage;
		command.m_uSequence = event.m_uSequence;
		command.m_uCorrelationId = event.m_uCorrelationId != 0 ? event.m_uCorrelationId : event.m_uSequence;
		command.m_uCancellationToken = event.m_uCancellationToken;
		EnsureBackendCommandEnvelope(command);
		ExecutePersistenceCommandOnCurrentThread(command);
		return;
	}

	if (event.m_eType == ApplicationEventSearchStartRequested) {
		if (emuledlg != NULL && emuledlg->searchwnd != NULL && emuledlg->searchwnd->m_pwndResults != NULL) {
			if (event.m_eBackendCommandSource == BackendCommandSourceWebServer)
				emuledlg->searchwnd->m_pwndResults->StartWebSearchFromCommand(new SSearchParams(event.m_searchParams));
			else
				emuledlg->searchwnd->m_pwndResults->StartSearchFromCommand(new SSearchParams(event.m_searchParams));
		} else
			AddDebugLogLine(DLP_LOW, false, _T("Search start command skipped because search window is unavailable. sequence=%I64u correlation=%I64u\n"), event.m_uSequence, event.m_uCorrelationId);
		return;
	}

	if (event.m_eType == ApplicationEventSearchCancelRequested) {
		if (emuledlg != NULL && emuledlg->searchwnd != NULL && emuledlg->searchwnd->m_pwndResults != NULL)
			emuledlg->searchwnd->m_pwndResults->CancelSearchFromCommand(event.m_uSearchID);
		else
			AddDebugLogLine(DLP_LOW, false, _T("Search cancel command skipped because search window is unavailable. sequence=%I64u correlation=%I64u search=%u\n"), event.m_uSequence, event.m_uCorrelationId, event.m_uSearchID);
		return;
	}

	if (event.m_eType == ApplicationEventCollectionImportRequested) {
		if (emuledlg != NULL)
			emuledlg->ProcessCollectionFile(event.m_strFilePath);
		else
			AddDebugLogLine(DLP_LOW, false, _T("Collection import command skipped because main dialog is unavailable. sequence=%I64u correlation=%I64u path=%s\n"), event.m_uSequence, event.m_uCorrelationId, (LPCTSTR)event.m_strFilePath);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadProcessLinkRequested) {
		if (emuledlg != NULL && ::IsWindow(emuledlg->m_hWnd) && !event.m_strMessage.IsEmpty())
			emuledlg->ProcessED2KLink(event.m_strMessage);
		else
			AddDebugLogLine(DLP_LOW, false, _T("Download process-link UI bridge skipped because the main window is unavailable. sequence=%I64u correlation=%I64u\n"), event.m_uSequence, event.m_uCorrelationId);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadRemoveRequested) {
		if (emuledlg != NULL && emuledlg->transferwnd != NULL && emuledlg->transferwnd->GetDownloadList() != NULL) {
			CStringArray astrItemHashes;
			for (std::vector<CString>::const_iterator it = event.m_vecItemHashes.begin(); it != event.m_vecItemHashes.end(); ++it)
				astrItemHashes.Add(*it);
			emuledlg->transferwnd->GetDownloadList()->StartChunkedRemoveDownloadsFromCommand(astrItemHashes, event.m_bAddToCanceledMet, event.m_bDeleteCompletedFile, event.m_uSequence, event.m_uCorrelationId);
		} else
			AddDebugLogLine(DLP_LOW, false, _T("Download remove command skipped because download list is unavailable. sequence=%I64u correlation=%I64u total=%u\n"), event.m_uSequence, event.m_uCorrelationId, static_cast<UINT>(event.m_vecItemHashes.size()));
		return;
	}

	if (event.m_eType == ApplicationEventDownloadStateRequested) {
		if (emuledlg != NULL && emuledlg->transferwnd != NULL && emuledlg->transferwnd->GetDownloadList() != NULL) {
			CStringArray astrItemHashes;
			for (std::vector<CString>::const_iterator it = event.m_vecItemHashes.begin(); it != event.m_vecItemHashes.end(); ++it)
				astrItemHashes.Add(*it);
			emuledlg->transferwnd->GetDownloadList()->StartChunkedDownloadStateChangeFromCommand(astrItemHashes, event.m_uAction, event.m_iActionValue, event.m_uSequence, event.m_uCorrelationId);
		} else
			AddDebugLogLine(DLP_LOW, false, _T("Download state command skipped because download list is unavailable. sequence=%I64u correlation=%I64u action=%u total=%u\n"), event.m_uSequence, event.m_uCorrelationId, event.m_uAction, static_cast<UINT>(event.m_vecItemHashes.size()));
		return;
	}

	AddDebugLogLine(DLP_LOW, false, _T("UI command bridge application event ignored by bridge dispatcher. event=%u sequence=%I64u correlation=%I64u\n"), static_cast<UINT>(event.m_eType), event.m_uSequence, event.m_uCorrelationId);
}

void CemuleApp::DispatchUiNotificationApplicationEvent(const SApplicationEvent &event)
{
	if (event.m_eType == ApplicationEventLocalEd2kSearchEnd) {
		if (searchlist != NULL && event.m_lSearchGeneration != 0) {
			const LONG lCurrentGeneration = searchlist->GetSearchAnswerParseGeneration(event.m_uSearchID);
			if (lCurrentGeneration != 0 && event.m_lSearchGeneration != lCurrentGeneration) {
				AddDebugLogLine(DLP_LOW, false, _T("Local ED2K search end event dropped because generation is stale. search=%u generation=%ld current=%ld\n"), event.m_uSearchID, event.m_lSearchGeneration, lCurrentGeneration);
				return;
			}
		}
		if (emuledlg != NULL && emuledlg->searchwnd != NULL)
			emuledlg->searchwnd->LocalEd2kSearchEnd(event.m_uTotal, event.m_bMoreResultsAvailable);
		else
			AddDebugLogLine(DLP_LOW, false, _T("Local ED2K search end event skipped because search window is unavailable. search=%u count=%u more=%u\n"), event.m_uSearchID, event.m_uTotal, event.m_bMoreResultsAvailable ? 1U : 0U);
		return;
	}

	if (event.m_eType == ApplicationEventSearchResultsChanged) {
		if (searchlist != NULL) {
			const LONG lCurrentGeneration = searchlist->GetSearchAnswerParseGeneration(event.m_uSearchID);
			if (event.m_lSearchGeneration != 0 && lCurrentGeneration != 0 && event.m_lSearchGeneration != lCurrentGeneration) {
				AddDebugLogLine(DLP_LOW, false, _T("Search results changed event dropped because generation is stale. search=%u generation=%ld current=%ld sequence=%I64u correlation=%I64u\n"),
					event.m_uSearchID, event.m_lSearchGeneration, lCurrentGeneration, event.m_uSequence, event.m_uCorrelationId);
				return;
			}
			searchlist->UpdateSearchIngestOutputWndFromUiThread(event.m_uSearchID, event.m_strMessage, event.m_bUseKadReloadThrottle);
		} else
			AddDebugLogLine(DLP_LOW, false, _T("Search results changed event skipped because search list is unavailable. search=%u sequence=%I64u correlation=%I64u\n"), event.m_uSearchID, event.m_uSequence, event.m_uCorrelationId);
		return;
	}

	if (event.m_eType == ApplicationEventClientChatMessage || event.m_eType == ApplicationEventClientChatCloseRequested || event.m_eType == ApplicationEventClientCaptchaRequested || event.m_eType == ApplicationEventClientCaptchaResult || event.m_eType == ApplicationEventClientChatConnectingResult || event.m_eType == ApplicationEventClientChatConnectionProgress) {
		if (emuledlg == NULL || emuledlg->chatwnd == NULL || !::IsWindow(emuledlg->chatwnd->GetSafeHwnd())) {
			if (event.m_eType == ApplicationEventClientCaptchaRequested && event.m_hClientBitmap != NULL)
				::DeleteObject(event.m_hClientBitmap);
			return;
		}

		CUpDownClient *pClient = NULL;
		if (event.m_eType != ApplicationEventClientChatCloseRequested)
			pClient = (clientlist != NULL) ? clientlist->AcquireTrackedClientByRuntimeIDAndGeneration(event.m_uClientRuntimeID, event.m_lClientRuntimeGeneration) : NULL;

		if (event.m_eType != ApplicationEventClientChatCloseRequested && pClient == NULL) {
			AddDebugLogLine(DLP_LOW, false, _T("Client side chat event dropped because target is stale. event=%u runtime=%lu generation=%ld\n"), static_cast<UINT>(event.m_eType), event.m_uClientRuntimeID, event.m_lClientRuntimeGeneration);
			if (event.m_eType == ApplicationEventClientCaptchaRequested && event.m_hClientBitmap != NULL)
				::DeleteObject(event.m_hClientBitmap);
			return;
		}

		switch (event.m_eType) {
			case ApplicationEventClientChatMessage:
				emuledlg->chatwnd->chatselector.ProcessMessage(pClient, event.m_strMessage);
				break;
			case ApplicationEventClientChatCloseRequested:
				emuledlg->chatwnd->chatselector.EndSessionByRuntime(event.m_uClientRuntimeID, event.m_lClientRuntimeGeneration);
				break;
			case ApplicationEventClientCaptchaRequested:
				if (!emuledlg->chatwnd->chatselector.ShowCaptchaRequest(pClient, event.m_hClientBitmap))
					pClient->SetChatCaptchaState(CA_ACCEPTING);
				if (event.m_hClientBitmap != NULL)
					::DeleteObject(event.m_hClientBitmap);
				break;
			case ApplicationEventClientCaptchaResult:
				emuledlg->chatwnd->chatselector.ShowCaptchaResult(pClient, event.m_strMessage);
				break;
			case ApplicationEventClientChatConnectingResult:
				emuledlg->chatwnd->chatselector.ConnectingResult(pClient, event.m_iActionValue != 0);
				break;
			case ApplicationEventClientChatConnectionProgress:
				emuledlg->chatwnd->chatselector.ReportConnectionProgress(pClient, event.m_strMessage, event.m_iActionValue != 0);
				break;
		}

		if (pClient != NULL)
			pClient->ReleaseRuntimeReference();
		return;
	}

	if (event.m_eType == ApplicationEventKadConnectionStateChanged) {
		if (emuledlg != NULL && ::IsWindow(emuledlg->GetSafeHwnd()))
			emuledlg->ShowConnectionState();
		else
			AddDebugLogLine(DLP_LOW, false, _T("Kad connection state event skipped because main window is unavailable. stage=%s\n"), (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventKadUiStatusRefresh) {
		if (emuledlg == NULL || !::IsWindow(emuledlg->GetSafeHwnd())) {
			AddDebugLogLine(DLP_LOW, false, _T("Kad UI status refresh event skipped because main window is unavailable. flags=%u stage=%s\n"), event.m_uAction, (LPCTSTR)event.m_strMessage);
			return;
		}

		if ((event.m_uAction & KadUiStatusUpnp) != 0)
			emuledlg->RefreshUPnP();
		if ((event.m_uAction & KadUiStatusUserCount) != 0)
			emuledlg->ShowUserCount();
		if ((event.m_uAction & KadUiStatusContactList) != 0 && emuledlg->kademliawnd != NULL && ::IsWindow(emuledlg->kademliawnd->GetSafeHwnd()))
			emuledlg->kademliawnd->StartUpdateContacts();
		return;
	}

	if (event.m_eType == ApplicationEventKadSearchCancelUiRequested) {
		if (emuledlg != NULL && emuledlg->searchwnd != NULL && emuledlg->searchwnd->m_pwndResults != NULL)
			emuledlg->searchwnd->m_pwndResults->CancelKadSearch(event.m_uSearchID);
		else
			AddDebugLogLine(DLP_LOW, false, _T("Kad search cancel UI event skipped because search window is unavailable. search=%u stage=%s\n"), event.m_uSearchID, (LPCTSTR)event.m_strMessage);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadListRowsRemoved) {
		if (emuledlg != NULL && emuledlg->transferwnd != NULL) {
			CDownloadListCtrl *pDownloadList = emuledlg->transferwnd->GetDownloadList();
			if (pDownloadList != NULL && ::IsWindow(pDownloadList->GetSafeHwnd()))
				pDownloadList->RemoveFilesByHash(event.m_vecItemHashes);
		}
		CSharedFilesCtrl *pSharedFilesCtrl = GetSharedFilesCtrlForCommandBridge();
		if (pSharedFilesCtrl != NULL && ::IsWindow(pSharedFilesCtrl->GetSafeHwnd()))
			pSharedFilesCtrl->RemoveBackendDownloadRowsByHash(event.m_vecItemHashes);
		return;
	}

	if (event.m_eType == ApplicationEventDownloadListDeletedCompletedRowsRemoved) {
		if (emuledlg != NULL && emuledlg->transferwnd != NULL) {
			CDownloadListCtrl *pDownloadList = emuledlg->transferwnd->GetDownloadList();
			if (pDownloadList != NULL && ::IsWindow(pDownloadList->GetSafeHwnd()))
				pDownloadList->RemoveDeletedCompletedFilesByHash(event.m_vecItemHashes);
		}
		CSharedFilesCtrl *pSharedFilesCtrl = GetSharedFilesCtrlForCommandBridge();
		if (pSharedFilesCtrl != NULL && ::IsWindow(pSharedFilesCtrl->GetSafeHwnd()))
			pSharedFilesCtrl->RemoveBackendDownloadRowsByHash(event.m_vecItemHashes);
		return;
	}

	if (event.m_eType == ApplicationEventBulkOperationOverlayRefresh) {
		if (emuledlg != NULL)
			emuledlg->RefreshActiveBulkOperationOverlays();
		return;
	}

	if (event.m_eType == ApplicationEventDownloadListChanged) {
		if (emuledlg != NULL && emuledlg->transferwnd != NULL) {
			CDownloadListCtrl *pDownloadList = emuledlg->transferwnd->GetDownloadList();
			if (pDownloadList != NULL && ::IsWindow(pDownloadList->GetSafeHwnd())) {
				if (event.m_strMessage == _T("bulk-add-finalized") || event.m_strMessage == _T("download-add"))
					pDownloadList->RefreshAfterDownloadListMembershipChanged();
				else
					pDownloadList->RefreshAfterBackendDownloadCommand(0);
			}
		}
		return;
	}

	if (event.m_eType == ApplicationEventSharedFilesListChanged) {
		if (emuledlg != NULL && emuledlg->sharedfileswnd != NULL && ::IsWindow(emuledlg->sharedfileswnd->m_hWnd)) {
			CSharedFilesCtrl &sharedCtrl = emuledlg->sharedfileswnd->sharedfilesctrl;
			if (::IsWindow(sharedCtrl.GetSafeHwnd()))
				sharedCtrl.ReloadListFromApplicationEvent(false, static_cast<EListStateField>(LSF_SELECTION));
		}
		return;
	}

	if (event.m_eType == ApplicationEventUploadQueueListChanged || event.m_eType == ApplicationEventUploadListChanged || event.m_eType == ApplicationEventUploadBandwidthSnapshotChanged) {
		if (emuledlg != NULL && emuledlg->transferwnd != NULL && emuledlg->transferwnd->m_pwndTransfer != NULL) {
			if (event.m_uUploadWaitingCount != 0 || event.m_eType == ApplicationEventUploadQueueListChanged)
				emuledlg->transferwnd->ShowQueueCount(event.m_uUploadWaitingCount);
			if (event.m_eType == ApplicationEventUploadListChanged || event.m_eType == ApplicationEventUploadBandwidthSnapshotChanged) {
				CUploadListCtrl *pUploadList = emuledlg->transferwnd->GetUploadList();
				if (pUploadList != NULL && ::IsWindow(pUploadList->GetSafeHwnd()))
					pUploadList->UpdateView();
			}
			if (event.m_eType == ApplicationEventUploadQueueListChanged) {
				CQueueListCtrl *pQueueList = emuledlg->transferwnd->GetQueueList();
				if (pQueueList != NULL && ::IsWindow(pQueueList->GetSafeHwnd()))
					pQueueList->UpdateView();
			}
		}
		return;
	}

	if (event.m_eType == ApplicationEventUploadClientRowsRemoved) {
		if (emuledlg != NULL && emuledlg->transferwnd != NULL && emuledlg->transferwnd->m_pwndTransfer != NULL) {
			if ((event.m_uUploadTargetFlags & UploadClientUiTargetUploadList) != 0) {
				CUploadListCtrl *pUploadList = emuledlg->transferwnd->GetUploadList();
				if (pUploadList != NULL && ::IsWindow(pUploadList->GetSafeHwnd()))
					pUploadList->RemoveClientByRuntimeID(event.m_uClientRuntimeID);
			}
			if ((event.m_uUploadTargetFlags & UploadClientUiTargetQueueList) != 0) {
				CQueueListCtrl *pQueueList = emuledlg->transferwnd->GetQueueList();
				if (pQueueList != NULL && ::IsWindow(pQueueList->GetSafeHwnd()))
					pQueueList->RemoveClientByRuntimeID(event.m_uClientRuntimeID);
			}
		}
		return;
	}

	if (event.m_eType != ApplicationEventUploadClientRowsChanged && event.m_eType != ApplicationEventClientRowUpdateRequested)
		return;
	if (event.m_uClientRuntimeID == 0 || event.m_lClientRuntimeGeneration == 0)
		return;

	CUpDownClient* pClient = (clientlist != NULL) ? clientlist->AcquireTrackedClientByRuntimeIDAndGeneration(event.m_uClientRuntimeID, event.m_lClientRuntimeGeneration) : NULL;
	if (pClient == NULL) {
		AddDebugLogLine(DLP_LOW, false, _T("Client row update event dropped because target is stale. runtime=%lu source=%s scope=%s key=%s sequence=%I64u correlation=%I64u\n"),
			event.m_uClientRuntimeID, GetBackendCommandSourceName(event.m_eBackendCommandSource), GetBackendCommandOrderingScopeName(event.m_eBackendCommandOrderingScope),
			(LPCTSTR)event.m_strBackendCommandOrderingKey, event.m_uSequence, event.m_uCorrelationId);
		return;
	}

	if (event.m_eType == ApplicationEventClientRowUpdateRequested) {
		if (emuledlg != NULL && emuledlg->transferwnd != NULL && emuledlg->transferwnd->m_pwndTransfer != NULL) {
			CClientListCtrl *pClientList = emuledlg->transferwnd->GetClientList();
			if (pClientList != NULL && ::IsWindow(pClientList->GetSafeHwnd()))
				pClientList->RefreshClient(pClient);
		}
		pClient->ReleaseRuntimeReference();
		return;
	}

	if (emuledlg != NULL && emuledlg->transferwnd != NULL && emuledlg->transferwnd->m_pwndTransfer != NULL) {
		if ((event.m_uUploadTargetFlags & UploadClientUiTargetUploadList) != 0) {
			CUploadListCtrl *pUploadList = emuledlg->transferwnd->GetUploadList();
			if (pUploadList != NULL && ::IsWindow(pUploadList->GetSafeHwnd()) && theApp.uploadqueue != NULL) {
				const bool bListed = pUploadList->m_ListItemsMap.find(event.m_uClientRuntimeID) != pUploadList->m_ListItemsMap.end();
				const bool bUploading = theApp.uploadqueue->IsDownloading(pClient);
				if (bUploading) {
					if (bListed)
						pUploadList->RefreshClient(pClient);
					else if (event.m_eType == ApplicationEventUploadClientRowsChanged)
						pUploadList->AddClient(pClient);
				} else if (bListed)
					pUploadList->RemoveClient(pClient);
			}
		}
		if ((event.m_uUploadTargetFlags & UploadClientUiTargetQueueList) != 0) {
			CQueueListCtrl *pQueueList = emuledlg->transferwnd->GetQueueList();
			if (pQueueList != NULL && ::IsWindow(pQueueList->GetSafeHwnd()) && theApp.uploadqueue != NULL) {
				const bool bListed = pQueueList->m_ListItemsMap.find(event.m_uClientRuntimeID) != pQueueList->m_ListItemsMap.end();
				const bool bWaiting = theApp.uploadqueue->IsOnUploadQueue(pClient);
				if (bWaiting) {
					if (bListed)
						pQueueList->RefreshClient(pClient);
					else if (event.m_eType == ApplicationEventUploadClientRowsChanged)
						pQueueList->AddClient(pClient);
				} else if (bListed)
					pQueueList->RemoveClient(pClient);
			}
		}
		if ((event.m_uUploadTargetFlags & UploadClientUiTargetDownloadClients) != 0) {
			CDownloadClientsCtrl *pDownloadClientsList = emuledlg->transferwnd->GetDownloadClientsList();
			if (pDownloadClientsList != NULL && ::IsWindow(pDownloadClientsList->GetSafeHwnd()))
				pDownloadClientsList->RefreshClient(pClient);
		}
	}
	pClient->ReleaseRuntimeReference();
}

void CemuleApp::ClearChunkedDownloadJobs()
{
	m_bChunkedDownloadMessagePending = false;
	SetActiveDownloadAddOperationProgress(0, 0, false);
	while (!m_chunkedDownloadJobs.IsEmpty()) {
		SChunkedDownloadJob *pJob = m_chunkedDownloadJobs.RemoveHead();
		if (pJob != NULL) {
			EndChunkedDownloadJobBulkAdd(*pJob);
			delete pJob;
		}
	}
}

UINT CemuleApp::GetChunkedDownloadJobItemCount(const SChunkedDownloadJob &job) const
{
	return static_cast<UINT>(job.m_command.m_astrLinks.GetSize() + static_cast<INT_PTR>(GetDownloadCommandSnapshotCount(job.m_command)));
}

bool CemuleApp::ProcessChunkedDownloadSnapshotItem(SChunkedDownloadJob &job, const SDownloadFileSnapshot &snapshot)
{
	if (downloadqueue == NULL || snapshot.m_strFileName.IsEmpty() || snapshot.m_uFileSize == 0)
		return false;

	try {
		downloadqueue->AddFileSnapshotToDownload(snapshot.m_strFileName, snapshot.m_uFileSize, snapshot.m_abyFileHash, snapshot.m_strAICHHash, job.m_command.m_iCat);
		return true;
	} catch (CException *ex) {
		ex->Delete();
		if (IsUiThread())
			LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
		else
			AddDebugLogLine(DLP_HIGH, false, _T("Download snapshot was not added on backend owner lane. file=%s\n"), (LPCTSTR)snapshot.m_strFileName);
		return false;
	} catch (...) {
		if (IsUiThread())
			LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
		else
			AddDebugLogLine(DLP_HIGH, false, _T("Download snapshot was not added on backend owner lane. file=%s\n"), (LPCTSTR)snapshot.m_strFileName);
		return false;
	}
}

bool CemuleApp::ProcessChunkedDownloadItem(SChunkedDownloadJob &job, const CString &strLink)
{
	if (strLink.IsEmpty())
		return true;

	if (job.m_command.m_eType == DownloadCommandProcessLinks) {
		CString strDecodedLink(strLink);
		strDecodedLink.Replace(_T("%7c"), _T("|"));
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(OptUtf8ToStr(URLDecode(strDecodedLink)));
			if (pLink == NULL)
				return false;
			if (pLink->GetKind() == CED2KLink::kFile) {
				if (downloadqueue == NULL) {
					delete pLink;
					return false;
				}
				CED2KFileLink *pFileLink = pLink->GetFileLink();
				if (pFileLink == NULL) {
					delete pLink;
					return false;
				}
				downloadqueue->AddFileLinkToDownload(*pFileLink, job.m_command.m_iCat);
				delete pLink;
				return true;
			}
			delete pLink;
			pLink = NULL;

			if (IsUiThread()) {
				if (emuledlg == NULL || !::IsWindow(emuledlg->m_hWnd))
					return false;
				emuledlg->ProcessED2KLink(strLink);
			} else {
				SApplicationEvent event;
				event.m_eType = ApplicationEventDownloadProcessLinkRequested;
				event.m_eDownloadCommandType = DownloadCommandProcessLinks;
				SetApplicationEventBackendEnvelope(event, BackendCommandDownload, job.m_eSource, job.m_eOrderingScope, job.m_strOrderingKey.IsEmpty() ? (LPCTSTR)_T("download:process-links") : (LPCTSTR)job.m_strOrderingKey, job.m_uSequence, job.m_uCorrelationId);
				event.m_uCancellationToken = job.m_uCancellationToken;
				event.m_strMessage = strLink;
				QueueApplicationEvent(event);
			}
			return true;
		} catch (const CString &error) {
			delete pLink;
			CString sBuffer;
			sBuffer.Format(GetResString(_T("ERR_INVALIDLINK")), (LPCTSTR)error);
			if (IsUiThread())
				LogError(LOG_STATUSBAR, GetResString(_T("ERR_LINKERROR")), (LPCTSTR)sBuffer);
			else
				AddDebugLogLine(DLP_HIGH, false, _T("Download process-link rejected on backend owner lane. error=%s link=%s\n"), (LPCTSTR)sBuffer, (LPCTSTR)strLink);
			return false;
		} catch (CException *ex) {
			delete pLink;
			ex->Delete();
			if (IsUiThread())
				LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
			else
				AddDebugLogLine(DLP_HIGH, false, _T("Download process-link was not handled on backend owner lane. link=%s\n"), (LPCTSTR)strLink);
			return false;
		} catch (...) {
			delete pLink;
			if (IsUiThread())
				LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
			else
				AddDebugLogLine(DLP_HIGH, false, _T("Download process-link was not handled on backend owner lane. link=%s\n"), (LPCTSTR)strLink);
			return false;
		}
	}

	if (downloadqueue == NULL)
		return false;

	const bool bSlash = (strLink[strLink.GetLength() - 1] == _T('/'));
	CED2KLink *pLink = NULL;
	try {
		pLink = CED2KLink::CreateLinkFromUrl(bSlash ? strLink : strLink + _T('/'));
		if (pLink == NULL)
			return false;
		if (pLink->GetKind() != CED2KLink::kFile)
			throwCStr(_T("bad link"));
		downloadqueue->AddFileLinkToDownload(*pLink->GetFileLink(), job.m_command.m_iCat);
		delete pLink;
		return true;
	} catch (const CString &error) {
		delete pLink;
		CString sBuffer;
		sBuffer.Format(GetResString(_T("ERR_INVALIDLINK")), (LPCTSTR)error);
		if (IsUiThread())
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_LINKERROR")), (LPCTSTR)sBuffer);
		else
			AddDebugLogLine(DLP_HIGH, false, _T("Download link rejected on backend owner lane. error=%s link=%s\n"), (LPCTSTR)sBuffer, (LPCTSTR)strLink);
		return false;
	} catch (CException *ex) {
		delete pLink;
		ex->Delete();
		if (IsUiThread())
			LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
		else
			AddDebugLogLine(DLP_HIGH, false, _T("Download link was not added on backend owner lane. link=%s\n"), (LPCTSTR)strLink);
		return false;
	} catch (...) {
		delete pLink;
		if (IsUiThread())
			LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
		else
			AddDebugLogLine(DLP_HIGH, false, _T("Download link was not added on backend owner lane. link=%s\n"), (LPCTSTR)strLink);
		return false;
	}
}

void CemuleApp::SearchClipboard()
{
	if (m_bGuardClipboardPrompt)
		return;

	const CString strLinks(CopyTextFromClipboard());
	if (strLinks.IsEmpty())
		return;

	if (strLinks == m_strLastClipboardContents)
		return;

	// Do not alter (trim) 'strLinks' and then copy back to 'm_strLastClipboardContents'! The
	// next clipboard content compare would fail because of the modified string.
	LPCTSTR pszTrimmedLinks = strLinks;
	while (_istspace(*pszTrimmedLinks)) // Skip leading white space
		++pszTrimmedLinks;
	m_bGuardClipboardPrompt = !_tcsnicmp(pszTrimmedLinks, _T("ed2k://|file|"), 13);
	if (m_bGuardClipboardPrompt) {
		// Don't feed too long strings into the MessageBox function, it may freak out.
		CString strLinksDisplay(GetResString(_T("ADDDOWNLOADSFROMCB")));
		if (strLinks.GetLength() > 512)
			strLinksDisplay.AppendFormat(_T("\r\n%s..."), (LPCTSTR)strLinks.Left(509));
		else
			strLinksDisplay.AppendFormat(_T("\r\n%s"), (LPCTSTR)strLinks);
		const CString strCaption(GetResString(_T("PW_MOD")));
		if (CDarkMode::MessageBoxWithCaption(strLinksDisplay, strCaption, MB_YESNO | MB_TOPMOST) == IDYES)
			AddEd2kLinksToDownload(pszTrimmedLinks, 0);
	}
	m_strLastClipboardContents = strLinks; // Save the unmodified(!) clipboard contents
	m_bGuardClipboardPrompt = false;
}

void CemuleApp::PasteClipboard(int cat)
{
	CString strLinks(CopyTextFromClipboard());
	if (!strLinks.Trim().IsEmpty())
		AddEd2kLinksToDownload(strLinks, cat);
}

bool CemuleApp::IsEd2kLinkInClipboard(LPCSTR pszLinkType, int iLinkTypeLen)
{
	bool bFoundLink = false;
	if (IsClipboardFormatAvailable(CF_TEXT)) {
		if (OpenClipboard(NULL)) {
			HGLOBAL	hText = GetClipboardData(CF_TEXT);
			if (hText != NULL) {
				// Use the ANSI string
				LPCSTR pszText = static_cast<LPCSTR>(::GlobalLock(hText));
				if (pszText != NULL) {
					while (isspace(*pszText))
						++pszText;
					bFoundLink = (_strnicmp(pszText, pszLinkType, iLinkTypeLen) == 0);
					::GlobalUnlock(hText);
				}
			}
			CloseClipboard();
		}
	}

	return bFoundLink;
}

bool CemuleApp::IsEd2kFileLinkInClipboard()
{
	static const char _szEd2kFileLink[] = "ed2k://|file|"; // Use the ANSI string
	return IsEd2kLinkInClipboard(_szEd2kFileLink, (sizeof _szEd2kFileLink) - 1);
}

bool CemuleApp::IsEd2kServerLinkInClipboard()
{
	static const char _szEd2kServerLink[] = "ed2k://|server|"; // Use the ANSI string
	return IsEd2kLinkInClipboard(_szEd2kServerLink, (sizeof _szEd2kServerLink) - 1);
}

void CemuleApp::QueueDebugLogLine(bool bAddToStatusbar, LPCTSTR line, ...)
{
	if (!thePrefs.GetVerbose())
		return;

	CString bufferline;
	va_list argptr;
	va_start(argptr, line);
	bufferline.FormatV(line, argptr);
	va_end(argptr);
	if (!bufferline.IsEmpty()) {
		if (IsNatTraversalDebugLog(bufferline) && !thePrefs.GetLogNatTraversalEvents())
			return;
		if (IsUiResponsivenessDebugLog(bufferline) && !thePrefs.GetLogUiResponsivenessEvents())
			return;

		SLogItem *newItem = new SLogItem;
		newItem->uFlags = LOG_DEBUG | (bAddToStatusbar ? LOG_STATUSBAR : 0);
		newItem->line = bufferline;

		m_queueLock.Lock();
		m_QueueDebugLog.AddTail(newItem);
		m_queueLock.Unlock();
	}
}

void CemuleApp::QueueLogLine(bool bAddToStatusbar, LPCTSTR line, ...)
{
	CString bufferline;
	va_list argptr;
	va_start(argptr, line);
	bufferline.FormatV(line, argptr);
	va_end(argptr);
	if (!bufferline.IsEmpty()) {
		SLogItem *newItem = new SLogItem;
		newItem->uFlags = bAddToStatusbar ? LOG_STATUSBAR : 0;
		newItem->line = bufferline;

		m_queueLock.Lock();
		m_QueueLog.AddTail(newItem);
		m_queueLock.Unlock();
	}
}

void CemuleApp::QueueDebugLogLineEx(UINT uFlags, LPCTSTR line, ...)
{
	if (!thePrefs.GetVerbose())
		return;

	CString bufferline;
	va_list argptr;
	va_start(argptr, line);
	bufferline.FormatV(line, argptr);
	va_end(argptr);
	if (!bufferline.IsEmpty()) {
		if (IsNatTraversalDebugLog(bufferline) && !thePrefs.GetLogNatTraversalEvents())
			return;
		if (IsUiResponsivenessDebugLog(bufferline) && !thePrefs.GetLogUiResponsivenessEvents())
			return;

		SLogItem *newItem = new SLogItem;
		newItem->uFlags = uFlags | LOG_DEBUG;
		newItem->line = bufferline;

		m_queueLock.Lock();
		m_QueueDebugLog.AddTail(newItem);
		m_queueLock.Unlock();
	}
}

void CemuleApp::QueueLogLineEx(UINT uFlags, LPCTSTR line, ...)
{
	CString bufferline;
	va_list argptr;
	va_start(argptr, line);
	bufferline.FormatV(line, argptr);
	va_end(argptr);
	if (!bufferline.IsEmpty()) {
		SLogItem *newItem = new SLogItem;
		newItem->uFlags = uFlags;
		newItem->line = bufferline;

		m_queueLock.Lock();
		m_QueueLog.AddTail(newItem);
		m_queueLock.Unlock();
	}
}

void CemuleApp::HandleDebugLogQueue()
{
	m_queueLock.Lock();
	while (!m_QueueDebugLog.IsEmpty()) {
		const SLogItem *newItem = m_QueueDebugLog.RemoveHead();
		if (thePrefs.GetVerbose())
			Log(newItem->uFlags, _T("%s"), (LPCTSTR)newItem->line);
		delete newItem;
	}
	m_queueLock.Unlock();
}

void CemuleApp::HandleLogQueue()
{
	m_queueLock.Lock();
	while (!m_QueueLog.IsEmpty()) {
		const SLogItem *newItem = m_QueueLog.RemoveHead();
		Log(newItem->uFlags, _T("%s"), (LPCTSTR)newItem->line);
		delete newItem;
	}
	m_queueLock.Unlock();
}

void CemuleApp::ClearDebugLogQueue(bool bDebugPendingMsgs)
{
	m_queueLock.Lock();
	while (!m_QueueDebugLog.IsEmpty()) {
		if (bDebugPendingMsgs)
			TRACE(_T("Queued dbg log msg: %s\n"), (LPCTSTR)m_QueueDebugLog.GetHead()->line);
		delete m_QueueDebugLog.RemoveHead();
	}
	m_queueLock.Unlock();
}

void CemuleApp::ClearLogQueue(bool bDebugPendingMsgs)
{
	m_queueLock.Lock();
	while (!m_QueueLog.IsEmpty()) {
		if (bDebugPendingMsgs)
			TRACE(_T("Queued log msg: %s\n"), (LPCTSTR)m_QueueLog.GetHead()->line);
		delete m_QueueLog.RemoveHead();
	}
	m_queueLock.Unlock();
}

void CemuleApp::CreateAllFonts()
{
	///////////////////////////////////////////////////////////////////////////
	// Symbol font
	//
	// Creating that font with 'SYMBOL_CHARSET' should be safer (seen in ATL/MFC code). Though
	// it seems that it does not solve the problem with '6' and '9' characters which are
	// shown for some ppl.
	m_fontSymbol.CreateFont(::GetSystemMetrics(SM_CYMENUCHECK), 0, 0, 0,
		FW_NORMAL, 0, 0, 0, SYMBOL_CHARSET, 0, 0, 0, 0, _T("Marlett"));


	///////////////////////////////////////////////////////////////////////////
	// Default GUI Font
	//
	// Fonts which are returned by 'GetStockObject'
	// --------------------------------------------
	// OEM_FIXED_FONT		Terminal
	// ANSI_FIXED_FONT		Courier
	// ANSI_VAR_FONT		MS Sans Serif
	// SYSTEM_FONT			System
	// DEVICE_DEFAULT_FONT	System
	// SYSTEM_FIXED_FONT	Fixedsys
	// DEFAULT_GUI_FONT		MS Shell Dlg (*1)
	//
	// (*1) Do not use 'GetStockObject(DEFAULT_GUI_FONT)' to get the 'Tahoma' font. It does
	// not work...
	//
	// The documentation in MSDN states that DEFAULT_GUI_FONT returns 'Tahoma' on
	// Win2000/XP systems. Though this is wrong, it may be true for US-English locales, but
	// it is wrong for other locales. Furthermore it is even documented that "MS Shell Dlg"
	// gets mapped to "MS Sans Serif" on Windows XP systems. Only "MS Shell Dlg 2" would
	// get mapped to "Tahoma", but "MS Shell Dlg 2" can not be used on prior Windows
	// systems.
	//
	// The reason why "MS Shell Dlg" is though mapped to "Tahoma" when used within dialog
	// resources is unclear.
	//
	// So, to get the same font which is used within dialogs which were created via dialog
	// resources which have the "MS Shell Dlg, 8" specified (again, in that special case
	// "MS Shell Dlg" gets mapped to "Tahoma" and not to "MS Sans Serif"), we just query
	// the main window (which is also a dialog) for the current font.
	//
	LOGFONT lfDefault;
	AfxGetMainWnd()->GetFont()->GetLogFont(&lfDefault);
	// WinXP: lfDefault.lfFaceName = "MS Shell Dlg 2" (!)
	// Vista: lfDefault.lfFaceName = "MS Shell Dlg 2"
	//
	// It would not be an error if that font name does not match our pre-determined
	// font name, I just want to know if that ever happens.
	ASSERT(m_strDefaultFontFaceName == lfDefault.lfFaceName);


	///////////////////////////////////////////////////////////////////////////
	// Bold Default GUI Font
	//
	LOGFONT lfDefaultBold = lfDefault;
	lfDefaultBold.lfWeight = FW_BOLD;
	VERIFY(m_fontDefaultBold.CreateFontIndirect(&lfDefaultBold));


	///////////////////////////////////////////////////////////////////////////
	// Server Log-, Message- and IRC-Window font
	//
	// Since we use "MS Shell Dlg 2" under WinXP (which will give us "Tahoma"),
	// that font is nevertheless set to "MS Sans Serif" because a scaled up "Tahoma"
	// font unfortunately does not look as good as a scaled up "MS Sans Serif" font.
	//
	// No! Do *not* use "MS Sans Serif" (never!). This will give a very old fashioned
	// font on certain Asian Windows systems. So, better use "MS Shell Dlg" or
	// "MS Shell Dlg 2" to let Windows map that font to the proper font on all Windows
	// systems.
	//
	LPLOGFONT plfHyperText = thePrefs.GetHyperTextLogFont();
	if (plfHyperText->lfFaceName[0] == _T('\0') || !m_fontHyperText.CreateFontIndirect(plfHyperText))
		CreatePointFont(m_fontHyperText, 10 * 10, lfDefault.lfFaceName);

	///////////////////////////////////////////////////////////////////////////
	// Verbose Log-font
	//
	// Why can't this font set via the font dialog??
	LPLOGFONT plfLog = thePrefs.GetLogFont();
	if (plfLog->lfFaceName[0] != _T('\0'))
		m_fontLog.CreateFontIndirect(plfLog);

	///////////////////////////////////////////////////////////////////////////
	// Font used for Message and IRC edit control, default font, just a little
	// larger.
	//
	// Since we use "MS Shell Dlg 2" under WinXP (which will give us "Tahoma"),
	// that font is nevertheless set to "MS Sans Serif" because a scaled up "Tahoma"
	// font unfortunately does not look as good as a scaled up "MS Sans Serif" font.
	//
	// No! Do *not* use "MS Sans Serif" (never!). This will give a very old fashioned
	// font on certain Asian Windows systems. So, better use "MS Shell Dlg" or
	// "MS Shell Dlg 2" to let Windows map that font to the proper font on all Windows
	// systems.
	//
	CreatePointFont(m_fontChatEdit, 11 * 10, lfDefault.lfFaceName);
}

const CString& CemuleApp::GetDefaultFontFaceName()
{
	return m_strDefaultFontFaceName;
}

void CemuleApp::CreateBackwardDiagonalBrush()
{
	static const WORD awBackwardDiagonalBrushPattern[8] = {0x0f, 0x1e, 0x3c, 0x78, 0xf0, 0xe1, 0xc3, 0x87};
	CBitmap bm;
	if (bm.CreateBitmap(8, 8, 1, 1, awBackwardDiagonalBrushPattern)) {
		LOGBRUSH logBrush = {};
		logBrush.lbStyle = BS_PATTERN;
		logBrush.lbHatch = (ULONG_PTR)bm.GetSafeHandle();
		VERIFY(m_brushBackwardDiagonal.CreateBrushIndirect(&logBrush));
	}
}

void CemuleApp::UpdateDesktopColorDepth()
{
	g_bLowColorDesktop = (GetDesktopColorDepth() <= 8);
#ifdef _DEBUG
	if (!g_bLowColorDesktop)
		g_bLowColorDesktop = (GetProfileInt(_T("eMule"), _T("LowColorRes"), 0) != 0);
#endif

	if (g_bLowColorDesktop) {
		// If we have 4- or 8-bit desktop color depth, Windows will (by design) load only
		// the 16 color versions of icons. Thus we force all image lists also to 4-bit format.
		m_iDfltImageListColorFlags = ILC_COLOR4;
	} else {
		// Get current desktop color depth and derive the image list format from it
		m_iDfltImageListColorFlags = GetAppImageListColorFlag();

		// Don't use 32-bit image lists if not supported by COMCTL32.DLL
		if (m_iDfltImageListColorFlags == ILC_COLOR32 && m_ullComCtrlVer < MAKEDLLVERULL(6, 0, 0, 0)) {
			// We fall back to 16-bit image lists because we do not provide 24-bit
			// versions of icons any longer (due to resource size restrictions for Win98). We
			// could also fall back to 24-bit image lists here but the difference is minimal
			// and considered not to be worth the additional memory consumption.
			//
			// Though, do not fall back to 8-bit image lists because this would let Windows
			// reduce the color resolution to the standard 256 color window system palette.
			// We need a 16-bit or 24-bit image list to hold all our 256 color icons (which
			// are not pre-quantized to standard 256 color windows system palette) without
			// losing any colors.
			m_iDfltImageListColorFlags = ILC_COLOR16;
		}
	}

	// Doesn't help.
}

BOOL WINAPI ConsoleCtrlHandler(DWORD dwCtrlType) noexcept
{
	// *) This function is invoked by the system from within a *DIFFERENT* thread !!
	//
	// *) This function is invoked only, if eMule was started with "RUNAS"
	//		- when user explicitly/manually logs off from the system (CTRL_LOGOFF_EVENT).
	//		- when user explicitly/manually does a reboot or shutdown (also: CTRL_LOGOFF_EVENT).
	//		- when eMule issues an ExitWindowsEx(EWX_LOGOFF/EWX_REBOOT/EWX_SHUTDOWN)
	//
	// NOTE: Windows will in each case forcefully terminate the process after 20 seconds!
	// Every action which is started after receiving this notification will get forcefully
	// terminated by Windows after 20 seconds.

	if (thePrefs.GetDebug2Disk()) {
		static TCHAR szCtrlType[40];
		LPCTSTR pszCtrlType;
		switch (dwCtrlType) {
		case CTRL_C_EVENT:
			pszCtrlType = _T("CTRL_C_EVENT");
			break;
		case CTRL_BREAK_EVENT:
			pszCtrlType = _T("CTRL_BREAK_EVENT");
			break;
		case CTRL_CLOSE_EVENT:
			pszCtrlType = _T("CTRL_CLOSE_EVENT");
			break;
		case CTRL_LOGOFF_EVENT:
			pszCtrlType = _T("CTRL_LOGOFF_EVENT");
			break;
		case CTRL_SHUTDOWN_EVENT:
			pszCtrlType = _T("CTRL_SHUTDOWN_EVENT");
			break;
		default:
			_sntprintf(szCtrlType, _countof(szCtrlType), _T("0x%08lx"), dwCtrlType);
			szCtrlType[_countof(szCtrlType) - 1] = _T('\0');
			pszCtrlType = szCtrlType;
		}
		theVerboseLog.Logf(_T("%hs: CtrlType=%s"), __FUNCTION__, pszCtrlType);

		// Default ProcessShutdownParameters: Level=0x00000280, Flags=0x00000000
		// Setting 'SHUTDOWN_NORETRY' does not prevent from getting terminated after 20 sec.
	}

	if (dwCtrlType == CTRL_CLOSE_EVENT || dwCtrlType == CTRL_LOGOFF_EVENT || dwCtrlType == CTRL_SHUTDOWN_EVENT) {
		if (theApp.emuledlg->m_hWnd) {
			if (thePrefs.GetDebug2Disk())
				theVerboseLog.Logf(_T("%hs: Sending TM_CONSOLETHREADEVENT to main window"), __FUNCTION__);

			// Use 'SendMessage' to send the message to the (different) main thread. This is
			// done by intention because it lets this thread wait as long as the main thread
			// has called 'ExitProcess' or returns from processing the message. This is
			// needed to not let Windows terminate the process before the 20 sec. timeout.
			if (!theApp.emuledlg->SendMessage(TM_CONSOLETHREADEVENT, dwCtrlType, (LPARAM)GetCurrentThreadId())) {
				theApp.BeginBackendShutdownLifecycle(_T("console-control-fallback"));
				if (thePrefs.GetDebug2Disk())
					theVerboseLog.Logf(_T("%hs: Error: Failed to send TM_CONSOLETHREADEVENT to main window - error %u"), __FUNCTION__, ::GetLastError());
			}
		}
	}

	// Returning FALSE does not cause Windows to immediately terminate the process. Though,
	// that only depends on the next registered console control handler. The default seems
	// to wait 20 sec. until the process has terminated. After that timeout Windows
	// nevertheless terminates the process.
	//
	// For whatever unknown reason, this is *not* always true!? It may happen that Windows
	// terminates the process *before* the 20 sec. timeout if (and only if) the console
	// control handler thread has already terminated. So, we have to take care that we do not
	// exit this thread before the main thread has called 'ExitProcess' (in a synchronous
	// way) -- see also the 'SendMessage' above.
	if (thePrefs.GetDebug2Disk())
		theVerboseLog.Logf(_T("%hs: returning"), __FUNCTION__);
	return FALSE; // FALSE: Let the system kill the process with the default handler.
}

void CemuleApp::UpdateLargeIconSize()
{
	// initialize with system values in case we don't find the Shell's registry key
	m_sizBigSystemIcon.cx = ::GetSystemMetrics(SM_CXICON);
	m_sizBigSystemIcon.cy = ::GetSystemMetrics(SM_CYICON);

	// get the Shell's registry key for the large icon size - the large icons which are
	// returned by the Shell are based on that size rather than on the system icon size
	CRegKey key;
	if (key.Open(HKEY_CURRENT_USER, _T("Control Panel\\desktop\\WindowMetrics"), KEY_READ) == ERROR_SUCCESS) {
		TCHAR szShellLargeIconSize[12];
		ULONG ulChars = _countof(szShellLargeIconSize);
		if (key.QueryStringValue(_T("Shell Icon Size"), szShellLargeIconSize, &ulChars) == ERROR_SUCCESS) {
			UINT uIconSize = 0;
			if (_stscanf(szShellLargeIconSize, _T("%u"), &uIconSize) == 1 && uIconSize > 0) {
				m_sizBigSystemIcon.cx = uIconSize;
				m_sizBigSystemIcon.cy = uIconSize;
			}
		}
	}
}

void CemuleApp::ResetStandByIdleTimer()
{
	// Prevent system from falling asleep if connected or there are ongoing data transfers (upload or download)
	// Since Windows 11 there is no option to reset the idle timer
	if (IsConnected()
		|| (uploadqueue != NULL && uploadqueue->GetUploadQueueLength() > 0)
		|| (downloadqueue != NULL && downloadqueue->GetDatarate() > 0))
	{
		if (!m_bStandbyOff && ::SetThreadExecutionState(ES_SYSTEM_REQUIRED | ES_CONTINUOUS))
			m_bStandbyOff = true;
	} else if (m_bStandbyOff && ::SetThreadExecutionState(ES_CONTINUOUS))
		m_bStandbyOff = false;
}

bool CemuleApp::IsEd2kFriendLinkInClipboard()
{
	static const CHAR _szEd2kFriendLink[] = "ed2k://|friend|";
	return IsEd2kLinkInClipboard(_szEd2kFriendLink, (sizeof(_szEd2kFriendLink) / sizeof(_szEd2kFriendLink[0])) - 1);
}

void CemuleApp::Backup(bool bOnExit)
{
	if (!bOnExit && theApp.IsClosing())
		return;

	if (::InterlockedCompareExchange(&m_lBackupWorkerActive, 0, 0) != 0) {
		AddLogLine(true, GetResString(_T("BACKUP_IN_PROGRESS")));
		return;
	}

	if (bOnExit) {
		BackupMain(); // We'll backup on main thread during exit to keep the shutdown path synchronized.
		return;
	}

	if (GetWorkerTopologyState(WorkerTopologyPersistence) == WorkerTopologyStopped && !StartPersistenceWorker()) {
		AddDebugLogLine(DLP_HIGH, false, _T("Backup request could not start PersistenceWorker.\n"));
		return;
	}

	if (::InterlockedCompareExchange(&m_lBackupWorkerActive, 1, 0) != 0) {
		AddLogLine(true, GetResString(_T("BACKUP_IN_PROGRESS")));
		return;
	}

	SWorkerTopologyItem item;
	item.m_eRole = WorkerTopologyPersistence;
	item.m_eType = WorkerTopologyItemPersistenceSave;
	item.m_dwCreatedTick = ::GetTickCount();
	item.m_dwDueTick = item.m_dwCreatedTick;
	item.m_strStage = _T("backup");
	item.m_strCoalesceKey = _T("backup");
	if (!QueuePersistenceWorkerItem(item)) {
		::InterlockedExchange(&m_lBackupWorkerActive, 0);
		AddDebugLogLine(DLP_HIGH, false, _T("Backup request could not queue PersistenceWorker item.\n"));
	}
}

void CemuleApp::BackupMain()
{
	bool error = false;

	try {
		LPCTSTR extensionsToBack[] = { _T("*.ini"), _T("*.dat"), _T("*.met"), _T("*.conf"), _T("*.bak"), _T("downloads.txt"), _T("download_inspector.txt") };
		WIN32_FIND_DATA findData;
		HANDLE hSearch;
		CString configDir = CString(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));
		if (!configDir.IsEmpty() && configDir[configDir.GetLength() - 1] != _T('\\'))
			configDir += _T('\\');

		// Ensure base Backup directory exists (long-path aware)
		CString backupBase = configDir + _T("Backup\\");
		WIN32_FILE_ATTRIBUTE_DATA fad = { 0 };
		bool baseExists = (::GetFileAttributesEx(PreparePathForWin32LongPath(backupBase), GetFileExInfoStandard, &fad) != 0) && ((fad.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0);
		if (!baseExists) {
			if (!::CreateDirectory(PrepareDirectoryPathForWin32LongPath(backupBase), NULL)) {
				theApp.QueueDebugLogLineEx(LOG_ERROR, _T("[CemuleApp::BackupMain] Failed to create backup base directory: %s (err=%u)"), (LPCTSTR)EscPercent(backupBase), GetLastError());
				error = true; // Still continue to try, but mark error
			} else {
				// Set requested compression state on base directory
				SetDirectoryCompression(backupBase, thePrefs.GetBackupCompressed());
			}
		} else {
			// Directory exists, ensure compression state matches preference
			SetDirectoryCompression(backupBase, thePrefs.GetBackupCompressed());
		}

		// Get current time for folder naming
		SYSTEMTIME st;
		::GetLocalTime(&st);
		CString newDirName;
		newDirName.Format(_T("%04d%02d%02d_%02d%02d%02d"), st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

		// Enumerate existing backup folders and manage retention
		std::vector<CString> backupFolders;
		WIN32_FIND_DATA wfd;
		{
			CString pattern = backupBase + _T("*");
			HANDLE hFind = ::FindFirstFile(PreparePathForWin32LongPath(pattern), &wfd);
			if (hFind != INVALID_HANDLE_VALUE) {
				do {
					if ((wfd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0) {
						CString folderName = wfd.cFileName;
						if (folderName != _T("." ) && folderName != _T("..")) {
							std::wregex datePattern(LR"(^\d{8}_\d{6}$)");
							if (std::regex_match((LPCTSTR)folderName, datePattern))
								backupFolders.push_back(folderName);
						}
					}
				} while (::FindNextFile(hFind, &wfd));
				::FindClose(hFind);
			}
		}

		std::sort(backupFolders.begin(), backupFolders.end(), [](const CString& a, const CString& b) { return a < b; });

		while (backupFolders.size() >= (size_t)thePrefs.GetBackupMax()) {
			CString oldestBackup = backupBase + backupFolders.front();
			std::error_code ec;
			CString longOld = PreparePathForWin32LongPath(oldestBackup);
			std::filesystem::remove_all((LPCTSTR)longOld, ec);
			if (!ec) {
				backupFolders.erase(backupFolders.begin());
			} else {
				theApp.QueueDebugLogLineEx(LOG_ERROR, _T("[CemuleApp::BackupMain] Deleting old backup directory failed: %s (ec=%d)"), (LPCTSTR)EscPercent(oldestBackup), (int)ec.value());
				break; // Avoid infinite loop on persistent failure
			}
		}

		// Create new backup directory with the current timestamp
		CString newBackupDir = backupBase + newDirName;
		if (!::CreateDirectory(PrepareDirectoryPathForWin32LongPath(newBackupDir), NULL)) {
			theApp.QueueDebugLogLineEx(LOG_ERROR, _T("[CemuleApp::BackupMain] Failed to create new backup directory: %s (err=%u)"), (LPCTSTR)EscPercent(newBackupDir), GetLastError());
			error = true;
		} else {
			// Ensure compression state for the new backup directory before copying files
			SetFileOrDirectoryCompression(newBackupDir, thePrefs.GetBackupCompressed());

			// Backup the files with the specified extensions from configDir
			for (int i = 0; i < (int)(sizeof(extensionsToBack) / sizeof(extensionsToBack[0])); ++i) {
				CString pattern = configDir + extensionsToBack[i];
				hSearch = ::FindFirstFile(PreparePathForWin32LongPath(pattern), &findData);
				if (hSearch == INVALID_HANDLE_VALUE) {
					// No files for this pattern; continue with next
					continue;
				}

				for (;;) {
					CString src = configDir + findData.cFileName;
					CString dst = newBackupDir + _T("\\") + findData.cFileName;
					::CopyFile(PreparePathForWin32LongPath(src), PreparePathForWin32LongPath(dst), FALSE);

					if (!::FindNextFile(hSearch, &findData)) {
						DWORD gle = ::GetLastError();
						::FindClose(hSearch);
						if (gle == ERROR_NO_MORE_FILES)
							break;
						else {
							error = true;
							break;
						}
					}
				}
			}
			// As a safety net, ensure directory tree compression matches preference
			SetDirectoryCompression(newBackupDir, thePrefs.GetBackupCompressed());
		}
	} catch (CException* ex) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("[CemuleApp::BackupMain] Unhandled exception in backup process: %s"), (LPCTSTR)EscPercent(CString(CExceptionStr(*ex))));
		ex->Delete();
		ASSERT(0);
		error = true;
	} catch (...) {
		theApp.QueueDebugLogLineEx(LOG_ERROR, _T("[CemuleApp::BackupMain] Unhandled exception in backup process."));
		ASSERT(0);
		error = true;
	}

	if (error)
		theApp.QueueLogLineEx(LOG_ERROR, GetResString(_T("BACKUPERROR")), _T("BACKUPERROR"));

	theApp.QueueLogLine(true, GetResString(_T("BACKUP_COMPLETED")));
	tLastBackupTime = time(NULL); // Update last backup time
}

time_t CemuleApp::GetLastBackupTime()
{
	CString szDirPath(static_cast<LPCTSTR>(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR)));
	szDirPath += _T("Backup\\");
    WIN32_FIND_DATA findFileData;
    HANDLE hFind = FindFirstFile(PreparePathForWin32LongPath(szDirPath + _T("*")), &findFileData); // Find all folders (long-path aware)

	if (hFind != INVALID_HANDLE_VALUE) {
		do {
			if (findFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
				CString folderName = findFileData.cFileName;

				// Check if the folder name matches the pattern YYYYMMDD_HHMMSS using regex
				std::wregex datePattern(LR"(^\d{8}_\d{6}$)");  // YYYYMMDD_HHMMSS format
				if (std::regex_match((LPCTSTR)folderName, datePattern)) {
					// Extract date and time from the folder name
					struct tm tm = { 0 };
					if (_stscanf_s(folderName, _T("%4d%2d%2d_%2d%2d%2d"),
						&tm.tm_year, &tm.tm_mon, &tm.tm_mday,
						&tm.tm_hour, &tm.tm_min, &tm.tm_sec) == 6) {
						tm.tm_year -= 1900;  // Adjust year
						tm.tm_mon -= 1;      // Adjust month
						FindClose(hFind);
						return mktime(&tm);  // Return the time in seconds since epoch
					}
				}
			}
            } while (FindNextFile(hFind, &findFileData) != 0);
            FindClose(hFind);
    }
    return 0;  // Return 0 if no valid folder is found
}

// Threadpool I/O based always-on directory watcher (compact, exception-free)
struct TPIODirWatch {
	HANDLE hDir;
	TPW_PTP_IO tpIo;
	OVERLAPPED ov;
	HANDLE hEvent;            // Manual-reset event bound to overlapped
	BYTE* pBuf;
	DWORD cbBuf;
	CString rootPath;
	volatile LONG nCallbacks; // Number of in-flight I/O callbacks (atomic)
	volatile LONG nArmed;     // 1 if an overlapped ReadDirectoryChangesW is armed
	volatile LONG nRetiring;  // 1 once runtime rebuild has canceled this watch and it must not rearm
	volatile LONG nCleanupQueued; // 1 once the watch has been queued for deferred cleanup
	TPIODirWatch() : hDir(INVALID_HANDLE_VALUE), tpIo(NULL), hEvent(NULL), pBuf(NULL), cbBuf(0), nCallbacks(0), nArmed(0), nRetiring(0), nCleanupQueued(0) { ZeroMemory(&ov, sizeof(ov)); }
};

// Globals for watcher state (file scope, not exposed)
static CRITICAL_SECTION g_tpNewSharedDirsCS;
static CRITICAL_SECTION g_tpCleanupCS;
static CRITICAL_SECTION g_tpWatchListCS;
static std::vector<TPIODirWatch*>* g_tpWatches = NULL;
static std::vector<TPIODirWatch*>* g_tpCleanup = NULL; // Deferred cleanup queue
static std::vector<CString>* g_tpNewSharedDirs = NULL;
static CRITICAL_SECTION g_tpChangedDirsCS;
static std::vector<CString>* g_tpChangedDirs = NULL;
static CRITICAL_SECTION g_tpChangedFilesCS;
static std::vector<CString>* g_tpChangedFiles = NULL;
static volatile LONG g_tpTimerArmed = 0;
static volatile LONG g_tpStopping = 0;
static volatile LONG g_tpForceTreeReload = 0; // Set to 1 by I/O cb when directory-level change requires tree rebuild
static volatile LONG g_tpCleanupTimerArmed = 0;
static volatile LONG g_tpRefreshPending = 0;
static volatile LONG g_tpCleanupPending = 0;
static volatile LONG g_lDirWatchChangeGeneration = 1;
static bool g_tpNewSharedDirsCSInit = false;
static bool g_tpCleanupCSInit = false;
static bool g_tpWatchListCSInit = false;
static bool g_tpChangedDirsCSInit = false;
static bool g_tpChangedFilesCSInit = false;
static DWORD g_dwRefreshDueAt = 0;
static DWORD g_dwCleanupDueAt = 0;

// Deleted paths are collected on the TP I/O callback thread and coalesced. GUI thread can (now or later) consume this list to proactively prune waiters.
static CRITICAL_SECTION g_tpDelCS;
static bool g_tpCsInit = false;
static std::vector<CString>* g_tpDeleted = NULL;

static const DWORD kDirWatchFileRefreshDelayMs = 300;
static const DWORD kDirWatchStructuralRefreshDelayMs = 2000;

static LONG IncrementDirWatchChangeGeneration()
{
	return InterlockedIncrement(&g_lDirWatchChangeGeneration);
}

static void ScheduleDirWatchRefresh(bool bStructural)
{
	if (bStructural)
		InterlockedExchange(&g_tpForceTreeReload, 1);

	const bool bUseStructuralDelay = bStructural || InterlockedCompareExchange(&g_tpForceTreeReload, 0, 0) != 0;
	InterlockedExchange(&g_tpRefreshPending, 1);
	g_dwRefreshDueAt = ::GetTickCount() + (bUseStructuralDelay ? kDirWatchStructuralRefreshDelayMs : kDirWatchFileRefreshDelayMs);
}

static std::vector<TPIODirWatch*>& GetDirWatchActiveList()
{
	if (!g_tpWatchListCSInit) {
		InitializeCriticalSection(&g_tpWatchListCS);
		g_tpWatchListCSInit = true;
	}

	if (g_tpWatches == NULL)
		g_tpWatches = new std::vector<TPIODirWatch*>();
	return *g_tpWatches;
}

static std::vector<TPIODirWatch*>& GetDirWatchCleanupList()
{
	if (g_tpCleanup == NULL)
		g_tpCleanup = new std::vector<TPIODirWatch*>();
	return *g_tpCleanup;
}

static void AddDirWatchToActiveList(TPIODirWatch* pW)
{
	if (pW == NULL)
		return;

	GetDirWatchActiveList();
	EnterCriticalSection(&g_tpWatchListCS);
	g_tpWatches->push_back(pW);
	LeaveCriticalSection(&g_tpWatchListCS);
}

static void RemoveDirWatchFromActiveList(TPIODirWatch* pW)
{
	if (pW == NULL || !g_tpWatchListCSInit)
		return;

	EnterCriticalSection(&g_tpWatchListCS);
	if (g_tpWatches != NULL) {
		for (std::vector<TPIODirWatch*>::iterator it = g_tpWatches->begin(); it != g_tpWatches->end(); ++it) {
			if (*it == pW) {
				g_tpWatches->erase(it);
				break;
			}
		}
	}
	LeaveCriticalSection(&g_tpWatchListCS);
}

static void DetachAllDirWatches(std::vector<TPIODirWatch*>& outWatches)
{
	outWatches.clear();
	if (!g_tpWatchListCSInit)
		return;

	EnterCriticalSection(&g_tpWatchListCS);
	if (g_tpWatches != NULL) {
		outWatches.swap(*g_tpWatches);
		delete g_tpWatches;
		g_tpWatches = NULL;
	}
	LeaveCriticalSection(&g_tpWatchListCS);
}

static size_t GetActiveDirWatchCount()
{
	if (!g_tpWatchListCSInit)
		return 0;

	size_t nCount = 0;
	EnterCriticalSection(&g_tpWatchListCS);
	if (g_tpWatches != NULL)
		nCount = g_tpWatches->size();
	LeaveCriticalSection(&g_tpWatchListCS);
	return nCount;
}

// Root set change detector (lightweight): hash + periodic TP timer.
static volatile LONG g_tpRebuildingRoots = 0; // 1 while a roots reload or missing-root rearm is already pending/running
static volatile DWORD g_tpRootsHash = 0; // Hash of the configured roots already acknowledged by the UI reload flow
static volatile DWORD g_tpActiveRootsHash = 0; // Hash of the roots currently represented by active watchers
static volatile DWORD g_tpMissingRootRetryAt = 0; // Next retry tick for incomplete watcher sets
static volatile LONG g_tpStartupArmPending = 0; // Defer initial watcher arming until the UI is already pumping messages.
static const DWORD kDirWatchMissingRootRetryDelayMs = SEC2MS(60);

// Forward declerations
static VOID CALLBACK DirWatchRootsTimerCb(PVOID /*inst*/, PVOID /*ctx*/, PVOID /*timer*/);
static VOID CALLBACK DirWatchTimerCb(PVOID /*instance*/, PVOID /*context*/, PVOID /*timer*/);
static DWORD ComputeConfiguredWatchRootsHash();

static void AddDirWatchRoot(std::vector<CString>& roots, const CString& rawPath)
{
	CString path(rawPath);
	if (path.IsEmpty())
		return;

	if (path.Right(1) != _T("\\"))
		path += _T("\\");

	for (std::vector<CString>::iterator it = roots.begin(); it != roots.end();) {
		if (EqualPaths(*it, path) || IsSubDirectoryOf(path, *it))
			return;

		if (IsSubDirectoryOf(*it, path))
			it = roots.erase(it);
		else
			++it;
	}

	roots.push_back(path);
}

static __forceinline void HashDirWatchRootCi(DWORD& h, const CString& s)
{
	// Simple FNV-1a 32-bit over uppercased UTF-16 chars.
	const UINT32 FNV_PRIME = 16777619u;
	UINT32 v = h ? h : 2166136261u;

	for (int i = 0; i < s.GetLength(); ++i) {
		WCHAR c = s[i];
		if (c >= L'a' && c <= L'z') c = (WCHAR)(c - L'a' + L'A');
		v ^= (UINT32)c;
		v *= FNV_PRIME;
	}

	h = v;
}

static DWORD ComputeDirWatchRootsHash(const std::vector<CString>& roots)
{
	DWORD h = 0;
	for (size_t i = 0; i < roots.size(); ++i) {
		CString norm = roots[i];
		if (!norm.IsEmpty() && norm[norm.GetLength() - 1] == _T('\\'))
			norm = norm.Left(norm.GetLength() - 1);

		HashDirWatchRootCi(h, norm);
	}

	return h;
}

static void CollectConfiguredWatchRoots(std::vector<CString>& roots)
{
	roots.clear();
	roots.reserve(static_cast<size_t>(thePrefs.GetCatCount() + 8));
	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);

	// Incoming
	AddDirWatchRoot(roots, thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

	// Categories
	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i)
		AddDirWatchRoot(roots, thePrefs.GetCatPath(i));

	// Shared directories
	POSITION pos = sharedDirs.GetHeadPosition();
	while (pos)
		AddDirWatchRoot(roots, sharedDirs.GetNext(pos));
}

static DWORD ComputeConfiguredWatchRootsHash()
{
	std::vector<CString> roots;
	CollectConfiguredWatchRoots(roots);
	return ComputeDirWatchRootsHash(roots);
}

static DWORD ComputeActiveWatchRootsHash()
{
	std::vector<CString> roots;
	if (g_tpWatchListCSInit) {
		EnterCriticalSection(&g_tpWatchListCS);
		if (g_tpWatches != NULL) {
			roots.reserve(g_tpWatches->size());
			for (size_t i = 0; i < g_tpWatches->size(); ++i) {
				TPIODirWatch* pW = (*g_tpWatches)[i];
				if (pW != NULL && InterlockedCompareExchange(&pW->nRetiring, 0, 0) == 0)
					AddDirWatchRoot(roots, pW->rootPath);
			}
		}
		LeaveCriticalSection(&g_tpWatchListCS);
	}

	return ComputeDirWatchRootsHash(roots);
}

static void EnsureDirWatchCleanupInitialized()
{
	if (!g_tpCleanupCSInit) {
		InitializeCriticalSection(&g_tpCleanupCS);
		g_tpCleanup = new std::vector<TPIODirWatch*>();
		g_tpCleanupCSInit = true;
		return;
	}

	EnterCriticalSection(&g_tpCleanupCS);
	if (g_tpCleanup == NULL)
		g_tpCleanup = new std::vector<TPIODirWatch*>();
	LeaveCriticalSection(&g_tpCleanupCS);
}

static void QueueDirWatchForDeferredCleanup(TPIODirWatch* pW)
{
	if (pW == NULL)
		return;
	if (InterlockedCompareExchange(&pW->nCleanupQueued, 1, 0) != 0)
		return;

	EnsureDirWatchCleanupInitialized();
	EnterCriticalSection(&g_tpCleanupCS);
	GetDirWatchCleanupList().push_back(pW);
	LeaveCriticalSection(&g_tpCleanupCS);
	InterlockedExchange(&g_tpCleanupPending, 1);
	g_dwCleanupDueAt = ::GetTickCount() + 50;
}

static bool TryDrainDirWatchShutdown(TPIODirWatch* pW, DWORD dwTimeoutMs)
{
	if (pW == NULL)
		return true;

	const DWORD dwStart = ::GetTickCount();
	const DWORD dwPollMs = 25;
	bool bSawCompletion = (pW->hEvent == NULL);

	for (;;) {
		if (pW->hEvent != NULL) {
			const DWORD dwWait = ::WaitForSingleObject(pW->hEvent, 0);
			if (dwWait == WAIT_OBJECT_0)
				bSawCompletion = true;
			else if (dwWait == WAIT_FAILED)
				return false;
		}

		const LONG nArmed = InterlockedCompareExchange(&pW->nArmed, 0, 0);
		const LONG nCallbacks = InterlockedCompareExchange(&pW->nCallbacks, 0, 0);
		if (nArmed == 0 && nCallbacks == 0) {
			if (!bSawCompletion && pW->hEvent != NULL)
				return false;

			return (pW->tpIo == NULL) || (TPW_WaitForThreadpoolIoCallbacks(pW->tpIo, FALSE) != FALSE);
		}

		const DWORD dwElapsed = ::GetTickCount() - dwStart;
		if (dwElapsed >= dwTimeoutMs)
			return false;

		const DWORD dwSleep = min(dwPollMs, dwTimeoutMs - dwElapsed);
		if (pW->hEvent != NULL)
			::WaitForSingleObject(pW->hEvent, dwSleep);
		else
			::Sleep(dwSleep);
	}
}

static void DestroyDirWatchSync(TPIODirWatch* pW)
{
	if (pW == NULL)
		return;

	constexpr DWORD kDirWatchShutdownDrainTimeoutMs = 1000;
	InterlockedExchange(&pW->nRetiring, 1);
	RemoveDirWatchFromActiveList(pW);

	if (pW->hEvent == NULL)
		pW->hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);

	if (pW->hDir != INVALID_HANDLE_VALUE)
		// Cancel the overlapped watcher I/O; the completion callback drains the started TP I/O.
		TPW_CancelIoEx(pW->hDir, &pW->ov);

	if (!TryDrainDirWatchShutdown(pW, kDirWatchShutdownDrainTimeoutMs)) {
		TRACE(_T("Shared Files Watcher: deferred release skipped during shutdown because callbacks did not drain safely.\n"));
		return; // Leak on shutdown rather than risking a hang or use-after-free.
	}

	if (pW->tpIo != NULL)
		TPW_CloseThreadpoolIoX(&pW->tpIo);

	TPW_SafeCloseHandle(&pW->hDir);
	TPW_SafeCloseHandleNull(&pW->hEvent);

	if (pW->pBuf != NULL) {
		free(pW->pBuf);
		pW->pBuf = NULL;
	}

	delete pW;
}

static VOID CALLBACK DirWatchRootsTimerCb(PVOID /*inst*/, PVOID /*ctx*/, PVOID /*timer*/)
{
	if (InterlockedCompareExchange(&g_tpStopping, 0, 0) != 0)
		return; // App is stopping; ignore

	const DWORD cur = ComputeConfiguredWatchRootsHash();
	const DWORD prev = (DWORD)InterlockedCompareExchange((LONG*)&g_tpRootsHash, 0, 0);
	if (cur != prev) {
		IncrementDirWatchChangeGeneration();
		if (CemuleDlg* pDlg = theApp.emuledlg) {
			if (pDlg->sharedfileswnd && ::IsWindow(pDlg->sharedfileswnd->m_hWnd) && InterlockedCompareExchange(&g_tpRebuildingRoots, 1, 0) == 0) {
				if (!pDlg->sharedfileswnd->PostAutoReloadSharedFilesAsync(1))
					InterlockedExchange(&g_tpRebuildingRoots, 0);
			}
		}

		return;
	}

	const DWORD active = (DWORD)InterlockedCompareExchange((LONG*)&g_tpActiveRootsHash, 0, 0);
	if (cur == active)
		return;

	const DWORD retryAt = (DWORD)InterlockedCompareExchange((LONG*)&g_tpMissingRootRetryAt, 0, 0);
	if (retryAt != 0 && (LONG)(::GetTickCount() - retryAt) < 0)
		return;

	if (InterlockedCompareExchange(&g_tpRebuildingRoots, 1, 0) == 0) {
		TRACE(_T("Shared Files Watcher: retrying incomplete watcher root set.\n"));
		theApp.StartDirWatchTP();
	}
}

static __forceinline void PushDeletedSharedDirPath(const CString& full)
{
	if (full.IsEmpty())
		return;

	if (!g_tpCsInit)
		return;

	EnterCriticalSection(&g_tpDelCS);
	if (g_tpDeleted == NULL)
		g_tpDeleted = new std::vector<CString>();
	g_tpDeleted->push_back(full);
	LeaveCriticalSection(&g_tpDelCS);
}

static CString NormalizeWatchedDirPath(const CString& rawPath)
{
	CString path(rawPath);
	if (!path.IsEmpty())
		slosh(path);
	return path;
}

static __forceinline void PushChangedDirectoryPath(const CString& full)
{
	if (full.IsEmpty())
		return;

	if (!g_tpChangedDirsCSInit || g_tpChangedDirs == NULL)
		return;

	const CString normalized(NormalizeWatchedDirPath(full));
	EnterCriticalSection(&g_tpChangedDirsCS);
	if (g_tpChangedDirs != NULL)
		g_tpChangedDirs->push_back(normalized);
	LeaveCriticalSection(&g_tpChangedDirsCS);
}

static bool VectorContainsExactPath(const std::vector<CString>& paths, const CString& path)
{
	for (size_t i = 0; i < paths.size(); ++i) {
		if (EqualPaths(paths[i], path))
			return true;
	}
	return false;
}

static bool ListContainsExactPath(const CStringList& paths, const CString& path)
{
	for (POSITION pos = paths.GetHeadPosition(); pos != NULL;) {
		if (EqualPaths(paths.GetNext(pos), path))
			return true;
	}
	return false;
}

static bool IsFixedDrivePath(const CString& rawPath)
{
	CString path(NormalizeWatchedDirPath(rawPath));
	const int iDrive = ::PathGetDriveNumber(path);
	if (iDrive < 0 || iDrive > 25)
		return false;

	TCHAR szRootPath[4] = _T("@:\\");
	*szRootPath = (TCHAR)(_T('A') + iDrive);
	return ::GetDriveType(szRootPath) == DRIVE_FIXED;
}

enum DIRWATCH_DIR_PRESENCE
{
	DIRWATCH_PRESENCE_UNKNOWN = 0,
	DIRWATCH_PRESENCE_MISSING,
	DIRWATCH_PRESENCE_EXISTS
};

static DIRWATCH_DIR_PRESENCE QueryDirectoryPresenceLongPath(const CString& rawPath)
{
	CString path(NormalizeWatchedDirPath(rawPath));
	if (path.IsEmpty())
		return DIRWATCH_PRESENCE_UNKNOWN;

	::SetLastError(ERROR_SUCCESS);
	const CString longPath = PreparePathForWin32LongPath(path);
	const DWORD attrs = ::GetFileAttributes(longPath);
	if (attrs == INVALID_FILE_ATTRIBUTES) {
		const DWORD dwError = ::GetLastError();
		if (dwError == ERROR_FILE_NOT_FOUND || dwError == ERROR_PATH_NOT_FOUND)
			return DIRWATCH_PRESENCE_MISSING;
		return DIRWATCH_PRESENCE_UNKNOWN;
	}

	return ((attrs & FILE_ATTRIBUTE_DIRECTORY) != 0) ? DIRWATCH_PRESENCE_EXISTS : DIRWATCH_PRESENCE_UNKNOWN;
}

static DIRWATCH_DIR_PRESENCE QueryDirectoryPresenceLongPathCached(const CString& rawPath, std::vector<CString>& existingPaths, std::vector<CString>& missingPaths, std::vector<CString>& unknownPaths)
{
	CString path(NormalizeWatchedDirPath(rawPath));
	if (VectorContainsExactPath(existingPaths, path))
		return DIRWATCH_PRESENCE_EXISTS;
	if (VectorContainsExactPath(missingPaths, path))
		return DIRWATCH_PRESENCE_MISSING;
	if (VectorContainsExactPath(unknownPaths, path))
		return DIRWATCH_PRESENCE_UNKNOWN;

	const DIRWATCH_DIR_PRESENCE presence = QueryDirectoryPresenceLongPath(path);
	if (presence == DIRWATCH_PRESENCE_EXISTS)
		existingPaths.push_back(path);
	else if (presence == DIRWATCH_PRESENCE_MISSING)
		missingPaths.push_back(path);
	else
		unknownPaths.push_back(path);
	return presence;
}

static bool DirectoryExistsLongPathCached(const CString& rawPath, std::vector<CString>& existingPaths, std::vector<CString>& missingPaths, std::vector<CString>& unknownPaths)
{
	return QueryDirectoryPresenceLongPathCached(rawPath, existingPaths, missingPaths, unknownPaths) == DIRWATCH_PRESENCE_EXISTS;
}

static bool DirectoryMissingLongPathCached(const CString& rawPath, std::vector<CString>& existingPaths, std::vector<CString>& missingPaths, std::vector<CString>& unknownPaths)
{
	return QueryDirectoryPresenceLongPathCached(rawPath, existingPaths, missingPaths, unknownPaths) == DIRWATCH_PRESENCE_MISSING;
}

static bool IsCoveredByExistingCollapsedRoot(const CString& rawPath, const CStringList& collapsedSharedDirs, std::vector<CString>& existingPaths, std::vector<CString>& missingPaths, std::vector<CString>& unknownPaths)
{
	CString path(NormalizeWatchedDirPath(rawPath));
	for (POSITION pos = collapsedSharedDirs.GetHeadPosition(); pos != NULL;) {
		CString root(NormalizeWatchedDirPath(collapsedSharedDirs.GetNext(pos)));
		if (EqualPaths(root, path) || !IsSubDirectoryOf(path, root))
			continue;
		if (DirectoryExistsLongPathCached(root, existingPaths, missingPaths, unknownPaths))
			return true;
	}
	return false;
}

static bool ShouldPruneMissingDeletedSharedRoot(const CString& path, const std::vector<CString>& deletedDirs, const CStringList& collapsedSharedDirs)
{
	if (thePrefs.m_bKeepUnavailableFixedSharedDirs || !ListContainsExactPath(collapsedSharedDirs, path))
		return false;

	for (size_t i = 0; i < deletedDirs.size(); ++i) {
		if (EqualPaths(deletedDirs[i], path))
			return true;
	}

	return false;
}

static void EnsureDeletedPathQueueInitialized()
{
	if (!g_tpCsInit) {
		InitializeCriticalSection(&g_tpDelCS);
		g_tpDeleted = new std::vector<CString>();
		g_tpCsInit = true;
		return;
	}

	EnterCriticalSection(&g_tpDelCS);
	if (g_tpDeleted == NULL)
		g_tpDeleted = new std::vector<CString>();
	LeaveCriticalSection(&g_tpDelCS);
}

static void SyncActiveWatchRootsHash()
{
	const DWORD activeHash = ComputeActiveWatchRootsHash();
	InterlockedExchange((LONG*)&g_tpActiveRootsHash, (LONG)activeHash);
}

static bool HandleRuntimeMissingWatchRoot(TPIODirWatch* pW);

static void RetireWatchAfterRuntimeFailure(TPIODirWatch* pW)
{
	if (pW == NULL || InterlockedCompareExchange(&pW->nRetiring, 1, 0) != 0)
		return;

	RemoveDirWatchFromActiveList(pW);

	if (pW->hDir != INVALID_HANDLE_VALUE)
		TPW_CancelIoEx(pW->hDir, &pW->ov);

	QueueDirWatchForDeferredCleanup(pW);
	SyncActiveWatchRootsHash();
	InterlockedExchange((LONG*)&g_tpMissingRootRetryAt, 0);
}

static void HandleRuntimeWatchRearmFailure(TPIODirWatch* pW)
{
	if (pW == NULL)
		return;

	if (!HandleRuntimeMissingWatchRoot(pW))
		RetireWatchAfterRuntimeFailure(pW);
}

static bool HandleRuntimeMissingWatchRoot(TPIODirWatch* pW)
{
	if (pW == NULL || QueryDirectoryPresenceLongPath(pW->rootPath) != DIRWATCH_PRESENCE_MISSING)
		return false;

	PushDeletedSharedDirPath(pW->rootPath);
	PushChangedDirectoryPath(pW->rootPath);
	IncrementDirWatchChangeGeneration();
	ScheduleDirWatchRefresh(true);
	RetireWatchAfterRuntimeFailure(pW);
	return true;
}

static bool HandleRuntimeWatchCompletionFailure(TPIODirWatch* pW, ULONG ioResult)
{
	if (pW == NULL || ioResult == ERROR_SUCCESS || ioResult == ERROR_OPERATION_ABORTED)
		return false;

	return HandleRuntimeMissingWatchRoot(pW);
}

static void DrainDeletedAutoSharedDirsFromQueue()
{
	if (!g_tpCsInit || g_tpDeleted == NULL || theApp.sharedfiles == NULL)
		return;

	std::vector<CString> todo;
	EnterCriticalSection(&g_tpDelCS);
	if (g_tpDeleted != NULL)
		todo.swap(*g_tpDeleted);
	LeaveCriticalSection(&g_tpDelCS);

	if (todo.empty())
		return;

	CStringList sharedDirs;
	thePrefs.CopySharedDirectoryList(sharedDirs);
	if (sharedDirs.IsEmpty())
		return;

	CStringList excludedSharedDirs;
	if (theApp.sharedfiles != NULL)
		theApp.sharedfiles->CopyExcludedSharedDirectories(excludedSharedDirs);

	const bool bAutoShareSubdirs = thePrefs.GetAutoShareSubdirs();
	CStringList collapsedSharedDirs;
	collapsedSharedDirs.AddTail(&sharedDirs);
	CPreferences::CollapseSharedDirsToRoots(collapsedSharedDirs, excludedSharedDirs.IsEmpty() ? NULL : &excludedSharedDirs);

	std::vector<CString> deletedDirs;
	deletedDirs.reserve(todo.size());
	for (size_t i = 0; i < todo.size(); ++i) {
		CString deleted(NormalizeWatchedDirPath(todo[i]));
		if (!deleted.IsEmpty() && !VectorContainsExactPath(deletedDirs, deleted))
			deletedDirs.push_back(deleted);
	}
	if (deletedDirs.empty())
		return;

	CStringList keptSharedDirs;
	std::vector<CString> existingPaths;
	std::vector<CString> missingPaths;
	std::vector<CString> unknownPaths;
	bool bRemoved = false;

	for (POSITION pos = sharedDirs.GetHeadPosition(); pos != NULL;) {
		CString current(sharedDirs.GetNext(pos));
		CString normalizedCurrent(NormalizeWatchedDirPath(current));
		bool bMatchesDeletedTree = false;
		for (size_t i = 0; i < deletedDirs.size(); ++i) {
			if (EqualPaths(normalizedCurrent, deletedDirs[i]) || IsSubDirectoryOf(normalizedCurrent, deletedDirs[i])) {
				bMatchesDeletedTree = true;
				break;
			}
		}

		const bool bIsCollapsedRoot = ListContainsExactPath(collapsedSharedDirs, normalizedCurrent);
		const bool bPruneMissingDeletedRoot = ShouldPruneMissingDeletedSharedRoot(normalizedCurrent, deletedDirs, collapsedSharedDirs);
		const bool bPruneRedundantDeletedChild = bAutoShareSubdirs && !bIsCollapsedRoot && IsCoveredByExistingCollapsedRoot(normalizedCurrent, collapsedSharedDirs, existingPaths, missingPaths, unknownPaths);

		if (!bMatchesDeletedTree
			|| !IsFixedDrivePath(normalizedCurrent)
			|| !DirectoryMissingLongPathCached(normalizedCurrent, existingPaths, missingPaths, unknownPaths)
			|| (!bPruneMissingDeletedRoot && !bPruneRedundantDeletedChild))
		{
			keptSharedDirs.AddTail(current);
			continue;
		}

		TRACE(_T("Shared Files Watcher: pruned stale auto-shared deleted directory '%s'.\n"), (LPCTSTR)normalizedCurrent);
		bRemoved = true;
	}

	if (!bRemoved)
		return;

	thePrefs.ReplaceSharedDirectoryList(keptSharedDirs);
	thePrefs.SaveSharedFolders();
}

// Parse FILE_NOTIFY_INFORMATION buffer and collect deleted/renamed-old paths.
static void CollectDeletedFromBuffer(struct TPIODirWatch* pW, DWORD bytes)
{
	if (!pW || !pW->pBuf)
		return;

	// Clamp to the allocated buffer size and bail out on tiny payloads.
	DWORD remaining = (bytes < pW->cbBuf) ? (DWORD)bytes : pW->cbBuf;
	if (remaining < sizeof(FILE_NOTIFY_INFORMATION))
		return;

	// Collect removed/renamed-old entries for this I/O batch and decide if a tree rebuild is needed.
	std::vector<CString> batch;
	batch.reserve(32);

	BYTE* cur = pW->pBuf;
	BYTE* bufEnd = pW->pBuf + pW->cbBuf; // Track buffer end for safety
	while (remaining >= sizeof(FILE_NOTIFY_INFORMATION)) {
		FILE_NOTIFY_INFORMATION* p = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(cur);

		// Defensive checks against corrupted records and buffer overrun.
		if (cur + sizeof(FILE_NOTIFY_INFORMATION) > bufEnd)
			break; // Prevent buffer overrun

		DWORD nameBytes = p->FileNameLength;
		if (nameBytes % sizeof(WCHAR))
			break; // Invalid name length

		// Ensure the entire record (including filename) fits in the buffer.
		DWORD recordSize = FIELD_OFFSET(FILE_NOTIFY_INFORMATION, FileName) + nameBytes;
		if (cur + recordSize > bufEnd)
			break; // Record extends beyond buffer

		if (p->Action == FILE_ACTION_REMOVED || p->Action == FILE_ACTION_RENAMED_OLD_NAME) {
			CString name(p->FileName, static_cast<int>(nameBytes / sizeof(WCHAR)));
			CString full = pW->rootPath;

			if (!full.IsEmpty() && full[full.GetLength() - 1] != _T('\\'))
				full += _T('\\');

			full += name;
			batch.push_back(full);
		}

		// If this is the last entry (NextEntryOffset == 0), stop after processing it.
		if (p->NextEntryOffset == 0)
			break;

		// Validate NextEntryOffset before advancing.
		if (p->NextEntryOffset < sizeof(FILE_NOTIFY_INFORMATION) || p->NextEntryOffset > remaining)
			break; // Corrupt chain; stop parsing defensively

		cur += p->NextEntryOffset;
		remaining -= p->NextEntryOffset;
	}

	// Heuristic 1: If we saw multiple removals under the same prefix, the common prefix entry is likely a directory.
	std::vector<CString> dirCandidates;
	dirCandidates.reserve(batch.size());
	for (size_t i = 0; i < batch.size(); ++i) {
		const CString& a = batch[i];
		const int la = a.GetLength();
		for (size_t j = 0; j < batch.size(); ++j) {
			if (i == j)
				continue;

			const CString& b = batch[j];
			if (b.GetLength() > la && _tcsnicmp(b, a, la) == 0 && b[la] == _T('\\')) {
				if (!VectorContainsExactPath(dirCandidates, a))
					dirCandidates.push_back(a);
				break;
			}
		}
	}

	// Heuristic 2: No dot after the last backslash (common directory naming).
	for (size_t i = 0; i < batch.size(); ++i) {
		const CString& s = batch[i];
		const int slash = s.ReverseFind(_T('\\'));
		const int dot = s.ReverseFind(_T('.'));
		if (slash >= 0 && (dot < 0 || dot < slash)) {
			if (!VectorContainsExactPath(dirCandidates, s))
				dirCandidates.push_back(s);
		}
	}

	// Promote to forced tree reload if a directory was (very likely) removed and queue those candidates for narrow UI-thread pruning.
	if (!dirCandidates.empty()) {
		InterlockedExchange(&g_tpForceTreeReload, 1);
		for (size_t i = 0; i < dirCandidates.size(); ++i)
			PushChangedDirectoryPath(dirCandidates[i]);
		if (thePrefs.GetAutoShareSubdirs()) {
			for (size_t i = 0; i < dirCandidates.size(); ++i)
				PushDeletedSharedDirPath(dirCandidates[i]);
		}
	}

}

static __forceinline void PushChangedFilePath(const CString& full)
{
	if (full.IsEmpty())
		return;

	if (!g_tpChangedFilesCSInit || g_tpChangedFiles == NULL)
		return;

	EnterCriticalSection(&g_tpChangedFilesCS);
	if (g_tpChangedFiles != NULL)
		g_tpChangedFiles->push_back(full);
	LeaveCriticalSection(&g_tpChangedFilesCS);
}

static VOID CALLBACK DirWatchTimerCb(PVOID /*instance*/, PVOID /*context*/, PVOID /*timer*/)
{
	// Coalesced GUI refresh after FS change; promote to forced tree reload if a dir-level change was seen.
	InterlockedExchange(&g_tpTimerArmed, 0);
	CemuleDlg* pDlg = theApp.emuledlg;

	if (pDlg != NULL && pDlg->sharedfileswnd != NULL && ::IsWindow(pDlg->sharedfileswnd->m_hWnd)) {
		WPARAM wp = (InterlockedExchange(&g_tpForceTreeReload, 0) != 0) ? 2 : 0; // 2 => force tree rebuild
		pDlg->sharedfileswnd->PostAutoReloadSharedFilesAsync(wp);
	}
}

static bool RearmWatch(TPIODirWatch* pW)
{
	if (!pW)
		return false;
	ZeroMemory(&pW->ov, sizeof(pW->ov));

	if (pW->hEvent == NULL) 
		pW->hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL); // Manual-reset, non-signaled

	::ResetEvent(pW->hEvent);
	pW->ov.hEvent = pW->hEvent; // Bind overlapped to event for deterministic shutdown wait
	DWORD dwBytes = 0;
	TPW_StartThreadpoolIo(pW->tpIo);
	const DWORD notifyMask = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
	BOOL ok = ::ReadDirectoryChangesW(pW->hDir, pW->pBuf, pW->cbBuf, TRUE, notifyMask, &dwBytes, &pW->ov, NULL);
	
	if (!ok) {
		TPW_CancelThreadpoolIo(pW->tpIo);
		return false; 
	}

	InterlockedExchange(&pW->nArmed, 1);
	return true;
}


void CemuleApp::SyncDirWatchRootsHash()
{
	// UI thread: acknowledge the current roots after a tree reload or watcher rebuild.
	const DWORD cur = ComputeConfiguredWatchRootsHash();
	InterlockedExchange((LONG*)&g_tpRootsHash, (LONG)cur);
	InterlockedExchange(&g_tpRebuildingRoots, 0);
}

void CemuleApp::QueueStartupDirWatchInit()
{
	InterlockedExchange((LONG*)&g_tpRootsHash, (LONG)ComputeConfiguredWatchRootsHash());
	InterlockedExchange((LONG*)&g_tpActiveRootsHash, 0);
	InterlockedExchange((LONG*)&g_tpMissingRootRetryAt, 0);
	InterlockedExchange(&g_tpRebuildingRoots, 0);
	InterlockedExchange(&g_tpStartupArmPending, 1);
}

bool CemuleApp::DirWatchRootsChanged() const
{
	const DWORD cur = ComputeConfiguredWatchRootsHash();
	const DWORD prev = (DWORD)InterlockedCompareExchange((LONG*)&g_tpRootsHash, 0, 0);
	return cur != prev;
}

LONG CemuleApp::GetDirWatchChangeGeneration() const
{
	return InterlockedCompareExchange(&g_lDirWatchChangeGeneration, 0, 0);
}

void CemuleApp::DrainDirWatchChangedDirectories(CStringArray& outDirs)
{
	outDirs.RemoveAll();
	if (!g_tpChangedDirsCSInit || g_tpChangedDirs == NULL)
		return;

	std::vector<CString> todo;
	EnterCriticalSection(&g_tpChangedDirsCS);
	if (g_tpChangedDirs != NULL)
		todo.swap(*g_tpChangedDirs);
	LeaveCriticalSection(&g_tpChangedDirsCS);

	for (size_t i = 0; i < todo.size(); ++i)
		outDirs.Add(todo[i]);
}

void CemuleApp::DrainDirWatchChangedFiles(CStringArray& outFiles)
{
	outFiles.RemoveAll();
	if (!g_tpChangedFilesCSInit || g_tpChangedFiles == NULL)
		return;

	std::vector<CString> todo;
	EnterCriticalSection(&g_tpChangedFilesCS);
	if (g_tpChangedFiles != NULL)
		todo.swap(*g_tpChangedFiles);
	LeaveCriticalSection(&g_tpChangedFilesCS);

	for (size_t i = 0; i < todo.size(); ++i)
		outFiles.Add(todo[i]);
}

void CemuleApp::DrainDeletedAutoSharedDirs()
{
	DrainDeletedAutoSharedDirsFromQueue();
}

// Drains newly created subdirs (AutoShareSubdirs) and appends to thePrefs.shared list.
void CemuleApp::DrainAutoSharedNewDirs()
{
	if (!g_tpNewSharedDirsCSInit || g_tpNewSharedDirs == NULL)
		return;

	std::vector<CString> todo;
	EnterCriticalSection(&g_tpNewSharedDirsCS);
	if (g_tpNewSharedDirs != NULL)
		todo.swap(*g_tpNewSharedDirs);
	LeaveCriticalSection(&g_tpNewSharedDirsCS);

	if (todo.empty())
		return;

	const bool bAuto = thePrefs.GetAutoShareSubdirs();
	const CString sIncoming(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));

	// Collect category incoming paths for exclusion when AutoShareSubdirs is enabled
	std::vector<CString> catIncoming;
	if (bAuto) {
		for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
			const CString& p = thePrefs.GetCatPath(i);
			if (!p.IsEmpty() && !EqualPaths(p, sIncoming))
				catIncoming.push_back(p);
		}
	}

	bool bAdded = false;
	for (size_t i = 0; i < todo.size(); ++i) {
		CString s = todo[i];
		if (s.IsEmpty())
			continue;

		// Normalize trailing backslash
		if (s.Right(1) != _T("\\"))
			s += _T("\\");

		// Skip auto-added dirs under Incoming/categories when AutoShareSubdirs is enabled
		if (bAuto) {
			if (EqualPaths(s, sIncoming) || IsSubDirectoryOf(s, sIncoming))
				continue;
			bool underCat = false;
			for (size_t k = 0; k < catIncoming.size(); ++k) {
				if (EqualPaths(s, catIncoming[k]) || IsSubDirectoryOf(s, catIncoming[k])) {
					underCat = true;
					break;
				}
			}
			if (underCat)
				continue;
		}

		if (thePrefs.IsShareableDirectory(s) && thePrefs.AddSharedDirectoryIfAbsent(s)) {
			bAdded = true;
		}
	}

	if (bAdded)
		thePrefs.SaveSharedFolders();
}

static VOID CALLBACK DirWatchIoCb(PVOID /*instance*/, PVOID ctx, PVOID /*overlapped*/, ULONG ioResult, ULONG_PTR bytes, PVOID /*tpIo*/)
{
	TPIODirWatch* pW = reinterpret_cast<TPIODirWatch*>(ctx);
	// Track in-flight callbacks and mark current I/O as no longer armed.
	InterlockedIncrement(&pW->nCallbacks);
	InterlockedExchange(&pW->nArmed, 0);

	// Ensure counter is decremented even on early-return paths via RAII guard.
	struct CCbGuard { TPIODirWatch* w; ~CCbGuard() { if (w) InterlockedDecrement(&w->nCallbacks); } } _guard{ pW };

	if (InterlockedCompareExchange(&g_tpStopping, 0, 0) != 0 || InterlockedCompareExchange(&pW->nRetiring, 0, 0) != 0)
		return; // Stopping, do not rearm

	if (ioResult == ERROR_OPERATION_ABORTED)
		return; // Canceled during shutdown

	if (HandleRuntimeWatchCompletionFailure(pW, ioResult))
		return;

	// Use the byte count from I/O completion to safely walk the buffer.
	const DWORD cbData = (bytes < (ULONG_PTR)pW->cbBuf) ? (DWORD)bytes : pW->cbBuf;
	if (cbData == 0 && HandleRuntimeMissingWatchRoot(pW))
		return;

	// Detect directory creations/renames to force tree reload and (optionally) enqueue for AutoShareSubdirs
	if (pW->pBuf != NULL && ioResult == ERROR_SUCCESS && cbData >= sizeof(FILE_NOTIFY_INFORMATION)) {
		BYTE* cur = pW->pBuf;
		DWORD remaining = cbData;

		while (remaining >= sizeof(FILE_NOTIFY_INFORMATION)) {
			FILE_NOTIFY_INFORMATION* p = reinterpret_cast<FILE_NOTIFY_INFORMATION*>(cur);

			if (p->Action == FILE_ACTION_ADDED || p->Action == FILE_ACTION_RENAMED_NEW_NAME) {
				DWORD nameBytes = p->FileNameLength;
				if (nameBytes % sizeof(WCHAR)) 
					break; // Invalid record, stop parsing

				CString name(p->FileName, static_cast<int>(nameBytes / sizeof(WCHAR)));
				CString full = pW->rootPath;

				if (!full.IsEmpty() && full[full.GetLength() - 1] != _T('\\'))
					full += _T('\\');

				full += name;
				DWORD attr = ::GetFileAttributes(full);

				if (attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_DIRECTORY)) {
					InterlockedExchange(&g_tpForceTreeReload, 1);

					if (thePrefs.GetAutoShareSubdirs() && g_tpNewSharedDirsCSInit && g_tpNewSharedDirs != NULL) {
						if (full.Right(1) != _T("\\"))
							full += _T("\\");

						EnterCriticalSection(&g_tpNewSharedDirsCS);
						if (g_tpNewSharedDirs != NULL)
							g_tpNewSharedDirs->push_back(full);
						LeaveCriticalSection(&g_tpNewSharedDirsCS);
					}

					PushChangedDirectoryPath(full);

					break; // One is enough per batch
				}

				PushChangedFilePath(full);
			}

			DWORD advance = (p->NextEntryOffset != 0) ? p->NextEntryOffset : remaining;
			if (advance < sizeof(FILE_NOTIFY_INFORMATION) || advance > remaining)
				break; // Corrupt chain; stop parsing defensively

			if (p->NextEntryOffset == 0)
				break; // Last entry

			cur += p->NextEntryOffset;
			remaining -= p->NextEntryOffset;
		}
	}

	CollectDeletedFromBuffer(pW, cbData);

	// Debounced GUI refresh via UploadTimer. Structural batches need a longer settle window.
	if (InterlockedCompareExchange(&g_tpStopping, 0, 0) == 0 && InterlockedCompareExchange(&pW->nRetiring, 0, 0) == 0) {
		IncrementDirWatchChangeGeneration();
		if (!RearmWatch(pW))
			HandleRuntimeWatchRearmFailure(pW);
		else
			ScheduleDirWatchRefresh(false);
	}
}

void CemuleApp::StartDirWatchTP()
{
	InterlockedExchange(&g_tpStartupArmPending, 0);
	StopDirWatchTP(false);
	InterlockedExchange(&g_tpStopping, 0);
	InterlockedExchange(&g_tpRefreshPending, 0);
	g_dwRefreshDueAt = 0;
	InterlockedExchange(&g_tpTimerArmed, 0);

	// Init per-run queue for auto-added subdirs (if feature enabled)
	if (!g_tpNewSharedDirsCSInit) {
		InitializeCriticalSection(&g_tpNewSharedDirsCS);
		g_tpNewSharedDirs = new std::vector<CString>();
		g_tpNewSharedDirsCSInit = true;
	}

	if (g_tpNewSharedDirsCSInit) {
		EnterCriticalSection(&g_tpNewSharedDirsCS);
		if (g_tpNewSharedDirs != NULL)
			g_tpNewSharedDirs->clear();
		LeaveCriticalSection(&g_tpNewSharedDirsCS);
	}

	if (!g_tpChangedDirsCSInit) {
		InitializeCriticalSection(&g_tpChangedDirsCS);
		g_tpChangedDirs = new std::vector<CString>();
		g_tpChangedDirsCSInit = true;
	}

	if (g_tpChangedDirsCSInit) {
		EnterCriticalSection(&g_tpChangedDirsCS);
		if (g_tpChangedDirs != NULL)
			g_tpChangedDirs->clear();
		LeaveCriticalSection(&g_tpChangedDirsCS);
	}

	// Init per-run queue for changed files so watcher callbacks never race on lazy initialization.
	if (!g_tpChangedFilesCSInit) {
		InitializeCriticalSection(&g_tpChangedFilesCS);
		g_tpChangedFiles = new std::vector<CString>();
		g_tpChangedFilesCSInit = true;
	}

	if (g_tpChangedFilesCSInit) {
		EnterCriticalSection(&g_tpChangedFilesCS);
		if (g_tpChangedFiles != NULL)
			g_tpChangedFiles->clear();
		LeaveCriticalSection(&g_tpChangedFilesCS);
	}

	// Init deferred cleanup queue before any watch can queue retirement from callback threads.
	EnsureDirWatchCleanupInitialized();

	EnsureDeletedPathQueueInitialized();

	// Build unique root list (incoming, categories, shared dirs)
	std::vector<CString> roots;
	CollectConfiguredWatchRoots(roots);

	for (size_t i = 0; i < roots.size(); ++i) {
		const CString& path = roots[i];
		const CString lpath = PreparePathForWin32LongPath(path);
		HANDLE h = ::CreateFile(lpath, FILE_LIST_DIRECTORY, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, NULL);
		if (h == INVALID_HANDLE_VALUE)
			continue;

		TPIODirWatch* pW = new TPIODirWatch();
		pW->hDir = h;
		pW->cbBuf = 64 * 1024;
		pW->pBuf = (BYTE*)malloc(pW->cbBuf);
		pW->rootPath = path;

		if (pW->pBuf == NULL) {
			TPW_SafeCloseHandle(&h);
			delete pW;
			continue;
		}

		// Create TP I/O and hEvent, then rearm
		pW->tpIo = TPW_CreateThreadpoolIo(pW->hDir, DirWatchIoCb, pW, NULL);
		if (pW->tpIo == NULL) {
			free(pW->pBuf);
			TPW_SafeCloseHandle(&h);
			delete pW;
			continue;
		}

		if (!RearmWatch(pW)) {
			// Runtime failure while arming: close TP I/O for this watch (not exiting)
			TPW_CloseThreadpoolIoRealX(&pW->tpIo);
			free(pW->pBuf);
			TPW_SafeCloseHandleNull(&pW->hEvent);
			TPW_SafeCloseHandle(&h);
			delete pW;
			continue;
		}

		AddDirWatchToActiveList(pW);
	}

	const size_t nActiveWatchCount = GetActiveDirWatchCount();
	if (nActiveWatchCount > 0)
		TRACE2(_T("Shared Files Watcher (TP I/O): %u roots armed\n"), (UINT)nActiveWatchCount);

	const DWORD desiredHash = ComputeDirWatchRootsHash(roots);
	const DWORD activeHash = ComputeActiveWatchRootsHash();
	if (activeHash != desiredHash)
		TRACE(_T("Shared Files Watcher (TP I/O): %u/%u roots armed; next retry deferred.\n"), (UINT)nActiveWatchCount, (UINT)roots.size());

	InterlockedExchange((LONG*)&g_tpRootsHash, (LONG)desiredHash);
	InterlockedExchange((LONG*)&g_tpActiveRootsHash, (LONG)activeHash);
	InterlockedExchange((LONG*)&g_tpMissingRootRetryAt, (LONG)((activeHash != desiredHash) ? (::GetTickCount() + kDirWatchMissingRootRetryDelayMs) : 0));
	InterlockedExchange(&g_tpRebuildingRoots, 0);
}

void CemuleApp::StopDirWatchTP(bool bWaitForCallbacks)
{
	if (bWaitForCallbacks)
		InterlockedExchange(&g_tpStopping, 1);

	InterlockedExchange(&g_tpRefreshPending, 0);
	InterlockedExchange(&g_tpTimerArmed, 0);
	InterlockedExchange((LONG*)&g_tpActiveRootsHash, 0);
	InterlockedExchange((LONG*)&g_tpMissingRootRetryAt, 0);
	std::vector<TPIODirWatch*> deferred;
	std::vector<TPIODirWatch*> activeWatches;

	if (bWaitForCallbacks) {
		InterlockedExchange(&g_tpCleanupPending, 0);
		EnsureDirWatchCleanupInitialized();
		if (g_tpCleanupCSInit) {
			EnterCriticalSection(&g_tpCleanupCS);
			if (g_tpCleanup != NULL)
				deferred.swap(*g_tpCleanup);
			LeaveCriticalSection(&g_tpCleanupCS);
		}
	}

	DetachAllDirWatches(activeWatches);

	// Runtime rebuild: retire current watches immediately and let the upload timer clean them up off the hot UI path.
	for (size_t i = 0; i < activeWatches.size(); ++i) {
		TPIODirWatch* pW = activeWatches[i];
		if (!pW)
			continue;

		if (!bWaitForCallbacks) {
			if (InterlockedCompareExchange(&pW->nRetiring, 1, 0) != 0)
				continue;
			if (pW->hDir != INVALID_HANDLE_VALUE)
				// Let ReadDirectoryChangesW cancellation complete naturally; avoid CancelThreadpoolIo on armed I/O.
				TPW_CancelIoEx(pW->hDir, &pW->ov);
			QueueDirWatchForDeferredCleanup(pW);
			continue;
		}

		DestroyDirWatchSync(pW);
	}

	if (!bWaitForCallbacks)
		return;

	for (size_t i = 0; i < deferred.size(); ++i)
		DestroyDirWatchSync(deferred[i]);

	// Reset per-run queue
	if (g_tpNewSharedDirsCSInit) {
		EnterCriticalSection(&g_tpNewSharedDirsCS);
		if (g_tpNewSharedDirs != NULL) {
			g_tpNewSharedDirs->clear();
			delete g_tpNewSharedDirs;
			g_tpNewSharedDirs = NULL;
		}
		LeaveCriticalSection(&g_tpNewSharedDirsCS);
		DeleteCriticalSection(&g_tpNewSharedDirsCS);
		g_tpNewSharedDirsCSInit = false;
	}

	// Delete cleanup CS if we created it.
	if (g_tpCleanupCSInit) {
		delete g_tpCleanup;
		g_tpCleanup = NULL;
		DeleteCriticalSection(&g_tpCleanupCS);
		g_tpCleanupCSInit = false;
	}

	if (g_tpChangedFilesCSInit) {
		EnterCriticalSection(&g_tpChangedFilesCS);
		if (g_tpChangedFiles != NULL) {
			g_tpChangedFiles->clear();
			delete g_tpChangedFiles;
			g_tpChangedFiles = NULL;
		}
		LeaveCriticalSection(&g_tpChangedFilesCS);
		DeleteCriticalSection(&g_tpChangedFilesCS);
		g_tpChangedFilesCSInit = false;
	}

	if (g_tpChangedDirsCSInit) {
		EnterCriticalSection(&g_tpChangedDirsCS);
		if (g_tpChangedDirs != NULL) {
			g_tpChangedDirs->clear();
			delete g_tpChangedDirs;
			g_tpChangedDirs = NULL;
		}
		LeaveCriticalSection(&g_tpChangedDirsCS);
		DeleteCriticalSection(&g_tpChangedDirsCS);
		g_tpChangedDirsCSInit = false;
	}

	if (g_tpWatchListCSInit) {
		if (g_tpWatches != NULL) {
			delete g_tpWatches;
			g_tpWatches = NULL;
		}
		DeleteCriticalSection(&g_tpWatchListCS);
		g_tpWatchListCSInit = false;
	}

	// Delete deleted-paths CS if it was initialized anywhere (defensive: initialize here on demand)
	if (g_tpCsInit) {
		delete g_tpDeleted;
		g_tpDeleted = NULL;
		DeleteCriticalSection(&g_tpDelCS);
		g_tpCsInit = false;
	}
}

void CemuleApp::OnUploadTick_100ms_DirWatch() noexcept
{
	// During shutdown, do not process or post anything.
	if (InterlockedCompareExchange(&g_tpStopping, 0, 0) != 0)
		return;

	// Debounced GUI refresh
	if (InterlockedCompareExchange(&g_tpRefreshPending, 0, 0) == 1) {
		const DWORD now = ::GetTickCount();
		if ((LONG)(now - g_dwRefreshDueAt) >= 0) {
			InterlockedExchange(&g_tpRefreshPending, 0);
			DirWatchTimerCb(NULL, NULL, NULL); // Post UM_AUTO_RELOAD_SHARED_FILES
		}
	}

	// Deferred cleanup of canceled watches
	if (InterlockedCompareExchange(&g_tpCleanupPending, 0, 0) == 1) {
		const DWORD now = ::GetTickCount();
		if ((LONG)(now - g_dwCleanupDueAt) >= 0) {
			InterlockedExchange(&g_tpCleanupPending, 0);
			std::vector<TPIODirWatch*> todo;

			if (g_tpCleanupCSInit) {
				EnterCriticalSection(&g_tpCleanupCS);
				if (g_tpCleanup != NULL)
					todo.swap(*g_tpCleanup);
				LeaveCriticalSection(&g_tpCleanupCS);
			}

			if (!todo.empty()) {
				std::vector<TPIODirWatch*> remaining;
				for (size_t i = 0; i < todo.size(); ++i) {
					TPIODirWatch* pW = todo[i];
					const LONG inFlight = InterlockedCompareExchange(&pW->nCallbacks, 0, 0);
					const LONG armed = InterlockedCompareExchange(&pW->nArmed, 0, 0);
					if (inFlight == 0 && armed == 0) {
						// We're in runtime timer (OnUploadTick_100ms_DirWatch returns early if stopping),
						// so we can safely close the TP I/O once drained to avoid leaking PTP_IO objects.
						if (pW->tpIo != NULL)
							TPW_CloseThreadpoolIoRealX(&pW->tpIo);
						TPW_SafeCloseHandle(&pW->hDir);
						TPW_SafeCloseHandleNull(&pW->hEvent);
						if (pW->pBuf != NULL) {
							free(pW->pBuf);
							pW->pBuf = NULL;
						}
						delete pW;
					} else
						remaining.push_back(pW);
				}

				if (!remaining.empty()) {
					if (g_tpCleanupCSInit) {
						EnterCriticalSection(&g_tpCleanupCS);
						for (size_t i = 0; i < remaining.size(); ++i)
							GetDirWatchCleanupList().push_back(remaining[i]);
						LeaveCriticalSection(&g_tpCleanupCS);
					}

					InterlockedExchange(&g_tpCleanupPending, 1);
					g_dwCleanupDueAt = ::GetTickCount() + 50;
				}
			}
		}
	}
}

void CemuleApp::OnUploadTick_1s_DirWatch() noexcept
{
	DrainDeletedAutoSharedDirs(); // Prune stale auto-shared deleted subdirectories without a full tree scan
	DrainAutoSharedNewDirs(); // Drain auto-added dirs (if any) without blocking UI
	if (!IsClosing()) {
		CemuleDlg* pDlg = emuledlg;
		if (pDlg != NULL && pDlg->sharedfileswnd != NULL && ::IsWindow(pDlg->sharedfileswnd->m_hWnd) && pDlg->sharedfileswnd->IsWindowVisible())
			pDlg->sharedfileswnd->PostAutoReloadSharedFilesAsync(3);
	}
	if (InterlockedExchange(&g_tpStartupArmPending, 0) == 1 && !IsClosing())
		StartDirWatchTP();
}

void CemuleApp::OnUploadTick_5s_DirWatch() noexcept
{
	DirWatchRootsTimerCb(NULL, NULL, NULL); // Check for root set changes and rebuild watcher if needed
}
