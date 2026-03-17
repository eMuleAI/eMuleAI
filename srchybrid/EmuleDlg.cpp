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
#include <afxinet.h>
#include "eMuleAI/DarkMode.h"
#include "eMuleAI/DebugLeakHelper.h"
#include "ToolTipCtrlX.h"

#define MMNODRV			// mmsystem: Installable driver support
//#define MMNOSOUND		// mmsystem: Sound support
#define MMNOWAVE		// mmsystem: Waveform support
#define MMNOMIDI		// mmsystem: MIDI support
#define MMNOAUX			// mmsystem: Auxiliary audio support
#define MMNOMIXER		// mmsystem: Mixer support
#define MMNOTIMER		// mmsystem: Timer support
#define MMNOJOY			// mmsystem: Joystick support
#define MMNOMCI			// mmsystem: MCI support
#define MMNOMMIO		// mmsystem: Multimedia file I/O support
#define MMNOMMSYSTEM	// mmsystem: General MMSYSTEM functions

#include <Mmsystem.h>
#include <HtmlHelp.h>
#include <share.h>
#include <math.h>
#include <dbt.h>
#include "emule.h"
#include "emuleDlg.h"
#include "otherfunctions.h"
#include "ServerWnd.h"
#include "KademliaWnd.h"
#include "TransferWnd.h"
#include "TransferDlg.h"
#include "SearchResultsWnd.h"
#include "SearchDlg.h"
#include "SharedFilesWnd.h"
#include "ChatWnd.h"
#include "IrcWnd.h"
#include "StatisticsDlg.h"
#include "PreferencesDlg.h"
#include "eMuleAI\MigrationWizardDlg.h"
#include "ServerConnect.h"
#include "KnownFileList.h"
#include "ServerList.h"
#include "Opcodes.h"
#include "SharedFileList.h"
#include "ED2KLink.h"
#include "Splashscreen.h"
#include "PartFileConvert.h"
#include "EnBitmap.h"
#include "Exceptions.h"
#include "SearchList.h"
#include "HTRichEditCtrl.h"
#include "FrameGrabThread.h"
#include "kademlia/kademlia/kademlia.h"
#include "kademlia/kademlia/SearchManager.h"
#include "kademlia/routing/RoutingZone.h"
#include "kademlia/routing/contact.h"
#include "kademlia/kademlia/prefs.h"
#include "KadSearchListCtrl.h"
#include "KadContactListCtrl.h"
#include "PerfLog.h"
#include "DropTarget.h"
#include "LastCommonRouteFinder.h"
#include "WebServer.h"
#include "DownloadQueue.h"
#include "ClientUDPSocket.h"
#include "UploadQueue.h"
#include "ClientList.h"
#include "UploadBandwidthThrottler.h"
#include "FriendList.h"
#include "IPFilter.h"
#include "Statistics.h"
#include "MuleToolbarCtrl.h"
#include "TaskbarNotifier.h"
#include "MuleStatusbarCtrl.h"
#include "ListenSocket.h"
#include "Server.h"
#include "PartFile.h"
#include "Scheduler.h"
#include "ClientCredits.h"
#include "MenuCmds.h"
#include "MuleSystrayDlg.h"
#include "IPFilterDlg.h"
#include "WebServices.h"
#include "DirectDownloadDlg.h"
#include "eMuleAI/GeoLiteDownloadDlg.h"
#include "Statistics.h"
#include "FirewallOpener.h"
#include "StringConversion.h"
#include "aichsyncthread.h"
#include "Log.h"
#include "MiniMule.h"
#include "UserMsgs.h"
#include "TextToSpeech.h"
#include "Collection.h"
#include "CollectionViewDialog.h"
#include "UPnPImpl.h"
#include "UPnPImplWrapper.h"
#include "ExitBox.h"
#include "UploadDiskIOThread.h"
#include "PartFileWriteThread.h"
#include "Preferences.h"
#include "eMuleAI/GeoLite2.h" 
#include "Friend.h"
#include "EditDelayed.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

extern BOOL FirstTimeWizard();

#define	SYS_TRAY_ICON_COOKIE_FORCE_UPDATE	UINT_MAX

UINT g_uMainThreadId = 0;
static const UINT UWM_ARE_YOU_EMULE = RegisterWindowMessage(EMULE_GUID);

#ifdef HAVE_WIN7_SDK_H
static const UINT UWM_TASK_BUTTON_CREATED = RegisterWindowMessage(_T("TaskbarButtonCreated"));
#endif

namespace
{
	template <typename T>
	void DestroyAndDeleteWnd(T*& pWnd)
	{
		if (pWnd == NULL)
			return;

		HWND hWnd = pWnd->GetSafeHwnd();
		if (hWnd != NULL && ::IsWindow(hWnd))
			pWnd->DestroyWindow();

		delete pWnd;
		pWnd = NULL;
	}

	template <typename T>
	void DestroyAndDeleteFrameWndSafe(T*& pWnd, HWND& hWndTracked)
	{
		if (hWndTracked == NULL) {
			pWnd = NULL;
			return;
		}

		CWnd* pLive = CWnd::FromHandlePermanent(hWndTracked);
		if (::IsWindow(hWndTracked)) {
			if (pLive != NULL)
				pLive->DestroyWindow();
			else
				::DestroyWindow(hWndTracked);
		}

		pWnd = NULL;
		hWndTracked = NULL;
	}
}

#ifndef NMRBCUSTOMDRAW
typedef struct tagNMRBCUSTOMDRAW {
	NMCUSTOMDRAW nmcd;
	COLORREF     clrText;
	COLORREF     clrTextHighlight;
	RECT         rcBand;
	UINT         fStyle;
	UINT         wID;
	COLORREF     clrBtnText;
	COLORREF     clrBtnTextHighlight;
	RECT         rcChevronLocation;
	UINT         uChevronState;
} NMRBCUSTOMDRAW, * LPNMRBCUSTOMDRAW;
#endif

namespace
{
	const UINT TITLE_VERSION_ANIMATION_INTERVAL_MS = 60;
	const UINT TITLE_VERSION_ANIMATION_HUE_STEP = 7;
	const UINT TITLE_VERSION_CHARACTER_HUE_STEP = 28;
	const UINT TITLE_VERSION_WAVE_PHASE_STEP = 20;
	const UINT SPECIAL_THANKS_ANIMATION_INTERVAL_MS = 33;
	const DWORD VERSIONCHECK_HTTP_TIMEOUT_MS = SEC2MS(8);
	const int VERSIONCHECK_MAX_TEXT_BYTES = 4096;
	const int TITLE_VERSION_TEXT_MIN_CHANNEL = 80;
	const int TITLE_VERSION_TEXT_SHADOW_OFFSET = 1;
	const int TITLE_VERSION_TEXT_HORIZONTAL_PADDING = 8;
	const int TITLE_VERSION_OVERLAY_PADDING_X = 3;
	const int TITLE_VERSION_OVERLAY_PADDING_Y = 2;
	const int TITLE_VERSION_TEXT_WAVE_AMPLITUDE = 2;
	const double TITLE_VERSION_DEG_TO_RAD = 3.14159265358979323846 / 180.0;
	const COLORREF TITLE_VERSION_OVERLAY_COLORKEY = RGB(255, 0, 255);
	const LPCTSTR TITLE_VERSION_LINK_TEXT_KEY = _T("CB_TBN_ONNEWVERSION_EMULEAI_RELEASED");
	const LPCTSTR EMULE_AI_CHATGPT_HELP_BASE_URL = _T("https://chatgpt.com/?");
	const LPCTSTR EMULE_AI_GEMINI_HELP_BASE_URL = _T("https://gemini.google.com/guided-learning?");
	const LPCTSTR EMULE_AI_CHATGPT_PROMPT_PARAM = _T("prompt");
	const LPCTSTR EMULE_AI_GEMINI_PROMPT_PARAM = _T("query");

#ifndef DWMWA_CAPTION_BUTTON_BOUNDS
	#define DWMWA_CAPTION_BUTTON_BOUNDS 5
#endif

	struct EmuleAIVersionCheckRequest
	{
		HWND hNotifyWnd;
		CString strVersionRawUrl;
		CString strCurrentVersion;
		bool bManual;
	};

	struct EmuleAIVersionCheckResult
	{
		bool bRequestSucceeded;
		bool bHasNewVersion;
		CString strRemoteVersion;
		bool bManual;
	};

	bool IsUnreservedUrlByte(BYTE byValue)
	{
		return (byValue >= 'A' && byValue <= 'Z')
			|| (byValue >= 'a' && byValue <= 'z')
			|| (byValue >= '0' && byValue <= '9')
			|| byValue == '-'
			|| byValue == '_'
			|| byValue == '.'
			|| byValue == '~';
	}

	CString EncodeUrlQueryUtf8(const CString& strValue)
	{
		const CStringA strUtf8(StrToUtf8(strValue));
		CString strEncoded;
		static const TCHAR s_hexDigits[] = _T("0123456789ABCDEF");

		for (int i = 0; i < strUtf8.GetLength(); ++i) {
			const BYTE byValue = static_cast<BYTE>(strUtf8[i]);
			if (IsUnreservedUrlByte(byValue)) {
				strEncoded.AppendChar(static_cast<TCHAR>(byValue));
			} else {
				strEncoded.AppendChar(_T('%'));
				strEncoded.AppendChar(s_hexDigits[(byValue >> 4) & 0x0F]);
				strEncoded.AppendChar(s_hexDigits[byValue & 0x0F]);
			}
		}

		return strEncoded;
	}

	CString BuildEmuleAiAssistantPromptText(LPCTSTR pszPromptTemplateResKey)
	{
		const CString strPromptTemplate(GetResString(pszPromptTemplateResKey));
		CString strPromptText;
		strPromptText.Format(strPromptTemplate, MOD_PAGES_BASE_URL, MOD_REPO_BASE_URL);
		return strPromptText;
	}

	CString BuildEmuleAiAssistantUrl(LPCTSTR pszBaseUrl, LPCTSTR pszQueryParamName, LPCTSTR pszPromptTemplateResKey)
	{
		const CString strPromptText(BuildEmuleAiAssistantPromptText(pszPromptTemplateResKey));
		CString strUrl;
		strUrl.Format(_T("%s%s=%s"), pszBaseUrl, pszQueryParamName, (LPCTSTR)EncodeUrlQueryUtf8(strPromptText));
		return strUrl;
	}

	CString NormalizeVersionString(const CString& strVersion)
	{
		CString strNormalized(strVersion);
		strNormalized.Replace(_T("\r"), _T(""));
		strNormalized.Replace(_T("\n"), _T(""));
		strNormalized.Trim();

		CString strPrefixCheck(strNormalized);
		strPrefixCheck.MakeLower();
		if (strPrefixCheck.Left(4) == _T("ai v")) {
			strNormalized = strNormalized.Mid(4);
			strNormalized.TrimLeft();
		} else if (strPrefixCheck.Left(3) == _T("ai ")) {
			strNormalized = strNormalized.Mid(3);
			strNormalized.TrimLeft();
		}

		if (!strNormalized.IsEmpty() && (strNormalized[0] == _T('v') || strNormalized[0] == _T('V'))) {
			strNormalized = strNormalized.Mid(1);
			strNormalized.TrimLeft();
		}

		return strNormalized;
	}

	enum VersionSuffixType
	{
		VersionSuffixDashed = 0,
		VersionSuffixNone = 1,
		VersionSuffixInline = 2
	};

	struct ParsedVersionString
	{
		CArray<UINT, UINT> aComponents;
		CString strSuffix;
		VersionSuffixType eSuffixType;
	};

	bool TryParseVersionString(const CString& str, ParsedVersionString& parsedVersion)
	{
		parsedVersion.aComponents.RemoveAll();
		parsedVersion.strSuffix.Empty();
		parsedVersion.eSuffixType = VersionSuffixNone;

		if (str.IsEmpty())
			return false;

		bool bHasDot = false;
		int nPos = 0;
		while (nPos < str.GetLength()) {
			if (!_istdigit(str[nPos]))
				return false;

			UINT uComponent = 0;
			while (nPos < str.GetLength() && _istdigit(str[nPos])) {
				const UINT uDigit = static_cast<UINT>(str[nPos] - _T('0'));
				if (uComponent > (UINT_MAX - uDigit) / 10)
					return false;
				uComponent = (uComponent * 10) + uDigit;
				++nPos;
			}
			parsedVersion.aComponents.Add(uComponent);

			if (nPos >= str.GetLength())
				break;

			if (str[nPos] == _T('.')) {
				bHasDot = true;
				++nPos;
				if (nPos >= str.GetLength())
					return false;
				continue;
			}

			if (str[nPos] == _T('-')) {
				parsedVersion.eSuffixType = VersionSuffixDashed;
				++nPos;
			} else if (_istalpha(str[nPos])) {
				parsedVersion.eSuffixType = VersionSuffixInline;
			} else {
				return false;
			}

			if (nPos >= str.GetLength())
				return false;

			for (int i = nPos; i < str.GetLength(); ++i) {
				if (!_istalnum(str[i]))
					return false;
			}

			parsedVersion.strSuffix = str.Mid(nPos);
			break;
		}

		return bHasDot && parsedVersion.aComponents.GetSize() > 0;
	}

	bool IsValidVersionString(const CString& str)
	{
		ParsedVersionString parsedVersion;
		return TryParseVersionString(str, parsedVersion);
	}

	int CompareVersionStrings(const CString& strLeft, const CString& strRight)
	{
		ParsedVersionString leftVersion;
		ParsedVersionString rightVersion;
		if (!TryParseVersionString(strLeft, leftVersion) || !TryParseVersionString(strRight, rightVersion))
			return 0;

		const int nMaxComponentCount = max(leftVersion.aComponents.GetSize(), rightVersion.aComponents.GetSize());
		for (int i = 0; i < nMaxComponentCount; ++i) {
			const UINT uLeftComponent = (i < leftVersion.aComponents.GetSize()) ? leftVersion.aComponents[i] : 0;
			const UINT uRightComponent = (i < rightVersion.aComponents.GetSize()) ? rightVersion.aComponents[i] : 0;
			if (uLeftComponent < uRightComponent)
				return -1;
			if (uLeftComponent > uRightComponent)
				return 1;
		}

		if (leftVersion.eSuffixType < rightVersion.eSuffixType)
			return -1;
		if (leftVersion.eSuffixType > rightVersion.eSuffixType)
			return 1;

		const int nSuffixCompare = leftVersion.strSuffix.CompareNoCase(rightVersion.strSuffix);
		if (nSuffixCompare < 0)
			return -1;
		if (nSuffixCompare > 0)
			return 1;

		return 0;
	}

	bool IsRemoteVersionNewer(const CString& strRemoteVersion, const CString& strCurrentVersion)
	{
		if (!IsValidVersionString(strRemoteVersion) || !IsValidVersionString(strCurrentVersion))
			return false;

		return CompareVersionStrings(strRemoteVersion, strCurrentVersion) > 0;
	}

	CString GetFirstNonEmptyLine(const CString& strText)
	{
		int nPos = 0;
		for (;;) {
			CString strLine = strText.Tokenize(_T("\r\n"), nPos);
			strLine.Trim();
			if (!strLine.IsEmpty())
				return strLine;
			if (nPos == -1)
				break;
		}
		return _T("");
	}

	bool QueryVersionCheckHttpStatusCode(HINTERNET hVersionFile, DWORD& dwStatusCode)
	{
		dwStatusCode = 0;
		DWORD dwStatusCodeSize = sizeof(dwStatusCode);
		if (!::HttpQueryInfo(hVersionFile, HTTP_QUERY_STATUS_CODE | HTTP_QUERY_FLAG_NUMBER, &dwStatusCode, &dwStatusCodeSize, NULL)) {
			TRACE(_T("Version check failed in HttpQueryInfo(STATUS_CODE), Error: %lu\n"), ::GetLastError());
			return false;
		}

		return true;
	}

	bool QueryVersionCheckContentType(HINTERNET hVersionFile, CString& strContentType)
	{
		strContentType.Empty();
		TCHAR szContentType[128] = {};
		DWORD dwContentTypeSize = _countof(szContentType);
		if (!::HttpQueryInfo(hVersionFile, HTTP_QUERY_CONTENT_TYPE, szContentType, &dwContentTypeSize, NULL)) {
			TRACE(_T("Version check failed in HttpQueryInfo(CONTENT_TYPE), Error: %lu\n"), ::GetLastError());
			return false;
		}

		strContentType = szContentType;
		strContentType.Trim();
		return !strContentType.IsEmpty();
	}

	bool IsVersionCheckContentTypeAllowed(const CString& strContentType)
	{
		CString strNormalizedContentType(strContentType);
		strNormalizedContentType.MakeLower();
		return strNormalizedContentType.Left(10) == _T("text/plain")
			|| strNormalizedContentType == _T("application/octet-stream");
	}

	bool TryExtractVersionFromDownloadedText(const CString& strDownloadedText, CString& strVersion)
	{
		strVersion = NormalizeVersionString(GetFirstNonEmptyLine(strDownloadedText));
		if (strVersion.IsEmpty()) {
			TRACE(_T("Version check failed because the response body did not contain a non-empty version line.\n"));
			return false;
		}

		if (!IsValidVersionString(strVersion)) {
			TRACE(_T("Version check failed because the response body was not a valid version string. Response=\"%s\"\n"), (LPCTSTR)strVersion);
			return false;
		}

		return true;
	}

	bool DownloadVersionTextFromUrl(const CString& strUrl, CString& strDownloadedText)
	{
		strDownloadedText.Empty();
		HINTERNET hInternet = ::InternetOpen(_T("eMuleAI-VersionCheck"), INTERNET_OPEN_TYPE_PRECONFIG, NULL, NULL, 0);
		if (hInternet == NULL) {
			TRACE(_T("Version check failed in InternetOpen, Error: %lu\n"), ::GetLastError());
			return false;
		}

		DWORD dwTimeoutMs = VERSIONCHECK_HTTP_TIMEOUT_MS;
		VERIFY(::InternetSetOption(hInternet, INTERNET_OPTION_CONNECT_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs)) != FALSE);
		VERIFY(::InternetSetOption(hInternet, INTERNET_OPTION_RECEIVE_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs)) != FALSE);
		VERIFY(::InternetSetOption(hInternet, INTERNET_OPTION_SEND_TIMEOUT, &dwTimeoutMs, sizeof(dwTimeoutMs)) != FALSE);

		const DWORD dwOpenUrlFlags = INTERNET_FLAG_RELOAD | INTERNET_FLAG_NO_CACHE_WRITE | INTERNET_FLAG_PRAGMA_NOCACHE | INTERNET_FLAG_KEEP_CONNECTION;
		HINTERNET hVersionFile = ::InternetOpenUrl(hInternet, strUrl, NULL, 0, dwOpenUrlFlags, 0);
		if (hVersionFile == NULL) {
			TRACE(_T("Version check failed in InternetOpenUrl, Error: %lu\n"), ::GetLastError());
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}

		DWORD dwHttpStatusCode = 0;
		if (!QueryVersionCheckHttpStatusCode(hVersionFile, dwHttpStatusCode)) {
			VERIFY(::InternetCloseHandle(hVersionFile));
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}
		if (dwHttpStatusCode != HTTP_STATUS_OK) {
			TRACE(_T("Version check failed because the server returned HTTP status %lu.\n"), dwHttpStatusCode);
			VERIFY(::InternetCloseHandle(hVersionFile));
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}

		CString strContentType;
		if (!QueryVersionCheckContentType(hVersionFile, strContentType)) {
			VERIFY(::InternetCloseHandle(hVersionFile));
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}
		if (!IsVersionCheckContentTypeAllowed(strContentType)) {
			TRACE(_T("Version check failed because the server returned an unexpected content type. ContentType=\"%s\"\n"), (LPCTSTR)strContentType);
			VERIFY(::InternetCloseHandle(hVersionFile));
			VERIFY(::InternetCloseHandle(hInternet));
			return false;
		}

		CStringA strContentA;
		char acBuffer[512];
		bool bReadOk = true;
		for (;;) {
			DWORD dwBytesRead = 0;
			if (!::InternetReadFile(hVersionFile, acBuffer, sizeof(acBuffer), &dwBytesRead)) {
				TRACE(_T("Version check failed in InternetReadFile, Error: %lu\n"), ::GetLastError());
				bReadOk = false;
				break;
			}
			if (dwBytesRead == 0)
				break;
			strContentA.Append(acBuffer, (int)dwBytesRead);
			if (strContentA.GetLength() > VERSIONCHECK_MAX_TEXT_BYTES) {
				strContentA = strContentA.Left(VERSIONCHECK_MAX_TEXT_BYTES);
				break;
			}
		}

		VERIFY(::InternetCloseHandle(hVersionFile));
		VERIFY(::InternetCloseHandle(hInternet));

		if (!bReadOk || strContentA.IsEmpty())
			return false;

		strDownloadedText = OptUtf8ToStr(strContentA);
		if (!strDownloadedText.IsEmpty() && strDownloadedText[0] == 0xFEFF)
			strDownloadedText = strDownloadedText.Mid(1);
		strDownloadedText.Trim();
		return !strDownloadedText.IsEmpty();
	}

	bool GetCaptionButtonBounds(HWND hWnd, CRect& rcCaptionButtons)
	{
		rcCaptionButtons.SetRectEmpty();

		HMODULE hDwmApi = ::LoadLibrary(_T("dwmapi.dll"));
		if (hDwmApi == NULL)
			return false;

		HRESULT(WINAPI * pfnDwmGetWindowAttribute)(HWND, DWORD, PVOID, DWORD) = NULL;
		(FARPROC&)pfnDwmGetWindowAttribute = ::GetProcAddress(hDwmApi, "DwmGetWindowAttribute");
		if (pfnDwmGetWindowAttribute == NULL) {
			VERIFY(::FreeLibrary(hDwmApi));
			return false;
		}

		RECT rcButtons = {};
		const HRESULT hResult = pfnDwmGetWindowAttribute(hWnd, DWMWA_CAPTION_BUTTON_BOUNDS, &rcButtons, sizeof(rcButtons));
		VERIFY(::FreeLibrary(hDwmApi));
		if (FAILED(hResult) || ::IsRectEmpty(&rcButtons))
			return false;

		rcCaptionButtons = rcButtons;
		CRect rcWindow;
		if (::GetWindowRect(hWnd, &rcWindow)) {
			// DWMWA_CAPTION_BUTTON_BOUNDS can be window-relative; normalize to screen space.
			const bool bLooksWindowRelative = (rcCaptionButtons.left >= 0 && rcCaptionButtons.top >= 0
				&& rcCaptionButtons.right <= rcWindow.Width() && rcCaptionButtons.bottom <= rcWindow.Height());
			if (bLooksWindowRelative)
				rcCaptionButtons.OffsetRect(rcWindow.left, rcWindow.top);
		}
		return true;
	}
}

class CTitleVersionOverlayWnd : public CWnd
{
public:
	CTitleVersionOverlayWnd()
		: m_pOwner(NULL)
	{
	}

	bool CreateOverlay(CemuleDlg* pOwner)
	{
		m_pOwner = pOwner;
		const CString strClassName(AfxRegisterWndClass(CS_DBLCLKS, ::LoadCursor(NULL, IDC_HAND), NULL, NULL));
		if (!CWnd::CreateEx(WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, strClassName, _T(""), WS_POPUP, 0, 0, 0, 0, (pOwner != NULL) ? pOwner->GetSafeHwnd() : NULL, NULL))
			return false;

		if (!::SetLayeredWindowAttributes(m_hWnd, TITLE_VERSION_OVERLAY_COLORKEY, 0, LWA_COLORKEY))
			TRACE(_T("SetLayeredWindowAttributes failed for title version overlay. Error: %lu\n"), ::GetLastError());

		return true;
	}

	void SetOwner(CemuleDlg* pOwner)
	{
		m_pOwner = pOwner;
	}

protected:
	afx_msg void OnPaint()
	{
		CPaintDC dc(this);
		CRect rcClient;
		GetClientRect(&rcClient);
		dc.FillSolidRect(&rcClient, TITLE_VERSION_OVERLAY_COLORKEY);

		if (m_pOwner != NULL)
			m_pOwner->DrawTitleVersionOverlay(dc, rcClient);
	}

	afx_msg void OnLButtonDown(UINT nFlags, CPoint point)
	{
		UNREFERENCED_PARAMETER(nFlags);
		UNREFERENCED_PARAMETER(point);
		if (m_pOwner != NULL)
			m_pOwner->OpenVersionReleasesURL();
	}

	afx_msg BOOL OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
	{
		UNREFERENCED_PARAMETER(pWnd);
		UNREFERENCED_PARAMETER(nHitTest);
		UNREFERENCED_PARAMETER(message);
		::SetCursor(AfxGetApp()->LoadStandardCursor(IDC_HAND));
		return TRUE;
	}

	DECLARE_MESSAGE_MAP()

private:
	CemuleDlg* m_pOwner;
};

BEGIN_MESSAGE_MAP(CTitleVersionOverlayWnd, CWnd)
	ON_WM_PAINT()
	ON_WM_LBUTTONDOWN()
	ON_WM_SETCURSOR()
END_MESSAGE_MAP()

///////////////////////////////////////////////////////////////////////////
// CemuleDlg Dialog

IMPLEMENT_DYNAMIC(CMsgBoxException, CException)

BEGIN_MESSAGE_MAP(CemuleDlg, CTrayDialog)
	///////////////////////////////////////////////////////////////////////////
	// Windows messages
	//
	ON_WM_SYSCOMMAND()
	ON_WM_PAINT()
	ON_WM_QUERYDRAGICON()
	ON_WM_ENDSESSION()
	ON_WM_SIZE()
	ON_WM_MOVING()
	ON_WM_SIZING()
	ON_WM_WINDOWPOSCHANGED()
	ON_WM_CLOSE()
	ON_WM_MENUCHAR()
	ON_WM_QUERYENDSESSION()
	ON_WM_SYSCOLORCHANGE()
	ON_WM_CTLCOLOR()
	ON_MESSAGE(WM_COPYDATA, OnWMData)
	ON_MESSAGE(WM_KICKIDLE, OnKickIdle)
	ON_MESSAGE(WM_USERCHANGED, OnUserChanged)
	ON_WM_SHOWWINDOW()
	ON_WM_DESTROY()
	ON_WM_SETTINGCHANGE()
	ON_WM_DEVICECHANGE()
	ON_WM_NCPAINT()
	ON_WM_NCLBUTTONDOWN()
	ON_WM_SETCURSOR()
	ON_WM_TIMER()
	ON_MESSAGE(WM_DISPLAYCHANGE, OnDisplayChange)
	ON_MESSAGE(WM_POWERBROADCAST, OnPowerBroadcast)

	///////////////////////////////////////////////////////////////////////////
	// WM_COMMAND messages
	//
	ON_COMMAND(MP_CONNECT, OnCommandConnect)
	ON_COMMAND(MP_DISCONNECT, CloseConnection)
	ON_COMMAND(MP_EXIT, OnClose)
	ON_COMMAND(MP_RESTORE, RestoreWindow)
	// quick-speed changer --
	ON_COMMAND_RANGE(MP_QS_U10, MP_QS_UP10, QuickSpeedUpload)
	ON_COMMAND_RANGE(MP_QS_D10, MP_QS_DC, QuickSpeedDownload)
	//--- quickspeed - paralize all ---
	ON_COMMAND_RANGE(MP_QS_PA, MP_QS_UA, QuickSpeedOther)
	// quick-speed changer -- based on xrmb
	ON_NOTIFY_EX_RANGE(RBN_CHEVRONPUSHED, 0, 0xffff, OnChevronPushed)

	ON_REGISTERED_MESSAGE(UWM_ARE_YOU_EMULE, OnAreYouEmule)
	ON_BN_CLICKED(IDC_HOTMENU, OnBnClickedHotmenu)

	ON_REGISTERED_MESSAGE(UWM_CONCHECKER, OnConChecker)

	ON_MESSAGE(UWM_POST_INIT_CONTROLS, OnPostInitControls)

	///////////////////////////////////////////////////////////////////////////
	// WM_USER messages
	//
	ON_MESSAGE(UM_TASKBARNOTIFIERCLICKED, OnTaskbarNotifierClicked)
	ON_MESSAGE(UM_CLOSE_MINIMULE, OnCloseMiniMule)

	// Web Server messages
	ON_MESSAGE(WEB_GUI_INTERACTION, OnWebGUIInteraction)
	ON_MESSAGE(WEB_CLEAR_COMPLETED, OnWebServerClearCompleted)
	ON_MESSAGE(WEB_FILE_RENAME, OnWebServerFileRename)
	ON_MESSAGE(WEB_ADDDOWNLOADS, OnWebAddDownloads)
	ON_MESSAGE(WEB_CATPRIO, OnWebSetCatPrio)
	ON_MESSAGE(WEB_ADDREMOVEFRIEND, OnAddRemoveFriend)

	ON_MESSAGE(UWM_EMULEAI_VERSIONCHECK_RESULT, OnEmuleAIVersionCheckResult)

	// UPnP
	ON_MESSAGE(UM_UPNP_RESULT, OnUPnPResult)

	///////////////////////////////////////////////////////////////////////////
	// WM_APP messages
	//
	ON_MESSAGE(TM_FINISHEDHASHING, OnFileHashed)
	ON_MESSAGE(TM_FILEOPPROGRESS, OnFileOpProgress)
	ON_MESSAGE(TM_HASHFAILED, OnHashFailed)
	ON_MESSAGE(TM_IMPORTPART, OnImportPart)
	ON_MESSAGE(TM_FRAMEGRABFINISHED, OnFrameGrabFinished)
	ON_MESSAGE(TM_FILEALLOCEXC, OnFileAllocExc)
	ON_MESSAGE(TM_FILECOMPLETED, OnFileCompleted)
	ON_MESSAGE(TM_CONSOLETHREADEVENT, OnConsoleThreadEvent)
	ON_MESSAGE(TM_SHAREDFILELISTFOUNDFILES, OnSharedFileListFoundFiles)
	ON_MESSAGE(TM_SHAREDFILESCTRLUPDATEFILE, OnSharedFilesCtrlUpdateFile)

	ON_MESSAGE(UM_APP_SWITCH_DARKMODE, OnDarkModeSwitch)
	ON_NOTIFY(NM_CUSTOMDRAW, AFX_IDW_REBAR, OnRebarCustomDraw)

