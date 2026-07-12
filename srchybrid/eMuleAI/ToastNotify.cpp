//This file is part of eMule AI
//Copyright (C)2026 eMule AI
//
//This program is free software; you can redistribute it and/or
//modify it under the terms of the GNU General Public License
//as published by the Free Software Foundation; either
//version 2 of the License, or (at your option) any later version.
#include "stdafx.h"
#include "ToastNotify.h"
#include "../Log.h"
#include "../OtherFunctions.h"
#include "../UserMsgs.h"
#include <map>
#include <memory>
#include <propkey.h>
#include <propsys.h>
#include <propvarutil.h>
#include <roapi.h>
#include <shlobj.h>
#include <shellapi.h>
#include <vector>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/base.h>

#ifndef KF_FLAG_CREATE
#define KF_FLAG_CREATE 0x00008000
#endif

namespace
{
	constexpr UINT kMaxTrackedToasts = 64;
	constexpr DWORD kToastRetryDelayMs = 15 * 1000;

	class CScopedCoInit
	{
	public:
		explicit CScopedCoInit(DWORD dwFlags)
			: m_hr(::CoInitializeEx(NULL, dwFlags))
		{
		}

		~CScopedCoInit()
		{
			if (m_hr == S_OK || m_hr == S_FALSE)
				::CoUninitialize();
		}

		bool IsUsable() const { return m_hr == S_OK || m_hr == S_FALSE || m_hr == RPC_E_CHANGED_MODE; }

	private:
		HRESULT m_hr;
	};

	class CScopedRoInit
	{
	public:
		CScopedRoInit()
			: m_hr(::RoInitialize(RO_INIT_SINGLETHREADED))
		{
		}

		~CScopedRoInit()
		{
			if (IsInitialized())
				::RoUninitialize();
		}

		bool IsInitialized() const { return m_hr == S_OK || m_hr == S_FALSE; }
		bool IsUsable() const { return IsInitialized() || m_hr == RPC_E_CHANGED_MODE; }

	private:
		HRESULT m_hr;
	};

	bool IsCurrentProcessElevated()
	{
		HANDLE hToken = NULL;
		if (!::OpenProcessToken(::GetCurrentProcess(), TOKEN_QUERY, &hToken))
			return false;

		TOKEN_ELEVATION elevation = {};
		DWORD dwLength = 0;
		const BOOL bResult = ::GetTokenInformation(hToken, TokenElevation, &elevation, sizeof(elevation), &dwLength);
		::CloseHandle(hToken);
		return bResult && elevation.TokenIsElevated != 0;
	}

	bool IsRuntimeObjectAvailable()
	{
		HMODULE hModule = ::LoadLibrary(_T("runtimeobject.dll"));
		if (hModule == NULL)
			return false;
		::FreeLibrary(hModule);
		return true;
	}

	bool IsRetryDue(DWORD dwNextRetry)
	{
		return dwNextRetry == 0 || static_cast<LONG>(::GetTickCount() - dwNextRetry) >= 0;
	}

	DWORD GetNextRetryTick()
	{
		return ::GetTickCount() + kToastRetryDelayMs;
	}

	void LogToastFailure(LPCTSTR pszContext, const winrt::hresult_error& error)
	{
		const CString strMessage(error.message().c_str());
		DebugLogError(_T("Toast notification: %s failed (0x%08X: %s)"), pszContext, static_cast<DWORD>(static_cast<HRESULT>(error.code())), (LPCTSTR)strMessage);
	}

	void LogToastFailure(LPCTSTR pszContext)
	{
		DebugLogError(_T("Toast notification: %s failed with an unexpected exception."), pszContext);
	}

	CStringW EscapeToastXml(CStringW strValue)
	{
		strValue.Replace(L"&", L"&amp;");
		strValue.Replace(L"<", L"&lt;");
		strValue.Replace(L">", L"&gt;");
		strValue.Replace(L"\"", L"&quot;");
		strValue.Replace(L"'", L"&apos;");
		return strValue;
	}