#ifdef HAVE_WIN7_SDK_H
	ON_REGISTERED_MESSAGE(UWM_TASK_BUTTON_CREATED, OnTaskbarBtnCreated)
#endif
END_MESSAGE_MAP()

CemuleDlg::CemuleDlg(CWnd* pParent /*=NULL*/)
	: CTrayDialog(CemuleDlg::IDD, pParent)
	, m_pSplashWnd()
	, activewnd()
	, status()
	, m_wpFirstRestore()
	, m_hIcon()
	, m_connicons()
	, m_contactIcons()
	, transicons()
	, imicons()
	, m_icoSysTrayCurrent()
	, usericon()
	, m_icoSysTrayConnected()
	, m_icoSysTrayDisconnected()
	, m_icoSysTrayLowID()
	, m_pSystrayDlg()
	, m_pDropTarget()
	, m_iMsgIcon()
	, m_uLastSysTrayIconCookie(SYS_TRAY_ICON_COOKIE_FORCE_UPDATE)
	, m_uUpDatarate()
	, m_uDownDatarate()
	, m_bVersionCheckInProgress()
	, m_bNewVersionAvailable()
	, m_bStartMinimizedChecked()
	, m_bStartMinimized()
	, m_bMsgBlinkState()
	, m_bConnectRequestDelayedForUPnP()
	, m_bKadSuspendDisconnect()
	, m_bEd2kSuspendDisconnect()
	, m_bInitedCOM()
	, m_bSpecialThanksAnimationTimerActive()
	, m_thbButtons()
	, m_currentTBP_state(TBPF_NOPROGRESS)
	, m_prevProgress()
	, m_ovlIcon()
	, bPrevKadState()
	, bPrevEd2kState()
	, m_rcTitleVersionLink()
	, m_fontTitleVersionLink()
	, m_uTitleVersionAnimationHue()
	, m_bTitleVersionAnimationTimerActive()
	, m_pTitleVersionOverlay()
	, m_dwSplashTime(_UI32_MAX)
	, m_pMiniMule()
	, m_hTimer()
	, m_hUPnPTimeOutTimer()
	, m_hWndSearchDlg()
	, m_hWndTransferDlg()
	, notifierenabled()
{
	g_uMainThreadId = GetCurrentThreadId();
	SetClientIconList();
	preferenceswnd = new CPreferencesDlg;
	serverwnd = new CServerWnd;
	kademliawnd = new CKademliaWnd;
	transferwnd = new CTransferDlg;
	sharedfileswnd = new CSharedFilesWnd;
	searchwnd = new CSearchDlg;
	chatwnd = new CChatWnd;
	ircwnd = new CIrcWnd;
	statisticswnd = new CStatisticsDlg;
	toolbar = new CMuleToolbarCtrl;
	statusbar = new CMuleStatusBarCtrl;
	m_pDropTarget = new CMainFrameDropTarget;
}

void CemuleDlg::SetClientIconList()
{
	m_IconList.Create(16, 16, theApp.m_iDfltImageListColorFlags | ILC_MASK, 0, 1);
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkey")));			//0 - eDonkey
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientCompatible")));		//2 - Compat
	m_IconList.Add(CTempIconLoader(_T("ClientCompatiblePlus")));
	m_IconList.Add(CTempIconLoader(_T("Friend")));					//4 - friend
	m_IconList.Add(CTempIconLoader(_T("ClientMLDonkey")));			//5 - ML
	m_IconList.Add(CTempIconLoader(_T("ClientMLDonkeyPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyHybrid")));	//7 - Hybrid
	m_IconList.Add(CTempIconLoader(_T("ClientEDonkeyHybridPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientShareaza")));			//9 - Shareaza
	m_IconList.Add(CTempIconLoader(_T("ClientShareazaPlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientAMule")));			//11 - amule
	m_IconList.Add(CTempIconLoader(_T("ClientAMulePlus")));
	m_IconList.Add(CTempIconLoader(_T("ClientLPhant")));			//13 - Lphant
	m_IconList.Add(CTempIconLoader(_T("ClientLPhantPlus")));
	m_IconList.Add(CTempIconLoader(_T("Server")));					//15 - http source
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("ClientSecureOvl"))), 1);
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("OverlayObfu"))), 2);
	m_IconList.SetOverlayImage(m_IconList.Add(CTempIconLoader(_T("OverlaySecureObfu"))), 3);
}

CImageList& CemuleDlg::GetClientIconList()
{
	return m_IconList;
}


CemuleDlg::~CemuleDlg()
{
	CloseTTS();
	DestroyMiniMule();
	if (m_icoSysTrayCurrent)
		VERIFY(::DestroyIcon(m_icoSysTrayCurrent));
	if (m_hIcon)
		VERIFY(::DestroyIcon(m_hIcon));
	DestroyIconsArr(m_connicons, _countof(m_connicons));
	DestroyIconsArr(transicons, _countof(transicons));
	DestroyIconsArr(imicons, _countof(imicons));
	if (m_icoSysTrayConnected)
		VERIFY(::DestroyIcon(m_icoSysTrayConnected));
	if (m_icoSysTrayDisconnected)
		VERIFY(::DestroyIcon(m_icoSysTrayDisconnected));
	if (m_icoSysTrayLowID)
		VERIFY(::DestroyIcon(m_icoSysTrayLowID));
	if (usericon)
		VERIFY(::DestroyIcon(usericon));

#ifdef HAVE_WIN7_SDK_H
	if (m_pTaskbarList != NULL) {
		m_pTaskbarList.Release();
		ASSERT(m_bInitedCOM);
	}
	if (m_bInitedCOM)
		CoUninitialize();
#endif

	// already destroyed by windows?

	delete m_pDropTarget;
	delete statusbar;
	delete toolbar;
	DestroyAndDeleteWnd(statisticswnd);
	DestroyAndDeleteWnd(ircwnd);
	DestroyAndDeleteWnd(chatwnd);
	DestroyAndDeleteFrameWndSafe(searchwnd, m_hWndSearchDlg);
	DestroyAndDeleteWnd(sharedfileswnd);
	DestroyAndDeleteFrameWndSafe(transferwnd, m_hWndTransferDlg);
	DestroyAndDeleteWnd(kademliawnd);
	DestroyAndDeleteWnd(serverwnd);
	DestroyAndDeleteWnd(preferenceswnd);
}

void CemuleDlg::DoDataExchange(CDataExchange* pDX)
{
	CTrayDialog::DoDataExchange(pDX);
}

LRESULT CemuleDlg::OnAreYouEmule(WPARAM, LPARAM)
{
	return UWM_ARE_YOU_EMULE;
}

void DialogCreateIndirect(CDialog* pWnd, UINT uID)
{
#if 0
	// This could be a nice way to change the font size of the main windows without needing
	// to re-design the dialog resources. However, that technique does not work for the
	// SearchWnd and it also introduces new glitches (which would need to get resolved)
	// in almost all of the main windows.
	CDialogTemplate dlgTempl;
	dlgTempl.Load(MAKEINTRESOURCE(uID));
	dlgTempl.SetFont(_T("MS Shell Dlg"), 8);
	pWnd->CreateIndirect(dlgTempl.m_hTemplate);
	FreeResource(dlgTempl.Detach());
#else
	pWnd->Create(uID);
#endif
}

LRESULT CemuleDlg::OnPostInitControls(WPARAM, LPARAM)
{
	// Subclass and anchor status bar once the dialog and children are live
	if (!statusbar->m_hWnd) {
		if (CWnd* pSB = GetDlgItem(IDC_STATUSBAR); pSB && ::IsWindow(pSB->m_hWnd)) {
			statusbar->SubclassWindow(pSB->m_hWnd);
			statusbar->EnableToolTips(true);
			SetStatusBarPartsSize();
			AddOrReplaceAnchor(this, *statusbar, BOTTOM_LEFT, BOTTOM_RIGHT);
		} else
			return 0;
	}

	return 0;
}

CString CemuleDlg::GetMainWindowTitleText() const
{
	CString strTitle(_T("eMule "));
	strTitle += MOD_VERSION;
#ifdef _DEBUG
	#if defined(_M_ARM64)
	const LPCTSTR pszPlatform = _T("arm64");
	#elif defined(_M_X64) || defined(_M_AMD64) || defined(_WIN64)
	const LPCTSTR pszPlatform = _T("x64");
	#elif defined(_M_IX86) || defined(_X86_)
	const LPCTSTR pszPlatform = _T("x86");
	#else
	const LPCTSTR pszPlatform = _T("unknown");
	#endif
	strTitle.AppendFormat(_T(" Debug %s"), pszPlatform);
#endif
	return strTitle;
}

BOOL CemuleDlg::OnInitDialog()
{
	theStats.starttime = ::GetTickCount();
#ifdef HAVE_WIN7_SDK_H
	// allow the TaskbarButtonCreated- & (tbb-)WM_COMMAND message to be sent to our window if our app is running elevated
	if (thePrefs.GetWindowsVersion() >= _WINVER_7_) {
		m_bInitedCOM = SUCCEEDED(CoInitialize(NULL));
		if (m_bInitedCOM) {
			typedef BOOL(WINAPI* PChangeWindowMessageFilter)(UINT message, DWORD dwFlag);
			PChangeWindowMessageFilter ChangeWindowMessageFilter
				= (PChangeWindowMessageFilter)(GetProcAddress(GetModuleHandle(_T("user32.dll")), "ChangeWindowMessageFilter"));
			if (ChangeWindowMessageFilter) {
				ChangeWindowMessageFilter(UWM_TASK_BUTTON_CREATED, 1);
				ChangeWindowMessageFilter(WM_COMMAND, 1);
				ChangeWindowMessageFilter(WM_SETTINGCHANGE, 1); // Also allow settings broadcasts while elevated (UIPI guard).
			}
		}
		else
			ASSERT(0);
	}
#endif

	// temporary disable the 'startup minimized' option, otherwise no window will be shown at all
	if (!thePrefs.IsFirstStart())
		m_bStartMinimized = thePrefs.GetStartMinimized() || theApp.DidWeAutoStart();

	// show splash screen as early as possible to "entertain" user while starting emule up
	if (thePrefs.UseSplashScreen() && !m_bStartMinimized)
		ShowSplash();

	// Create global GUI objects
	theApp.CreateAllFonts();
	theApp.CreateBackwardDiagonalBrush();
	m_wndTaskbarNotifier.SetTextDefaultFont();
	CTrayDialog::OnInitDialog();
	CreateToolbarCmdIconMap();

	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL) {
		pSysMenu->AppendMenu(MF_SEPARATOR);

		ASSERT((MP_ABOUTBOX & 0xFFF0) == MP_ABOUTBOX && MP_ABOUTBOX < 0xF000);
		pSysMenu->AppendMenu(MF_STRING, MP_ABOUTBOX, GetResString(_T("ABOUTBOX")));

		// remaining system menu entries are created later...
	}

	CWnd* pwndToolbarX = toolbar;
	if (toolbar->Create(WS_CHILD | WS_VISIBLE, CRect(), this, IDC_TOOLBAR)) {
		toolbar->Init();
		if (thePrefs.GetUseReBarToolbar()) {
			if (m_ctlMainTopReBar.Create(WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN |
				RBS_BANDBORDERS | RBS_AUTOSIZE | CCS_NODIVIDER,
				CRect(), this, AFX_IDW_REBAR))
			{
				CSize sizeBar;
				VERIFY(toolbar->GetMaxSize(&sizeBar));
				REBARBANDINFO rbbi = {};
				rbbi.cbSize = (UINT)sizeof rbbi;
				rbbi.fMask = RBBIM_STYLE | RBBIM_SIZE | RBBIM_CHILD | RBBIM_CHILDSIZE | RBBIM_IDEALSIZE | RBBIM_ID;
				rbbi.fStyle = RBBS_NOGRIPPER | RBBS_BREAK | RBBS_USECHEVRON;
				rbbi.hwndChild = toolbar->m_hWnd;
				rbbi.cxMinChild = sizeBar.cy;
				rbbi.cyMinChild = sizeBar.cy;
				rbbi.cxIdeal = sizeBar.cx;
				rbbi.cx = rbbi.cxIdeal;
				VERIFY(m_ctlMainTopReBar.InsertBand(UINT_MAX, &rbbi));
				toolbar->SaveCurHeight();
				toolbar->UpdateBackground();

				pwndToolbarX = &m_ctlMainTopReBar;
			}
		}
	}

	// set title
	SetWindowText(GetMainWindowTitleText());
	EnsureMfcThreadToolTipCtrlX(this);

	// Init taskbar notifier
	m_wndTaskbarNotifier.CreateWnd(this);
	LoadNotifier(thePrefs.GetNotifierConfiguration());

	// set statusbar
	// the statusbar control is created as a custom control in the dialog resource,
	// this solves font and sizing problems when using large system fonts
	CWnd* pSB = GetDlgItem(IDC_STATUSBAR);
	if (pSB && ::IsWindow(pSB->m_hWnd)) {
		statusbar->SubclassWindow(GetDlgItem(IDC_STATUSBAR)->m_hWnd);
		statusbar->EnableToolTips(true);
		SetStatusBarPartsSize();
	} else
		PostMessage(UWM_POST_INIT_CONTROLS); // defer until created

	// create main window dialog pages
	DialogCreateIndirect(serverwnd, IDD_SERVER);
	DialogCreateIndirect(sharedfileswnd, IDD_FILES);
	searchwnd->CreateWnd(this); // can not use 'DialogCreateIndirect' for the SearchWnd, grrr...
	m_hWndSearchDlg = searchwnd->GetSafeHwnd();
	DialogCreateIndirect(chatwnd, IDD_CHAT);
	transferwnd->CreateWnd(this);
	m_hWndTransferDlg = transferwnd->GetSafeHwnd();
	DialogCreateIndirect(statisticswnd, IDD_STATISTICS);
	DialogCreateIndirect(kademliawnd, IDD_KADEMLIAWND);
	DialogCreateIndirect(ircwnd, IDD_IRC);

	// with the top rebar control, some XP themes look better with additional lite borders, some not.

	// optional: restore last used main window dialog
	if (thePrefs.GetRestoreLastMainWndDlg()) {
		CWnd* activate;
		switch (thePrefs.GetLastMainWndDlgID()) {
		case IDD_FILES:
			activate = sharedfileswnd;
			break;
		case IDD_SEARCH:
			activate = searchwnd;
			break;
		case IDD_CHAT:
			activate = chatwnd;
			break;
		case IDD_TRANSFER:
			activate = transferwnd;
			break;
		case IDD_STATISTICS:
			activate = statisticswnd;
			break;
		case IDD_KADEMLIAWND:
			activate = kademliawnd;
			break;
		case IDD_IRC:
			activate = ircwnd;
			break;
			//case IDD_SERVER:
		default:
			activate = serverwnd;
		}
		SetActiveDialog(activate);
	}
	// if still no active window, activate server window
	if (activewnd == NULL)
		SetActiveDialog(serverwnd);

	SetAllIcons();
	InitWindowStyles(this, true); // Moved down
	Localize();

	// set update interval of graphic rate display (in seconds)

	// adjust all main window sizes for toolbar height and maximize the child windows
	CRect rcClient, rcToolbar, rcStatusbar;
	GetClientRect(&rcClient);
	pwndToolbarX->GetWindowRect(&rcToolbar);

	rcStatusbar.SetRectEmpty();
	if (statusbar->m_hWnd)
		statusbar->GetWindowRect(&rcStatusbar);
	rcClient.top += rcToolbar.Height();
	rcClient.bottom -= rcStatusbar.Height();

	CWnd* const apWnds[] =
	{
		serverwnd,
		kademliawnd,
		transferwnd,
		sharedfileswnd,
		searchwnd,
		chatwnd,
		ircwnd,
		statisticswnd
	};
	for (unsigned i = 0; i < _countof(apWnds); ++i) {
		apWnds[i]->SetWindowPos(NULL, rcClient.left, rcClient.top, rcClient.Width(), rcClient.Height(), SWP_NOZORDER);
		AddOrReplaceAnchor(this, *apWnds[i], TOP_LEFT, BOTTOM_RIGHT);
	}

	ResizeSpeedGraph();

	// anchor bars
	AddOrReplaceAnchor(this, *pwndToolbarX, TOP_LEFT, TOP_RIGHT);
	if (statusbar->m_hWnd)
		AddOrReplaceAnchor(this, *statusbar, BOTTOM_LEFT, BOTTOM_RIGHT);

	statisticswnd->ShowInterval();

	// tray icon
	TraySetMinimizeToTray(thePrefs.GetMinTrayPTR());
	TrayMinimizeToTrayChange();

	ShowTransferRate(true);
	ShowPing();
	searchwnd->UpdateCatTabs();

	///////////////////////////////////////////////////////////////////////////
	// Restore saved window placement
	//
	WINDOWPLACEMENT wp;
	wp.length = (UINT)sizeof wp;
	wp = thePrefs.GetEmuleWindowPlacement();
	if (m_bStartMinimized) {
		// To avoid the window flickering during startup we try to set the proper window show state right here.
		if (*thePrefs.GetMinTrayPTR()) {
			// Minimize to System Tray
			//
			// Unfortunately this does not work. The eMule main window is a modal dialog which is invoked
			// by CDialog::DoModal which eventually calls CWnd::RunModalLoop. Look at 'MLF_SHOWONIDLE' and
			// 'bShowIdle' in the above noted functions to see why it's not possible to create the window
			// right in hidden state.

			//--- attempt #1
			//--- doesn't work at all

			//--- attempt #2
			//--- creates window flickering

			//--- attempt #3
			// Minimize the window into the task bar and later move it into the tray
			if (wp.showCmd == SW_SHOWMAXIMIZED)
				wp.flags = WPF_RESTORETOMAXIMIZED;
			wp.showCmd = SW_MINIMIZE;
			m_bStartMinimizedChecked = false;

			// to get properly restored from tray bar (after attempt #3) we have to use a patched 'restore' window cmd
			m_wpFirstRestore = thePrefs.GetEmuleWindowPlacement();
			m_wpFirstRestore.length = (UINT)sizeof m_wpFirstRestore;
			if (m_wpFirstRestore.showCmd != SW_SHOWMAXIMIZED)
				m_wpFirstRestore.showCmd = SW_SHOWNORMAL;
		}
		else {
			// Minimize to System Taskbar
			if (wp.showCmd == SW_SHOWMAXIMIZED)
				wp.flags = WPF_RESTORETOMAXIMIZED;
			wp.showCmd = SW_MINIMIZE; // Minimize window but do not activate it.
			m_bStartMinimizedChecked = true;
		}
	}
	else {
		// Allow only SW_SHOWNORMAL and SW_SHOWMAXIMIZED. Ignore SW_SHOWMINIMIZED to make sure
		// the window becomes visible.
		// If user wants SW_SHOWMINIMIZED, we already have an explicit option for this (see above).
		if (wp.showCmd != SW_SHOWMAXIMIZED)
			wp.showCmd = SW_SHOWNORMAL;
		m_bStartMinimizedChecked = true;
	}
	SetWindowPlacement(&wp);

	if (thePrefs.GetWSIsEnabled())
		theApp.webserver->StartServer();

	VERIFY((m_hTimer = ::SetTimer(NULL, 0, SEC2MS(3)/10, StartupTimer)) != 0);
	if (thePrefs.GetVerbose() && !m_hTimer)
		AddDebugLogLine(true, _T("Failed to create 'startup' timer - %s"), (LPCTSTR)EscPercent(GetErrorMessage(::GetLastError())));

	// Start UPnP port forwarding
	if (thePrefs.IsUPnPEnabled())
		StartUPnP();

	if (thePrefs.IsFirstStart()) {
		// temporary disable the 'startup minimized' option, otherwise no window will be shown at all
		m_bStartMinimized = false;
		DestroySplash();
		FirstTimeWizard();
	}

	VERIFY(m_pDropTarget->Register(this));

	// start aichsyncthread

	// debug info
	DebugLog(_T("Using '%s' as config directory"), (LPCTSTR)thePrefs.GetMuleDirectory(EMULE_CONFIGDIR));

	if (!thePrefs.HasCustomTaskIconColor())
		SetTaskbarIconColor();

	if (thePrefs.GetConnectionChecker())
		theApp.ConChecker->Start();

	// Ensure any files already queued by CSharedFileListSearchThread are processed
	PostMessage(TM_SHAREDFILELISTFOUNDFILES, 0, 0);

	return TRUE;
}

void CemuleDlg::DoVersioncheck(bool manual)
{
	const CString strCurrentVersion = NormalizeVersionString(MOD_VERSION);
	CString strLastKnownVersionOnServer = NormalizeVersionString(thePrefs.GetLastKnownVersionOnServer());
	if (!strLastKnownVersionOnServer.IsEmpty()) {
		if (!IsRemoteVersionNewer(strLastKnownVersionOnServer, strCurrentVersion)) {
			thePrefs.SetLastKnownVersionOnServer(_T(""));
			strLastKnownVersionOnServer.Empty();
		} else if (!manual) {
			m_bNewVersionAvailable = true;
			StartTitleVersionAnimation();
			InvalidateTitleVersionFrame();
			TRACE(_T("eMuleAI version check skipped. Local=\"%s\" LastKnownServer=\"%s\"\n"), (LPCTSTR)strCurrentVersion, (LPCTSTR)strLastKnownVersionOnServer);
			return;
		}
	}

#ifndef _DEVBUILD
	if (!manual && thePrefs.GetLastVC() != 0) {
		CTime last(thePrefs.GetLastVC());
		struct tm tmTemp;
		time_t tLast = safe_mktime(last.GetLocalTm(&tmTemp));
		time_t tNow = safe_mktime(CTime::GetCurrentTime().GetLocalTm(&tmTemp));
		if (difftime(tNow, tLast) / DAY2S(1) < 1)
			return;
	}
#endif

	if (m_bVersionCheckInProgress)
		return;

	EmuleAIVersionCheckRequest* pRequest = new EmuleAIVersionCheckRequest;
	pRequest->hNotifyWnd = m_hWnd;
	pRequest->strVersionRawUrl = thePrefs.GetVersionCheckRawURL();
	pRequest->strCurrentVersion = MOD_VERSION;
	pRequest->bManual = manual;

	CWinThread* pThread = AfxBeginThread(EmuleAIVersionCheckThread, pRequest, THREAD_PRIORITY_BELOW_NORMAL);
	if (pThread == NULL) {
		delete pRequest;
		TRACE(_T("Version check thread could not be created.\n"));
		return;
	}

	AddLogLine(true, GetResString(_T("EMULE_AI_VERSION_CHECK_STARTED")));
	m_bVersionCheckInProgress = true;
}