	CStringW GetToastFallbackTitle(TbnMsg nMsgType)
	{
		switch (nMsgType) {
		case TBN_CHAT:
			return GetResString(_T("PW_TBN_POP_ALWAYS"));
		case TBN_DOWNLOADFINISHED:
			return GetResString(_T("PW_TBN_ONDOWNLOAD"));
		case TBN_DOWNLOADADDED:
			return GetResString(_T("TBN_ONNEWDOWNLOAD"));
		case TBN_LOG:
			return GetResString(_T("PW_TBN_ONLOG"));
		case TBN_IMPORTANTEVENT:
			return GetResString(_T("ERROR"));
		case TBN_NEWVERSION:
			return GetResString(_T("CB_TBN_ONNEWVERSION_EMULEAI_RELEASED"));
		default:
			return GetResString(_T("EMULENOTIFICATION"));
		}
	}

	void SplitToastText(LPCTSTR pszText, TbnMsg nMsgType, CStringW& strTitle, CStringW& strBody)
	{
		CStringW strText(pszText != NULL ? pszText : _T(""));
		strText.Replace(L"\r\n", L"\n");
		strText.Replace(L"\r", L"\n");
		strText.Trim();

		const int iBreak = strText.Find(L'\n');
		if (iBreak >= 0) {
			strTitle = strText.Left(iBreak);
			strBody = strText.Mid(iBreak + 1);
		} else {
			strTitle = GetToastFallbackTitle(nMsgType);
			strBody = strText;
		}

		strTitle.Trim();
		strBody.Trim();
		if (strTitle.IsEmpty())
			strTitle = GetToastFallbackTitle(nMsgType);
		if (strBody.IsEmpty())
			strBody = strTitle;
	}

	CStringW BuildToastXml(UINT uToastId, LPCTSTR pszText, TbnMsg nMsgType)
	{
		CStringW strTitle;
		CStringW strBody;
		SplitToastText(pszText, nMsgType, strTitle, strBody);

		CStringW strXml;
		strXml.Format(
			L"<toast launch=\"emule-ai-toast:%u\">"
			L"<visual><binding template=\"ToastGeneric\">"
			L"<text>%s</text><text>%s</text>"
			L"</binding></visual>"
			L"</toast>",
			uToastId,
			static_cast<LPCWSTR>(EscapeToastXml(strTitle)),
			static_cast<LPCWSTR>(EscapeToastXml(strBody)));
		return strXml;
	}

	bool GetModulePath(CStringW& strModulePath)
	{
		std::vector<WCHAR> buffer(MAX_PATH, L'\0');
		while (buffer.size() <= 32768) {
			const DWORD dwLength = ::GetModuleFileNameW(NULL, buffer.data(), static_cast<DWORD>(buffer.size()));
			if (dwLength == 0) {
				strModulePath.Empty();
				return false;
			}
			if (dwLength < buffer.size()) {
				buffer[dwLength] = L'\0';
				strModulePath = buffer.data();
				return !strModulePath.IsEmpty();
			}
			buffer.resize(buffer.size() * 2, L'\0');
		}
		strModulePath.Empty();
		::SetLastError(ERROR_BUFFER_OVERFLOW);
		return false;
	}

	DWORD HashToastIdentityValue(const CStringW& strValue)
	{
		CStringW strNormalized(strValue);
		strNormalized.MakeLower();

		DWORD dwHash = 2166136261u;
		for (int i = 0; i < strNormalized.GetLength(); ++i) {
			dwHash ^= static_cast<DWORD>(strNormalized[i]);
			dwHash *= 16777619u;
		}
		return dwHash;
	}

	void BuildToastIdentity(CStringW& strAppId, CStringW& strShortcutName)
	{
		CStringW strModulePath;
		const DWORD dwIdentityHash = GetModulePath(strModulePath) ? HashToastIdentityValue(strModulePath) : 0;
		strAppId.Format(L"eMuleAI.Notifications.%08X", dwIdentityHash);
		strShortcutName.Format(L"eMule AI %08X.lnk", dwIdentityHash);
	}

	typedef HRESULT(WINAPI* SHGetKnownFolderPathFunc)(REFKNOWNFOLDERID, DWORD, HANDLE, PWSTR*);
	typedef HRESULT(WINAPI* SHGetFolderPathWFunc)(HWND, int, HANDLE, DWORD, LPWSTR);
	typedef HRESULT(WINAPI* SetCurrentProcessExplicitAppUserModelIDFunc)(PCWSTR);

	HMODULE GetShell32Module()
	{
		HMODULE hModule = ::GetModuleHandle(_T("shell32.dll"));
		if (hModule == NULL)
			hModule = ::LoadLibrary(_T("shell32.dll"));
		return hModule;
	}