void CemuleDlg::OpenVersionReleasesURL() const
{
	BrowserOpen(thePrefs.GetVersionCheckURL(), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
}

bool CemuleDlg::IsInitializing()
{
	if (theApp.emuledlg->status == 255)
		return false;
	return true;
}

void CALLBACK CemuleDlg::StartupTimer(HWND /*hwnd*/, UINT /*uiMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) noexcept
{
	// NOTE: Always handle all type of MFC exceptions in TimerProcs - otherwise we'll get mem leaks
	try {
		switch (theApp.emuledlg->status) {
		case 0:
			++theApp.emuledlg->status;
			theApp.sharedfiles->SetOutputCtrl(&theApp.emuledlg->sharedfileswnd->sharedfilesctrl);
			++theApp.emuledlg->status;
		case 1:
			break;
		case 2:
			++theApp.emuledlg->status;
			try {
				theApp.serverlist->Init();
			} catch (...) {
				ASSERT(0);
				LogError(LOG_STATUSBAR, _T("Failed to initialize server list - Unknown exception"));
			}
			++theApp.emuledlg->status;
		case 3:
			break;
		case 4:
		{
			++theApp.emuledlg->status;
			bool bError = false;

			// NOTE: If we have an unhandled exception in CDownloadQueue::Init, MFC will silently catch it
			// and the creation of the TCP and the UDP socket will not be done -> client will get a LowID!
			try {
				theApp.downloadqueue->Init();
			}
			catch (...) {
				ASSERT(0);
				LogError(LOG_STATUSBAR, _T("Failed to initialize download queue - Unknown exception"));
				bError = true;
			}

			theApp.DownloadChecker->ReloadMap();
			if (thePrefs.IsStoringSearchesEnabled())
				theApp.searchlist->LoadSearches();

			if (!theApp.listensocket->StartListening()) {
				CString strError;
				strError.Format(GetResString(_T("MAIN_SOCKETERROR")), thePrefs.GetPort());
				LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
				if (thePrefs.GetNotifierOnImportantError())
					theApp.emuledlg->ShowNotifier(strError, TBN_IMPORTANTEVENT);
				bError = true;
			}
			if (!theApp.clientudp->Create()) {
				CString strError;
				strError.Format(GetResString(_T("MAIN_SOCKETERROR")), thePrefs.GetUDPPort());
				LogError(LOG_STATUSBAR, _T("%s"), (LPCTSTR)strError);
				if (thePrefs.GetNotifierOnImportantError())
					theApp.emuledlg->ShowNotifier(strError, TBN_IMPORTANTEVENT);
			}

			if (!bError) // show the success msg, only if we had no serious error
				AddLogLine(true, GetResString(_T("MAIN_READY")), (LPCTSTR)(theApp.GetAppVersion().Mid(6)));

			theApp.m_app_state = APP_STATE_RUNNING; //initialization completed
			theApp.emuledlg->toolbar->EnableButton(TBBTN_CONNECT, TRUE);
			theApp.emuledlg->m_SysMenuOptions.EnableMenuItem(MP_CONNECT, MF_ENABLED);
			theApp.emuledlg->serverwnd->GetDlgItem(IDC_ED2KCONNECT)->EnableWindow();
			theApp.emuledlg->kademliawnd->UpdateControlsState(); //application state change is not tracked - force update

			// moved down
#ifdef HAVE_WIN7_SDK_H
			theApp.emuledlg->UpdateStatusBarProgress();
#endif
		}
		break;
		// delay load shared files
		case 5:
			theApp.sharedfiles->SetOutputCtrl(&theApp.emuledlg->sharedfileswnd->sharedfilesctrl);

			// BEGIN SiRoB: SafeHash fix originaly in OnInitDialog (delay load shared files)
			// start aichsyncthread
			AfxBeginThread(RUNTIME_CLASS(CAICHSyncThread), THREAD_PRIORITY_BELOW_NORMAL, 0);
			// END SiRoB: SafeHash
			theApp.emuledlg->status++;
			break;
		case 255:
			break;
		default:
		//autoconnect only after emule loaded completely
			theApp.emuledlg->status = 255;
			if (thePrefs.DoAutoConnect())
				theApp.emuledlg->OnBnClickedConnect();
			theApp.emuledlg->StopTimer();
		}
	}
	CATCH_DFLT_EXCEPTIONS(_T("CemuleDlg::StartupTimer"))
}

void CemuleDlg::StopTimer()
{
	if (m_hTimer) {
		VERIFY(::KillTimer(NULL, m_hTimer));
		m_hTimer = 0;
	}
	if (true)
		DoVersioncheck(false);

	if (!theApp.m_strPendingLink.IsEmpty()) {
		OnWMData(NULL, (LPARAM)&theApp.sendstruct);
		theApp.m_strPendingLink.Empty();
	}
}


void CemuleDlg::OnSysCommand(UINT nID, LPARAM lParam)
{
	// System menu - Speed selector
	if (nID >= MP_QS_U10 && nID <= MP_QS_UP10) {
		QuickSpeedUpload(nID);
		return;
	}
	if (nID >= MP_QS_D10 && nID <= MP_QS_DC) {
		QuickSpeedDownload(nID);
		return;
	}
	if (nID == MP_QS_PA || nID == MP_QS_UA) {
		QuickSpeedOther(nID);
		return;
	}

	switch (nID) {
	case MP_ABOUTBOX:
		if (m_pSplashWnd != NULL)
			DestroySplash();
		ShowSplash(false);
		break;
	case MP_CONNECT:
		StartConnection(true);
		break;
	case MP_DISCONNECT:
		CloseConnection();
		break;
	default:
		CTrayDialog::OnSysCommand(nID, lParam);
	}

	switch (nID & 0xFFF0) {
	case SC_MINIMIZE:
	case MP_MINIMIZETOTRAY:
	case SC_RESTORE:
	case SC_MAXIMIZE:
		ShowTransferRate(true);
		ShowPing();
		transferwnd->UpdateCatTabTitles();
	}
}

void CemuleDlg::PostStartupMinimized()
{
	if (!m_bStartMinimizedChecked) {
		//TODO: Use full initialized 'WINDOWPLACEMENT' and remove the 'OnCancel' call...
		// Isn't that easy. Read comments in OnInitDialog.
		m_bStartMinimizedChecked = true;
		if (m_bStartMinimized) {
			if (theApp.DidWeAutoStart() && !thePrefs.mintotray) {
				thePrefs.mintotray = true;
				MinimizeWindow();
				thePrefs.mintotray = false;
			}
			else
				MinimizeWindow();
		}
	}
}

void CemuleDlg::OnPaint()
{
	if (IsIconic()) {
		CPaintDC dc(this);

		SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);

		int cxIcon = ::GetSystemMetrics(SM_CXICON);
		int cyIcon = ::GetSystemMetrics(SM_CYICON);
		CRect rect;
		GetClientRect(&rect);
		int x = (rect.Width() - cxIcon + 1) / 2;
		int y = (rect.Height() - cyIcon + 1) / 2;

		dc.DrawIcon(x, y, m_hIcon);
	}
	else
		CTrayDialog::OnPaint();
}

HCURSOR CemuleDlg::OnQueryDragIcon()
{
	return static_cast<HCURSOR>(m_hIcon);
}

void CemuleDlg::OnBnClickedConnect()
{
	if (!theApp.IsConnected() && !theApp.serverconnect->IsConnecting() && !Kademlia::CKademlia::IsRunning())
		//connect if not currently connected or connecting
		StartConnection(true);
	else
		CloseConnection();
}

void CemuleDlg::OnCommandConnect()
{
	StartConnection(true);
}

void CemuleDlg::ResetServerInfo()
{
	serverwnd->servermsgbox->Reset();
}

void CemuleDlg::ResetLog()
{
	serverwnd->logbox->Reset();
}

void CemuleDlg::ResetDebugLog()
{
	serverwnd->debuglog->Reset();
}

void CemuleDlg::ResetLeecherLog()
{
	serverwnd->protectionlog->Reset();
}

void CemuleDlg::AddLogText(UINT uFlags, LPCTSTR pszText)
{
	if (GetCurrentThreadId() != g_uMainThreadId) {
		theApp.QueueLogLineEx(uFlags, _T("%s"), pszText);
		return;
	}

	if (uFlags & LOG_STATUSBAR) {
		if (statusbar && statusbar->m_hWnd && ::IsWindow(statusbar->m_hWnd) && !theApp.IsClosing())
			statusbar->SetText(pszText, SBarLog, 0);
		else if (!theApp.IsClosing())
			CDarkMode::MessageBox(pszText);
	}
#if defined(_DEBUG) || defined(USE_DEBUG_DEVICE)
	Debug(_T("%s\n"), pszText);
#endif

	if ((uFlags & LOG_DEBUG) && !thePrefs.GetVerbose())
		return;

	if ((uFlags & LOG_LEECHER) && !thePrefs.GetVerbose())
		return;

	TCHAR temp[1060];
	int iLen = _sntprintf(temp, _countof(temp), _T("%s: %s\r\n"), (LPCTSTR)CTime::GetCurrentTime().Format(thePrefs.GetDateTimeFormat4Log()), pszText);
	if (iLen >= 0) {
		auto TryAppend = [&](CHTRichEditCtrl *ctrl) -> bool {
			if (theApp.IsClosing() || ctrl == NULL || ctrl->m_hWnd == NULL || !::IsWindow(ctrl->m_hWnd))
				return false;
			__try {
				ctrl->AddTyped(temp, iLen, uFlags & LOGMSGTYPEMASK);
				return true;
			} __except(EXCEPTION_EXECUTE_HANDLER) {
				TRACE(_T("AddLogText: UI log append failed (code=%lx)\n"), GetExceptionCode());
				return false;
			}
		};

		bool bHandled = false;
		if (!(uFlags & LOG_DEBUG) && !(uFlags & LOG_LEECHER)) {
			bHandled = TryAppend(serverwnd ? serverwnd->logbox : NULL);
			if (bHandled) {
				if (::IsWindow(serverwnd->StatusSelector) && serverwnd->StatusSelector.GetCurSel() != CServerWnd::PaneLog)
					serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneLog, TRUE);
				if (!(uFlags & LOG_DONTNOTIFY) && status) //status!=0 means this dialog has been created
					ShowNotifier(pszText, TBN_LOG);
				if (thePrefs.GetLog2Disk())
					theLog.Log(temp, iLen);
			}
		}
		else if (thePrefs.GetVerbose() && (uFlags & LOG_LEECHER)) {
			bHandled = TryAppend(serverwnd ? serverwnd->protectionlog : NULL);
			if (bHandled) {
				if (IsWindow(serverwnd->StatusSelector) && serverwnd->StatusSelector.GetCurSel() != CServerWnd::PaneLeecherLog)
					serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneLeecherLog, TRUE);
				if (thePrefs.GetDebug2Disk())
					theVerboseLog.Log(temp, iLen);
			}
		} else
				if (thePrefs.GetVerbose() && ((uFlags & LOG_DEBUG) || thePrefs.GetFullVerbose())) {
					bHandled = TryAppend(serverwnd ? serverwnd->debuglog : NULL);
					if (bHandled) {
						if (::IsWindow(serverwnd->StatusSelector) && serverwnd->StatusSelector.GetCurSel() != CServerWnd::PaneVerboseLog)
							serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneVerboseLog, TRUE);

						if (thePrefs.GetDebug2Disk())
							theVerboseLog.Log(temp, iLen);
					}
				}

		if (!bHandled) {
			if (!(uFlags & LOG_DEBUG) && !(uFlags & LOG_LEECHER)) {
				if (thePrefs.GetLog2Disk())
					theLog.Log(temp, iLen);
			}
			else if (thePrefs.GetVerbose() && thePrefs.GetDebug2Disk())
				theVerboseLog.Log(temp, iLen);
			else
				TRACE(_T("App Log (fallback): %s\n"), pszText);
		}
	}
}

CString CemuleDlg::GetLastLogEntry()
{
	return serverwnd->logbox->GetLastLogEntry();
}

CString CemuleDlg::GetAllLogEntries()
{
	return serverwnd->logbox->GetAllLogEntries();
}

CString CemuleDlg::GetLastDebugLogEntry()
{
	return serverwnd->debuglog->GetLastLogEntry();
}

CString CemuleDlg::GetAllDebugLogEntries()
{
	return serverwnd->debuglog->GetAllLogEntries();
}

CString CemuleDlg::GetServerInfoText()
{
	return serverwnd->servermsgbox->GetText();
}

void CemuleDlg::AddServerMessageLine(UINT uFlags, LPCTSTR pszLine)
{
	CString strMsgLine(pszLine);
	strMsgLine += _T('\n');
	if ((uFlags & LOGMSGTYPEMASK) == LOG_INFO)
		serverwnd->servermsgbox->AppendText(strMsgLine);
	else
		serverwnd->servermsgbox->AddTyped(strMsgLine, strMsgLine.GetLength(), uFlags & LOGMSGTYPEMASK);
	if (::IsWindow(serverwnd->StatusSelector) && serverwnd->StatusSelector.GetCurSel() != CServerWnd::PaneServerInfo)
		serverwnd->StatusSelector.HighlightItem(CServerWnd::PaneServerInfo, TRUE);
}


UINT CemuleDlg::GetConnectionStateIconIndex() const
{
	//Calculate index in 'm_connicons' array
	//3 KAD states per group: "disconnected", "firewalled", "open"
	//Groups correspond to ED2K states: "disconnected", "low ID", "high ID"
	UINT idx = static_cast<UINT>(Kademlia::CKademlia::IsConnected());
	if (idx)
		idx += static_cast<UINT>(!Kademlia::CKademlia::IsFirewalled());
	if (theApp.serverconnect->IsConnected())
		idx += theApp.serverconnect->IsLowID() ? 3 : 6;
	return idx;
}

CString CemuleDlg::GetConnectionStateString()
{
	LPCTSTR ed2k;
	LPCTSTR kad;
	if (theApp.serverconnect->IsConnected())
		ed2k = _T("CONNECTED");
	else
		ed2k = theApp.serverconnect->IsConnecting() ? _T("CONNECTING") : _T("DISCONNECTED");

	if (Kademlia::CKademlia::IsConnected())
		kad = _T("CONNECTED");
	else
		kad = Kademlia::CKademlia::IsRunning() ? _T("CONNECTING") : _T("DISCONNECTED");

	CString state;
	state.Format(_T("eD2K:%s|Kad:%s"), (LPCTSTR)GetResString(ed2k), (LPCTSTR)GetResString(kad));
	return state;
}

void CemuleDlg::ShowConnectionStateIcon()
{
	// eD2K Icon
	int iED2KIcon;
	if (!theApp.serverconnect->IsConnected())
		iED2KIcon = 4; // CONTACT4
	else if (!theApp.serverconnect->IsLowID())
		iED2KIcon = 0; // CONTACT0 (High ID)
	else if (theApp.clientlist->GetEServerBuddyStatus() == Connected)
		iED2KIcon = 1; // CONTACT1 (Low ID + Buddy connected)
	else
		iED2KIcon = 2; // CONTACT2 (Low ID + No Buddy)
	statusbar->SetIcon(SBarED2K, m_contactIcons[iED2KIcon]);

	// Kad Icon
	int iKadIcon;
	if (!Kademlia::CKademlia::IsConnected())
		iKadIcon = 4; // CONTACT4
	else if (!Kademlia::CKademlia::IsFirewalled())
		iKadIcon = 0; // CONTACT0 (Open)
	else if (theApp.clientlist->GetServingBuddyStatus() == Connected)
		iKadIcon = 1; // CONTACT1 (Firewalled + Buddy connected)
	else
		iKadIcon = 2; // CONTACT2 (Firewalled + No Buddy)
	statusbar->SetIcon(SBarKad, m_contactIcons[iKadIcon]);
}


void CemuleDlg::ShowConnectionState()
{
	if (theApp.IsClosing())
		return;
	theApp.downloadqueue->OnConnectionState(theApp.IsConnected());
	serverwnd->UpdateMyInfo();
	serverwnd->UpdateControlsState();
	kademliawnd->UpdateControlsState();

	ShowConnectionStateIcon();

	LPCTSTR ed2kKey;
	if (theApp.serverconnect->IsConnected())
		ed2kKey = _T("IDS_SBAR_ED2K_CONNECTED");
	else
		ed2kKey = theApp.serverconnect->IsConnecting() ? _T("IDS_SBAR_ED2K_CONNECTING") : _T("IDS_SBAR_ED2K_DISCONNECTED");

	LPCTSTR kadKey;
	if (Kademlia::CKademlia::IsConnected())
		kadKey = _T("IDS_SBAR_KAD_CONNECTED");
	else
		kadKey = Kademlia::CKademlia::IsRunning() ? _T("IDS_SBAR_KAD_CONNECTING") : _T("IDS_SBAR_KAD_DISCONNECTED");

	statusbar->SetText(GetResString(ed2kKey), SBarED2K, 0);
	statusbar->SetText(GetResString(kadKey), SBarKad, 0);

	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof(TBBUTTONINFO);
	tbbi.dwMask = TBIF_IMAGE | TBIF_TEXT;

	if (theApp.IsConnected()) {
		CString strPane(GetResString(_T("MAIN_BTN_DISCONNECT")));
		tbbi.iImage = 1;
		tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
		toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
		strPane.Remove(_T('&'));
		if (!theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_CONNECT, MF_STRING, MP_DISCONNECT, strPane))
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_DISCONNECT, MF_STRING, MP_DISCONNECT, strPane); //replace "Cancel" with "Disconnect"
	}
	else {
		if (theApp.serverconnect->IsConnecting() || Kademlia::CKademlia::IsRunning()) {
			CString strPane(GetResString(_T("MAIN_BTN_CANCEL")));
			tbbi.iImage = 2;
			tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
			toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
			strPane.Remove(_T('&'));
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_CONNECT, MF_STRING, MP_DISCONNECT, strPane);
		}
		else {
			CString strPane(GetResString(_T("MAIN_BTN_CONNECT")));
			tbbi.iImage = 0;
			tbbi.pszText = const_cast<LPTSTR>((LPCTSTR)strPane);
			toolbar->SetButtonInfo(TBBTN_CONNECT, &tbbi);
			strPane.Remove(_T('&'));
			theApp.emuledlg->m_SysMenuOptions.ModifyMenuW(MP_DISCONNECT, MF_STRING, MP_CONNECT, strPane);
		}
	}
	ShowUserCount();
#ifdef HAVE_WIN7_SDK_H
	UpdateThumbBarButtons();
#endif
}

void CemuleDlg::ShowUserCount()
{
	uint32 totaluser, totalfile;
	theApp.serverlist->GetUserFileStatus(totaluser, totalfile);
	CString buffer;
	if (theApp.serverconnect->IsConnected() && Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
		buffer.Format(_T("%s:%s(%s)|%s:%s(%s)"), (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)CastItoIShort(totaluser, false, 1), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaUsers(), false, 1), (LPCTSTR)GetResString(_T("FILES")), (LPCTSTR)CastItoIShort(totalfile, false, 1), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaFiles(), false, 1));
	else if (theApp.serverconnect->IsConnected())
		buffer.Format(_T("%s:%s|%s:%s"), (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)CastItoIShort(totaluser, false, 1), (LPCTSTR)GetResString(_T("FILES")), (LPCTSTR)CastItoIShort(totalfile, false, 1));
	else if (Kademlia::CKademlia::IsRunning() && Kademlia::CKademlia::IsConnected())
		buffer.Format(_T("%s:%s|%s:%s"), (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaUsers(), false, 1), (LPCTSTR)GetResString(_T("FILES")), (LPCTSTR)CastItoIShort(Kademlia::CKademlia::GetKademliaFiles(), false, 1));
	else
		buffer.Format(_T("%s:0|%s:0"), (LPCTSTR)GetResString(_T("UUSERS")), (LPCTSTR)GetResString(_T("FILES")));
	statusbar->SetText(buffer, SBarUsers, 0);
}

void CemuleDlg::ShowMessageState(UINT nIcon)
{
	m_iMsgIcon = nIcon;
	statusbar->SetIcon(SBarChatMsg, imicons[m_iMsgIcon]);
}

void CemuleDlg::ShowTransferStateIcon()
{
	int i = (m_uDownDatarate ? 1 : 0) | (m_uUpDatarate ? 2 : 0);
	statusbar->SetIcon(SBarUpDown, transicons[i]);
}

CString CemuleDlg::GetUpDatarateString(UINT uUpDatarate)
{
	if (uUpDatarate != UINT_MAX)
		m_uUpDatarate = uUpDatarate;
	else
		m_uUpDatarate = theApp.uploadqueue->HasActiveUploads() ? theApp.uploadqueue->GetDatarate() : 0;
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(_T("%.1f (%.1f)"), m_uUpDatarate / 1024.0, theStats.GetUpDatarateOverhead() / 1024.0);
	else
		szBuff.Format(_T("%.1f"), m_uUpDatarate / 1024.0);
	return szBuff;
}

CString CemuleDlg::GetDownDatarateString(UINT uDownDatarate)
{
	if (uDownDatarate != UINT_MAX)
		m_uDownDatarate = uDownDatarate;
	else
		m_uDownDatarate = (theStats.m_dwOverallStatus & STATE_DOWNLOADING) != 0 ? theApp.downloadqueue->GetDatarate() : 0;
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(_T("%.1f (%.1f)"), m_uDownDatarate / 1024.0, theStats.GetDownDatarateOverhead() / 1024.0);
	else
		szBuff.Format(_T("%.1f"), m_uDownDatarate / 1024.0);
	return szBuff;
}

CString CemuleDlg::GetTransferRateString()
{
	CString szBuff;
	if (thePrefs.ShowOverhead())
		szBuff.Format(GetResString(_T("UPDOWN"))
			, m_uUpDatarate / 1024.0, theStats.GetUpDatarateOverhead() / 1024.0
			, m_uDownDatarate / 1024.0, theStats.GetDownDatarateOverhead() / 1024.0);
	else
		szBuff.Format(GetResString(_T("UPDOWNSMALL")), m_uUpDatarate / 1024.0, m_uDownDatarate / 1024.0);
	return szBuff;
}

void CemuleDlg::ShowTransferRate(bool bForceAll)
{
	if (bForceAll)
		m_uLastSysTrayIconCookie = SYS_TRAY_ICON_COOKIE_FORCE_UPDATE;

	const bool bHasActiveDownloads = (theStats.m_dwOverallStatus & STATE_DOWNLOADING) != 0;
	const bool bHasActiveUploads = theApp.uploadqueue->HasActiveUploads();
	m_uDownDatarate = bHasActiveDownloads ? theApp.downloadqueue->GetDatarate() : 0;
	m_uUpDatarate = bHasActiveUploads ? theApp.uploadqueue->GetDatarate() : 0;

	const CString& strTransferRate = GetTransferRateString();
	if (TrayIconVisible() || bForceAll) {
		// set tray icon
		int iDownRatePercent = (int)ceil((m_uDownDatarate / 10.24) / thePrefs.GetMaxGraphDownloadRate());
		UpdateTrayIcon(min(iDownRatePercent, 100));

		CString buffer;
		buffer.Format(_T("eMule v%s (%s)\r\n%s")
			, (LPCTSTR)theApp.GetAppVersion().Mid(6)
			, (LPCTSTR)GetResString(theApp.IsConnected() ? _T("CONNECTED") : _T("DISCONNECTED"))
			, (LPCTSTR)strTransferRate);

		TraySetToolTip(buffer);
	}

	if (IsWindowVisible() || bForceAll) {
		statusbar->SetText(strTransferRate, SBarUpDown, 0);
		ShowTransferStateIcon();
	}
	if (IsWindowVisible() && thePrefs.ShowRatesOnTitle()) {
		CString szBuff;
		szBuff.Format(_T("(U:%.1f D:%.1f) %s"), m_uUpDatarate / 1024.0f, m_uDownDatarate / 1024.0f, (LPCTSTR)GetMainWindowTitleText());
		SetWindowText(szBuff);
	}
	if (m_pMiniMule && m_pMiniMule->m_hWnd && m_pMiniMule->IsWindowVisible() && !m_pMiniMule->GetAutoClose() && !m_pMiniMule->IsInInitDialog())
		m_pMiniMule->UpdateContent(m_uUpDatarate, m_uDownDatarate);
}

void CemuleDlg::ShowPing()
{
	if (IsWindowVisible() && thePrefs.IsDynUpEnabled()) {
		CurrentPingStruct lastPing = theApp.lastCommonRouteFinder->GetCurrentPing();
		CString& strState(lastPing.state);
		if (strState.IsEmpty()) {
			if (lastPing.lowest > 0 && !thePrefs.IsDynUpUseMillisecondPingTolerance())
				strState.Format(_T("%.1f | %ums | %u%%"), lastPing.currentLimit / 1024.0f, lastPing.latency, lastPing.latency * 100 / lastPing.lowest);
			else
				strState.Format(_T("%.1f | %ums"), lastPing.currentLimit / 1024.0f, lastPing.latency);
		}
		statusbar->SetText(strState, SBarUSS, 0);
	}
}

void CemuleDlg::OnOK()
{
}

void CemuleDlg::OnCancel()
{
	if (!thePrefs.GetStraightWindowStyles())
		MinimizeWindow();
}

void CemuleDlg::MinimizeWindow()
{
	StopTitleVersionAnimation();
	HideTitleVersionOverlayWindow();

	if (*thePrefs.GetMinTrayPTR()) {
		TrayShow();
		ShowWindow(SW_HIDE);
	}
	else
		ShowWindow(SW_MINIMIZE);

	ShowTransferRate();
	ShowPing();
}

void CemuleDlg::SetActiveDialog(CWnd* dlg)
{
	if (dlg == activewnd)
		return;
	if (activewnd)
		activewnd->ShowWindow(SW_HIDE);
	dlg->ShowWindow(SW_SHOW);
	dlg->SetFocus();
	activewnd = dlg;
	int iToolbarButtonID = MapWindowToToolbarButton(dlg);
	if (iToolbarButtonID != -1)
		toolbar->PressMuleButton(iToolbarButtonID);
	if (dlg == transferwnd) {
		if (thePrefs.ShowCatTabInfos())
			transferwnd->UpdateCatTabTitles();
	}
	else if (dlg == chatwnd)
		chatwnd->chatselector.ShowChat();
	else if (dlg == statisticswnd)
		statisticswnd->ShowStatistics();
}

void CemuleDlg::SetStatusBarPartsSize()
{
	RECT rect;
	statusbar->GetClientRect(&rect);
	int ussShift;
	if (thePrefs.IsDynUpEnabled())
		ussShift = thePrefs.IsDynUpUseMillisecondPingTolerance() ? 65 : 110;
	else
		ussShift = 0;
	int aiWidths[7] =
	{
		rect.right - 770 - ussShift,
		rect.right - 525 - ussShift,
		rect.right - 325 - ussShift,
		rect.right - 175 - ussShift,
		rect.right - 25 - ussShift,
		rect.right - 25,
		-1
	};
	statusbar->SetParts(_countof(aiWidths), aiWidths);
}

void CemuleDlg::OnSize(UINT nType, int cx, int cy)
{
	CTrayDialog::OnSize(nType, cx, cy);
	SetStatusBarPartsSize();

	// we might receive this message during shutdown -> bad
	if (transferwnd != NULL && !theApp.IsClosing())
		transferwnd->VerifyCatTabSize();

	toolbar->Refresh();
	UpdateTitleVersionOverlayWindow();
	if (nType == SIZE_MINIMIZED) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
	} else if (m_bNewVersionAvailable) {
		StartTitleVersionAnimation();
	}
}

void CemuleDlg::OnMoving(UINT fwSide, LPRECT pRect)
{
	CTrayDialog::OnMoving(fwSide, pRect);
	if (pRect == NULL)
		return;

	UpdateTitleVersionOverlayWindowForRect(*pRect);
}

void CemuleDlg::OnSizing(UINT fwSide, LPRECT pRect)
{
	CTrayDialog::OnSizing(fwSide, pRect);
	if (pRect == NULL)
		return;

	UpdateTitleVersionOverlayWindowForRect(*pRect);
}

void CemuleDlg::OnWindowPosChanged(WINDOWPOS* lpwndpos)
{
	CTrayDialog::OnWindowPosChanged(lpwndpos);
	if (lpwndpos == NULL)
		return;

	const UINT uFlags = lpwndpos->flags;
	const bool bHide = (uFlags & SWP_HIDEWINDOW) != 0;
	const bool bShow = (uFlags & SWP_SHOWWINDOW) != 0;
	const bool bMovedOrSized = ((uFlags & SWP_NOMOVE) == 0) || ((uFlags & SWP_NOSIZE) == 0);
	if (!bMovedOrSized && !bHide && !bShow)
		return;

	if (bHide) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
		return;
	}

	UpdateTitleVersionOverlayWindow();
	if ((bShow || bMovedOrSized) && m_bNewVersionAvailable)
		StartTitleVersionAnimation();
}

void CemuleDlg::ProcessED2KLink(LPCTSTR pszData)
{
	try {
		CString link(pszData);
		link.Replace(_T("%7c"), _T("|"));
		CED2KLink* pLink = CED2KLink::CreateLinkFromUrl(OptUtf8ToStr(URLDecode(link)));
		ASSERT(pLink);
		switch (pLink->GetKind()) {
		case CED2KLink::kFile:
		{
			CED2KFileLink* pFileLink = pLink->GetFileLink();
			ASSERT(pFileLink);
			theApp.downloadqueue->AddFileLinkToDownload(*pFileLink, searchwnd->GetSelectedCat());
		}
		break;
		case CED2KLink::kServerList:
		{
			CED2KServerListLink* pListLink = pLink->GetServerListLink();
			ASSERT(pListLink);
			const CString& strAddress(pListLink->GetAddress());
			if (!strAddress.IsEmpty())
				serverwnd->UpdateServerMetFromURL(strAddress);
		}
		break;
		case CED2KLink::kNodesList:
		{
			const CED2KNodesListLink* pListLink = pLink->GetNodesListLink();
			ASSERT(pListLink);
			const CString& strAddress(pListLink->GetAddress());
			// Because the nodes.dat is vital for kad and its routing and doesn't need to be
			// updated in general, we request a confirm to avoid accidental / malicious updating
			// of this file. This is a bit inconsistent as the same kinda applies to the server.met,
			// but those require more updates and are easier to understand
			if (!strAddress.IsEmpty()) {
				CString strConfirm;
				strConfirm.Format(GetResString(_T("CONFIRMNODESDOWNLOAD")), (LPCTSTR)strAddress);
				if (CDarkMode::MessageBox(strConfirm, MB_YESNO | MB_ICONQUESTION, 0) == IDYES)
					kademliawnd->UpdateNodesDatFromURL(strAddress);
			}
		}
		break;
		case CED2KLink::kServer:
		{
			CED2KServerLink* pSrvLink = pLink->GetServerLink();
			ASSERT(pSrvLink);
			CServer* pSrv = new CServer(pSrvLink->GetPort(), pSrvLink->GetAddress());
			ASSERT(pSrv);
			CString defName;
			pSrvLink->GetDefaultName(defName);
			pSrv->SetListName(defName);

			// Barry - Default all new servers to high priority
			if (thePrefs.GetManualAddedServersHighPriority())
				pSrv->SetPreference(SRV_PR_HIGH);

			if (!serverwnd->serverlistctrl.AddServer(pSrv, true))
				delete pSrv;
			else
				AddLogLine(true, GetResString(_T("SERVERADDED")), (LPCTSTR)EscPercent(pSrv->GetListName()));
		}
		break;
		case CED2KLink::kSearch:
		{
			CED2KSearchLink* pListLink = pLink->GetSearchLink();
			ASSERT(pListLink);
			SetActiveDialog(searchwnd);
			searchwnd->ProcessEd2kSearchLinkRequest(pListLink->GetSearchTerm());
		}
		case CED2KLink::kFriend:
		{
			// Better with dynamic_cast, but no RTTI enabled in the project
			CED2KFriendLink* pFriendLink = static_cast<CED2KFriendLink*>(pLink);
			uchar userHash[16];
			pFriendLink->GetUserHash(userHash);

			if (!theApp.friendlist->IsAlreadyFriend(userHash))
				theApp.friendlist->AddFriend(userHash, 0U, CAddress(), 0U, 0U, pFriendLink->GetUserName(), 1U);
			else
			{
				CString msg;
				msg.Format(GetResString(_T("USER_ALREADY_FRIEND")), pFriendLink->GetUserName());
				AddLogLine(true, (LPCTSTR)EscPercent(msg));
			}
		}
		break;
		case CED2KLink::kFriendList:
		{
			// Better with dynamic_cast, but no RTTI enabled in the project
			CED2KFriendListLink* pFrndLstLink = static_cast<CED2KFriendListLink*>(pLink);
			CString sAddress = pFrndLstLink->GetAddress();
			if (!sAddress.IsEmpty())
				this->chatwnd->UpdateEmfriendsMetFromURL(sAddress);
		}
		break;
		default:
			break;
		}
		delete pLink;
	}
	catch (const CString& strError) {
		LogWarning(LOG_STATUSBAR, _T("%s - %s"), (LPCTSTR)GetResString(_T("LINKNOTADDED")), (LPCTSTR)strError);
	}
	catch (CException *e) {
		e->Delete();
		LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
	}
	catch (...) {
		LogWarning(LOG_STATUSBAR, GetResString(_T("LINKNOTADDED")));
	}
}

LRESULT CemuleDlg::OnWMData(WPARAM, LPARAM lParam)
{
	PCOPYDATASTRUCT data = (PCOPYDATASTRUCT)lParam;
	ULONG_PTR op = data->dwData;
	if ((op == OP_ED2KLINK && thePrefs.IsBringToFront()) || op == OP_COLLECTION) {
		if (IsIconic())
			ShowWindow(SW_SHOWNORMAL);
		else
			RestoreWindow();
		FlashWindow(TRUE);
	}
	switch (op) {
	case OP_ED2KLINK:
		ProcessED2KLink((LPCTSTR)data->lpData);
		break;
	case OP_COLLECTION:
	{
		CCollection* pCollection = new CCollection();
		const CString& strPath((LPCTSTR)data->lpData);
		if (pCollection->InitCollectionFromFile(strPath, strPath.Right(strPath.GetLength() - 1 - strPath.ReverseFind(_T('\\'))))) {
			CCollectionViewDialog dialog;
			dialog.SetCollection(pCollection);
			dialog.DoModal();
		}
		delete pCollection;
	}
	break;
	case OP_CLCOMMAND:
	{
		// command line command received
		CString clcommand((LPCTSTR)data->lpData);
		clcommand.MakeLower();
		AddLogLine(true, _T("CLI: %s"), (LPCTSTR)EscPercent(clcommand));

		if (clcommand == _T("connect"))
			StartConnection();
		else if (clcommand == _T("disconnect"))
			theApp.serverconnect->Disconnect();
		else if (clcommand == _T("exit")) {
			theApp.m_app_state = APP_STATE_SHUTTINGDOWN; // do no ask to close
			OnClose();
		}
		else if (clcommand == _T("help") || clcommand == _T("/?"))
			; // show usage
		else if (clcommand.Left(7) == _T("limits=") && clcommand.GetLength() > 8) {
			clcommand.Delete(0, 7);
			int pos = clcommand.Find(_T(','));
			if (pos > 0) {
				if (clcommand[pos + 1])
					thePrefs.SetMaxDownload(_tstoi(CPTR(clcommand, pos + 1)));
				clcommand.Truncate(pos);
			}
			if (!clcommand.IsEmpty())
				thePrefs.SetMaxUpload(_tstoi(clcommand));
		}
		else if (clcommand == _T("reloadipf"))
			theApp.ipfilter->LoadFromDefaultFile();
		else if (clcommand == _T("restore"))
			RestoreWindow();
		else if (clcommand == _T("resume"))
			theApp.downloadqueue->StartNextFile();
		else if (clcommand == _T("status")) {
			FILE* file = _tfsopen(thePrefs.GetMuleDirectory(EMULE_CONFIGBASEDIR) + _T("status.log"), _T("wt"), _SH_DENYWR);
			if (file) {
				LPCTSTR uid;
				if (theApp.serverconnect->IsConnected())
					uid = _T("CONNECTED");
				else if (theApp.serverconnect->IsConnecting())
					uid = _T("CONNECTING");
				else
					uid = _T("DISCONNECTED");
				_ftprintf(file, _T("%s\n"), (LPCTSTR)GetResString(uid));
				_ftprintf(file, (LPCTSTR)GetResString(_T("UPDOWNSMALL")), theApp.uploadqueue->GetDatarate() / 1024.0f, theApp.downloadqueue->GetDatarate() / 1024.0f);
				// next string (getTextList) is already prefixed with '\n'!
				_ftprintf(file, _T("%s\n"), (LPCTSTR)transferwnd->GetDownloadList()->getTextList());

				fclose(file);
			}
		}
		//else show "unknown command"; Or "usage"
	}
	}
	return TRUE;
}

LRESULT CemuleDlg::OnFileHashed(WPARAM wParam, LPARAM lParam)
{
	CKnownFile* result = reinterpret_cast<CKnownFile*>(lParam);
	ASSERT(result->IsKindOf(RUNTIME_CLASS(CKnownFile)));

	if (theApp.IsClosing()) {
		delete result;
		return FALSE;
	}

	if (wParam) {
		// File hashing finished for a part file when:
		// - part file just completed
		// - part file was rehashed at startup because the file date of part.met did not match the part file date

		CPartFile* requester = reinterpret_cast<CPartFile*>(wParam);
		ASSERT(requester->IsKindOf(RUNTIME_CLASS(CPartFile)));

		// SLUGFILLER: SafeHash - could have been cancelled
		if (theApp.downloadqueue->IsPartFile(requester))
			requester->PartFileHashFinished(result);
		else
			delete result;
		// SLUGFILLER: SafeHash
	}
	else {
		ASSERT(!result->IsKindOf(RUNTIME_CLASS(CPartFile)));

		// File hashing finished for a shared file (not a partfile) when:
		//	- reading shared directories at startup and hashing files which were not found in known.met
		//	- reading shared directories during runtime (user hit Reload button, added a shared directory, ...)
		theApp.sharedfiles->FileHashingFinished(result);
	}
	return TRUE;
}

LRESULT CemuleDlg::OnFileOpProgress(WPARAM wParam, LPARAM lParam)
{
	if (!theApp.IsClosing()) {
		CKnownFile* pKnownFile = reinterpret_cast<CKnownFile*>(lParam);
		ASSERT(pKnownFile->IsKindOf(RUNTIME_CLASS(CKnownFile)));

		if (pKnownFile->IsKindOf(RUNTIME_CLASS(CPartFile))) {
			CPartFile* pPartFile = static_cast<CPartFile*>(pKnownFile);
			pPartFile->SetFileOpProgress(wParam);
			pPartFile->UpdateDisplayedInfo(true);
		}
	}
	return 0;
}

// SLUGFILLER: SafeHash
LRESULT CemuleDlg::OnHashFailed(WPARAM, LPARAM lParam)
{
	theApp.sharedfiles->HashFailed(reinterpret_cast<UnknownFile_Struct*>(lParam));
	return 0;
}
// SLUGFILLER: SafeHash

LRESULT CemuleDlg::OnFileAllocExc(WPARAM wParam, LPARAM lParam)
{
	if (lParam == 0)
		reinterpret_cast<CPartFile*>(wParam)->FlushBuffersExceptionHandler();
	else
		reinterpret_cast<CPartFile*>(wParam)->FlushBuffersExceptionHandler(reinterpret_cast<CFileException*>(lParam));
	return 0;
}

LRESULT CemuleDlg::OnFileCompleted(WPARAM wParam, LPARAM lParam)
{
	CPartFile* partfile = reinterpret_cast<CPartFile*>(lParam);
	ASSERT(partfile != NULL);
	if (partfile)
		partfile->PerformFileCompleteEnd((DWORD)wParam);
	return 0;
}

LRESULT CemuleDlg::OnImportPart(WPARAM wParam, LPARAM lParam)
{
	CPartFile* partfile = reinterpret_cast<CPartFile*>(lParam);
	ImportPart_Struct* imp = reinterpret_cast<ImportPart_Struct*>(wParam);
	if (theApp.downloadqueue->IsPartFile(partfile) && !theApp.IsClosing()) // could have been cancelled
		if (partfile->WriteToBuffer(imp->end - imp->start + 1, imp->data, imp->start, imp->end, NULL, NULL, false))
			imp->data = NULL; //do not delete the buffer

	delete[] imp->data;
	delete imp;
	return 0;
}

#ifdef _DEBUG
void BeBusy(UINT uSeconds, LPCSTR pszCaller)
{
	UINT s = 0;
	while (uSeconds--) {
		theVerboseLog.Logf(_T("%hs: called=%hs, waited %u sec."), __FUNCTION__, pszCaller, s++);
		::Sleep(SEC2MS(1));
	}
}
#endif

BOOL CemuleDlg::OnQueryEndSession()
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	if (!CTrayDialog::OnQueryEndSession())
		return FALSE;

	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning TRUE"), __FUNCTION__);
	return TRUE;
}

void CemuleDlg::OnEndSession(BOOL bEnding)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: bEnding=%d"), __FUNCTION__, bEnding);
	if (bEnding && !theApp.IsClosing()) {
		// If eMule was *not* started with "RUNAS":
		// When user is logging of (or reboots or shutdown system), Windows sends the
		// WM_QUERYENDSESSION/WM_ENDSESSION to all top level windows.
		// Here we can consume as much time as we need to perform our shutdown. Even if we
		// take longer than 20 seconds, Windows will just show a dialog box that 'emule'
		// is not terminating in time and gives the user a chance to cancel that. If the user
		// does not cancel the Windows dialog, Windows will though wait until eMule has
		// terminated by itself - no data loss, no file corruption, everything is fine.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
		OnClose();
	}

	CTrayDialog::OnEndSession(bEnding);
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning"), __FUNCTION__);
}

LRESULT CemuleDlg::OnUserChanged(WPARAM, LPARAM)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	// Just want to know if we ever get this message. Maybe it helps us to handle the
	// logoff/reboot/shutdown problem when eMule was started with "RUNAS".
	return Default();
}

LRESULT CemuleDlg::OnConsoleThreadEvent(WPARAM wParam, LPARAM lParam)
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs: nEvent=%u, nThreadID=%u"), __FUNCTION__, wParam, lParam);

	// If eMule was started with "RUNAS":
	// This message handler receives a 'console event' from the concurrently and thus
	// asynchronously running console control handler thread which was spawned by Windows
	// in case the user logs off/reboots/shutdown. Even if the console control handler thread
	// is waiting on the result from this message handler (is waiting until the main thread
	// has finished processing this inter-application message), the application will get
	// forcefully terminated by Windows after 20 seconds! There is no known way to prevent
	// that. This means, that if we would invoke our standard shutdown code ('OnClose') here
	// and the shutdown takes longer than 20 sec, we will get forcefully terminated by
	// Windows, regardless of what we are doing. This means, MET-file and PART-file corruption
	// may occur. Because the shutdown code in 'OnClose' does also shutdown Kad (which takes
	// a noticeable amount of time) it is not that unlikely that we run into problems with
	// not being finished with our shutdown in 20 seconds.
	//
	if (!theApp.IsClosing()) {
#if 1
		// And it really should be OK to expect that emule can shutdown in 20 sec on almost
		// all computers. So, use the proper shutdown.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;
		OnClose();	// do not invoke if shutdown takes longer than 20 sec, read above
#else
		// As a minimum action we at least set the 'shutting down' flag, this will help e.g.
		// interrupted by windows and which would then lead to corrupted MET-files.
		// Setting this flag also helps any possible running threads to stop their work.
		theApp.m_app_state = APP_STATE_SHUTTINGDOWN;

#ifdef _DEBUG
		// Simulate some work.
		//
		// NOTE: If the console thread has already exited, Windows may terminate the process
		// even before the 20 sec. timeout!
#endif

		// Actually, just calling 'ExitProcess' should be the most safe thing which we can
		// do here. Because we received this message via the main message queue we are
		// totally in-sync with the application and therefore we know that we are currently
		// not within a file save action and thus we simply can not cause any file corruption
		// when we exit right now.
		//
		// Of course, there may be some data loss. But it's the same amount of data loss which
		// could occur if we keep running. But if we keep running and wait until Windows
		// terminates us after 20 sec, there is also the chance for file corruption.
		if (thePrefs.GetDebug2Disk()) {
			theVerboseLog.Logf(_T("%hs: ExitProcess"), __FUNCTION__);
			theVerboseLog.Close();
		}
		ExitProcess(0);
#endif
	}

	AddDebugLogLine(DLP_VERYLOW, _T("%hs: returning"), __FUNCTION__);
	return 1;
}

void CemuleDlg::OnDestroy()
{
	AddDebugLogLine(DLP_VERYLOW, _T("%hs"), __FUNCTION__);
	StopTitleVersionAnimation();
	DestroyTitleVersionOverlayWindow();
	m_rcTitleVersionLink.SetRectEmpty();
	m_fontTitleVersionLink.DeleteObject();

	// If eMule was started with "RUNAS":
	// When user is logging of (or reboots or shutdown system), Windows may or may not send
	// a WM_DESTROY (depends on how long the application needed to process the
	// CTRL_LOGOFF_EVENT). But, regardless of what happened and regardless of how long any
	// application specific shutdown took, Windows fill forcefully terminate the process
	// after 1-2 seconds after WM_DESTROY! So, we can not use WM_DESTROY for any lengthy
	// shutdown actions in that case.
	CTrayDialog::OnDestroy();
}

bool CemuleDlg::CanClose()
{
	if (theApp.m_app_state == APP_STATE_RUNNING && thePrefs.IsConfirmExitEnabled()) {
		theApp.m_app_state = APP_STATE_ASKCLOSE; //disable tray menu
		RestoreWindow(); // make sure the window is in foreground for this prompt
		ExitBox request;
		request.DoModal();
		if (request.WasCancelled()) {
			if (theApp.m_app_state == APP_STATE_ASKCLOSE) //if the application state has not changed
				theApp.m_app_state = APP_STATE_RUNNING; //then keep running
			return false;
		}
	}
	return true;
}

void CemuleDlg::OnClose()
{
	static LONG closing = 0;
	if (::InterlockedExchange(&closing, 1))
		return; //already closing
	if (!CanClose()) {
		::InterlockedExchange(&closing, 0);
		return;
	}
	theApp.m_app_state = APP_STATE_SHUTTINGDOWN;

	notifierenabled = false;
	//flush queued messages
	theApp.HandleDebugLogQueue();
	theApp.HandleLogQueue();

	theApp.ConChecker->Stop();

	// Drain pending hash completion/failure messages to avoid leaking CKnownFile/UnknownFile_Struct on shutdown.
	MSG pendingMsg;
	while (::PeekMessage(&pendingMsg, m_hWnd, TM_FINISHEDHASHING, TM_HASHFAILED, PM_REMOVE)) {
		if (pendingMsg.message == TM_FINISHEDHASHING) {
			CKnownFile* result = reinterpret_cast<CKnownFile*>(pendingMsg.lParam);
			delete result;
		} else if (pendingMsg.message == TM_HASHFAILED) {
			UnknownFile_Struct* hashed = reinterpret_cast<UnknownFile_Struct*>(pendingMsg.lParam);
			delete hashed;
		}
	}

	Log(_T("Closing eMule"));
	CloseTTS();
	m_pDropTarget->Revoke();
	theApp.serverconnect->Disconnect();
	theApp.OnlineSig(); // Added By Bouc7

	// get main window placement
	WINDOWPLACEMENT wp;
	wp.length = (UINT)sizeof wp;
	if (GetWindowPlacement(&wp)) {
		ASSERT(wp.showCmd == SW_SHOWMAXIMIZED || wp.showCmd == SW_SHOWMINIMIZED || wp.showCmd == SW_SHOWNORMAL);
		if (wp.showCmd == SW_SHOWMINIMIZED && (wp.flags & WPF_RESTORETOMAXIMIZED))
			wp.showCmd = SW_SHOWMAXIMIZED;
		wp.flags = 0;
		thePrefs.SetWindowLayout(wp);
	}

	// get active main window dialog
	if (activewnd) {
		if (activewnd->IsKindOf(RUNTIME_CLASS(CServerWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_SERVER);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CSharedFilesWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_FILES);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CSearchDlg)))
			thePrefs.SetLastMainWndDlgID(IDD_SEARCH);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CChatWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_CHAT);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CTransferDlg)))
			thePrefs.SetLastMainWndDlgID(IDD_TRANSFER);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CStatisticsDlg)))
			thePrefs.SetLastMainWndDlgID(IDD_STATISTICS);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CKademliaWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_KADEMLIAWND);
		else if (activewnd->IsKindOf(RUNTIME_CLASS(CIrcWnd)))
			thePrefs.SetLastMainWndDlgID(IDD_IRC);
		else {
			ASSERT(0);
			thePrefs.SetLastMainWndDlgID(0);
		}
	}

	Kademlia::CKademlia::Stop();	// couple of data files are written

	// try to wait until the hashing thread notices that we are shutting down
	CSingleLock sLock1(&theApp.hashing_mut); // only one file hash at a time
	sLock1.Lock(SEC2MS(2));

	theApp.m_pUploadDiskIOThread->EndThread();
	theApp.m_pPartFileWriteThread->EndThread();

	// saving data & stuff
	theApp.emuledlg->preferenceswnd->m_wndSecurity.DeleteDDB();

	theApp.knownfiles->Save();
	theApp.sharedfiles->Save();
	searchwnd->SaveAllSettings();
	serverwnd->SaveAllSettings();
	kademliawnd->SaveAllSettings();

	theApp.scheduler->RestoreOriginals();
	theApp.searchlist->SaveSpamFilter();
	if (thePrefs.IsStoringSearchesEnabled())
		theApp.searchlist->StoreSearches();

	if (thePrefs.GetSaveLoadSources()) {
		for (POSITION pos = theApp.downloadqueue->filelist.GetHeadPosition(); pos != 0;) {
			CPartFile* file = theApp.downloadqueue->filelist.GetNext(pos);
			if (file && !file->IsStopped())
				file->m_sourcesaver.SaveSources(file, true);
		}
	}

	// close uPnP Ports
	theApp.m_pUPnPFinder->GetImplementation()->StopAsyncFind();
	if (thePrefs.CloseUPnPOnExit())
		theApp.m_pUPnPFinder->GetImplementation()->DeletePorts();

	thePrefs.Save();
	thePerfLog.Shutdown();

	if (thePrefs.GetBackupOnExit())
		theApp.Backup(true);

	// explicitly delete all listview items which may hold ptrs to objects which will get deleted
	// by the dtors (some lines below) to avoid potential problems during application shutdown.
	transferwnd->GetDownloadList()->DeleteAllItems();
	chatwnd->chatselector.DeleteAllItems();
	chatwnd->m_FriendListCtrl.DeleteAllItems();
	theApp.clientlist->DeleteAll();
	transferwnd->GetQueueList()->DeleteAllItems();
	transferwnd->GetUploadList()->DeleteAllItems();
	transferwnd->GetDownloadClientsList()->DeleteAllItems();
	serverwnd->serverlistctrl.DeleteAllItems();

	CPartFileConvert::CloseGUI();
	CPartFileConvert::RemoveAllJobs();

	theApp.uploadBandwidthThrottler->EndThread();
	theApp.lastCommonRouteFinder->EndThread();

	theApp.sharedfiles->DeletePartFileInstances();

	if (searchwnd != NULL)
		searchwnd->SendMessage(WM_CLOSE);
	if (transferwnd != NULL)
		transferwnd->SendMessage(WM_CLOSE);

	theApp.g_UtpSockets.clear();

	// NOTE: Do not move those dtors into 'CemuleApp::InitInstance' (although they should be there). The
	// dtors are indirectly calling functions which access several windows which would not be available
	// after we have closed the main window -> crash!
	delete theApp.listensocket;				theApp.listensocket = NULL;
	delete theApp.clientudp;				theApp.clientudp = NULL;
	delete theApp.sharedfiles;				theApp.sharedfiles = NULL;
	delete theApp.serverconnect;			theApp.serverconnect = NULL;
	delete theApp.serverlist;				theApp.serverlist = NULL;		// CServerList::SaveServermetToFile
	delete theApp.knownfiles;				theApp.knownfiles = NULL;
	delete theApp.searchlist;				theApp.searchlist = NULL;
	delete theApp.clientcredits;			theApp.clientcredits = NULL;	// CClientCreditsList::SaveList
	delete theApp.downloadqueue;			theApp.downloadqueue = NULL;	// N * (CPartFile::FlushBuffer + CPartFile::SavePartFile)
	delete theApp.uploadqueue;				theApp.uploadqueue = NULL;
	delete theApp.clientlist;				theApp.clientlist = NULL;
	delete theApp.friendlist;				theApp.friendlist = NULL;		// CFriendList::SaveList
	delete theApp.scheduler;				theApp.scheduler = NULL;
	delete theApp.ipfilter;					theApp.ipfilter = NULL;			// CIPFilter::SaveToDefaultFile
	delete theApp.searchlist;				theApp.searchlist = NULL;
	delete theApp.webserver;				theApp.webserver = NULL;
	delete theApp.m_pFirewallOpener;		theApp.m_pFirewallOpener = NULL;
	delete theApp.uploadBandwidthThrottler;	theApp.uploadBandwidthThrottler = NULL;
	delete theApp.lastCommonRouteFinder;	theApp.lastCommonRouteFinder = NULL;
	delete theApp.m_pUPnPFinder;			theApp.m_pUPnPFinder = NULL;
	delete theApp.m_pUploadDiskIOThread;	theApp.m_pUploadDiskIOThread = NULL;
	delete theApp.m_pPartFileWriteThread;	theApp.m_pPartFileWriteThread = NULL;
	delete theApp.ConChecker;				theApp.ConChecker = NULL;
	delete theApp.DownloadChecker;			theApp.DownloadChecker = NULL;
	delete theApp.geolite2;					theApp.geolite2 = NULL;
	delete theApp.shield;					theApp.shield = NULL;

	thePrefs.Uninit();
	theApp.m_app_state = APP_STATE_DONE;
	CTrayDialog::OnCancel();
	//flush queued messages
	theApp.HandleDebugLogQueue();
	theApp.HandleLogQueue();
	AddDebugLogLine(DLP_VERYLOW, _T("Closed eMule"));
}

void CemuleDlg::DestroyMiniMule()
{
	if (m_pMiniMule)
		if (m_pMiniMule->IsInInitDialog()) {
			TRACE("%s - *** Cannot destroy Minimule, it's still in 'OnInitDialog'\n", __FUNCTION__);
			m_pMiniMule->SetDestroyAfterInitDialog();
		} else if (!m_pMiniMule->IsInCallback()) { // for safety
			TRACE("%s - m_pMiniMule->DestroyWindow();\n", __FUNCTION__);
			m_pMiniMule->DestroyWindow();
			ASSERT(m_pMiniMule == NULL);
			m_pMiniMule = NULL;
		} else
			ASSERT(0);
}

LRESULT CemuleDlg::OnCloseMiniMule(WPARAM wParam, LPARAM)
{
	TRACE("%s -> DestroyMiniMule();\n", __FUNCTION__);
	DestroyMiniMule();
	if (wParam)
		RestoreWindow();
	return 0;
}

void CemuleDlg::OnTrayLButtonUp()
{
	if (theApp.IsClosing())
		return;

	// Avoid re-entrance problems with the main window, options dialog and minimule window
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	if (m_pMiniMule) {
		if (!m_pMiniMule->IsInInitDialog()) {
			TRACE("%s - m_pMiniMule->ShowWindow(SW_SHOW);\n", __FUNCTION__);
			m_pMiniMule->ShowWindow(SW_SHOW);
			m_pMiniMule->SetForegroundWindow();
			m_pMiniMule->BringWindowToTop();
		}
		return;
	}

	if (thePrefs.GetEnableMiniMule())
		try {
			TRACE("%s - m_pMiniMule = new CMiniMule(this);\n", __FUNCTION__);
			ASSERT(m_pMiniMule == NULL);
			m_pMiniMule = new CMiniMule(this);
			m_pMiniMule->Create(CMiniMule::IDD, this);
			if (m_pMiniMule->GetDestroyAfterInitDialog())
				DestroyMiniMule();
			else {
				m_pMiniMule->SetForegroundWindow();
				m_pMiniMule->BringWindowToTop();
			}
		} catch (...) {
			ASSERT(0);
			m_pMiniMule = NULL;
		}
	}

void CemuleDlg::OnTrayRButtonUp(CPoint pt)
{
	if (theApp.m_app_state != APP_STATE_RUNNING)
		return;

	// Avoid re-entrance problems with main window, options dialog and minimule window
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	if (m_pMiniMule) {
		if (m_pMiniMule->GetAutoClose()) {
			TRACE("%s - m_pMiniMule->GetAutoClose() -> DestroyMiniMule();\n", __FUNCTION__);
			DestroyMiniMule();
		} else if (m_pMiniMule->m_hWnd && !m_pMiniMule->IsWindowEnabled()) {
			// Avoid re-entrance problems with main window, options dialog and minimule window
			MessageBeep(MB_OK);
			return;
		}
	}

	if (m_pSystrayDlg) {
		m_pSystrayDlg->BringWindowToTop();
		return;
	}

	try {
		m_pSystrayDlg = new CMuleSystrayDlg(this, pt
			, thePrefs.GetMaxGraphUploadRate(true), thePrefs.GetMaxGraphDownloadRate()
			, thePrefs.GetMaxUpload(), thePrefs.GetMaxDownload());
	} catch (...) {
		return;
	}

	INT_PTR nResult = m_pSystrayDlg->DoModal();
	delete m_pSystrayDlg;
	m_pSystrayDlg = NULL;
	switch (nResult) {
	case IDC_TOMAX:
		QuickSpeedOther(MP_QS_UA);
		break;
	case IDC_TOMIN:
		QuickSpeedOther(MP_QS_PA);
		break;
	case IDC_RESTORE:
		RestoreWindow();
		break;
	case IDC_CONNECT:
		StartConnection(true);
		break;
	case IDC_DISCONNECT:
		CloseConnection();
		break;
	case IDC_EXIT:
		OnClose();
		break;
	case IDC_PREFERENCES:
		ShowPreferences();
	}
}

void CemuleDlg::AddSpeedSelectorMenus(CMenu* addToMenu)
{
	const CString& kbyps(GetResString(_T("KBYTESPERSEC")));
	// Create UploadPopup Menu
	ASSERT(m_menuUploadCtrl.m_hMenu == NULL);
	CString text;
	if (m_menuUploadCtrl.CreateMenu()) {
		int rate = thePrefs.GetMaxGraphUploadRate(true);
		text.Format(_T("20%%\t%i %s"), max(rate * 1 / 5, 1), (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U20, text);
		text.Format(_T("40%%\t%i %s"), max(rate * 2 / 5, 1), (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U40, text);
		text.Format(_T("60%%\t%i %s"), max(rate * 3 / 5, 1), (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U60, text);
		text.Format(_T("80%%\t%i %s"), max(rate * 4 / 5, 1), (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U80, text);
		text.Format(_T("100%%\t%i %s"), rate, (LPCTSTR)kbyps);
		m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_U100, text);
		m_menuUploadCtrl.AppendMenu(MF_SEPARATOR);

		if (GetRecMaxUpload() > 0) {
			text.Format(GetResString(_T("PW_MINREC")) + GetResString(_T("KBYTESPERSEC")), GetRecMaxUpload());
			m_menuUploadCtrl.AppendMenu(MF_STRING, MP_QS_UP10, text);
		}

		text = GetResString(_T("PW_UPL")) + _T(':');
		addToMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_menuUploadCtrl.m_hMenu, text);
	}

	// Create DownloadPopup Menu
	ASSERT(m_menuDownloadCtrl.m_hMenu == NULL);
	if (m_menuDownloadCtrl.CreateMenu()) {
		int rate = thePrefs.GetMaxGraphDownloadRate();
		text.Format(_T("20%%\t%i %s"), (int)(rate * 0.2), (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D20, text);
		text.Format(_T("40%%\t%i %s"), (int)(rate * 0.4), (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D40, text);
		text.Format(_T("60%%\t%i %s"), (int)(rate * 0.6), (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D60, text);
		text.Format(_T("80%%\t%i %s"), (int)(rate * 0.8), (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D80, text);
		text.Format(_T("100%%\t%i %s"), rate, (LPCTSTR)kbyps);
		m_menuDownloadCtrl.AppendMenu(MF_STRING | MF_POPUP, MP_QS_D100, text);

		text = GetResString(_T("PW_DOWNL")) + _T(':');
		addToMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_menuDownloadCtrl.m_hMenu, text);
	}

	addToMenu->AppendMenu(MF_SEPARATOR);
	addToMenu->AppendMenu(MF_STRING | MF_GRAYED, MP_CONNECT, GetResString(_T("MAIN_BTN_CONNECT")));
}

void CemuleDlg::StartConnection()
{
	StartConnection(false);
}

void CemuleDlg::StartConnection(bool bUserInitiated)
{
	if ((!theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsConnected()) || !Kademlia::CKademlia::IsRunning()) {
		// UPnP is still trying to open the ports. In order to not get a LowID by connecting to the servers / kad before
		// the ports are opened we delay the connection until UPnP gets a result or the timeout is reached
		// If the user clicks two times on the button, let him have his will and connect regardless
		m_bConnectRequestDelayedForUPnP = m_hUPnPTimeOutTimer != 0 && !m_bConnectRequestDelayedForUPnP;
		if (m_bConnectRequestDelayedForUPnP) {
			AddLogLine(false, GetResString(_T("DELAYEDBYUPNP")));
			AddLogLine(true, GetResString(_T("DELAYEDBYUPNP2")));
			return;
		}
		if (m_hUPnPTimeOutTimer != 0) {
			VERIFY(::KillTimer(NULL, m_hUPnPTimeOutTimer));
			m_hUPnPTimeOutTimer = 0;
		}

		const bool bWantEd2k = (thePrefs.GetNetworkED2K() || m_bEd2kSuspendDisconnect) && !theApp.serverconnect->IsConnecting()
			&& !theApp.serverconnect->IsConnected();
		const bool bWantKad = (thePrefs.GetNetworkKademlia() || m_bKadSuspendDisconnect) && !Kademlia::CKademlia::IsRunning();

		bool bWarnKad = false;
		bool bWarnEd2k = false;
		if (bUserInitiated) {
			bWarnKad = bWantKad && !HasNodesDatContacts();
			const bool bServerListEmpty = (theApp.serverlist == NULL) || (theApp.serverlist->GetServerCount() == 0);
			bWarnEd2k = bWantEd2k && bServerListEmpty;
			if (bWarnKad || bWarnEd2k) {
				CString strMsg;
				if (bWarnKad && bWarnEd2k) {
					const CString strKad = GetResString(_T("EMULE_AI_NODESDAT_REQUIRED_CONNECT"));
					const CString strEd2k = GetResString(_T("EMULE_AI_SERVERMET_REQUIRED_CONNECT"));
					strMsg.Format(_T("%s\n\n\n\n%s"), (LPCTSTR)strKad, (LPCTSTR)strEd2k);
				} else if (bWarnKad) {
					strMsg = GetResString(_T("EMULE_AI_NODESDAT_REQUIRED_CONNECT"));
				} else {
					strMsg = GetResString(_T("EMULE_AI_SERVERMET_REQUIRED_CONNECT"));
				}
				CDarkMode::MessageBox(strMsg, MB_OK | MB_ICONINFORMATION);
			}
		}

		const bool bStartEd2k = bWantEd2k && !bWarnEd2k;
		const bool bStartKad = bWantKad;
		if (bStartEd2k || bStartKad)
			AddLogLine(true, GetResString(_T("CONNECTING")));

		// ed2k
		if (bStartEd2k)
			theApp.serverconnect->ConnectToAnyServer();

		// kad
		if (bStartKad)
			Kademlia::CKademlia::Start();

		ShowConnectionState();
	}
	m_bEd2kSuspendDisconnect = false;
	m_bKadSuspendDisconnect = false;
}

void CemuleDlg::CloseConnection()
{
	theApp.serverconnect->StopConnectionTry();
	theApp.serverconnect->Disconnect();

	Kademlia::CKademlia::Stop();
	theApp.OnlineSig(); // Added By Bouc7
	ShowConnectionState();
}

void CemuleDlg::RestoreWindow()
{
	if (IsPreferencesDlgOpen()) {
		MessageBeep(MB_OK);
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return;
	}

	TrayHide();
	DestroyMiniMule();

	if (m_wpFirstRestore.length) {
		SetWindowPlacement(&m_wpFirstRestore);
		memset(&m_wpFirstRestore, 0, sizeof m_wpFirstRestore);
		SetForegroundWindow();
		BringWindowToTop();
	} else
		CTrayDialog::RestoreWindow();

	// Ensure the currently visible virtual list gets refreshed after the main window is shown again from the system tray.
	if (IsWindowVisible()) {
		if (sharedfileswnd && sharedfileswnd == activewnd && sharedfileswnd->sharedfilesctrl && sharedfileswnd->sharedfilesctrl.IsWindowVisible())
			sharedfileswnd->sharedfilesctrl.ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
		else if (searchwnd && searchwnd == activewnd && searchwnd->m_pwndResults && searchwnd->m_pwndResults->searchlistctrl && searchwnd->m_pwndResults->searchlistctrl.IsWindowVisible())
			searchwnd->m_pwndResults->searchlistctrl.ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
		else if (transferwnd && transferwnd == activewnd && transferwnd->GetDownloadList() && transferwnd->GetDownloadList()->IsWindowVisible())
			transferwnd->GetDownloadList()->ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
		else if (transferwnd && transferwnd == activewnd && transferwnd->GetClientList() && transferwnd->GetClientList()->IsWindowVisible())
			transferwnd->GetClientList()->ReloadList(false, static_cast<EListStateField>(LSF_SELECTION | LSF_SCROLL));
	}

	UpdateTitleVersionOverlayWindow();
	if (m_bNewVersionAvailable)
		StartTitleVersionAnimation();
}

void CemuleDlg::UpdateTrayIcon(int iPercent)
{
	// compute an id of the icon to be generated
	UINT uSysTrayIconCookie = (iPercent > 0) ? (16 - ((iPercent * 15 / 100) + 1)) : 0;
	if (theApp.IsConnected()) {
		if (!theApp.IsFirewalled())
			uSysTrayIconCookie += 50;
	} else
		uSysTrayIconCookie += 100;

	// don't update if the same icon as displayed would be generated
	if (m_uLastSysTrayIconCookie == uSysTrayIconCookie)
		return;
	m_uLastSysTrayIconCookie = uSysTrayIconCookie;

	// prepare it up
	if (m_iMsgIcon != 0 && thePrefs.DoFlashOnNewMessage()) {
		m_bMsgBlinkState = !m_bMsgBlinkState;

		if (m_bMsgBlinkState)
			m_TrayIcon.Init(imicons[1], 100, 1, 1, 16, 16, thePrefs.GetStatsColor(11));
	} else
		m_bMsgBlinkState = false;

	if (!m_bMsgBlinkState) {
		HICON trayicon;
		if (theApp.IsConnected())
			trayicon = theApp.IsFirewalled() ? m_icoSysTrayLowID : m_icoSysTrayConnected;
		else
			trayicon = m_icoSysTrayDisconnected;
		m_TrayIcon.Init(trayicon, 100, 1, 1, 16, 16, thePrefs.GetStatsColor(11));
	}

	// load our limit and color info
	static const int aiLimits[1] = { 100 }; // set the limits of where the bar color changes (low-high)
	COLORREF aColors[1] = { thePrefs.GetStatsColor(11) }; // set the corresponding color for each level
	m_TrayIcon.SetColorLevels(aiLimits, aColors, _countof(aiLimits));

	// generate the icon (do *not* destroy that icon using DestroyIcon(), that's done in 'TrayUpdate')
	int aiVals[1] = { iPercent };
	m_icoSysTrayCurrent = m_TrayIcon.Create(aiVals);
	ASSERT(m_icoSysTrayCurrent != NULL);
	if (m_icoSysTrayCurrent)
		TraySetIcon(m_icoSysTrayCurrent, true);
	TrayUpdate();
}

int CemuleDlg::OnCreate(LPCREATESTRUCT lpCreateStruct)
{
	return CTrayDialog::OnCreate(lpCreateStruct);
}

void CemuleDlg::OnShowWindow(BOOL bShow, UINT nStatus)
{
	if (!theApp.IsClosing()) {
		ShowTransferRate(true);

		if (bShow && activewnd == chatwnd)
			chatwnd->chatselector.ShowChat();
	}
	CTrayDialog::OnShowWindow(bShow, nStatus);
	if (!bShow) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
		return;
	}

	UpdateTitleVersionOverlayWindow();
	if (m_bNewVersionAvailable)
		StartTitleVersionAnimation();
}

void CemuleDlg::ShowNotifier(LPCTSTR pszText, TbnMsg nMsgType, LPCTSTR pszLink, bool bForceSoundOFF)
{
	if (!notifierenabled)
		return;

	LPCTSTR pszSoundEvent = NULL;
	int iSoundPrio = 0;
	bool bShowIt = false;
	switch (nMsgType) {
	case TBN_CHAT:
		if (thePrefs.GetNotifierOnChat()) {
			m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_Chat");
			iSoundPrio = 1;
		}
		break;
	case TBN_DOWNLOADFINISHED:
		if (thePrefs.GetNotifierOnDownloadFinished()) {
			m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_DownloadFinished");
			iSoundPrio = 1;
			SendNotificationMail(nMsgType, pszText);
		}
		break;
	case TBN_DOWNLOADADDED:
		if (thePrefs.GetNotifierOnNewDownload()) {
			m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_DownloadAdded");
			iSoundPrio = 1;
		}
		break;
	case TBN_LOG:
		if (thePrefs.GetNotifierOnLog()) {
			m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_LogEntryAdded");
		}
		break;
	case TBN_IMPORTANTEVENT:
		if (thePrefs.GetNotifierOnImportantError()) {
			m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
			bShowIt = true;
			pszSoundEvent = _T("eMule_Urgent");
			iSoundPrio = 1;
			SendNotificationMail(nMsgType, pszText);
		}
		break;
	case TBN_NEWVERSION:
		m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
		bShowIt = true;
		pszSoundEvent = _T("eMule_NewVersion");
		iSoundPrio = 1;
		break;
	case TBN_NULL:
		m_wndTaskbarNotifier.Show(pszText, nMsgType, pszLink);
		bShowIt = true;
	}

	if (bShowIt && !bForceSoundOFF && thePrefs.GetNotifierSoundType() != ntfstNoSound) {
		bool bNotifiedWithAudio = false;
		if (thePrefs.GetNotifierSoundType() == ntfstSpeech)
			bNotifiedWithAudio = Speak(pszText);

		if (!bNotifiedWithAudio) {
			if (!thePrefs.GetNotifierSoundFile().IsEmpty())
				PlaySound(thePrefs.GetNotifierSoundFile(), NULL, SND_FILENAME | SND_NOSTOP | SND_NOWAIT | SND_ASYNC);
			else if (pszSoundEvent) {
				// use 'SND_NOSTOP' only for low priority events, otherwise the 'Log message' event may overrule
				// a more important event which is fired nearly at the same time.
				PlaySound(pszSoundEvent, NULL, SND_APPLICATION | SND_ASYNC | SND_NODEFAULT | SND_NOWAIT | ((iSoundPrio > 0) ? 0 : SND_NOSTOP));
			}
		}
	}
}

void CemuleDlg::LoadNotifier(const CString& configuration)
{
	notifierenabled = m_wndTaskbarNotifier.LoadConfiguration(configuration);
}

LRESULT CemuleDlg::OnTaskbarNotifierClicked(WPARAM, LPARAM lParam)
{
	if (lParam) {
		ShellDefaultVerb((LPTSTR)lParam);
		free((void*)lParam);
	}

	switch (m_wndTaskbarNotifier.GetMessageType()) {
	case TBN_CHAT:
		RestoreWindow();
		SetActiveDialog(chatwnd);
		break;
	case TBN_DOWNLOADFINISHED:
		// if we had a link and opened the downloaded file, don't restore the app window
		if (lParam == 0) {
			RestoreWindow();
			SetActiveDialog(transferwnd);
		}
		break;
	case TBN_DOWNLOADADDED:
		RestoreWindow();
		SetActiveDialog(transferwnd);
		break;
	case TBN_IMPORTANTEVENT:
	case TBN_LOG:
		RestoreWindow();
		SetActiveDialog(serverwnd);
		break;
	case TBN_NEWVERSION:
		OpenVersionReleasesURL();
	}
	return 0;
}

void CemuleDlg::OnSettingChange(UINT uFlags, LPCTSTR lpszSection)
{
	// Safe trace: pass both arguments and guard a possible NULL section pointer.
	LPCTSTR pszSection = (lpszSection != NULL) ? lpszSection : _T("(null)");
	TRACE(_T("CemuleDlg::OnSettingChange: uFlags=0x%08X  lpszSection=\"%s\"\n"), uFlags, pszSection);

	// Do not update the Shell's large icon size, because we still have an image list
	// from the shell which contains the old large icon size.
	theApp.UpdateDesktopColorDepth();

	// Check if the change is related to dark mode
	if (lpszSection && _tcscmp(lpszSection, _T("ImmersiveColorSet")) == 0) {
		GetSystemDarkModeStatus();
		PostMessage(UM_APP_SWITCH_DARKMODE, 0, 0); // Switch color scheme for the entire application
	}

	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		m_fontTitleVersionLink.DeleteObject();
	UpdateTitleVersionOverlayWindow();

	CTrayDialog::OnSettingChange(uFlags, lpszSection);
}

void CemuleDlg::OnSysColorChange()
{
	theApp.UpdateDesktopColorDepth();
	CTrayDialog::OnSysColorChange();
	SetAllIcons();
	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		m_fontTitleVersionLink.DeleteObject();
	UpdateTitleVersionOverlayWindow();
}

HBRUSH CemuleDlg::OnCtlColor(CDC* pDC, CWnd* pWnd, UINT nCtlColor)
{
	HBRUSH hbr = GetCtlColor(pDC, pWnd, nCtlColor);
	return hbr ? hbr : __super::OnCtlColor(pDC, pWnd, nCtlColor);
}

HBRUSH CemuleDlg::GetCtlColor(CDC* /*pDC*/, CWnd* /*pWnd*/, UINT /*nCtlColor*/)
{
	// This function could be used to give the entire eMule (at least all of the main windows)
	// a somewhat more Vista like look by giving them all a bright background color.
	// However, again, the owner drawn tab controls are noticeably disturbing that attempt. They
	// do not change their background color accordingly. They don't use NMCUSTOMDRAW nor to they
	// use WM_CTLCOLOR...
	//
	return NULL;
}

void CemuleDlg::SetAllIcons()
{
	if (theApp.IsClosing())
		return;

	// application icon (although it's not customizable, we may need to load a different color resolution)
	if (m_hIcon)
		VERIFY(::DestroyIcon(m_hIcon));
	// NOTE: the application icon name is prefixed with "AAA" to make sure it's alphabetically sorted by the
	// resource compiler as the 1st icon in the resource table!
	m_hIcon = AfxGetApp()->LoadIcon(_T("AAAEMULEAPP"));
	SetIcon(m_hIcon, TRUE);
	// this scales the 32x32 icon down to 16x16, does not look nice at least under WinXP

	// connection state
	DestroyIconsArr(m_connicons, _countof(m_connicons));
	m_connicons[0] = theApp.LoadIcon(_T("ConnectedNotNot"), 16, 16);
	m_connicons[1] = theApp.LoadIcon(_T("ConnectedNotLow"), 16, 16);
	m_connicons[2] = theApp.LoadIcon(_T("ConnectedNotHigh"), 16, 16);
	m_connicons[3] = theApp.LoadIcon(_T("ConnectedLowNot"), 16, 16);
	m_connicons[4] = theApp.LoadIcon(_T("ConnectedLowLow"), 16, 16);
	m_connicons[5] = theApp.LoadIcon(_T("ConnectedLowHigh"), 16, 16);
	m_connicons[6] = theApp.LoadIcon(_T("ConnectedHighNot"), 16, 16);
	m_connicons[7] = theApp.LoadIcon(_T("ConnectedHighLow"), 16, 16);
	m_connicons[8] = theApp.LoadIcon(_T("ConnectedHighHigh"), 16, 16);
	
	DestroyIconsArr(m_contactIcons, _countof(m_contactIcons));
	m_contactIcons[0] = theApp.LoadIcon(_T("CONTACT0"), 16, 16);
	m_contactIcons[1] = theApp.LoadIcon(_T("CONTACT1"), 16, 16);
	m_contactIcons[2] = theApp.LoadIcon(_T("CONTACT2"), 16, 16);
	m_contactIcons[3] = theApp.LoadIcon(_T("CONTACT3"), 16, 16);
	m_contactIcons[4] = theApp.LoadIcon(_T("CONTACT4"), 16, 16);

	ShowConnectionStateIcon();

	// transfer state
	DestroyIconsArr(transicons, _countof(transicons));
	transicons[0] = theApp.LoadIcon(_T("UP0DOWN0"), 16, 16);
	transicons[1] = theApp.LoadIcon(_T("UP0DOWN1"), 16, 16);
	transicons[2] = theApp.LoadIcon(_T("UP1DOWN0"), 16, 16);
	transicons[3] = theApp.LoadIcon(_T("UP1DOWN1"), 16, 16);
	ShowTransferStateIcon();

	// users state
	if (usericon)
		VERIFY(::DestroyIcon(usericon));
	usericon = theApp.LoadIcon(_T("StatsClients"), 16, 16);
	ShowUserStateIcon();

	// system tray icons
	if (m_icoSysTrayConnected)
		VERIFY(::DestroyIcon(m_icoSysTrayConnected));
	if (m_icoSysTrayDisconnected)
		VERIFY(::DestroyIcon(m_icoSysTrayDisconnected));
	if (m_icoSysTrayLowID)
		VERIFY(::DestroyIcon(m_icoSysTrayLowID));
	m_icoSysTrayConnected = theApp.LoadIcon(_T("EMULEAIHEAD"), 16, 16);
	m_icoSysTrayDisconnected = theApp.LoadIcon(_T("EMULEAITRAYDISCONNECTED"), 16, 16);
	m_icoSysTrayLowID = theApp.LoadIcon(_T("EMULEAITRAYLOWID"), 16, 16);
	ShowTransferRate(true);

	DestroyIconsArr(imicons, _countof(imicons));

	imicons[0] = NULL;
	imicons[1] = theApp.LoadIcon(_T("Message"), 16, 16);
	imicons[2] = theApp.LoadIcon(_T("MessagePending"), 16, 16);
	ShowMessageState(m_iMsgIcon);
}

void CemuleDlg::Localize()
{
	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu) {
		VERIFY(pSysMenu->ModifyMenu(MP_ABOUTBOX, MF_BYCOMMAND | MF_STRING, MP_ABOUTBOX, GetResString(_T("ABOUTBOX"))));

			// localize the 'speed control' sub menus by deleting the current menus and creating a new ones.

			// remove any already available 'speed control' menus from system menu
			UINT uOptMenuPos = pSysMenu->GetMenuItemCount() - 1;
			CMenu* pAccelMenu = pSysMenu->GetSubMenu(uOptMenuPos);
			if (pAccelMenu) {
				ASSERT(pAccelMenu->m_hMenu == m_SysMenuOptions.m_hMenu);
				VERIFY(pSysMenu->RemoveMenu(uOptMenuPos, MF_BYPOSITION));
			}

			// destroy all 'speed control' menus
			if (m_menuUploadCtrl)
				VERIFY(m_menuUploadCtrl.DestroyMenu());
			if (m_menuDownloadCtrl)
				VERIFY(m_menuDownloadCtrl.DestroyMenu());
			if (m_SysMenuOptions)
				VERIFY(m_SysMenuOptions.DestroyMenu());

			// create new 'speed control' menus
			if (m_SysMenuOptions.CreateMenu()) {
				AddSpeedSelectorMenus(&m_SysMenuOptions);
				pSysMenu->AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)m_SysMenuOptions.m_hMenu, GetResString(_T("EM_PREFS")));
			}
		}

	ShowUserStateIcon();
	toolbar->Localize();
	ShowConnectionState();
	ShowTransferRate(true);
	ShowUserCount();
	CPartFileConvert::Localize();
	if (m_pMiniMule && !m_pMiniMule->IsInInitDialog())
		m_pMiniMule->Localize();
}