	bool GetStartMenuPath(CStringW& strStartMenuPath)
	{
		HMODULE hShell32 = GetShell32Module();
		if (hShell32 == NULL)
			return false;

		const SHGetKnownFolderPathFunc pfnSHGetKnownFolderPath = reinterpret_cast<SHGetKnownFolderPathFunc>(::GetProcAddress(hShell32, "SHGetKnownFolderPath"));
		if (pfnSHGetKnownFolderPath != NULL) {
			PWSTR pszKnownFolderPath = NULL;
			const HRESULT hrKnownFolder = pfnSHGetKnownFolderPath(FOLDERID_StartMenu, KF_FLAG_CREATE, NULL, &pszKnownFolderPath);
			if (SUCCEEDED(hrKnownFolder) && pszKnownFolderPath != NULL) {
				strStartMenuPath = pszKnownFolderPath;
				::CoTaskMemFree(pszKnownFolderPath);
				if (!strStartMenuPath.IsEmpty())
					return true;
			} else if (pszKnownFolderPath != NULL)
				::CoTaskMemFree(pszKnownFolderPath);
		}

		const SHGetFolderPathWFunc pfnSHGetFolderPathW = reinterpret_cast<SHGetFolderPathWFunc>(::GetProcAddress(hShell32, "SHGetFolderPathW"));
		if (pfnSHGetFolderPathW == NULL)
			return false;

		WCHAR szStartMenuPath[MAX_PATH] = {};
		if (FAILED(pfnSHGetFolderPathW(NULL, CSIDL_STARTMENU | CSIDL_FLAG_CREATE, NULL, SHGFP_TYPE_CURRENT, szStartMenuPath)))
			return false;

		strStartMenuPath = szStartMenuPath;
		return !strStartMenuPath.IsEmpty() && strStartMenuPath.GetLength() < MAX_PATH - 1;
	}

	bool SetToastAppUserModelId(LPCWSTR pszAppId)
	{
		HMODULE hShell32 = GetShell32Module();
		if (hShell32 == NULL)
			return false;

		const SetCurrentProcessExplicitAppUserModelIDFunc pfnSetCurrentProcessExplicitAppUserModelID = reinterpret_cast<SetCurrentProcessExplicitAppUserModelIDFunc>(::GetProcAddress(hShell32, "SetCurrentProcessExplicitAppUserModelID"));
		return pfnSetCurrentProcessExplicitAppUserModelID != NULL && SUCCEEDED(pfnSetCurrentProcessExplicitAppUserModelID(pszAppId));
	}

	bool EnsureToastShortcut(LPCWSTR pszAppId, LPCWSTR pszShortcutName)
	{
		CStringW strShortcutPath;
		if (!GetStartMenuPath(strShortcutPath))
			return false;
		strShortcutPath += L"\\Programs\\";
		strShortcutPath += pszShortcutName;

		CStringW strModulePath;
		if (!GetModulePath(strModulePath))
			return false;

		const CScopedCoInit coInit(COINIT_APARTMENTTHREADED);
		if (!coInit.IsUsable())
			return false;

		CComPtr<IShellLinkW> pShellLink;
		if (FAILED(pShellLink.CoCreateInstance(CLSID_ShellLink, NULL, CLSCTX_INPROC_SERVER)) || pShellLink == NULL)
			return false;
		pShellLink->SetPath(strModulePath.GetString());
		pShellLink->SetIconLocation(strModulePath.GetString(), 0);
		pShellLink->SetDescription(L"eMule AI");

		CComPtr<IPropertyStore> pPropertyStore;
		if (FAILED(pShellLink.QueryInterface(&pPropertyStore)) || pPropertyStore == NULL)
			return false;

		PROPVARIANT appId;
		::PropVariantInit(&appId);
		const bool bPropertySet = SUCCEEDED(::InitPropVariantFromString(pszAppId, &appId))
			&& SUCCEEDED(pPropertyStore->SetValue(PKEY_AppUserModel_ID, appId))
			&& SUCCEEDED(pPropertyStore->Commit());
		::PropVariantClear(&appId);
		if (!bPropertySet)
			return false;

		CComPtr<IPersistFile> pPersistFile;
		if (FAILED(pShellLink.QueryInterface(&pPersistFile)) || pPersistFile == NULL)
			return false;

		return SUCCEEDED(pPersistFile->Save(strShortcutPath, TRUE));
	}

	struct ToastPayload
	{
		TbnMsg nMsgType = TBN_NONOTIFY;
		CString strLink;
	};

	struct ToastState
	{
		HWND hWndNotify = NULL;
		CCriticalSection lock;
		std::map<UINT, ToastPayload> payloads;
	};

	enum EToastPreparationState
	{
		ToastPreparationNotStarted,
		ToastPreparationRunning,
		ToastPreparationReady,
		ToastPreparationFailed
	};

	struct ToastPreparation
	{
		CCriticalSection lock;
		EToastPreparationState eState = ToastPreparationNotStarted;
		DWORD dwNextRetry = 0;
		CStringW strAppId;
		CStringW strShortcutName;
	};

	UINT AFX_CDECL ToastPrepareThreadProc(LPVOID pParam)
	{
		std::unique_ptr<std::shared_ptr<ToastPreparation> > prepHolder(static_cast<std::shared_ptr<ToastPreparation>*>(pParam));
		const std::shared_ptr<ToastPreparation> prep(*prepHolder);

		CStringW strAppId;
		CStringW strShortcutName;
		{
			CSingleLock lock(&prep->lock, TRUE);
			strAppId = prep->strAppId;
			strShortcutName = prep->strShortcutName;
		}

		const bool bPrepared = SetToastAppUserModelId(strAppId) && EnsureToastShortcut(strAppId, strShortcutName);
		{
			CSingleLock lock(&prep->lock, TRUE);
			prep->eState = bPrepared ? ToastPreparationReady : ToastPreparationFailed;
			prep->dwNextRetry = bPrepared ? 0 : GetNextRetryTick();
		}
		return 0;
	}

	void PruneToastState(const std::shared_ptr<ToastState>& state)
	{
		CSingleLock lock(&state->lock, TRUE);
		while (state->payloads.size() > kMaxTrackedToasts)
			state->payloads.erase(state->payloads.begin());
	}

	void PostToastClicked(const std::shared_ptr<ToastState>& state, UINT uToastId)
	{
		ToastPayload payload;
		HWND hWndNotify = NULL;
		{
			CSingleLock lock(&state->lock, TRUE);
			const auto itPayload = state->payloads.find(uToastId);
			if (itPayload == state->payloads.end())
				return;

			payload = itPayload->second;
			state->payloads.erase(itPayload);
			hWndNotify = state->hWndNotify;
		}

		if (!::IsWindow(hWndNotify))
			return;

		LPTSTR pszLink = NULL;
		if (!payload.strLink.IsEmpty())
			pszLink = _tcsdup(payload.strLink);

		if (!::PostMessage(hWndNotify, UM_TOAST_NOTIFICATION_CLICKED, static_cast<WPARAM>(payload.nMsgType), reinterpret_cast<LPARAM>(pszLink)) && pszLink != NULL)
			free(pszLink);
	}
}

struct CToastNotify::Impl
{
	Impl()
		: state(std::make_shared<ToastState>())
		, preparation(std::make_shared<ToastPreparation>())
	{
		BuildToastIdentity(strAppId, strShortcutName);
		CSingleLock lock(&preparation->lock, TRUE);
		preparation->strAppId = strAppId;
		preparation->strShortcutName = strShortcutName;
	}

	std::shared_ptr<ToastState> state;
	std::shared_ptr<ToastPreparation> preparation;
	std::unique_ptr<CScopedRoInit> roInit;
	bool bInitialized = false;
	bool bRuntimeObjectUnavailable = false;
	DWORD dwNextInitializeRetry = 0;
	UINT uNextToastId = 1;
	CStringW strAppId;
	CStringW strShortcutName;
	winrt::Windows::UI::Notifications::ToastNotifier notifier{ nullptr };
	std::map<UINT, winrt::Windows::UI::Notifications::ToastNotification> activeToasts;