void CemuleDlg::ShowUserStateIcon()
{
	statusbar->SetIcon(SBarUsers, usericon);
}

void CemuleDlg::QuickSpeedOther(UINT nID)
{
	if (nID == MP_QS_PA) {
		thePrefs.SetMaxUpload(1);
		thePrefs.SetMaxDownload(1);
	} else if (nID == MP_QS_UA) {
		thePrefs.SetMaxUpload(thePrefs.GetMaxGraphUploadRate(true));
		thePrefs.SetMaxDownload(thePrefs.GetMaxGraphDownloadRate());
	}
}


void CemuleDlg::QuickSpeedUpload(UINT nID)
{
	switch (nID) {
	case MP_QS_U10:
		nID = 1;
		break;
	case MP_QS_U20:
		nID = 2;
		break;
	case MP_QS_U30:
		nID = 3;
		break;
	case MP_QS_U40:
		nID = 4;
		break;
	case MP_QS_U50:
		nID = 5;
		break;
	case MP_QS_U60:
		nID = 6;
		break;
	case MP_QS_U70:
		nID = 7;
		break;
	case MP_QS_U80:
		nID = 8;
		break;
	case MP_QS_U90:
		nID = 9;
		break;
	case MP_QS_U100:
		nID = 10;
		return;
	case MP_QS_UPC:
	default:
		return;
	case MP_QS_UP10:
		thePrefs.SetMaxUpload(GetRecMaxUpload());
		return;
	}
	thePrefs.SetMaxUpload((uint32)(thePrefs.GetMaxGraphUploadRate(true) * 0.1 * nID));
}