	bool EnsurePreparationReady()
	{
		{
			CSingleLock lock(&preparation->lock, TRUE);
			if (preparation->eState == ToastPreparationReady)
				return true;
			if (preparation->eState == ToastPreparationRunning)
				return false;
			if (preparation->eState == ToastPreparationFailed && !IsRetryDue(preparation->dwNextRetry))
				return false;
			preparation->eState = ToastPreparationRunning;
		}

		std::unique_ptr<std::shared_ptr<ToastPreparation> > prepHolder(new std::shared_ptr<ToastPreparation>(preparation));
		CWinThread* pThread = AfxBeginThread(ToastPrepareThreadProc, prepHolder.get(), THREAD_PRIORITY_BELOW_NORMAL, 0, 0, NULL);
		if (pThread == NULL) {
			CSingleLock lock(&preparation->lock, TRUE);
			preparation->eState = ToastPreparationFailed;
			preparation->dwNextRetry = GetNextRetryTick();
			return false;
		}
		prepHolder.release();
		return false;
	}

	bool Initialize(HWND hWndNotify)
	{
		{
			CSingleLock lock(&state->lock, TRUE);
			state->hWndNotify = hWndNotify;
		}

		if (bInitialized)
			return true;
		if (hWndNotify == NULL || IsCurrentProcessElevated() || bRuntimeObjectUnavailable)
			return false;
		if (!EnsurePreparationReady())
			return false;
		if (!IsRetryDue(dwNextInitializeRetry))
			return false;
		if (!IsRuntimeObjectAvailable()) {
			bRuntimeObjectUnavailable = true;
			return false;
		}

		roInit = std::make_unique<CScopedRoInit>();
		if (!roInit->IsUsable()) {
			roInit.reset();
			dwNextInitializeRetry = GetNextRetryTick();
			return false;
		}

		try {
			notifier = winrt::Windows::UI::Notifications::ToastNotificationManager::CreateToastNotifier(strAppId.GetString());
			bInitialized = true;
			dwNextInitializeRetry = 0;
		} catch (const winrt::hresult_error& error) {
			LogToastFailure(_T("CreateToastNotifier"), error);
			roInit.reset();
			dwNextInitializeRetry = GetNextRetryTick();
		} catch (...) {
			LogToastFailure(_T("CreateToastNotifier"));
			roInit.reset();
			dwNextInitializeRetry = GetNextRetryTick();
		}
		return bInitialized;
	}
};

CToastNotify::CToastNotify()
	: m_impl(std::make_unique<Impl>())
{
}

CToastNotify::~CToastNotify()
{
	Shutdown();
}

bool CToastNotify::Show(HWND hWndNotify, LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink)
{
	if (!m_impl->Initialize(hWndNotify))
		return false;

	const UINT uToastId = m_impl->uNextToastId++;
	const CStringW strXml = BuildToastXml(uToastId, pszText, nMsgType);

	try {
		winrt::Windows::Data::Xml::Dom::XmlDocument xmlDocument;
		xmlDocument.LoadXml(static_cast<LPCWSTR>(strXml));

		winrt::Windows::UI::Notifications::ToastNotification toast(xmlDocument);
		const std::shared_ptr<ToastState> state = m_impl->state;
		toast.Activated([state, uToastId](const auto&, const auto&) {
			PostToastClicked(state, uToastId);
		});

		{
			CSingleLock lock(&m_impl->state->lock, TRUE);
			ToastPayload payload;
			payload.nMsgType = nMsgType;
			payload.strLink = pszLink != NULL ? pszLink : _T("");
			m_impl->state->payloads[uToastId] = payload;
		}
		PruneToastState(m_impl->state);

		m_impl->notifier.Show(toast);
		m_impl->activeToasts.insert_or_assign(uToastId, toast);
		while (m_impl->activeToasts.size() > kMaxTrackedToasts)
			m_impl->activeToasts.erase(m_impl->activeToasts.begin());
		return true;
	} catch (const winrt::hresult_error& error) {
		LogToastFailure(_T("Show"), error);
		CSingleLock lock(&m_impl->state->lock, TRUE);
		m_impl->state->payloads.erase(uToastId);
	} catch (...) {
		LogToastFailure(_T("Show"));
		CSingleLock lock(&m_impl->state->lock, TRUE);
		m_impl->state->payloads.erase(uToastId);
	}
	return false;
}

void CToastNotify::Shutdown()
{
	if (m_impl == nullptr)
		return;

	{
		CSingleLock lock(&m_impl->state->lock, TRUE);
		m_impl->state->hWndNotify = NULL;
		m_impl->state->payloads.clear();
	}
	m_impl->activeToasts.clear();
	m_impl->notifier = nullptr;
	m_impl->bInitialized = false;
	m_impl->roInit.reset();
}