void CemuleDlg::QuickSpeedDownload(UINT nID)
{
	switch (nID) {
	case MP_QS_D10:
		nID = 1;
		break;
	case MP_QS_D20:
		nID = 2;
		break;
	case MP_QS_D30:
		nID = 3;
		break;
	case MP_QS_D40:
		nID = 4;
		break;
	case MP_QS_D50:
		nID = 5;
		break;
	case MP_QS_D60:
		nID = 6;
		break;
	case MP_QS_D70:
		nID = 7;
		break;
	case MP_QS_D80:
		nID = 8;
		break;
	case MP_QS_D90:
		nID = 9;
		break;
	case MP_QS_D100:
		nID = 10;
		return;
	case MP_QS_DC:
	default:
		return;
	}
	thePrefs.SetMaxDownload((UINT)(thePrefs.GetMaxGraphDownloadRate() * 0.1 * nID));
}

// quick-speed changer -- based on xrmb
int CemuleDlg::GetRecMaxUpload()
{
	int rate = thePrefs.GetMaxGraphUploadRate(true);
	if (rate < 7)
		return 0;
	if (rate < 15)
		return rate - 3;
	return rate - 4;
}

BOOL CemuleDlg::OnCommand(WPARAM wParam, LPARAM lParam)
{
	switch (wParam) {
	case TBBTN_CONNECT:
	case MP_HM_CON:
		OnBnClickedConnect();
		break;
	case TBBTN_KAD:
	case MP_HM_KAD:
		SetActiveDialog(kademliawnd);
		break;
	case TBBTN_SERVER:
	case MP_HM_SRVR:
		SetActiveDialog(serverwnd);
		break;
	case TBBTN_TRANSFERS:
	case MP_HM_TRANSFER:
		SetActiveDialog(transferwnd);
		switch (transferwnd->m_pwndTransfer->m_dwShowListIDC) {
		case IDC_CLIENTLIST:
			if (!thePrefs.IsKnownClientListDisabled())
				theApp.emuledlg->transferwnd->GetClientList()->ReloadList(false, LSF_SELECTION);
			break;
		case IDC_DOWNLOADLIST:
		case IDC_UPLOADLIST + IDC_DOWNLOADLIST:
			theApp.emuledlg->transferwnd->GetDownloadList()->ReloadList(false, LSF_SELECTION);
			break;
		}
		break;
	case TBBTN_SEARCH:
	case MP_HM_SEARCH:
		SetActiveDialog(searchwnd);
		theApp.emuledlg->searchwnd->m_pwndResults->searchlistctrl.ReloadList(false, LSF_SELECTION);
		break;
	case TBBTN_SHARED:
	case MP_HM_FILES:
		SetActiveDialog(sharedfileswnd);
		sharedfileswnd->sharedfilesctrl.ReloadList(false, LSF_SELECTION);
		break;
	case TBBTN_MESSAGES:
	case MP_HM_MSGS:
		SetActiveDialog(chatwnd);
		break;
	case TBBTN_IRC:
	case MP_HM_IRC:
		SetActiveDialog(ircwnd);
		break;
	case TBBTN_STATS:
	case MP_HM_STATS:
		SetActiveDialog(statisticswnd);
		break;
	case TBBTN_OPTIONS:
	case MP_HM_PREFS:
		toolbar->CheckButton(TBBTN_OPTIONS, TRUE);
		ShowPreferences();
		toolbar->CheckButton(TBBTN_OPTIONS, FALSE);
		break;
	case TBBTN_SAVESTATE:
	case MP_HM_SAVESTATE:
		theApp.uploadqueue->SaveAppState(false);
		break;
	case TBBTN_RELOADCONF:
	case MP_HM_RELOADCONF:
		thePrefs.LoadBlacklistFile(); // Loads blacklist.conf
		theApp.shield->LoadShieldFile(); // Loads shield.conf
		break;
	case TBBTN_BACKUP:
	case MP_HM_BACKUP:
		theApp.Backup(false);
		break;
	case TBBTN_TOOLS:
		ShowToolPopup(true);
		break;
	case TBBTN_EMULEAI:
		ShowEmuleAIPopup();
		break;
	case MP_HM_OPENINC:
		ShellOpenFile(thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR));
		break;
	case MP_HM_HELP:
		if (activewnd != NULL) {
			HELPINFO hi = {};
			hi.cbSize = (UINT)sizeof(HELPINFO);
			activewnd->SendMessage(WM_HELP, 0, (LPARAM)&hi);
		} else
			wParam = ID_HELP;
		break;
	case MP_HM_EXIT:
		OnClose();
		break;
	case MP_HM_LINK1: // MOD: don't remove!
		BrowserOpen(thePrefs.GetHomepageBaseURL(), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_HM_LINK2:
		BrowserOpen(thePrefs.GetHomepageBaseURL() + _T("/faq/"), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_ABOUT:
		if (m_pSplashWnd != NULL)
			DestroySplash();
		ShowSplash(false);
		break;
	case MP_EMULE_AI_SPECIAL_THANKS:
		if (m_pSplashWnd != NULL)
			DestroySplash();
		ShowSpecialThanks();
		break;
	case MP_EMULE_AI_CHECK_FOR_UPDATES:
		DoVersioncheck(true);
		break;
	case MP_EMULE_AI_GITHUB_REPO:
		BrowserOpen(MOD_REPO_BASE_URL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_REPORT_ISSUE:
		BrowserOpen(CString(MOD_REPO_BASE_URL) + _T("/issues"), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_DISCUSSIONS:
		BrowserOpen(CString(MOD_REPO_BASE_URL) + _T("/discussions"), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_HOMEPAGE_DOCUMENTATION:
		BrowserOpen(MOD_PAGES_BASE_URL, thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_EMULE_HELP:
		BrowserOpen(_T("https://www.emule-project.com/home/perl/help.cgi?l=1"), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_ASK_CHATGPT_HELP:
		BrowserOpen(BuildEmuleAiAssistantUrl(EMULE_AI_CHATGPT_HELP_BASE_URL, EMULE_AI_CHATGPT_PROMPT_PARAM, _T("EMULE_AI_PROMPT_FOR_HELP")), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_ASK_GEMINI_HELP:
		BrowserOpen(BuildEmuleAiAssistantUrl(EMULE_AI_GEMINI_HELP_BASE_URL, EMULE_AI_GEMINI_PROMPT_PARAM, _T("EMULE_AI_PROMPT_FOR_HELP")), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
		break;
	case MP_EMULE_AI_DOWNLOAD_GEOLITE_CITY:
	{
		CGeoLiteDownloadDlg dlg;
		dlg.DoModal();
	}
	break;
	case MP_WEBSVC_EDIT:
		theWebServices.Edit();
		break;
	case MP_HM_CONVERTPF:
		CPartFileConvert::ShowGUI();
		break;
	case MP_HM_SCHEDONOFF:
		thePrefs.SetSchedulerEnabled(!thePrefs.IsSchedulerEnabled());
		theApp.scheduler->Check(true);
		break;
	case MP_HM_1STSWIZARD:
		FirstTimeWizard();
		break;
	case MP_HM_MIGRATION_WIZARD:
	{
		thePrefs.SetMigrationWizardRunOnNextStart(true);
		CDarkMode::MessageBox(GetResString(_T("EMULE_AI_MIGRATION_WIZARD_SCHEDULED_RESTART")), MB_OK | MB_ICONINFORMATION);
	}
	break;
	case MP_HM_IPFILTER:
	{
		CIPFilterDlg dlg;
		dlg.DoModal();
	}
	break;
	case MP_HM_DIRECT_DOWNLOAD:
	{
		CDirectDownloadDlg dlg;
		dlg.DoModal();
	}
	}
	if (wParam >= MP_WEBURL && wParam <= MP_WEBURL + 99)
		theWebServices.RunURL(NULL, (UINT)wParam);
	else if (wParam >= MP_SCHACTIONS && wParam <= MP_SCHACTIONS + 99) {
		theApp.scheduler->ActivateSchedule(wParam - MP_SCHACTIONS);
		theApp.scheduler->SaveOriginals(); // use the new settings as original
#ifdef HAVE_WIN7_SDK_H
	}
	else if (HIWORD(wParam) == THBN_CLICKED) {
		OnTBBPressed(LOWORD(wParam));
		return TRUE;
#endif
	// Defining context menu of MuleToolBarCtrl as CMenuXP instead of CMenu causes an "Encounted and improper argument"
	// exception. Redirecting menu commands to theApp.emuledlg fixis this exception.
	} else
		toolbar->OnCommand(wParam, lParam);

	return CTrayDialog::OnCommand(wParam, lParam);
}

LRESULT CemuleDlg::OnMenuChar(UINT nChar, UINT nFlags, CMenu* pMenu)
{
	UINT nCmdID;
	if (toolbar->MapAccelerator((TCHAR)nChar, &nCmdID)) {
		OnCommand(nCmdID, 0);
		return MAKELONG(0, MNC_CLOSE);
	}
	return CTrayDialog::OnMenuChar(nChar, nFlags, pMenu);
}

void CemuleDlg::OnBnClickedHotmenu()
{
	ShowToolPopup(false);
}

void CemuleDlg::ShowToolPopup(bool toolsonly)
{
	POINT point = {};
	::GetCursorPos(&point);

	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(GetResString(toolsonly ? _T("TOOLS") : _T("HOTMENU")));

	CMenuXP Links;
	Links.CreateMenu();
	theWebServices.GetGeneralMenuEntries(&Links);
	if (Links.GetMenuItemCount() > 0)
		Links.AppendMenu(MF_SEPARATOR);
	Links.AppendMenu(MF_STRING, MP_WEBSVC_EDIT, GetResString(_T("WEBSVEDIT")));

	CMenuXP scheduler;
	scheduler.CreatePopupMenu();
	const CString& schedonoff(GetResString(thePrefs.IsSchedulerEnabled() ? _T("HM_SCHED_OFF") : _T("HM_SCHED_ON")));

	scheduler.AppendMenu(MF_STRING, MP_HM_SCHEDONOFF, schedonoff);
	if (theApp.scheduler->GetCount() > 0) {
		scheduler.AppendMenu(MF_SEPARATOR);
		for (INT_PTR i = 0; i < theApp.scheduler->GetCount(); ++i)
			scheduler.AppendMenu(MF_STRING, MP_SCHACTIONS + i, theApp.scheduler->GetSchedule(i)->title);
	}

	if (!toolsonly) {
		if (theApp.serverconnect->IsConnected())
			menu.AppendMenu(MF_STRING, MP_HM_CON, GetResString(_T("MAIN_BTN_DISCONNECT")), _T("DISCONNECT"));
		else if (theApp.serverconnect->IsConnecting())
			menu.AppendMenu(MF_STRING, MP_HM_CON, GetResString(_T("MAIN_BTN_CANCEL")), _T("STOPCONNECTING"));
		else
			menu.AppendMenu(MF_STRING, MP_HM_CON, GetResString(_T("MAIN_BTN_CONNECT")), _T("CONNECT"));

		menu.AppendMenu(MF_STRING, MP_HM_KAD, GetResString(_T("EM_KADEMLIA")), _T("KADEMLIA"));
		menu.AppendMenu(MF_STRING, MP_HM_SRVR, GetResString(_T("EM_SERVER")), _T("SERVER"));
		menu.AppendMenu(MF_STRING, MP_HM_TRANSFER, GetResString(_T("EM_TRANS")), _T("TRANSFER"));
		menu.AppendMenu(MF_STRING, MP_HM_SEARCH, GetResString(_T("EM_SEARCH")), _T("SEARCH"));
		menu.AppendMenu(MF_STRING, MP_HM_FILES, GetResString(_T("EM_FILES2")), _T("Files"));
		menu.AppendMenu(MF_STRING, MP_HM_MSGS, GetResString(_T("EM_MESSAGES")), _T("MESSAGES"));
		menu.AppendMenu(MF_STRING, MP_HM_IRC, GetResString(_T("IRC")), _T("IRC"));
		menu.AppendMenu(MF_STRING, MP_HM_STATS, GetResString(_T("EM_STATISTIC")), _T("STATISTICS"));
		menu.AppendMenu(MF_STRING, MP_HM_PREFS, GetResString(_T("EM_PREFS")), _T("PREFERENCES"));
		menu.AppendMenu(MF_STRING, MP_HM_SAVESTATE, GetResString(_T("SAVE_APP_STATE")), _T("SAVE"));
		menu.AppendMenu(MF_STRING, MP_HM_RELOADCONF, GetResString(_T("RELOAD_CONF")), _T("RELOAD CONF"));
		menu.AppendMenu(MF_STRING, MP_HM_BACKUP, GetResString(_T("BACKUP")), _T("BACKUP"));
		menu.AppendMenu(MF_SEPARATOR);
	}

	menu.AppendMenu(MF_STRING, MP_HM_OPENINC, GetResString(_T("OPENINC")) + _T("..."), _T("INCOMING"));
	menu.AppendMenu(MF_STRING, MP_HM_CONVERTPF, GetResString(_T("IMPORTSPLPF")) + _T("..."), _T("CONVERT"));
	menu.AppendMenu(MF_STRING, MP_HM_1STSWIZARD, GetResString(_T("WIZ1")) + _T("..."), _T("WIZARD"));
	menu.AppendMenu(MF_STRING, MP_HM_MIGRATION_WIZARD, GetResString(_T("EMULE_AI_MIGRATION_WIZARD")) + _T("..."), _T("WIZARD"));
	menu.AppendMenu(MF_STRING, MP_HM_IPFILTER, GetResString(_T("IPFILTER")) + _T("..."), _T("IPFILTER"));
	menu.AppendMenu(MF_STRING, MP_HM_DIRECT_DOWNLOAD, GetResString(_T("SW_DIRECTDOWNLOAD")) + _T("..."), _T("PASTELINK"));

	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)Links.m_hMenu, GetResString(_T("LINKS")), _T("WEB"));
	menu.AppendMenu(MF_STRING | MF_POPUP, (UINT_PTR)scheduler.m_hMenu, GetResString(_T("SCHEDULER")), _T("SCHEDULER"));

	if (!toolsonly) {
		menu.AppendMenu(MF_SEPARATOR);
		menu.AppendMenu(MF_STRING, MP_HM_EXIT, GetResString(_T("EXIT")), _T("EXIT"));
	}
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	VERIFY(Links.DestroyMenu());
	VERIFY(scheduler.DestroyMenu());
	VERIFY(menu.DestroyMenu());
}

void CemuleDlg::ShowEmuleAIPopup()
{
	POINT point = {};
	::GetCursorPos(&point);
	const LPCTSTR pszGithubIcon = IsDarkModeEnabled() ? _T("GITHUB_LIGHT") : _T("GITHUB");
	const LPCTSTR pszReportIssueIcon = _T("REPORTISSUE");
	const LPCTSTR pszChatGptIcon = IsDarkModeEnabled() ? _T("CHATGPT_LIGHT") : _T("CHATGPT");
	const LPCTSTR pszGeminiIcon = _T("GEMINI");

	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(GetResString(_T("EMULE_AI_LABEL")));

	// eMule AI main navigation
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_HOMEPAGE_DOCUMENTATION, GetResString(_T("EMULE_AI_MENU_HOMEPAGE_DOCUMENTATION")), _T("EMULEAI"));
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_EMULE_AI_GITHUB_REPO, GetResString(_T("EMULE_AI_MENU_GITHUB_REPO_STAR")), pszGithubIcon);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_REPORT_ISSUE, GetResString(_T("EMULE_AI_MENU_REPORT_ISSUE")), pszReportIssueIcon);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_DISCUSSIONS, GetResString(_T("EMULE_AI_MENU_DISCUSSIONS")), _T("Discussions"));
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_EMULE_AI_ASK_CHATGPT_HELP, GetResString(_T("EMULE_AI_MENU_ASK_CHATGPT_HELP")), pszChatGptIcon);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_ASK_GEMINI_HELP, GetResString(_T("EMULE_AI_MENU_ASK_GEMINI_HELP")), pszGeminiIcon);
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_EMULE_AI_DOWNLOAD_GEOLITE_CITY, GetResString(_T("EMULE_AI_MENU_DOWNLOAD_GEOLITE_CITY")), _T("GEOLITECITY"));
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_HM_LINK1, GetResString(_T("HM_LINKHP")), _T("MULE_VISTA_ICON"));
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_EMULE_HELP, GetResString(_T("EMULE_AI_MENU_EMULE_HELP")), _T("MULE_VISTA_ICON"));
	menu.AppendMenu(MF_STRING, MP_HM_LINK2, GetResString(_T("HM_LINKFAQ")), _T("MULE_VISTA_ICON"));
	menu.AppendMenu(MF_SEPARATOR);

	menu.AppendMenu(MF_STRING, MP_EMULE_AI_SPECIAL_THANKS, GetResString(_T("EMULE_AI_MENU_SPECIAL_THANKS")), _T("INFO"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_ABOUT, GetResString(_T("EMULE_AI_MENU_ABOUT")), _T("INFO"));
	menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, MP_EMULE_AI_CHECK_FOR_UPDATES, GetResString(_T("EMULE_AI_MENU_CHECK_FOR_UPDATES")), _T("EMULE_AI_CHECK_UPDATE"));
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON, point.x, point.y, this);
	VERIFY(menu.DestroyMenu());
}


void CemuleDlg::ApplyHyperTextFont(LPLOGFONT pFont)
{
	theApp.m_fontHyperText.DeleteObject();
	if (theApp.m_fontHyperText.CreateFontIndirect(pFont)) {
		thePrefs.SetHyperTextFont(pFont);
		serverwnd->servermsgbox->SetFont(&theApp.m_fontHyperText);
		chatwnd->chatselector.UpdateFonts(&theApp.m_fontHyperText);
		ircwnd->UpdateFonts(&theApp.m_fontHyperText);
	}
}

void CemuleDlg::ApplyLogFont(LPLOGFONT pFont)
{
	theApp.m_fontLog.DeleteObject();
	if (theApp.m_fontLog.CreateFontIndirect(pFont)) {
		thePrefs.SetLogFont(pFont);
		serverwnd->logbox->SetFont(&theApp.m_fontLog);
		serverwnd->debuglog->SetFont(&theApp.m_fontLog);
		serverwnd->protectionlog->SetFont(&theApp.m_fontLog);
	}
}

LRESULT CemuleDlg::OnFrameGrabFinished(WPARAM wParam, LPARAM lParam)
{
	CKnownFile* pOwner = reinterpret_cast<CKnownFile*>(wParam);
	FrameGrabResult_Struct* result = (FrameGrabResult_Struct*)lParam;

	if (theApp.knownfiles->IsKnownFile(pOwner) || theApp.downloadqueue->IsPartFile(pOwner))
		pOwner->GrabbingFinished(result->imgResults, result->nImagesGrabbed, result->pSender);
	else
		ASSERT(0);

	delete result;
	return 0;
}

void StraightWindowStyles(CWnd* pWnd)
{
	for (CWnd* pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		StraightWindowStyles(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (GetClassName(*pWnd, szClassName, _countof(szClassName))) {
		if (_tcsicmp(szClassName, _T("Button")) == 0)
			pWnd->ModifyStyle(BS_FLAT, 0);
		else if (_tcsicmp(szClassName, _T("EDIT")) == 0 && (pWnd->GetExStyle() & WS_EX_STATICEDGE)
			|| _tcsicmp(szClassName, _T("SysListView32")) == 0
			|| _tcsicmp(szClassName, _T("msctls_trackbar32")) == 0)
		{
			pWnd->ModifyStyleEx(WS_EX_STATICEDGE, WS_EX_CLIENTEDGE);
		}
	}
}

void ApplySystemFont(CWnd* pWnd)
{
	for (CWnd* pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		ApplySystemFont(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (GetClassName(*pWnd, szClassName, _countof(szClassName))
		&& (_tcsicmp(szClassName, _T("SysListView32")) == 0 || _tcsicmp(szClassName, _T("SysTreeView32")) == 0))
	{
		pWnd->SendMessage(WM_SETFONT, NULL, FALSE);
	}
}

// Adding WS_EX_STATICEDGE to controls causes flickering on some controls, especially on owner drawn controls.
// So we only apply the flat style to buttons and leave the rest as it is.
void FlatWindowStyles(CWnd* pWnd)
{
	for (CWnd* pWndChild = pWnd->GetWindow(GW_CHILD); pWndChild != NULL; pWndChild = pWndChild->GetNextWindow())
		FlatWindowStyles(pWndChild);

	TCHAR szClassName[MAX_PATH];
	if (GetClassName(*pWnd, szClassName, _countof(szClassName)) && _tcsicmp(szClassName, _T("Button")) == 0)
		pWnd->ModifyStyle(0, BS_FLAT);
}

void InitWindowStyles(CWnd* pWnd, bool bForTheApp)
{
	if (!thePrefs.GetStraightWindowStyles())
		FlatWindowStyles(pWnd);

	if (IsDarkModeEnabled()) {
		CDarkMode::Initialize();
		ApplyTheme(pWnd->GetSafeHwnd());
	}
}
LRESULT CemuleDlg::OnDarkModeSwitch(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	CWaitCursor curWait;

	// Close the preferences dialog if it is open
	if (preferenceswnd && ::IsWindow(preferenceswnd->GetSafeHwnd()) && preferenceswnd->m_bApplyButtonClicked)
		preferenceswnd->DestroyWindow(); // Close preferences dialog if apply button was clicked

	ApplyTheme(AfxGetMainWnd()->GetSafeHwnd()); // Switch color scheme for the entire application
	if (m_pMiniMule != NULL && !m_pMiniMule->IsInInitDialog() && ::IsWindow(m_pMiniMule->GetSafeHwnd()))
		ApplyThemeToWindow(m_pMiniMule->GetSafeHwnd(), true, false);

	if (preferenceswnd->m_bApplyButtonClicked)
		ShowPreferences(IDD_PPG_MOD); // Reopen the preferences dialog with IDD_PPG_MOD page selected if apply button was clicked

	return 0;
}

void CemuleDlg::OnRebarCustomDraw(NMHDR* pNMHDR, LRESULT* pResult)
{
	LPNMRBCUSTOMDRAW pDraw = reinterpret_cast<LPNMRBCUSTOMDRAW>(pNMHDR);
	if (!pDraw || !IsDarkModeEnabled()) {
		*pResult = CDRF_DODEFAULT;
		return;
	}

	switch (pDraw->nmcd.dwDrawStage) {
	case CDDS_PREPAINT:
		*pResult = CDRF_NOTIFYITEMDRAW;
		return;

	case CDDS_ITEMPREPAINT:
		::FillRect(pDraw->nmcd.hdc, &pDraw->rcBand, (HBRUSH)CDarkMode::m_brDefault);
		*pResult = CDRF_DODEFAULT; // Request post-paint to draw chevron
		return;
	}

	*pResult = CDRF_DODEFAULT;
}

UINT AFX_CDECL CemuleDlg::EmuleAIVersionCheckThread(LPVOID pParam)
{
	EmuleAIVersionCheckRequest* pRequest = reinterpret_cast<EmuleAIVersionCheckRequest*>(pParam);
	if (pRequest == NULL)
		return 0;

	EmuleAIVersionCheckResult* pResult = new EmuleAIVersionCheckResult;
	pResult->bRequestSucceeded = false;
	pResult->bHasNewVersion = false;
	pResult->bManual = pRequest->bManual;

	CString strDownloadedText;
	if (DownloadVersionTextFromUrl(pRequest->strVersionRawUrl, strDownloadedText)
		&& TryExtractVersionFromDownloadedText(strDownloadedText, pResult->strRemoteVersion))
	{
		pResult->bRequestSucceeded = true;

		const CString strCurrentVersion = NormalizeVersionString(pRequest->strCurrentVersion);
		if (!strCurrentVersion.IsEmpty()
			&& IsRemoteVersionNewer(pResult->strRemoteVersion, strCurrentVersion))
		{
			pResult->bHasNewVersion = true;
		}
	}

	if (::IsWindow(pRequest->hNotifyWnd))
		::PostMessage(pRequest->hNotifyWnd, UWM_EMULEAI_VERSIONCHECK_RESULT, 0, reinterpret_cast<LPARAM>(pResult));
	else
		delete pResult;

	delete pRequest;
	return 0;
}

LRESULT CemuleDlg::OnEmuleAIVersionCheckResult(WPARAM, LPARAM lParam)
{
	EmuleAIVersionCheckResult* pResult = reinterpret_cast<EmuleAIVersionCheckResult*>(lParam);
	m_bVersionCheckInProgress = false;
	if (pResult == NULL)
		return 0;

	if (false) {
		m_bNewVersionAvailable = false;
		StopTitleVersionAnimation();
		InvalidateTitleVersionFrame();
		delete pResult;
		return 0;
	}

	if (pResult->bRequestSucceeded) {
		thePrefs.UpdateLastVC();
		m_bNewVersionAvailable = pResult->bHasNewVersion;
		if (m_bNewVersionAvailable)
			thePrefs.SetLastKnownVersionOnServer(pResult->strRemoteVersion);
		else
			thePrefs.SetLastKnownVersionOnServer(_T(""));
		if (m_bNewVersionAvailable) {
			StartTitleVersionAnimation();
			TRACE(_T("eMuleAI new version found. Local=\"%s\" Remote=\"%s\"\n"), (LPCTSTR)NormalizeVersionString(MOD_VERSION), (LPCTSTR)pResult->strRemoteVersion);
			AddLogLine(true, GetResString(_T("EMULE_AI_VERSION_CHECK_NEW_VERSION")), (LPCTSTR)NormalizeVersionString(MOD_VERSION), (LPCTSTR)pResult->strRemoteVersion);
		} else {
			StopTitleVersionAnimation();
			AddLogLine(true, GetResString(_T("EMULE_AI_VERSION_CHECK_UP_TO_DATE")), (LPCTSTR)NormalizeVersionString(MOD_VERSION));
		}
		InvalidateTitleVersionFrame();
	} else {
		TRACE(_T("eMuleAI version check request failed.\n"));
		AddLogLine(true, GetResString(_T("EMULE_AI_VERSION_CHECK_FAILED")));
	}

	delete pResult;
	return 0;
}

void CemuleDlg::EnsureTitleVersionLinkFont()
{
	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		return;

	NONCLIENTMETRICS ncm = {};
	ncm.cbSize = sizeof(ncm);
	if (!::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0))
		return;

	ncm.lfCaptionFont.lfUnderline = TRUE;
	ncm.lfCaptionFont.lfWeight = FW_BOLD;
	m_fontTitleVersionLink.CreateFontIndirect(&ncm.lfCaptionFont);
}

CString CemuleDlg::GetTitleVersionLinkText() const
{
	return GetResString(TITLE_VERSION_LINK_TEXT_KEY);
}

bool CemuleDlg::TryBuildTitleVersionLinkRect(CDC& dc, const CRect& rcWindow, CRect& rcOut)
{
	rcOut.SetRectEmpty();
	if (!IsTitleVersionLinkVisible() || rcWindow.IsRectEmpty())
		return false;

	EnsureTitleVersionLinkFont();

	CFont* pOldFont = NULL;
	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		pOldFont = dc.SelectObject(&m_fontTitleVersionLink);

	const CString strText(GetTitleVersionLinkText());
	if (strText.IsEmpty()) {
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return false;
	}

	const CSize sizText = dc.GetTextExtent(strText);
	if (pOldFont != NULL)
		dc.SelectObject(pOldFont);
	if (sizText.cx <= 0 || sizText.cy <= 0)
		return false;

	CRect rcCurrentWindow;
	GetWindowRect(&rcCurrentWindow);
	CRect rcClient;
	GetClientRect(&rcClient);
	ClientToScreen(&rcClient);

	int nCaptionHeight = rcClient.top - rcCurrentWindow.top;
	if (nCaptionHeight <= 0)
		nCaptionHeight = ::GetSystemMetrics(SM_CYCAPTION);
	nCaptionHeight = max(1, nCaptionHeight);

	const int nTextTop = rcWindow.top + max(0, (nCaptionHeight - sizText.cy) / 2);

	int nPaddedBorder = 0;
#ifdef SM_CXPADDEDBORDER
	nPaddedBorder = ::GetSystemMetrics(SM_CXPADDEDBORDER);
#endif
	const int nFrameX = ::GetSystemMetrics(SM_CXFRAME) + nPaddedBorder;
	const int nCaptionButtonsWidth = ::GetSystemMetrics(SM_CXSIZE) * 3;

	int nLeftBound = rcWindow.left + nFrameX + ::GetSystemMetrics(SM_CXSMICON) + (TITLE_VERSION_TEXT_HORIZONTAL_PADDING * 2);
	int nRightBound = rcWindow.right - nFrameX - nCaptionButtonsWidth - TITLE_VERSION_TEXT_HORIZONTAL_PADDING;

	CRect rcCaptionButtons;
	if (GetCaptionButtonBounds(m_hWnd, rcCaptionButtons)) {
		const int nCaptionButtonsRightMargin = rcCurrentWindow.right - rcCaptionButtons.right;
		const int nCaptionButtonsLeft = rcWindow.right - nCaptionButtonsRightMargin - rcCaptionButtons.Width();
		nRightBound = min(nRightBound, nCaptionButtonsLeft - TITLE_VERSION_TEXT_HORIZONTAL_PADDING);
	}

	CString strWindowTitle;
	GetWindowText(strWindowTitle);
	strWindowTitle.Trim();
	if (!strWindowTitle.IsEmpty()) {
		NONCLIENTMETRICS ncm = {};
		ncm.cbSize = sizeof(ncm);
		int nTitleWidth = 0;
		if (::SystemParametersInfo(SPI_GETNONCLIENTMETRICS, ncm.cbSize, &ncm, 0)) {
			CFont fontCaption;
			if (fontCaption.CreateFontIndirect(&ncm.lfCaptionFont)) {
				CFont* pOldCaptionFont = dc.SelectObject(&fontCaption);
				nTitleWidth = dc.GetTextExtent(strWindowTitle).cx;
				if (pOldCaptionFont != NULL)
					dc.SelectObject(pOldCaptionFont);
			}
		}
		if (nTitleWidth <= 0)
			nTitleWidth = dc.GetTextExtent(strWindowTitle).cx;

		const int nTitleLeft = rcWindow.left + nFrameX + ::GetSystemMetrics(SM_CXSMICON) + TITLE_VERSION_TEXT_HORIZONTAL_PADDING;
		nLeftBound = max(nLeftBound, nTitleLeft + nTitleWidth + TITLE_VERSION_TEXT_HORIZONTAL_PADDING);
	}

	if (nRightBound - nLeftBound <= sizText.cx)
		return false;

	const int nIdealLeft = rcWindow.left + max(0, (rcWindow.Width() - sizText.cx) / 2);
	const int nIdealRight = nIdealLeft + sizText.cx;
	if (nIdealLeft < nLeftBound || nIdealRight > nRightBound)
		return false;

	rcOut.SetRect(nIdealLeft, nTextTop, nIdealRight, nTextTop + sizText.cy);
	return true;
}

void CemuleDlg::UpdateTitleVersionLinkRect(CDC& dc)
{
	m_rcTitleVersionLink.SetRectEmpty();
	CRect rcWindow;
	GetWindowRect(&rcWindow);
	TryBuildTitleVersionLinkRect(dc, rcWindow, m_rcTitleVersionLink);
}

void CemuleDlg::DrawTitleVersionLink(CDC& dc)
{
	if (!IsTitleVersionLinkVisible())
		return;

	EnsureTitleVersionLinkFont();
	UpdateTitleVersionLinkRect(dc);
	if (m_rcTitleVersionLink.IsRectEmpty())
		return;

	CRect rcWindow;
	GetWindowRect(&rcWindow);
	const int nTextLeft = m_rcTitleVersionLink.left - rcWindow.left;
	const int nTextTop = m_rcTitleVersionLink.top - rcWindow.top;

	CFont* pOldFont = NULL;
	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		pOldFont = dc.SelectObject(&m_fontTitleVersionLink);

	dc.SetBkMode(TRANSPARENT);
	const CString strText(GetTitleVersionLinkText());
	if (strText.IsEmpty()) {
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return;
	}
	const CSize sizText = dc.GetTextExtent(strText);
	const int nTextY = nTextTop + max(0, (m_rcTitleVersionLink.Height() - sizText.cy) / 2);

	int nTextX = nTextLeft;
	for (int i = 0; i < strText.GetLength(); ++i) {
		CString strChar(strText.Mid(i, 1));
		CSize sizChar = dc.GetTextExtent(strChar);
		dc.SetTextColor(RGB(0, 0, 0));
		dc.TextOut(nTextX + TITLE_VERSION_TEXT_SHADOW_OFFSET, nTextY + TITLE_VERSION_TEXT_SHADOW_OFFSET, strChar);
		dc.SetTextColor(GetRainbowColor((int)m_uTitleVersionAnimationHue + i * TITLE_VERSION_CHARACTER_HUE_STEP));
		dc.TextOut(nTextX, nTextY, strChar);
		nTextX += sizChar.cx;
	}

	if (pOldFont != NULL)
		dc.SelectObject(pOldFont);
}

COLORREF CemuleDlg::GetRainbowColor(int nHueDegrees) const
{
	nHueDegrees %= 360;
	if (nHueDegrees < 0)
		nHueDegrees += 360;

	const int nSection = nHueDegrees / 60;
	const int nRemainder = nHueDegrees % 60;
	const int nRise = (255 * nRemainder) / 60;
	const int nFall = (255 * (60 - nRemainder)) / 60;

	int nRed = 0;
	int nGreen = 0;
	int nBlue = 0;

	switch (nSection) {
	case 0:
		nRed = 255; nGreen = nRise; nBlue = 0;
		break;
	case 1:
		nRed = nFall; nGreen = 255; nBlue = 0;
		break;
	case 2:
		nRed = 0; nGreen = 255; nBlue = nRise;
		break;
	case 3:
		nRed = 0; nGreen = nFall; nBlue = 255;
		break;
	case 4:
		nRed = nRise; nGreen = 0; nBlue = 255;
		break;
	default:
		nRed = 255; nGreen = 0; nBlue = nFall;
		break;
	}

	nRed = TITLE_VERSION_TEXT_MIN_CHANNEL + ((255 - TITLE_VERSION_TEXT_MIN_CHANNEL) * nRed) / 255;
	nGreen = TITLE_VERSION_TEXT_MIN_CHANNEL + ((255 - TITLE_VERSION_TEXT_MIN_CHANNEL) * nGreen) / 255;
	nBlue = TITLE_VERSION_TEXT_MIN_CHANNEL + ((255 - TITLE_VERSION_TEXT_MIN_CHANNEL) * nBlue) / 255;

	return RGB(nRed, nGreen, nBlue);
}

bool CemuleDlg::IsTitleVersionLinkVisible() const
{
	return m_bNewVersionAvailable && IsWindowVisible() && !IsIconic();
}

void CemuleDlg::InvalidateTitleVersionFrame()
{
	if (!::IsWindow(m_hWnd))
		return;

	UpdateTitleVersionOverlayWindow();
	if (m_pTitleVersionOverlay != NULL && ::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()) && m_pTitleVersionOverlay->IsWindowVisible())
		m_pTitleVersionOverlay->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CemuleDlg::PaintTitleVersionLinkNow()
{
	if (!::IsWindow(m_hWnd))
		return;

	UpdateTitleVersionOverlayWindow();
	if (m_pTitleVersionOverlay != NULL && ::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()) && m_pTitleVersionOverlay->IsWindowVisible())
		m_pTitleVersionOverlay->RedrawWindow(NULL, NULL, RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
}

void CemuleDlg::DrawTitleVersionOverlay(CDC& dc, const CRect& rcClient)
{
	if (!IsTitleVersionLinkVisible() || rcClient.IsRectEmpty())
		return;

	EnsureTitleVersionLinkFont();

	CFont* pOldFont = NULL;
	if (m_fontTitleVersionLink.GetSafeHandle() != NULL)
		pOldFont = dc.SelectObject(&m_fontTitleVersionLink);

	const CString strText(GetTitleVersionLinkText());
	if (strText.IsEmpty()) {
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return;
	}

	const CSize sizText = dc.GetTextExtent(strText);
	if (sizText.cx <= 0 || sizText.cy <= 0) {
		if (pOldFont != NULL)
			dc.SelectObject(pOldFont);
		return;
	}

	const int nTextXStart = TITLE_VERSION_OVERLAY_PADDING_X;
	const int nTextYBase = TITLE_VERSION_OVERLAY_PADDING_Y + TITLE_VERSION_TEXT_WAVE_AMPLITUDE;

	dc.SetBkMode(TRANSPARENT);
	int nTextX = nTextXStart;
	for (int i = 0; i < strText.GetLength(); ++i) {
		const CString strChar(strText.Mid(i, 1));
		const CSize sizChar = dc.GetTextExtent(strChar);
		const double dWavePhase = ((double)m_uTitleVersionAnimationHue + (double)i * TITLE_VERSION_WAVE_PHASE_STEP) * TITLE_VERSION_DEG_TO_RAD;
		const double dWaveValue = ::sin(dWavePhase) * TITLE_VERSION_TEXT_WAVE_AMPLITUDE;
		const int nWaveOffset = (dWaveValue >= 0.0) ? (int)(dWaveValue + 0.5) : (int)(dWaveValue - 0.5);
		const int nCharY = nTextYBase + nWaveOffset;

		dc.SetTextColor(RGB(0, 0, 0));
		dc.TextOut(nTextX + TITLE_VERSION_TEXT_SHADOW_OFFSET, nCharY + TITLE_VERSION_TEXT_SHADOW_OFFSET, strChar);

		dc.SetTextColor(GetRainbowColor((int)m_uTitleVersionAnimationHue + i * TITLE_VERSION_CHARACTER_HUE_STEP));
		dc.TextOut(nTextX, nCharY, strChar);
		nTextX += sizChar.cx;
	}

	if (pOldFont != NULL)
		dc.SelectObject(pOldFont);
}

void CemuleDlg::EnsureTitleVersionOverlayWindow()
{
	if (m_pTitleVersionOverlay != NULL && ::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		return;

	DestroyTitleVersionOverlayWindow();

	m_pTitleVersionOverlay = new CTitleVersionOverlayWnd;
	if (m_pTitleVersionOverlay == NULL)
		return;

	if (!m_pTitleVersionOverlay->CreateOverlay(this)) {
		TRACE(_T("Failed to create title version overlay window.\n"));
		delete m_pTitleVersionOverlay;
		m_pTitleVersionOverlay = NULL;
		return;
	}
}

void CemuleDlg::DestroyTitleVersionOverlayWindow()
{
	if (m_pTitleVersionOverlay == NULL)
		return;

	if (::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		m_pTitleVersionOverlay->DestroyWindow();

	delete m_pTitleVersionOverlay;
	m_pTitleVersionOverlay = NULL;
}

void CemuleDlg::HideTitleVersionOverlayWindow()
{
	m_rcTitleVersionLink.SetRectEmpty();
	if (m_pTitleVersionOverlay != NULL && ::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		m_pTitleVersionOverlay->ShowWindow(SW_HIDE);
}

void CemuleDlg::ApplyTitleVersionOverlayRect()
{
	if (m_pTitleVersionOverlay == NULL || !::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		return;

	CRect rcOverlay(m_rcTitleVersionLink);
	rcOverlay.InflateRect(TITLE_VERSION_OVERLAY_PADDING_X, TITLE_VERSION_OVERLAY_PADDING_Y + TITLE_VERSION_TEXT_WAVE_AMPLITUDE);
	rcOverlay.right += TITLE_VERSION_TEXT_SHADOW_OFFSET;
	rcOverlay.bottom += TITLE_VERSION_TEXT_SHADOW_OFFSET;

	m_pTitleVersionOverlay->SetWindowPos(&CWnd::wndTop, rcOverlay.left, rcOverlay.top, max(1, rcOverlay.Width()), max(1, rcOverlay.Height()), SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
}

void CemuleDlg::UpdateTitleVersionOverlayWindowForRect(const CRect& rcWindow)
{
	if (!::IsWindow(m_hWnd)) {
		HideTitleVersionOverlayWindow();
		return;
	}

	if (!IsTitleVersionLinkVisible()) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
		return;
	}

	CWindowDC dc(this);
	if (!TryBuildTitleVersionLinkRect(dc, rcWindow, m_rcTitleVersionLink) || m_rcTitleVersionLink.IsRectEmpty()) {
		HideTitleVersionOverlayWindow();
		return;
	}

	EnsureTitleVersionOverlayWindow();
	if (m_pTitleVersionOverlay == NULL || !::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		return;

	ApplyTitleVersionOverlayRect();
	StartTitleVersionAnimation();
}

void CemuleDlg::UpdateTitleVersionOverlayWindow()
{
	if (!::IsWindow(m_hWnd)) {
		HideTitleVersionOverlayWindow();
		return;
	}

	if (!IsTitleVersionLinkVisible()) {
		StopTitleVersionAnimation();
		HideTitleVersionOverlayWindow();
		return;
	}

	CWindowDC dc(this);
	UpdateTitleVersionLinkRect(dc);
	if (m_rcTitleVersionLink.IsRectEmpty()) {
		HideTitleVersionOverlayWindow();
		return;
	}

	EnsureTitleVersionOverlayWindow();
	if (m_pTitleVersionOverlay == NULL || !::IsWindow(m_pTitleVersionOverlay->GetSafeHwnd()))
		return;

	ApplyTitleVersionOverlayRect();
	StartTitleVersionAnimation();
}

void CemuleDlg::StartTitleVersionAnimation()
{
	if (!IsTitleVersionLinkVisible() || !::IsWindow(m_hWnd) || m_bTitleVersionAnimationTimerActive)
		return;

	if (SetTimer(TIMER_TITLE_VERSION_ANIMATION, TITLE_VERSION_ANIMATION_INTERVAL_MS, NULL) == 0)
		TRACE(_T("Failed to start title version animation timer.\n"));
	else
		m_bTitleVersionAnimationTimerActive = true;
}

void CemuleDlg::StopTitleVersionAnimation()
{
	if (::IsWindow(m_hWnd) && m_bTitleVersionAnimationTimerActive)
		KillTimer(TIMER_TITLE_VERSION_ANIMATION);
	m_bTitleVersionAnimationTimerActive = false;
}

void CemuleDlg::OnNcPaint()
{
	CTrayDialog::OnNcPaint();
	UpdateTitleVersionOverlayWindow();
}

void CemuleDlg::OnNcLButtonDown(UINT nHitTest, CPoint point)
{
	CTrayDialog::OnNcLButtonDown(nHitTest, point);
}

BOOL CemuleDlg::OnSetCursor(CWnd* pWnd, UINT nHitTest, UINT message)
{
	return CTrayDialog::OnSetCursor(pWnd, nHitTest, message);
}

void CemuleDlg::OnTimer(UINT_PTR nIDEvent)
{
	if (nIDEvent == TIMER_TITLE_VERSION_ANIMATION) {
		if (!IsTitleVersionLinkVisible()) {
			StopTitleVersionAnimation();
			return;
		}

		m_uTitleVersionAnimationHue = (m_uTitleVersionAnimationHue + TITLE_VERSION_ANIMATION_HUE_STEP) % 360;
		PaintTitleVersionLinkNow();
		return;
	}

	if (nIDEvent == TIMER_SPECIAL_THANKS_ANIMATION) {
		if (m_pSplashWnd == NULL || !m_pSplashWnd->IsSpecialThanksMode() || !::IsWindow(m_pSplashWnd->GetSafeHwnd())) {
			if (m_bSpecialThanksAnimationTimerActive) {
				KillTimer(TIMER_SPECIAL_THANKS_ANIMATION);
				m_bSpecialThanksAnimationTimerActive = false;
			}
			return;
		}

		m_pSplashWnd->AdvanceAnimationFrame();
		return;
	}

	CTrayDialog::OnTimer(nIDEvent);
}

void CemuleDlg::ShowSpecialThanks()
{
	ShowSplash(false, CSplashScreen::DisplayModeSpecialThanks);
}

void CemuleDlg::ShowSplash(bool bAutoClose, CSplashScreen::DisplayMode eDisplayMode)
{
	ASSERT(m_pSplashWnd == NULL);
	if (m_pSplashWnd == NULL) {
		if (!bAutoClose && eDisplayMode == CSplashScreen::DisplayModeSplash)
			eDisplayMode = CSplashScreen::DisplayModeAbout;

		try {
			m_pSplashWnd = new CSplashScreen;
			m_pSplashWnd->m_bAutoClose = bAutoClose;
			m_pSplashWnd->SetDisplayMode(eDisplayMode);
		} catch (...) {
			return;
		}
		ASSERT(m_hWnd);
		if (m_pSplashWnd->Create(CSplashScreen::IDD, this)) {
			m_pSplashWnd->ShowWindow(SW_SHOW);
			m_pSplashWnd->UpdateWindow();
			if (eDisplayMode == CSplashScreen::DisplayModeSpecialThanks) {
				if (SetTimer(TIMER_SPECIAL_THANKS_ANIMATION, SPECIAL_THANKS_ANIMATION_INTERVAL_MS, NULL) != 0)
					m_bSpecialThanksAnimationTimerActive = true;
				else
					TRACE(_T("Failed to start special thanks animation timer.\n"));
			}
			if (bAutoClose) {
				m_dwSplashTime = ::GetTickCount();
			} else {
				m_dwSplashTime = _UI32_MAX;
			}
		} else {
			delete m_pSplashWnd;
			m_pSplashWnd = NULL;
		}
	}
}

void CemuleDlg::DestroySplash()
{
	if (::IsWindow(m_hWnd) && m_bSpecialThanksAnimationTimerActive) {
		KillTimer(TIMER_SPECIAL_THANKS_ANIMATION);
		m_bSpecialThanksAnimationTimerActive = false;
	}

	if (m_pSplashWnd != NULL) {
		CSplashScreen* pSplashWnd = m_pSplashWnd;
		m_pSplashWnd = NULL;
		if (pSplashWnd->GetSafeHwnd() != NULL && ::IsWindow(pSplashWnd->GetSafeHwnd()))
			pSplashWnd->DestroyWindow();
		else
			delete pSplashWnd;
	}
#ifdef _BETA
	// only do it once to not be annoying given that the beta phases are expected to last longer these days
	if (!thePrefs.IsFirstStart() && thePrefs.ShouldBetaNag()) {
		thePrefs.SetDidBetaNagging();
		LocMessageBox(_T("BETANAG"), MB_ICONINFORMATION | MB_OK, 0);
	}
#endif
}

BOOL CemuleApp::IsIdleMessage(MSG* pMsg)
{
	// This function is closely related to 'CemuleDlg::OnKickIdle'.
	//
	// * See MFC source code for 'CWnd::RunModalLoop' to see how those functions are related
	//	 to each other.
	//
	// * See MFC documentation for 'CWnd::IsIdleMessage' to see why WM_TIMER messages are
	//	 filtered here.
	//
	// Generally we want to filter WM_TIMER messages because they are triggering idle
	// processing (e.g. cleaning up temp. MFC maps) and because they are occurring very often
	// in eMule (we have a rather high frequency timer in upload queue). To save CPU load but
	// do not miss the chance to cleanup MFC temp. maps and other stuff, we do not use each
	// occurring WM_TIMER message -- that would just be overkill! However, we can not simply
	// filter all WM_TIMER messages. If eMule is running in taskbar the only messages which
	// are received by main window are those WM_TIMER messages, thus those messages are the
	// only chance to trigger some idle processing. So, we must use at last some of those
	// messages because otherwise we would not do any idle processing at all in some cases.

	static DWORD s_dwLastIdleMessage;
	if (pMsg->message == WM_TIMER) {
		// Allow this WM_TIMER message to trigger idle processing only if we did not do so
		// since some seconds.
		const DWORD curTick = ::GetTickCount();
		if (curTick >= s_dwLastIdleMessage + SEC2MS(5)) {
			s_dwLastIdleMessage = curTick;
			return TRUE;// Request idle processing (will send a WM_KICKIDLE)
		}
		return FALSE;	// No idle processing
	}

	if (!CWinApp::IsIdleMessage(pMsg))
		return FALSE;	// No idle processing

	s_dwLastIdleMessage = ::GetTickCount();
	return TRUE;		// Request idle processing (will send a WM_KICKIDLE)
}

LRESULT CemuleDlg::OnKickIdle(WPARAM, LPARAM lIdleCount)
{
	LRESULT lResult = 0;

	if (m_pSplashWnd) {
		if (m_pSplashWnd->m_bAutoClose) {
			if (::GetTickCount() >= m_dwSplashTime + (DWORD)SEC2MS(2.5)) {
				// timeout expired, destroy the splash window
				DestroySplash();
				UpdateWindow();
			} else {
				// check again later...
				lResult = 1;
			}
		}
	}

	if (m_bStartMinimized)
		PostStartupMinimized();

	if (searchwnd && searchwnd->m_hWnd) {
		if (!theApp.IsClosing()) {
			//			TCHAR szDbg[80];
						// NOTE: See also 'CemuleApp::IsIdleMessage'. If 'CemuleApp::IsIdleMessage'
						// would not filter most of the WM_TIMER messages we might get a performance
						// problem here because the idle processing would be performed very, very often.
						//
						// The default MFC implementation of 'CWinApp::OnIdle' is sufficient for us. We
						// will get called with 'lIdleCount=0' and with 'lIdleCount=1'.
						//
						// CWinApp::OnIdle(0)	takes care about pending MFC GUI stuff and returns 'TRUE'
						//						to request another invocation to perform more idle processing
						// CWinApp::OnIdle(>=1)	frees temporary internally MFC maps and returns 'FALSE'
						//						because no more idle processing is needed.
			lResult = theApp.OnIdle((LONG)lIdleCount);
		}
	}

	return lResult;
}

int CemuleDlg::MapWindowToToolbarButton(CWnd* pWnd) const
{
	if (pWnd == transferwnd)
		return TBBTN_TRANSFERS;
	if (pWnd == serverwnd)
		return TBBTN_SERVER;
	if (pWnd == sharedfileswnd)
		return TBBTN_SHARED;
	if (pWnd == searchwnd)
		return TBBTN_SEARCH;
	if (pWnd == statisticswnd)
		return TBBTN_STATS;
	if (pWnd == kademliawnd)
		return TBBTN_KAD;
	if (pWnd == ircwnd)
		return TBBTN_IRC;
	if (pWnd == chatwnd)
		return TBBTN_MESSAGES;
	ASSERT(0);
	return -1;
}

CWnd* CemuleDlg::MapToolbarButtonToWindow(int iButtonID) const
{
	switch (iButtonID) {
	case TBBTN_TRANSFERS:
		return transferwnd;
	case TBBTN_SERVER:
		return serverwnd;
	case TBBTN_SHARED:
		return sharedfileswnd;
	case TBBTN_SEARCH:
		return searchwnd;
	case TBBTN_STATS:
		return statisticswnd;
	case TBBTN_KAD:
		return kademliawnd;
	case TBBTN_IRC:
		return ircwnd;
	case TBBTN_MESSAGES:
		return chatwnd;
	}
	ASSERT(0);
	return NULL;
}

bool CemuleDlg::IsWindowToolbarButton(int iButtonID) const
{
	switch (iButtonID) {
	case TBBTN_TRANSFERS:
	case TBBTN_SERVER:
	case TBBTN_SHARED:
	case TBBTN_SEARCH:
	case TBBTN_STATS:
	case TBBTN_KAD:
	case TBBTN_IRC:
	case TBBTN_MESSAGES:
		return true;
	}
	return false;
}

int CemuleDlg::GetNextWindowToolbarButton(int iButtonID, int iDirection) const
{
	ASSERT(iDirection == 1 || iDirection == -1);
	int iButtonCount = toolbar->GetButtonCount();
	if (iButtonCount > 0) {
		int iButtonIdx = toolbar->CommandToIndex(iButtonID);
		if (iButtonIdx >= 0 && iButtonIdx < iButtonCount) {
			int iEvaluatedButtons = 0;
			while (iEvaluatedButtons < iButtonCount) {
				iButtonIdx = iButtonIdx + iDirection;
				if (iButtonIdx < 0)
					iButtonIdx = iButtonCount - 1;
				else if (iButtonIdx >= iButtonCount)
					iButtonIdx = 0;

				TBBUTTON tbbt = {};
				if (toolbar->GetButton(iButtonIdx, &tbbt)) {
					if (IsWindowToolbarButton(tbbt.idCommand))
						return tbbt.idCommand;
				}
				++iEvaluatedButtons;
			}
		}
	}
	return -1;
}

BOOL CemuleDlg::PreTranslateMessage(MSG* pMsg)
{
	BOOL bResult = CTrayDialog::PreTranslateMessage(pMsg);

	if (m_pSplashWnd && m_pSplashWnd->m_hWnd != NULL)
		switch (pMsg->message) {
		case WM_SYSCOMMAND:
			if (pMsg->wParam != SC_CLOSE)
				break;
		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_LBUTTONDOWN:
		case WM_RBUTTONDOWN:
		case WM_MBUTTONDOWN:
		case WM_NCLBUTTONDOWN:
		case WM_NCRBUTTONDOWN:
		case WM_NCMBUTTONDOWN:
			DestroySplash();
			UpdateWindow();
			return bResult;
		}

	// Handle Ctrl+Tab and Ctrl+Shift+Tab
	if (pMsg->message == WM_KEYDOWN)
		if (pMsg->wParam == VK_TAB && GetKeyState(VK_CONTROL) < 0) {
			int iButtonID = MapWindowToToolbarButton(activewnd);
			if (iButtonID != -1) {
				int iNextButtonID = GetNextWindowToolbarButton(iButtonID, GetKeyState(VK_SHIFT) < 0 ? -1 : 1);
				if (iNextButtonID != -1) {
					CWnd* pWndNext = MapToolbarButtonToWindow(iNextButtonID);
					if (pWndNext)
						SetActiveDialog(pWndNext);
				}
			}
		}

	return bResult;
}

void CemuleDlg::HtmlHelp(DWORD_PTR dwData, UINT nCmd)
{
	CWinApp* pApp = AfxGetApp();
	ASSERT_VALID(pApp);
	ASSERT(pApp->m_pszHelpFilePath != NULL);
	// to use HtmlHelp the method EnableHtmlHelp() must be called in
	// application's constructor
	ASSERT(pApp->m_eHelpType == afxHTMLHelp);

	CWaitCursor wait;

	PrepareForHelp();

	// need to use top level parent (for the case where m_hWnd is in DLL)
	CWnd* pWnd = GetTopLevelParent();

	TRACE(traceAppMsg, 0, _T("HtmlHelp: pszHelpFile = '%s', dwData: $%lx, fuCommand: %d.\n"), pApp->m_pszHelpFilePath, dwData, nCmd);

	bool bHelpError = false;
	CString strHelpError;
	for (int iTry = 0; iTry < 2; ++iTry) {
		if (AfxHtmlHelp(pWnd->m_hWnd, pApp->m_pszHelpFilePath, nCmd, dwData))
			return;
		bHelpError = true;
		strHelpError.LoadString(AFX_IDP_FAILED_TO_LAUNCH_HELP);

		typedef struct tagHH_LAST_ERROR
		{
			int		cbStruct;
			HRESULT	hr;
			BSTR	description;
		} HH_LAST_ERROR;
		HH_LAST_ERROR hhLastError = {};
		hhLastError.cbStruct = (int)sizeof hhLastError;
		if (!AfxHtmlHelp(pWnd->m_hWnd, NULL, HH_GET_LAST_ERROR, reinterpret_cast<DWORD_PTR>(&hhLastError))) {
			if (FAILED(hhLastError.hr)) {
				if (hhLastError.description) {
					strHelpError = hhLastError.description;
					SysFreeString(hhLastError.description);
				}
				if ((ULONG)hhLastError.hr == 0x8004020Aul  /*no topics IDs available in Help file*/
					|| (ULONG)hhLastError.hr == 0x8004020Bul) /*requested Help topic ID not found*/
				{
					// try opening once again without help topic ID
					if (nCmd != HH_DISPLAY_TOC) {
						nCmd = HH_DISPLAY_TOC;
						dwData = 0;
						continue;
					}
				}
			}
		}
		break;
	}

	if (bHelpError) {
		CString msg;
		msg.Format(_T("%s\n\n%s\n\n%s"), pApp->m_pszHelpFilePath, (LPCTSTR)strHelpError, (LPCTSTR)GetResString(_T("ERR_NOHELP")));
		if (CDarkMode::MessageBox(msg, MB_YESNO | MB_ICONERROR) == IDYES)
			BrowserOpen(thePrefs.GetHomepageBaseURL() + _T("/home/perl/help.cgi"), thePrefs.GetMuleDirectory(EMULE_EXECUTABLEDIR));
	}
}

void CemuleDlg::CreateToolbarCmdIconMap()
{
	m_mapTbarCmdToIcon[TBBTN_CONNECT] = _T("Connect");
	m_mapTbarCmdToIcon[TBBTN_KAD] = _T("Kademlia");
	m_mapTbarCmdToIcon[TBBTN_SERVER] = _T("Server");
	m_mapTbarCmdToIcon[TBBTN_TRANSFERS] = _T("Transfer");
	m_mapTbarCmdToIcon[TBBTN_SEARCH] = _T("Search");
	m_mapTbarCmdToIcon[TBBTN_SHARED] = _T("SharedFiles");
	m_mapTbarCmdToIcon[TBBTN_MESSAGES] = _T("Messages");
	m_mapTbarCmdToIcon[TBBTN_IRC] = _T("IRC");
	m_mapTbarCmdToIcon[TBBTN_STATS] = _T("Statistics");
	m_mapTbarCmdToIcon[TBBTN_OPTIONS] = _T("Preferences");
	m_mapTbarCmdToIcon[TBBTN_TOOLS] = _T("Tools");
	m_mapTbarCmdToIcon[TBBTN_SAVESTATE] = _T("Save");
	m_mapTbarCmdToIcon[TBBTN_RELOADCONF] = _T("Reload");
	m_mapTbarCmdToIcon[TBBTN_BACKUP] = _T("Backup");
	m_mapTbarCmdToIcon[TBBTN_EMULEAI] = _T("EMULEAI");
}

LPCTSTR CemuleDlg::GetIconFromCmdId(UINT uId)
{
	LPCTSTR pszIconId;
	return m_mapTbarCmdToIcon.Lookup(uId, pszIconId) ? pszIconId : NULL;
}

BOOL CemuleDlg::OnChevronPushed(UINT id, LPNMHDR pNMHDR, LRESULT* plResult)
{
	UNREFERENCED_PARAMETER(id);
	if (!thePrefs.GetUseReBarToolbar())
		return FALSE;

	NMREBARCHEVRON* pnmrc = (NMREBARCHEVRON*)pNMHDR;

	ASSERT(id == AFX_IDW_REBAR);
	ASSERT(pnmrc->uBand == 0);
	ASSERT(pnmrc->wID == 0);
	ASSERT(!m_mapTbarCmdToIcon.IsEmpty());

	// get visible area of rebar/toolbar
	CRect rcVisibleButtons;
	toolbar->GetClientRect(&rcVisibleButtons);

	// search the first toolbar button which is not fully visible
	int iButtons = toolbar->GetButtonCount();
	int i = 0;
	for (; i < iButtons; ++i) {
		RECT rcButton;
		toolbar->GetItemRect(i, &rcButton);

		CRect rcVisible;
		if (!rcVisible.IntersectRect(&rcVisibleButtons, &rcButton) || !::EqualRect(&rcButton, rcVisible))
			break;
	}

	// create menu for all toolbar buttons which are not (fully) visible
	BOOL bLastMenuItemIsSep = TRUE;
	CMenuXP menu;
	menu.CreatePopupMenu();
	menu.AddMenuSidebar(_T("eMule"));
	TCHAR szString[256];

	TBBUTTONINFO tbbi;
	tbbi.cbSize = (UINT)sizeof tbbi;
	tbbi.dwMask = TBIF_BYINDEX | TBIF_COMMAND | TBIF_STYLE | TBIF_STATE | TBIF_TEXT;

		tbbi.cchText = _countof(szString);
		tbbi.pszText = szString;
	for (; i < iButtons; ++i)
		if (toolbar->GetButtonInfo(i, &tbbi) >= 0)
			if (tbbi.fsStyle & TBSTYLE_SEP) {
				if (!bLastMenuItemIsSep)
					bLastMenuItemIsSep = menu.AppendMenu(MF_SEPARATOR, 0, (LPCTSTR)NULL);
			} else if (*szString && menu.AppendMenu(MF_STRING, tbbi.idCommand, szString, GetIconFromCmdId(tbbi.idCommand))) {
				bLastMenuItemIsSep = FALSE;
				if (tbbi.fsState & TBSTATE_CHECKED)
					menu.CheckMenuItem(tbbi.idCommand, MF_BYCOMMAND | MF_CHECKED);
				if ((tbbi.fsState & TBSTATE_ENABLED) == 0)
					menu.EnableMenuItem(tbbi.idCommand, MF_BYCOMMAND | MF_DISABLED | MF_GRAYED);
			}

	CPoint ptMenu(pnmrc->rc.left, pnmrc->rc.top);
	ClientToScreen(&ptMenu);
	ptMenu.y += rcVisibleButtons.Height();
	menu.TrackPopupMenu(TPM_LEFTALIGN | TPM_TOPALIGN | TPM_LEFTBUTTON | TPM_RIGHTBUTTON, ptMenu.x, ptMenu.y, this);
	*plResult = 1;
	return FALSE;
}

bool CemuleDlg::IsPreferencesDlgOpen() const
{
	return (preferenceswnd->m_hWnd != NULL);
}

INT_PTR CemuleDlg::ShowPreferences(UINT uStartPageID)
{
	if (IsPreferencesDlgOpen()) {
		preferenceswnd->SetForegroundWindow();
		preferenceswnd->BringWindowToTop();
		return -1;
	}
	if (uStartPageID != UINT_MAX)
		preferenceswnd->SetStartPage(uStartPageID);
	return preferenceswnd->DoModal();
}



//////////////////////////////////////////////////////////////////
// Web Server related

LRESULT CemuleDlg::OnWebAddDownloads(WPARAM wParam, LPARAM lParam)
{
	const CString link((LPCTSTR)wParam);
	if (link.GetLength() == 32 && _tcsnicmp(link, _T("ed2k"), 4) != 0) {
		uchar fileid[MDX_DIGEST_SIZE];
		DecodeBase16(link, link.GetLength(), fileid, _countof(fileid));
		theApp.searchlist->AddFileToDownloadByHash(fileid, (uint8)lParam);
	} else
		theApp.AddEd2kLinksToDownload(link, (int)lParam);

	return 0;
}

LRESULT CemuleDlg::OnAddRemoveFriend(WPARAM wParam, LPARAM lParam)
{
	if (lParam == 0) // remove
		theApp.friendlist->RemoveFriend(reinterpret_cast<CFriend*>(wParam));
	else		// add
		theApp.friendlist->AddFriend(reinterpret_cast<CUpDownClient*>(wParam));

	return 0;
}

LRESULT CemuleDlg::OnWebSetCatPrio(WPARAM wParam, LPARAM lParam)
{
	theApp.downloadqueue->SetCatPrio((UINT)wParam, (uint8)lParam);
	return 0;
}

LRESULT CemuleDlg::OnWebServerClearCompleted(WPARAM wParam, LPARAM lParam)
{
	if (!wParam)
		transferwnd->GetDownloadList()->ClearCompleted(static_cast<int>(lParam));
	else {
		uchar* pFileHash = reinterpret_cast<uchar*>(lParam);
		CKnownFile* file = theApp.knownfiles->FindKnownFileByID(pFileHash);
		if (file)
			transferwnd->GetDownloadList()->RemoveFile(static_cast<CPartFile*>(file));
		delete[] pFileHash;
	}

	return 0;
}

LRESULT CemuleDlg::OnWebServerFileRename(WPARAM wParam, LPARAM lParam)
{
	reinterpret_cast<CPartFile*>(wParam)->SetFileName((LPCTSTR)lParam);
	reinterpret_cast<CPartFile*>(wParam)->SavePartFile();
	reinterpret_cast<CPartFile*>(wParam)->UpdateDisplayedInfo();
	sharedfileswnd->sharedfilesctrl.UpdateFile(reinterpret_cast<CKnownFile*>(wParam));

	return 0;
}

LRESULT CemuleDlg::OnWebGUIInteraction(WPARAM wParam, LPARAM lParam)
{

	switch (wParam) {
	case WEBGUIIA_UPDATEMYINFO:
		serverwnd->UpdateMyInfo();
		break;
	case WEBGUIIA_WINFUNC:
		if (thePrefs.GetWebAdminAllowedHiLevFunc()) {
			try {
				HANDLE hToken;
				TOKEN_PRIVILEGES tkp;	// Get a token for this process.

				if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &hToken))
					throw 0; //parameterless throw not allowed here
				LookupPrivilegeValue(NULL, SE_SHUTDOWN_NAME, &tkp.Privileges[0].Luid);
				tkp.PrivilegeCount = 1;  // one privilege to set
				tkp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;	// Get the shutdown privilege for this process.
				AdjustTokenPrivileges(hToken, FALSE, &tkp, 0, (PTOKEN_PRIVILEGES)NULL, 0);

				if (lParam == 1) // shutdown
					ExitWindowsEx(EWX_SHUTDOWN | EWX_FORCE, 0);
				else if (lParam == 2)
					ExitWindowsEx(EWX_REBOOT | EWX_FORCE, 0);
			} catch (...) {
				AddLogLine(true, GetResString(_T("WEB_REBOOT")) + _T(' ') + GetResString(_T("FAILED")));
			}
		} else
			AddLogLine(true, GetResString(_T("WEB_REBOOT")) + _T(' ') + GetResString(_T("ACCESSDENIED")));
		break;
	case WEBGUIIA_UPD_CATTABS:
		theApp.emuledlg->transferwnd->UpdateCatTabTitles();
		break;
	case WEBGUIIA_UPD_SFUPDATE:
		if (lParam)
			theApp.sharedfiles->UpdateFile((CKnownFile*)lParam);
		break;
	case WEBGUIIA_UPDATESERVER:
		serverwnd->serverlistctrl.RefreshServer((CServer*)lParam);
		break;
	case WEBGUIIA_STOPCONNECTING:
		theApp.serverconnect->StopConnectionTry();
		break;
	case WEBGUIIA_CONNECTTOSERVER:
		if (!lParam)
			theApp.serverconnect->ConnectToAnyServer();
		else
			theApp.serverconnect->ConnectToServer(reinterpret_cast<CServer*>(lParam), false, false, true);
		break;
	case WEBGUIIA_DISCONNECT:
		if (lParam != 2)	// !KAD
			theApp.serverconnect->Disconnect();
		if (lParam != 1)	// !ED2K
			Kademlia::CKademlia::Stop();
		break;
	case WEBGUIIA_SERVER_REMOVE:
		serverwnd->serverlistctrl.RemoveServer(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_SHARED_FILES_RELOAD:
		theApp.sharedfiles->Reload();
		break;
	case WEBGUIIA_ADD_TO_STATIC:
		serverwnd->serverlistctrl.StaticServerFileAppend(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_REMOVE_FROM_STATIC:
		serverwnd->serverlistctrl.StaticServerFileRemove(reinterpret_cast<CServer*>(lParam));
		break;
	case WEBGUIIA_UPDATESERVERMETFROMURL:
		theApp.emuledlg->serverwnd->UpdateServerMetFromURL((TCHAR*)lParam);
		break;
	case WEBGUIIA_SHOWSTATISTICS:
		theApp.emuledlg->statisticswnd->ShowStatistics(lParam != 0);
		break;
	case WEBGUIIA_DELETEALLSEARCHES:
		theApp.emuledlg->searchwnd->DeleteAllSearches();
		break;
	case WEBGUIIA_KAD_BOOTSTRAP:
	{
		CString ip((LPCTSTR)lParam);
		int pos = ip.Find(_T(':'));
		if (pos >= 0) {
			uint16 port = (uint16)_tstoi(CPTR(ip, pos + 1));
			ip.Truncate(pos);
			Kademlia::CKademlia::Bootstrap(ip, port);
		}
	}
	break;
	case WEBGUIIA_KAD_START:
		Kademlia::CKademlia::Start();
		break;
	case WEBGUIIA_KAD_STOP:
		Kademlia::CKademlia::Stop();
		break;
	case WEBGUIIA_KAD_RCFW:
		Kademlia::CKademlia::RecheckFirewalled();
	}

	return 0;
}

void CemuleDlg::TrayMinimizeToTrayChange()
{
	CMenu* pSysMenu = GetSystemMenu(FALSE);
	if (pSysMenu != NULL) {
		if (!thePrefs.GetMinToTray()) {
			// just for safety, ensure that we are not adding duplicate menu entries
			if (pSysMenu->EnableMenuItem(MP_MINIMIZETOTRAY, MF_BYCOMMAND | MF_ENABLED) == UINT_MAX) {
				ASSERT((MP_MINIMIZETOTRAY & 0xFFF0) == MP_MINIMIZETOTRAY && MP_MINIMIZETOTRAY < 0xF000);
				VERIFY(pSysMenu->InsertMenu(SC_MINIMIZE, MF_BYCOMMAND, MP_MINIMIZETOTRAY, GetResString(_T("PW_TRAY"))));
			} else
				ASSERT(0);
		} else
			(void)pSysMenu->RemoveMenu(MP_MINIMIZETOTRAY, MF_BYCOMMAND);
	}
	CTrayDialog::TrayMinimizeToTrayChange();
}

void CemuleDlg::SetToolTipsDelay(UINT uMilliseconds)
{
	transferwnd->SetToolTipsDelay(uMilliseconds);
	sharedfileswnd->SetToolTipsDelay(uMilliseconds);
}

void CALLBACK CemuleDlg::UPnPTimeOutTimer(HWND /*hwnd*/, UINT /*uiMsg*/, UINT_PTR /*idEvent*/, DWORD /*dwTime*/) noexcept
{
	theApp.emuledlg->PostMessage(UM_UPNP_RESULT, (WPARAM)CUPnPImpl::UPNP_TIMEOUT, 0);
}

LRESULT CemuleDlg::OnUPnPResult(WPARAM wParam, LPARAM lParam)
{
	bool bWasRefresh = lParam != 0;
	CUPnPImpl* impl = theApp.m_pUPnPFinder->GetImplementation();

	//>>> WiZaRd - handle "UPNP_TIMEOUT" events!
	if (!bWasRefresh && wParam != CUPnPImpl::UPNP_OK) {
		//just to be sure, stop any running services and also delete the forwarded ports (if necessary)
		if (wParam == CUPnPImpl::UPNP_TIMEOUT) {
			impl->StopAsyncFind();
			impl->DeletePorts();
		}
		// UPnP failed, check if we can retry it with another implementation
		if (theApp.m_pUPnPFinder->SwitchImplentation()) {
			StartUPnP(false);
			return 0;
		}

		DebugLog(_T("No more available UPnP implementations left"));
	}

	if (m_hUPnPTimeOutTimer != 0) {
		VERIFY(::KillTimer(NULL, m_hUPnPTimeOutTimer));
		m_hUPnPTimeOutTimer = 0;
	}
	if (!bWasRefresh)
		if (wParam == CUPnPImpl::UPNP_OK) {
			// remember the last working implementation
			thePrefs.SetLastWorkingUPnPImpl(impl->GetImplementationID());
			Log(GetResString(_T("UPNPSUCCESS")), impl->GetUsedTCPPort(), impl->GetUsedUDPPort());
		} else
			LogWarning(GetResString(_T("UPNPFAILED")));

	if (theApp.IsRunning() && m_bConnectRequestDelayedForUPnP)
		StartConnection();

	return 0;
}

LRESULT CemuleDlg::OnPowerBroadcast(WPARAM wParam, LPARAM lParam)
{
	//DebugLog(_T("DEBUG:Power state change. wParam=%d lPararm=%ld"),wParam,lParam);
	switch (wParam) {
	case PBT_APMRESUMEAUTOMATIC:
		theApp.ResetStandbyOff();
		if (m_bEd2kSuspendDisconnect || m_bKadSuspendDisconnect) {
			DebugLog(_T("Reconnect after Power state change. wParam=%d lPararm=%ld"), wParam, lParam);
			RefreshUPnP(true);
			PostMessage(WM_SYSCOMMAND, MP_CONNECT, 0); // tell to connect. a sec later...
		}
		return TRUE; // message processed.
	case PBT_APMSUSPEND:
		DebugLog(_T("System is going is suspending operation, disconnecting. wParam=%d lPararm=%ld"), wParam, lParam);
		m_bEd2kSuspendDisconnect = theApp.serverconnect->IsConnected();
		m_bKadSuspendDisconnect = Kademlia::CKademlia::IsConnected();
		CloseConnection();
		return TRUE; // message processed.
	}
	return FALSE; // we do not process this message
}

void CemuleDlg::StartUPnP(bool bReset, uint16 nForceTCPPort, uint16 nForceUDPPort)
{
	if (theApp.m_pUPnPFinder != NULL && (m_hUPnPTimeOutTimer == 0 || !bReset)) {
		if (bReset) {
			theApp.m_pUPnPFinder->Reset();
			Log(GetResString(_T("UPNPSETUP")));
		}
		try {
			CUPnPImpl* impl = theApp.m_pUPnPFinder->GetImplementation();
			if (impl->IsReady()) {
				impl->SetMessageOnResult(this, UM_UPNP_RESULT);
				if (bReset)
					VERIFY((m_hUPnPTimeOutTimer = ::SetTimer(NULL, 0, SEC2MS(40), (TIMERPROC)UPnPTimeOutTimer)) != 0);
				impl->StartDiscovery((nForceTCPPort ? nForceTCPPort : thePrefs.GetPort())
					, (nForceUDPPort ? nForceUDPPort : thePrefs.GetUDPPort())
					, (thePrefs.GetWSUseUPnP() ? thePrefs.GetWSPort() : 0));
			} else
				/*theApp.emuledlg->*/PostMessage(UM_UPNP_RESULT, (WPARAM)CUPnPImpl::UPNP_FAILED, 0);
		} catch (const CUPnPImpl::UPnPError&) {
			//ignore
		} catch (CException *ex) {
			ex->Delete();
		}
	} else
		ASSERT(0);
}

void CemuleDlg::RefreshUPnP(bool bRequestAnswer)
{
	if (!thePrefs.IsUPnPEnabled())
		return;
	if (theApp.m_pUPnPFinder != NULL && m_hUPnPTimeOutTimer == 0) {
		try {
			CUPnPImpl* impl = theApp.m_pUPnPFinder->GetImplementation();
			if (impl->IsReady()) {
				if (bRequestAnswer)
					impl->SetMessageOnResult(this, UM_UPNP_RESULT);
				if (impl->CheckAndRefresh() && bRequestAnswer)
					VERIFY((m_hUPnPTimeOutTimer = ::SetTimer(NULL, 0, SEC2MS(10), UPnPTimeOutTimer)) != 0);
				else
					impl->SetMessageOnResult(NULL, 0);
			} else
				DebugLogWarning(_T("RefreshUPnP, implementation not ready"));
		} catch (const CUPnPImpl::UPnPError&) {
			//ignore
		} catch (CException *ex) {
			ex->Delete();
		}
	} else
		ASSERT(0);
}

BOOL CemuleDlg::OnDeviceChange(UINT nEventType, DWORD_PTR dwData)
{
	// WM_DEVICECHANGE is sent for:
	//	Drives which where created/deleted with "SUBST" command (handled like network drives)
	//	Drives which where created/deleted as regular network drives.
	//
	// WM_DEVICECHANGE is *NOT* sent for:
	//	Floppy disk drives
	//	ZIP disk drives (although Windows Explorer recognises a changed media, we do not get a message)
	//	CD-ROM drives (although MSDN says different...)
	//
	if ((nEventType == DBT_DEVICEARRIVAL || nEventType == DBT_DEVICEREMOVECOMPLETE) && !IsBadReadPtr((void*)dwData, sizeof(DEV_BROADCAST_HDR))) {
#ifdef _DEBUG
		CString strMsg(nEventType == DBT_DEVICEARRIVAL ? _T("DBT_DEVICEARRIVAL") : _T("DBT_DEVICEREMOVECOMPLETE"));
#endif
		const DEV_BROADCAST_HDR* pHdr = (DEV_BROADCAST_HDR*)dwData;
		if (pHdr->dbch_devicetype == DBT_DEVTYP_VOLUME && !IsBadReadPtr((void*)dwData, sizeof(DEV_BROADCAST_VOLUME))) {
			const DEV_BROADCAST_VOLUME* pVol = (DEV_BROADCAST_VOLUME*)pHdr;
#ifdef _DEBUG
			strMsg += _T(" Volume");
			if (pVol->dbcv_flags & DBTF_MEDIA)
				strMsg += _T(" Media");
			if (pVol->dbcv_flags & DBTF_NET)
				strMsg += _T(" Net");
			if ((pVol->dbcv_flags & ~(DBTF_NET | DBTF_MEDIA)) != 0)
				strMsg.AppendFormat(_T(" flags=0x%08x"), pVol->dbcv_flags);
#endif
			bool bVolumesChanged = false;
			for (UINT uDrive = 0; uDrive <= 25; ++uDrive) {
				UINT uMask = 1 << uDrive;
				if (pVol->dbcv_unitmask & uMask) {
					DEBUG_ONLY(strMsg.AppendFormat(_T(" %c:"), _T('A') + uDrive));
					if (pVol->dbcv_flags & (DBTF_MEDIA | DBTF_NET))
						ClearVolumeInfoCache(uDrive);
					bVolumesChanged = true;
				}
			}
			if (bVolumesChanged && sharedfileswnd)
				sharedfileswnd->OnVolumesChanged();
		} else
			DEBUG_ONLY(strMsg.AppendFormat(_T(" devicetype=0x%08x"), pHdr->dbch_devicetype));

#ifdef _DEBUG
		TRACE(_T("CemuleDlg::OnDeviceChange: %s\n"), (LPCTSTR)strMsg);
#endif
	} else
		TRACE2(_T("CemuleDlg::OnDeviceChange: nEventType=0x%08x  dwData=0x%08x\n"), nEventType, dwData);

	return __super::OnDeviceChange(nEventType, dwData);
}

LRESULT CemuleDlg::OnDisplayChange(WPARAM, LPARAM)
{
	TrayReset();
	return 0;
}


//////////////////////////////////////////////////////////////////
// Windows 7 GUI goodies

#ifdef HAVE_WIN7_SDK_H
// update thumbbarbutton structs and add/update the GUI thumbbar
void CemuleDlg::UpdateThumbBarButtons(bool initialAddToDlg)
{
	if (!m_pTaskbarList)
		return;

	THUMBBUTTONMASK dwMask = THB_ICON | THB_FLAGS;
	for (int i = TBB_FIRST; i <= TBB_LAST; ++i) {
		m_thbButtons[i].dwMask = dwMask;
		m_thbButtons[i].iId = i;
		m_thbButtons[i].iBitmap = 0;
		m_thbButtons[i].dwFlags = THBF_DISMISSONCLICK;

		LPCTSTR uid;
		switch (i) {
		case TBB_CONNECT:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("CONNECT"), 16, 16);
			uid = _T("MAIN_BTN_CONNECT");
			if (theApp.IsConnected())
				m_thbButtons[i].dwFlags |= THBF_DISABLED;
			break;
		case TBB_DISCONNECT:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("DISCONNECT"), 16, 16);
			uid = _T("MAIN_BTN_DISCONNECT");
			if (!theApp.IsConnected())
				m_thbButtons[i].dwFlags |= THBF_DISABLED;
			break;
		case TBB_THROTTLE:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("SPEEDMIN"), 16, 16);
			uid = _T("PW_PA");
			break;
		case TBB_UNTHROTTLE:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("SPEEDMAX"), 16, 16);
			uid = _T("PW_UA");
			break;
		case TBB_PREFERENCES:
			m_thbButtons[i].hIcon = theApp.LoadIcon(_T("PREFERENCES"), 16, 16);
			uid = _T("EM_PREFS");
			break;
		default:
			uid = EMPTY;
		}
		// set tooltips in widechar
		if (uid) {
			const CString& tooltip(GetResNoAmp(uid));
			wcscpy(m_thbButtons[i].szTip, tooltip);
			m_thbButtons[i].dwMask |= THB_TOOLTIP;
		}
	}

	if (initialAddToDlg)
		m_pTaskbarList->ThumbBarAddButtons(m_hWnd, ARRAYSIZE(m_thbButtons), m_thbButtons);
	else
		m_pTaskbarList->ThumbBarUpdateButtons(m_hWnd, ARRAYSIZE(m_thbButtons), m_thbButtons);

	// clean up icons, they were copied in the previous call
	for (int i = TBB_FIRST; i <= TBB_LAST; ++i)
		::DestroyIcon(m_thbButtons[i].hIcon);
}

// Handle thumbbar buttons
void CemuleDlg::OnTBBPressed(UINT id)
{
	switch (id) {
	case TBB_CONNECT:
		OnBnClickedConnect();
		break;
	case TBB_DISCONNECT:
		CloseConnection();
		break;
	case TBB_THROTTLE:
		QuickSpeedOther(MP_QS_PA);
		break;
	case TBB_UNTHROTTLE:
		QuickSpeedOther(MP_QS_UA);
		break;
	case TBB_PREFERENCES:
		ShowPreferences();
	}
}

// When Windows tells us, the taskbar button was created, it is safe to initialize our taskbar stuff
LRESULT CemuleDlg::OnTaskbarBtnCreated(WPARAM, LPARAM)
{
	// Sanity check that the OS is Win 7 or later
	if (thePrefs.GetWindowsVersion() >= _WINVER_7_ && !theApp.IsClosing()) {
		if (m_pTaskbarList)
			m_pTaskbarList.Release();

		if (m_pTaskbarList.CoCreateInstance(CLSID_TaskbarList) == S_OK) {
			m_pTaskbarList->SetProgressState(m_hWnd, TBPF_NOPROGRESS);

			m_currentTBP_state = TBPF_NOPROGRESS;
			m_prevProgress = 0;
			m_ovlIcon = NULL;

			UpdateThumbBarButtons(true);
			UpdateStatusBarProgress();
		} else
			ASSERT(0);
	}
	return 0;
}

// Updates global progress and /down state overlay icon
// Overlay icon looks rather annoying than useful, so it's disabled by default for the common user and can be enabled by ini setting only (Ornis)
void CemuleDlg::EnableTaskbarGoodies(bool enable)
{
	if (m_pTaskbarList) {
		m_pTaskbarList->SetOverlayIcon(m_hWnd, NULL, EMPTY);
		if (!enable) {
			m_pTaskbarList->SetProgressState(m_hWnd, TBPF_NOPROGRESS);
			m_currentTBP_state = TBPF_NOPROGRESS;
			m_prevProgress = 0;
			m_ovlIcon = NULL;
		} else
			UpdateStatusBarProgress();
	}
}

void CemuleDlg::UpdateStatusBarProgress()
{
	if (m_pTaskbarList && thePrefs.IsWin7TaskbarGoodiesEnabled()) {
		// calc global progress & status
		float finishedsize = theApp.emuledlg->transferwnd->GetDownloadList()->GetFinishedSize();
		float globalSize = theStats.m_fGlobalSize + finishedsize;

		if (globalSize == 0) {
			// if there is no download, disable progress
			if (m_currentTBP_state != TBPF_NOPROGRESS)
				m_currentTBP_state = TBPF_NOPROGRESS;
		} else {
			TBPFLAG new_state;
			if (theStats.m_dwOverallStatus & STATE_ERROROUS) // an error
				new_state = TBPF_ERROR;
			else if (theStats.m_dwOverallStatus & STATE_DOWNLOADING) // something downloading
				new_state = TBPF_NORMAL;
			else
				new_state = TBPF_PAUSED;

			if (new_state != m_currentTBP_state)
				m_currentTBP_state = new_state;

			float globalDone = theStats.m_fGlobalDone + finishedsize;
			float overallProgress = globalDone / globalSize;
			if (overallProgress != m_prevProgress) {
				m_prevProgress = overallProgress;
				m_pTaskbarList->SetProgressValue(m_hWnd, (ULONGLONG)(overallProgress * 100), 100);
			}
		}
		m_pTaskbarList->SetProgressState(m_hWnd, m_currentTBP_state);

		// overlay up/down-speed
		if (thePrefs.IsShowUpDownIconInTaskbar()) {
			bool bUp = theApp.emuledlg->transferwnd->GetUploadList()->GetItemCount() > 0;
			bool bDown = theStats.m_dwOverallStatus & STATE_DOWNLOADING;

			HICON newicon;
			if (bUp && bDown)
				newicon = transicons[3];
			else if (bUp)
				newicon = transicons[2];
			else if (bDown)
				newicon = transicons[1];
			else
				newicon = NULL;

			if (m_ovlIcon != newicon) {
				m_ovlIcon = newicon;
				m_pTaskbarList->SetOverlayIcon(m_hWnd, m_ovlIcon, _T("eMule Up/Down Indicator"));
			}
		}
	}
}
#endif

void CemuleDlg::SetTaskbarIconColor()
{
	bool bBrightTaskbarIconSpeed = false;
	bool bTransparent = false;
	COLORREF cr = RGB(0, 0, 0);
	if (thePrefs.IsRunningAeroGlassTheme()) {
		HMODULE hDWMAPI = LoadLibrary(_T("dwmapi.dll"));
		if (hDWMAPI) {
			HRESULT(WINAPI * pfnDwmGetColorizationColor)(DWORD*, BOOL*);
			(FARPROC&)pfnDwmGetColorizationColor = GetProcAddress(hDWMAPI, "DwmGetColorizationColor");
			if (pfnDwmGetColorizationColor != NULL) {
				DWORD dwGlassColor;
				BOOL bOpaque;
				if (pfnDwmGetColorizationColor(&dwGlassColor, &bOpaque) == S_OK) {
					uint8 byAlpha = (uint8)(dwGlassColor >> 24);
					cr = 0xFFFFFF & dwGlassColor;
					if (byAlpha < 200 && !bOpaque) {
						// on transparent themes we can never figure out what exact color is shown
						// (if we could in real time?), but given that a color is blended against
						// the background, it is a good guess that a bright speedbar will be
						// the best solution in most cases
						bTransparent = true;
					}
				}
			}
			FreeLibrary(hDWMAPI);
		}
	} else {
		DEBUG_ONLY(DebugLog(_T("Taskbar Notifier Color: GetCustomSysColor() used")));
		cr = GetCustomSysColor(COLOR_3DFACE);
	}
	uint8 iRed = GetRValue(cr);
	uint8 iBlue = GetBValue(cr);
	uint8 iGreen = GetGValue(cr);
	uint16 iBrightness = (uint16)sqrt(((iRed * iRed * 0.241f) + (iGreen * iGreen * 0.691f) + (iBlue * iBlue * 0.068f)));
	ASSERT(iBrightness <= 255);
	bBrightTaskbarIconSpeed = iBrightness < 132;
	DebugLog(_T("Taskbar Notifier Color: R:%u G:%u B:%u, Brightness: %u, Transparent: %s"), iRed, iGreen, iBlue, iBrightness, bTransparent ? _T("Yes") : _T("No"));
	thePrefs.SetStatsColor(11, ((bBrightTaskbarIconSpeed || bTransparent) ? RGB(255, 255, 255) : RGB(0, 0, 0)));
}
LRESULT CemuleDlg::OnConChecker(WPARAM wParam, LPARAM lParam)
{
	if (m_hTimer != 0)
		return 0;

	if (theApp.IsClosing() || theApp.serverconnect == NULL)
		return 0;

	if (wParam == 2) { //WRAPAM 2 = status	
		switch (lParam)
		{
			case CONSTATE_ONLINE:
				if (theApp.GetConnectionState() != CONSTATE_ONLINE)	{
					const bool bShouldStartConnection = thePrefs.DoAutoConnect()
						|| bPrevKadState
						|| bPrevEd2kState
						|| m_bKadSuspendDisconnect
						|| m_bEd2kSuspendDisconnect;

					AddLogLine(true, GetResString(_T("CONN_ONLINE")));
					theApp.SetConnectionState(CONSTATE_ONLINE);
					if (bShouldStartConnection)
						StartConnection(); // Restore auto-connect or previously active connections after connectivity returns.

					if (bPrevKadState && !Kademlia::CKademlia::IsConnected()) // If auto connect to KAD option isn't active, but KAD was manually connected
						Kademlia::CKademlia::Start();

				if (bPrevEd2kState && (!theApp.serverconnect->IsConnecting() && !theApp.serverconnect->IsConnected()))  // If auto connect to eD2K option isn't active, but eD2K server was manually connected
					theApp.serverconnect->ConnectToAnyServer();
			}
			break;
		case CONSTATE_OFFLINE:
			if (theApp.GetConnectionState() != CONSTATE_OFFLINE) {
				AddLogLine(true, GetResString(_T("CONN_OFFLINE")));
				bPrevKadState = Kademlia::CKademlia::IsConnected();
				bPrevEd2kState = theApp.serverconnect->IsConnecting() || theApp.serverconnect->IsConnected();
				CloseConnection(); // no need to keep the connection 'alive'... or even try!
				theApp.SetConnectionState(CONSTATE_OFFLINE);
			}
			break;
		default:
			AddLogLine(false, GetResString(_T("CONN_UNKNOWN")));
			theApp.SetConnectionState(CONSTATE_NULL);
			break;
		}
	}
	return 0;
}

void CemuleDlg::ShowSpeedGraph(bool bShow)
{
	if (bShow) {
		if (m_UpSpeedGraph.IsWindowVisible() == false || m_DownSpeedGraph.IsWindowVisible() == false) {
			toolbar->Refresh();
			SetSpeedGraphLimits();
			m_UpSpeedGraph.EnableWindow(true);
			m_UpSpeedGraph.ShowWindow(SW_SHOW);
			m_DownSpeedGraph.EnableWindow(true);
			m_DownSpeedGraph.ShowWindow(SW_SHOW);
		}
	} else {
		CRect rInvalidateUp, rInvalidateDown;
		m_UpSpeedGraph.ShowWindow(SW_HIDE);
		m_UpSpeedGraph.EnableWindow(false);
		m_UpSpeedGraph.GetWindowRect(&rInvalidateUp);
		ScreenToClient(rInvalidateUp);
		InvalidateRect(rInvalidateUp);
		m_DownSpeedGraph.ShowWindow(SW_HIDE);
		m_DownSpeedGraph.EnableWindow(false);
		m_DownSpeedGraph.GetWindowRect(&rInvalidateDown);
		theApp.emuledlg->toolbar->ScreenToClient(rInvalidateDown);
		theApp.emuledlg->toolbar->InvalidateRect(rInvalidateDown);
	}
}

void CemuleDlg::SetSpeedGraphLimits()
{
	const uint32 uUploadRate = theApp.uploadqueue->HasActiveUploads() ? theApp.uploadqueue->GetDatarate() : 0;
	m_UpSpeedGraph.Set_TrafficValue(uUploadRate);
	m_DownSpeedGraph.Set_TrafficValue(theApp.downloadqueue->GetDatarate());
}

void CemuleDlg::ResizeSpeedGraph()
{
	if (!thePrefs.GetUITweaksSpeedGraph())
		return;

	if (!thePrefs.GetUITweaksSpeedGraph()) {
		m_UpSpeedGraph.ShowWindow(SW_HIDE);
		m_DownSpeedGraph.ShowWindow(SW_HIDE);
		return;
	}

	CRect rect;
	CRect rect1, rect2;
	CSize csMaxSize;
	LONG m_lWidth = 265; // Width of SpeedGraph

	toolbar->GetMaxSize(&csMaxSize);
	toolbar->GetClientRect(&rect);

	// set updateintervall of graphic rate display (in seconds)
	rect1.top = rect.top + 2;
	rect1.right = rect.right - 2;
	rect1.bottom = rect.top + (rect.Height() / 2) - 1;
	rect1.left = rect.right - m_lWidth;

	rect2.top = rect.top + (rect.Height() / 2) + 1;
	rect2.right = rect.right - 2;
	rect2.bottom = rect.bottom - 2;
	rect2.left = rect.right - m_lWidth;

	if (!m_UpSpeedGraph.m_hWnd) {
		m_UpSpeedGraph.Create(IDD_SPEEDGRAPH, rect1, this);
		m_UpSpeedGraph.Init_Graph(_T("Up"), thePrefs.GetMaxGraphUploadRate(true));
	}

	if (!m_DownSpeedGraph.m_hWnd) {
		m_DownSpeedGraph.Create(IDD_SPEEDGRAPH, rect2, this);
		m_DownSpeedGraph.Init_Graph(_T("Down"), thePrefs.GetMaxGraphDownloadRate());
	}

	m_UpSpeedGraph.SetWindowPos(NULL, rect1.left, rect1.top, rect1.Width(), rect1.Height(), SWP_NOZORDER | SWP_SHOWWINDOW);
	m_DownSpeedGraph.SetWindowPos(NULL, rect2.left, rect2.top, rect2.Width(), rect2.Height(), SWP_NOZORDER | SWP_SHOWWINDOW);

	if (m_lWidth + csMaxSize.cx > rect.Width()) {
		// Not enough space in toolbar
		m_UpSpeedGraph.ShowWindow(SW_HIDE);
		m_DownSpeedGraph.ShowWindow(SW_HIDE);
	}
}

LRESULT CemuleDlg::OnSharedFileListFoundFiles(WPARAM /*wParam*/, LPARAM /*lParam*/)
{
	if (theApp.IsClosing())
		return 0;

	if (theApp.sharedfiles)
		theApp.sharedfiles->NotifyFoundFilesEvent();

	return 0;
}

LRESULT CemuleDlg::OnSharedFilesCtrlUpdateFile(WPARAM wParam, LPARAM /*lParam*/)
{
	if (theApp.IsClosing())
		return 0;

	if (sharedfileswnd)
		sharedfileswnd->sharedfilesctrl.UpdateFile(reinterpret_cast<CKnownFile*>(wParam));

	return 0;
}
