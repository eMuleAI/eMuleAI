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
#include "emule.h"
#include "opcodes.h"
#include "mdump.h"
#include "Scheduler.h"
#include "SearchList.h"
#include "kademlia/kademlia/Error.h"
#include "kademlia/kademlia/Kademlia.h"
#include "kademlia/kademlia/Prefs.h"
#include "kademlia/utils/UInt128.h"
#include "PerfLog.h"
#include <sockimpl.h> //for *m_pfnSockTerm()
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
#include "ServerList.h"
#include "ServerConnect.h"
#include "ListenSocket.h"
#include "ClientCredits.h"
#include "KnownFileList.h"
#include "Server.h"
#include "ED2KLink.h"
#include "Preferences.h"
#include "secrunasuser.h"
#include "SafeFile.h"
#include "emuleDlg.h"
#include "SearchDlg.h"
#include "enbitmap.h"
#include "FirewallOpener.h"
#include "StringConversion.h"
#include "Log.h"
#include "Collection.h"
#include "HelpIDs.h"
#include "UPnPImplWrapper.h"
#include "UploadDiskIOThread.h"
#include "PartFileWriteThread.h"
#include "eMuleAI/Shield.h"
#include "eMuleAI/AntiNick.h"
#include <iphlpapi.h>
#include <filesystem>
#include <regex>
#include "eMuleAI/DarkMode.h"
#include "UserMsgs.h"
#include <vector>
#include "eMuleAI/ThreadpoolWrapper.h"
// Kademlia cleanup helpers
#include "kademlia/kademlia/Entry.h"
#include "kademlia/routing/RoutingBin.h"
#include "eMuleAI/ThreadpoolWrapper.h"
#include "SHAHashSet.h"
#include "OtherFunctions.h"
#include "MediaInfo.h"
#include "eMuleAI\LanguageSelectDlg.h"
#include "eMuleAI\MigrationWizardDlg.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

#ifdef DEBUGLEAKHELPER
#include "eMuleAI/DebugLeakHelper.h"
#endif

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif


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


///////////////////////////////////////////////////////////////////////////////
// C-RTL Memory Debug Support
//
#ifdef _DEBUG
static CMemoryState oldMemState, newMemState, diffMemState;

_CRT_ALLOC_HOOK g_pfnPrevCrtAllocHook = NULL;
CMap<const unsigned char*, const unsigned char*, UINT, UINT> g_allocations;
int eMuleAllocHook(int mode, void *pUserData, size_t nSize, int nBlockUse, long lRequest, const unsigned char *pszFileName, int nLine) noexcept;

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
// object files which are used to link eMule.exe, the linker may or may not
// throw some errors about 'safeseh'. Those errors have to get resolved until
// the linker is capable of linking eMule.exe *with* "/SafeSEH".
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
		CDarkMode::MessageBox(_T("eMule.exe was not linked with /SafeSEH!"), MB_ICONSTOP);
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
	, m_bGuardClipboardPrompt()
	, m_bAutoStart()
	, m_bStandbyOff()
	, m_bFirstIPv4(true)
	, m_bFirstIPv6(true)
	, m_dwLastValidIPv4()
	, m_tLastDiskSpaceCheckTime()
	, MediaInfoLibHintGiven ()
	, tLastBackupTime()
	, m_nConnectionState(CONSTATE_ONLINE)

{
	// Initialize Windows security features.
#if !defined(_DEBUG) && !defined(_WIN64)
	InitSafeSEH();
#endif
	InitDEP();
	InitHeapCorruptionDetection();

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
	return m_app_state == APP_STATE_SHUTTINGDOWN || m_app_state == APP_STATE_DONE;
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
	OutputDebugString(_T("[DebugLeak] Helper initialized.\n"));
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
	g_pfnPrevCrtAllocHook = _CrtSetAllocHook(&eMuleAllocHook); 
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
	Log(_T("Starting %s"), (LPCTSTR)theApp.GetAppVersion());

	SetConsoleCtrlHandler(ConsoleCtrlHandler, TRUE);

	CDarkMode::Initialize();

	emuledlg = new CemuleDlg;
	m_pMainWnd = emuledlg;

	StartDirWatchTP();

#ifdef DEBUGLEAKHELPER
	_CrtMemCheckpoint(&g_msAfterInit); // Snapshot after init
#endif

	// Barry - Auto-take ed2k links
	if (thePrefs.AutoTakeED2KLinks())
		Ask4RegFix(false, true, false);

	SetAutoStart(thePrefs.GetAutoStart());

	m_pFirewallOpener = new CFirewallOpener();
	m_pFirewallOpener->Init(true); // we need to init it now (even if we may not use it yet) because of CoInitializeSecurity - which kinda ruins the sense of the class interface but ooohh well :P
	// Open WinXP firewall ports if set in preferences and possible
	if (thePrefs.IsOpenPortsOnStartupEnabled()) {
		if (m_pFirewallOpener->DoesFWConnectionExist()) {
			// delete old rules added by eMule
			m_pFirewallOpener->RemoveRule(EMULE_DEFAULTRULENAME_UDP);
			m_pFirewallOpener->RemoveRule(EMULE_DEFAULTRULENAME_TCP);
			// open port for this session
			if (m_pFirewallOpener->OpenPort(thePrefs.GetPort(), NAT_PROTOCOL_TCP, EMULE_DEFAULTRULENAME_TCP, true))
				QueueLogLine(false, GetResString(_T("FO_TEMPTCP_S")), thePrefs.GetPort());
			else
				QueueLogLine(false, GetResString(_T("FO_TEMPTCP_F")), thePrefs.GetPort());

			if (thePrefs.GetUDPPort()) {
				// open port for this session
				if (m_pFirewallOpener->OpenPort(thePrefs.GetUDPPort(), NAT_PROTOCOL_UDP, EMULE_DEFAULTRULENAME_UDP, true))
					QueueLogLine(false, GetResString(_T("FO_TEMPUDP_S")), thePrefs.GetUDPPort());
				else
					QueueLogLine(false, GetResString(_T("FO_TEMPUDP_F")), thePrefs.GetUDPPort());
			}
		}
	}

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
	geolite2 = new CGeoLite2();
	clientlist = new CClientList();
	friendlist = new CFriendList();
	searchlist = new CSearchList();
	knownfiles = new CKnownFileList();
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
	DownloadChecker = new CDownloadChecker();

	lastCommonRouteFinder = new LastCommonRouteFinder();
	uploadBandwidthThrottler = new UploadBandwidthThrottler();

	m_pUploadDiskIOThread = new CUploadDiskIOThread();
	m_pPartFileWriteThread = new CPartFileWriteThread();

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

	StopDirWatchTP();

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
	// This ensures that major buffers (like MaxMind DB 116MB) are freed and not reported as leaks.
	delete m_pPartFileWriteThread;
	m_pPartFileWriteThread = NULL;
	delete m_pUploadDiskIOThread;
	m_pUploadDiskIOThread = NULL;
	delete uploadBandwidthThrottler;
	uploadBandwidthThrottler = NULL;
	delete lastCommonRouteFinder;
	lastCommonRouteFinder = NULL;
	delete DownloadChecker;
	DownloadChecker = NULL;
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
	delete geolite2;
	geolite2 = NULL;
	delete shield;
	shield = NULL;
	delete m_pUPnPFinder;
	m_pUPnPFinder = NULL;
	delete m_pFirewallOpener;
	m_pFirewallOpener = NULL;

	// Ensure thread-local tooltips are destroyed for the main UI thread.
	AFX_MODULE_THREAD_STATE* pThreadState = AfxGetModuleThreadState();
	if (pThreadState != NULL && pThreadState->m_pToolTip != NULL) {
		pThreadState->m_pToolTip->DestroyWindow();
		delete pThreadState->m_pToolTip;
		pThreadState->m_pToolTip = NULL;
	}

	thePrefs.Uninit();

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
	_CRT_ALLOC_HOOK old_alloc = _CrtSetAllocHook(NULL); // Avoid re-entrancy while touching CMap
	g_allocations.RemoveAll();
	_CrtSetAllocHook(old_alloc);
#endif

	// 4) Ensure rich edit global smiley caches are purged before CRT leak snapshot.
	CHTRichEditCtrl::ForcePurgeSmileysForShutdown();

	// 5) Clear volume info cache (map of volume->FS name) to release internal CPlex blocks.
	ClearVolumeInfoCache();

	// 5b) Clear MediaInfo display-name caches to release CPlex blocks.
	ClearMediaInfoCaches();

	// 6) Reset Kademlia global tracking maps to avoid benign shutdown-time leaks in CRT snapshot.
	Kademlia::CKeyEntry::ResetGlobalTrackingMap();
	Kademlia::CRoutingBin::ResetGlobalTrackingMaps();

#ifdef DEBUGLEAKHELPER
	_CRT_ALLOC_HOOK oldHook = _CrtSetAllocHook(NULL); // Temporarily suspend our alloc hook while CRT enumerates the heap.

	DebugLeakHelper::DumpLeaksToCrt(); // writes full leak list to CRT output

	// Safer than walking from a stale header: use snapshot differences.
	_CrtMemState now, diff;
	_CrtMemCheckpoint(&now);

	OutputDebugString(_T("==== Stats since app start ====\n"));
	if (_CrtMemDifference(&diff, &g_msStart, &now))
		_CrtMemDumpStatistics(&diff);
	else
		OutputDebugString(_T("[DebugLeak] No differences since app start.\n"));

	OutputDebugString(_T("==== Stats since after init ====\n"));
	if (_CrtMemDifference(&diff, &g_msAfterInit, &now))
		_CrtMemDumpStatistics(&diff);
	else
		OutputDebugString(_T("[DebugLeak] No differences since after init.\n"));

	// Restore any previous hook (if ours was active) before tearing down the CS.
	if (oldHook == &eMuleAllocHook)
		_CrtSetAllocHook(g_pfnPrevCrtAllocHook);
	else
		_CrtSetAllocHook(oldHook);
	DeleteCriticalSection(&g_allocCS);
#endif

	return CWinApp::ExitInstance();
}

#ifdef _DEBUG
int CrtDebugReportCB(int reportType, char *message, int *returnValue) noexcept
{
	FILE *fp = _tfsopen(s_szCrtDebugReportFilePath, _T("a"), _SH_DENYWR);
	if (fp) {
		time_t tNow = time(NULL);
		TCHAR szTime[40];
		_tcsftime(szTime, _countof(szTime), _T("%H:%M:%S"), localtime(&tNow));
		_ftprintf(fp, _T("%ls  %i  %hs"), szTime, reportType, message);
		fclose(fp);
	}
	*returnValue = 0; // avoid invocation of 'AfxDebugBreak' in ASSERT macros
	return TRUE; // avoid further processing of this debug report message by the CRT
}

// allocation hook - for memory statistics gathering
int eMuleAllocHook(int mode, void *pUserData, size_t nSize, int nBlockUse, long lRequest, const unsigned char *pszFileName, int nLine) noexcept
{
#ifdef DEBUGLEAKHELPER
	// Break on selected allocation IDs (parsed from env/log)
	if (mode == _HOOK_ALLOC && DebugLeakHelper::ShouldBreakAlloc(lRequest)) {
	#ifdef _MSC_VER
			__debugbreak();
	#else
			DebugBreak();
	#endif
	}
#endif

	// Normalize NULL filenames to a stable, non-null key.
	const unsigned char* key = pszFileName ? pszFileName : reinterpret_cast<const unsigned char*>("<nullptr>");

#ifdef DEBUGLEAKHELPER
	EnterCriticalSection(&g_allocCS); // Protect the map itself
#endif

	UINT count = 0;
	g_allocations.Lookup(key, count);

	// Avoid re-entrancy: touching CMap may allocate; disable hook briefly.
	_CRT_ALLOC_HOOK old = _CrtSetAllocHook(NULL);
	if (mode == _HOOK_ALLOC)
		g_allocations[key] = count + 1;
	else if (mode == _HOOK_FREE)
		g_allocations[key] = (count > 0) ? (count - 1) : 0;
#ifdef DEBUGLEAKHELPER
	DebugLeakHelper::TrackAllocHookEvent(mode, pUserData, nSize, nBlockUse, lRequest, pszFileName, nLine);
#endif
	_CrtSetAllocHook(old);

#ifdef DEBUGLEAKHELPER
	LeaveCriticalSection(&g_allocCS);
#endif

	// Be robust if there was no previous hook installed.
	if (g_pfnPrevCrtAllocHook)
		return g_pfnPrevCrtAllocHook(mode, pUserData, nSize, nBlockUse, lRequest, pszFileName, nLine);
	return 1; // default: allow allocation/free to proceed
}
#endif

bool CemuleApp::ProcessCommandline()
{
	int nIgnoreInstanceProfile = GetProfileInt(_T("eMule"), _T("IgnoreInstance"), 0);
	bool bIgnoreRunningInstances = (nIgnoreInstanceProfile != 0);
	bIgnoreRunningInstances |= CPreferences::bIgnoreInstances;
	TRACE(_T("ProcessCommandline: IgnoreInstance profile=%d prefs=%d\n"), nIgnoreInstanceProfile, CPreferences::bIgnoreInstances ? 1 : 0);
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
				TRACE(_T("ProcessCommandline: IgnoreInstance enabled by command line.\n"));
			}

			m_bAutoStart |= (_tcsicmp(pszParam, _T("AutoStart")) == 0);
		}
	}

	CCommandLineInfo cmdInfo;
	ParseCommandLine(cmdInfo);
	TRACE(_T("ProcessCommandline: cmd=%d autostart=%d ignore=%d\n"), cmdInfo.m_nShellCommand, m_bAutoStart ? 1 : 0, bIgnoreRunningInstances ? 1 : 0);

	// If we create our TCP listen socket with SO_REUSEADDR, we have to ensure that there are
	// no 2 eMules are running on the same port.
	// NOTE: This will not prevent from some other application using that port!
	UINT uTcpPort = GetProfileInt(_T("eMule"), _T("Port"), 0);
	DWORD dwMutexErr = ERROR_SUCCESS;
	int nMutexAttempts = 0;
	if (uTcpPort == 0) {
		static const int kMaxPortTries = 16;
		for (int i = 0; i < kMaxPortTries; ++i) {
			uTcpPort = CPreferences::GetRandomTCPPort();
			CString strMutextName;
			strMutextName.Format(_T("%s:%u"), EMULE_GUID, uTcpPort);
			m_hMutexOneInstance = CreateMutex(NULL, FALSE, strMutextName);
			dwMutexErr = GetLastError();
			++nMutexAttempts;
			if (dwMutexErr != ERROR_ALREADY_EXISTS && dwMutexErr != ERROR_ACCESS_DENIED)
				break;
			if (m_hMutexOneInstance != NULL) {
				::CloseHandle(m_hMutexOneInstance);
				m_hMutexOneInstance = NULL;
			}
		}
		CPreferences::SetStartupTcpPortOverride(static_cast<uint16>(uTcpPort));
	} else {
		CString strMutextName;
		strMutextName.Format(_T("%s:%u"), EMULE_GUID, uTcpPort);
		m_hMutexOneInstance = CreateMutex(NULL, FALSE, strMutextName);
		dwMutexErr = GetLastError();
		nMutexAttempts = 1;
	}
	TRACE(_T("ProcessCommandline: mutex port=%u attempts=%d err=%lu\n"), uTcpPort, nMutexAttempts, dwMutexErr);

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
			TRACE(_T("ProcessCommandline: existing instance detected, hwnd=0x%p\n"), maininst);
		}

	if (cmdInfo.m_nShellCommand == CCommandLineInfo::FileOpen) {
		if (command.Find(_T("://")) > 0 || command.Find(_T("magnet:?")) >= 0) {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_ED2KLINK;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
				if (maininst) {
					SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
					TRACE(_T("ProcessCommandline: forwarded ed2k link to existing instance.\n"));
					return true;
				}

			m_strPendingLink = command;
		} else if (CCollection::HasCollectionExtention(command)) {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_COLLECTION;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
				if (maininst) {
					SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
					TRACE(_T("ProcessCommandline: forwarded collection to existing instance.\n"));
					return true;
				}

			m_strPendingLink = command;
		} else {
			sendstruct.cbData = static_cast<DWORD>((command.GetLength() + 1) * sizeof(TCHAR));
			sendstruct.dwData = OP_CLCOMMAND;
			sendstruct.lpData = const_cast<LPTSTR>((LPCTSTR)command);
				if (maininst) {
					SendMessage(maininst, WM_COPYDATA, (WPARAM)0, (LPARAM)(PCOPYDATASTRUCT)&sendstruct);
					TRACE(_T("ProcessCommandline: forwarded command to existing instance.\n"));
					return true;
				}
				// Don't start if we were invoked with 'exit' command.
				if (command.CompareNoCase(_T("exit")) == 0)
					return true;
		}
	}
	TRACE(_T("ProcessCommandline: returning %d (maininst=%d already=%d)\n"), (maininst || bAlreadyRunning) ? 1 : 0, maininst ? 1 : 0, bAlreadyRunning ? 1 : 0);
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

	// The 'onlinesig.dat' is potentially read by other applications at more or less frequent intervals.
	//	 -	Set the file sharing mode to allow other processes to read the file while we are writing
	//		it (see also next point).
	//	 -	Try to write the hole file data at once, so other applications are always reading
	//		a consistent amount of file data. C-RTL uses a 4 KB buffer, this is large enough to write
	//		those 2 lines into the onlinesig.dat file with one IO operation.
	//	 -	Although this file is a text file, we set the file mode to 'binary' because of backward
	//		compatibility with older eMule versions.
	CSafeBufferedFile file;
	CFileException fex;
	if (!file.Open(strSigPath, CFile::modeCreate | CFile::modeWrite | CFile::shareDenyWrite | CFile::typeBinary, &fex)) {
		LogError(LOG_STATUSBAR, _T("%s %s%s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), _szFileName, (LPCTSTR)CExceptionStrDash(fex));
		return;
	}

	try {
		char buffer[20];
		CStringA strBuff;
		if (IsConnected()) {
			file.Write("1|", 2);
			if (serverconnect->IsConnected())
				strBuff = serverconnect->GetCurrentServer()->GetListName();
			else
				strBuff = "Kademlia";
			file.Write(strBuff, strBuff.GetLength());

			file.Write("|", 1);
			if (serverconnect->IsConnected())
				strBuff = serverconnect->GetCurrentServer()->GetAddress();
			else
				strBuff = "0.0.0.0";
			file.Write(strBuff, strBuff.GetLength());

			file.Write("|", 1);
			if (serverconnect->IsConnected()) {
				_itoa(serverconnect->GetCurrentServer()->GetPort(), buffer, 10);
				file.Write(buffer, (UINT)strlen(buffer));
			} else
				file.Write("0", 1);
		} else
			file.Write("0", 1);
		file.Write("\n", 1);

		snprintf(buffer, _countof(buffer), "%.1f", (float)downloadqueue->GetDatarate() / 1024);
		file.Write(buffer, (UINT)strlen(buffer));
		file.Write("|", 1);

		snprintf(buffer, _countof(buffer), "%.1f", (float)uploadqueue->GetDatarate() / 1024);
		file.Write(buffer, (UINT)strlen(buffer));
		file.Write("|", 1);

		_itoa((int)uploadqueue->GetWaitingUserCount(), buffer, 10);
		file.Write(buffer, (UINT)strlen(buffer));

		file.Close();
	} catch (CFileException *ex) {
		LogError(LOG_STATUSBAR, _T("%s %s%s"), (LPCTSTR)GetResString(_T("ERROR_SAVEFILE")), _szFileName, (LPCTSTR)CExceptionStrDash(*ex));
		ex->Delete();
	}
} //End Added By Bouc7

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
	if (dwIP) {
		ASSERT(!::IsLowID(dwIP));

		if (m_dwPublicIP && Kademlia::CKademlia::IsConnected() && Kademlia::CKademlia::GetPrefs()->GetIPAddress() && htonl(Kademlia::CKademlia::GetIPAddress()) != dwIP) {
			AddDebugLogLine(DLP_DEFAULT, false, _T("Public IPv4 Address reported by Kademlia (%s) differs from new-found (%s)"), (LPCTSTR)ipstr(htonl(Kademlia::CKademlia::GetIPAddress())), (LPCTSTR)ipstr(dwIP));
			clientlist->ClearAllServedBuddies(); // Clear served-buddy pointers before Kad restart.
			Kademlia::CKademlia::Stop();
			Kademlia::CKademlia::Start();
			//Kad loaded the old IP, we must reset
			if (Kademlia::CKademlia::IsRunning())
				Kademlia::CKademlia::GetPrefs()->SetIPAddress(htonl(dwIP));
		}
	}

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
				// scheme 'Windows-Standard (extragro�)' -> always try to use 'LoadImage'!
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
		if (hIcon == NULL) {
			//TODO: Either do not use that function or copy the returned icon. All the calling code is designed
			// in a way that the icons returned by this function are to be freed with 'DestroyIcon'. But an
			// icon which was loaded with 'LoadIcon', is not be freed with 'DestroyIcon'.
			// Right now, we never come here...
			hIcon = CWinApp::LoadIcon(lpszResourceName);

			if (hIcon == NULL) {
				TRACE("Missing icon resource: %ls\n", lpszResourceName);
				ASSERT(!"Missing icon resource");
			}
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
	AfxGetMainWnd()->SendMessage(WM_SYSCOLORCHANGE);
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

void CemuleApp::AddEd2kLinksToDownload(const CString &strLinks, int cat)
{
	for (int iPos = 0; iPos >= 0;) {
		const CString &sToken(strLinks.Tokenize(_T(" \t\r\n"), iPos)); //tokenize by whitespace
		if (sToken.IsEmpty())
			break;
		bool bSlash = (sToken[sToken.GetLength() - 1] == _T('/'));
		CED2KLink *pLink = NULL;
		try {
			pLink = CED2KLink::CreateLinkFromUrl(bSlash ? sToken : sToken + _T('/'));
			if (pLink) {
				if (pLink->GetKind() != CED2KLink::kFile)
					throwCStr(_T("bad link"));
				downloadqueue->AddFileLinkToDownload(*pLink->GetFileLink(), cat);
				delete pLink;
				pLink = NULL;
			}
		} catch (const CString &error) {
			delete pLink;
			CString sBuffer;
			sBuffer.Format(GetResString(_T("ERR_INVALIDLINK")), (LPCTSTR)error);
			LogError(LOG_STATUSBAR, GetResString(_T("ERR_LINKERROR")), (LPCTSTR)sBuffer);
			return;
		}
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
		if (CDarkMode::MessageBox(strLinksDisplay, MB_YESNO | MB_TOPMOST) == IDYES)
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
				theApp.m_app_state = APP_STATE_SHUTTINGDOWN; // as a last attempt
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

	if (pBackupThread != NULL) {
		DWORD lpExitCode;
		GetExitCodeThread(pBackupThread->m_hThread, &lpExitCode);
		if (lpExitCode == STILL_ACTIVE) {
			AddLogLine(true, GetResString(_T("BACKUP_IN_PROGRESS")));
			return;
		}
	}

	if (bOnExit)
		BackupMain(); // We'll backup on main thread during exit to make it synchronized with the shutdown process.
	else
		pBackupThread = AfxBeginThread(RunProc, (LPVOID)this, THREAD_PRIORITY_IDLE); // Initialize a threaded backup.
}

UINT AFX_CDECL CemuleApp::RunProc(LPVOID pParam)
{
	if (theApp.IsClosing())
		return 1;

	theApp.QueueLogLine(true, GetResString(_T("BACKUP_STARTED")));
	DbgSetThreadName("Backup");
	theApp.BackupMain();

	return 0;
}

void CemuleApp::BackupMain()
{
	bool error = false;

	try {
		LPCTSTR extensionsToBack[] = { _T("*.ini"), _T("*.dat"), _T("*.met"), _T("*.conf"), _T("*.bak"), _T("downloads.txt") };
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
			if (!::CreateDirectory(PreparePathForWin32LongPath(backupBase), NULL)) {
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
		if (!::CreateDirectory(PreparePathForWin32LongPath(newBackupDir), NULL)) {
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
    CString szDirPath = CString(thePrefs.GetMuleDirectory(EMULE_CONFIGDIR)) + "Backup\\";
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
	BB_PTP_IO tpIo;
	OVERLAPPED ov;
	HANDLE hEvent;            // Manual-reset event bound to overlapped
	BYTE* pBuf;
	DWORD cbBuf;
	CString rootPath;
	volatile LONG nCallbacks; // Number of in-flight I/O callbacks (atomic)
	volatile LONG nArmed;     // 1 if an overlapped ReadDirectoryChangesW is armed
	TPIODirWatch() : hDir(INVALID_HANDLE_VALUE), tpIo(NULL), hEvent(NULL), pBuf(NULL), cbBuf(0), nCallbacks(0), nArmed(0) { ZeroMemory(&ov, sizeof(ov)); }
};

// Globals for watcher state (file scope, not exposed)
static CRITICAL_SECTION g_tpNewSharedDirsCS;
static CRITICAL_SECTION g_tpCleanupCS;
static std::vector<TPIODirWatch*> g_tpWatches;
static std::vector<TPIODirWatch*> g_tpCleanup; // Deferred cleanup queue
static std::vector<CString> g_tpNewSharedDirs;
static volatile LONG g_tpTimerArmed = 0;
static volatile LONG g_tpStopping = 0;
static volatile LONG g_tpForceTreeReload = 0; // Set to 1 by I/O cb when directory-level change requires tree rebuild
static volatile LONG g_tpCleanupTimerArmed = 0;
static volatile LONG g_tpRefreshPending = 0;
static volatile LONG g_tpCleanupPending = 0;
static bool g_tpNewSharedDirsCSInit = false;
static bool g_tpCleanupCSInit = false;
static DWORD g_dwRefreshDueAt = 0;
static DWORD g_dwCleanupDueAt = 0;

// Deleted paths are collected on the TP I/O callback thread and coalesced. GUI thread can (now or later) consume this list to proactively prune waiters.
static CRITICAL_SECTION g_tpDelCS;
static bool g_tpCsInit = false;
static std::vector<CString> g_tpDeleted;

// Root set change detector (lightweight): hash + periodic TP timer.
static volatile LONG g_tpRebuildingRoots = 0;
static volatile DWORD g_tpRootsHash = 0;
static volatile LONG g_tpSuppressRootPost = 0; // Suppress next root-change post after UI-driven rebuild

// Forward declerations
static VOID CALLBACK DirWatchRootsTimerCb(PVOID /*inst*/, PVOID /*ctx*/, PVOID /*timer*/);
static VOID CALLBACK DirWatchTimerCb(PVOID /*instance*/, PVOID /*context*/, PVOID /*timer*/);
static DWORD BB_ComputeWatchRootsHash();

static __forceinline void BB_HashFeedCi(DWORD& h, const CString& s)
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

static DWORD BB_ComputeWatchRootsHash()
{
	std::vector<CString> roots;

	// Incoming
	CString cur = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
	if (!cur.IsEmpty())
		roots.push_back(cur);

	// Categories
	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		const CString& p = thePrefs.GetCatPath(i);
		if (!p.IsEmpty()) roots.push_back(p);
	}

	// Shared directories
	POSITION pos = thePrefs.shareddir_list.GetHeadPosition();
	while (pos) {
		CString p = thePrefs.shareddir_list.GetNext(pos);
		if (!p.IsEmpty()) roots.push_back(p);
	}

	// Hash
	DWORD h = 0;
	for (size_t i = 0; i < roots.size(); ++i) {
		CString norm = roots[i];
		// Normalize trailing backslash
		if (!norm.IsEmpty() && norm[norm.GetLength() - 1] == _T('\\'))
			norm = norm.Left(norm.GetLength() - 1);

		BB_HashFeedCi(h, norm);
	}

	return h;
}

static VOID CALLBACK DirWatchRootsTimerCb(PVOID /*inst*/, PVOID /*ctx*/, PVOID /*timer*/)
{
	if (InterlockedCompareExchange(&g_tpStopping, 0, 0) != 0)
		return; // App is stopping; ignore

	const DWORD cur = BB_ComputeWatchRootsHash();
	const DWORD prev = (DWORD)InterlockedCompareExchange((LONG*)&g_tpRootsHash, (LONG)g_tpRootsHash, (LONG)g_tpRootsHash);
	if (cur != prev) {
		if (InterlockedExchange(&g_tpSuppressRootPost, 0) == 1) { // If we are suppressing the next root post, just update the hash and return.
			InterlockedExchange((LONG*)&g_tpRootsHash, (LONG)cur);
			return;
		}
				
		if (InterlockedExchange(&g_tpRebuildingRoots, 1) == 0) { // If we are not already rebuilding roots, start the watcher and post a message to the shared files window.
			theApp.StartDirWatchTP();
			InterlockedExchange((LONG*)&g_tpRootsHash, (LONG)cur);

			if (CemuleDlg* pDlg = theApp.emuledlg) {
				if (pDlg->sharedfileswnd && ::IsWindow(pDlg->sharedfileswnd->m_hWnd))
					::PostMessage(pDlg->sharedfileswnd->m_hWnd, UM_AUTO_RELOAD_SHARED_FILES, 1, 0);
			}

			InterlockedExchange(&g_tpRebuildingRoots, 0);
		}
	}
}

static __forceinline void BB_PushDeletedPath(const CString& full)
{
	if (!g_tpCsInit)
		return;

	EnterCriticalSection(&g_tpDelCS);
	g_tpDeleted.push_back(full);
	LeaveCriticalSection(&g_tpDelCS);
}

// Parse FILE_NOTIFY_INFORMATION buffer and collect deleted/renamed-old paths.
static void BB_CollectDeletedFromBuffer(struct TPIODirWatch* pW, DWORD bytes)
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

	// Heuristic 1: If we saw multiple removals under the same prefix, a directory likely vanished.
	bool dirLikely = false;
	for (size_t i = 0; !dirLikely && i < batch.size(); ++i) {
		const CString& a = batch[i];
		const int la = a.GetLength();
		for (size_t j = 0; j < batch.size(); ++j) {
			if (i == j)
				continue;

			const CString& b = batch[j];
			if (b.GetLength() > la && _tcsnicmp(b, a, la) == 0 && b[la] == _T('\\')) {
				dirLikely = true;
				break;
			}
		}
	}

	// Heuristic 2: No dot after the last backslash (common directory naming).
	if (!dirLikely) {
		for (size_t i = 0; i < batch.size(); ++i) {
			const CString& s = batch[i];
			const int slash = s.ReverseFind(_T('\\'));
			const int dot = s.ReverseFind(_T('.'));
			if (slash >= 0 && (dot < 0 || dot < slash)) {
				dirLikely = true;
				break;
			}
		}
	}

	// Promote to forced tree reload if a directory was (very likely) removed.
	if (dirLikely)
		InterlockedExchange(&g_tpForceTreeReload, 1);
}

static VOID CALLBACK DirWatchTimerCb(PVOID /*instance*/, PVOID /*context*/, PVOID /*timer*/)
{
	// Coalesced GUI refresh after FS change; promote to forced tree reload if a dir-level change was seen.
	InterlockedExchange(&g_tpTimerArmed, 0);
	CemuleDlg* pDlg = theApp.emuledlg;

	if (pDlg != NULL && pDlg->sharedfileswnd != NULL && ::IsWindow(pDlg->sharedfileswnd->m_hWnd)) {
		WPARAM wp = (InterlockedExchange(&g_tpForceTreeReload, 0) != 0) ? 2 : 0; // 2 => force tree rebuild
		::PostMessage(pDlg->sharedfileswnd->m_hWnd, UM_AUTO_RELOAD_SHARED_FILES, wp, 0);
	}
}

static bool RearmWatch(TPIODirWatch* pW)
{
	if (!pW)
		return false;
	ZeroMemory(&pW->ov, sizeof(pW->ov));

	if (pW->hEvent == NULL) 
		pW->hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL); // Manual-reset, non-signaled

	pW->ov.hEvent = pW->hEvent; // Bind overlapped to event for deterministic shutdown wait
	DWORD dwBytes = 0;
	BB_StartThreadpoolIo(pW->tpIo);
	const DWORD notifyMask = FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME | FILE_NOTIFY_CHANGE_ATTRIBUTES | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_CREATION;
	BOOL ok = ::ReadDirectoryChangesW(pW->hDir, pW->pBuf, pW->cbBuf, TRUE, notifyMask, &dwBytes, &pW->ov, NULL);
	
	if (!ok) {
		BB_CancelThreadpoolIo(pW->tpIo);
		return false; 
	}

	InterlockedExchange(&pW->nArmed, 1);
	return true;
}


void CemuleApp::SyncDirWatchRootsHash()
{
	// UI thread: rebuild roots hash and post a message to the shared files window.
	InterlockedExchange(&g_tpSuppressRootPost, 1);
	const DWORD cur = BB_ComputeWatchRootsHash();
	InterlockedExchange((LONG*)&g_tpRootsHash, (LONG)cur);
}

// Drains newly created subdirs (AutoShareSubdirs) and appends to thePrefs.shared list.
void CemuleApp::DrainAutoSharedNewDirs()
{
	if (!g_tpNewSharedDirsCSInit)
		return;

	std::vector<CString> todo;
	EnterCriticalSection(&g_tpNewSharedDirsCS);
	todo.swap(g_tpNewSharedDirs);
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

		bool present = false;
		for (POSITION pos = thePrefs.shareddir_list.GetHeadPosition(); pos != NULL; ) {
			const CString& cur = thePrefs.shareddir_list.GetNext(pos);
			if (cur.CompareNoCase(s) == 0) { present = true; break; }
		}

		if (!present && thePrefs.IsShareableDirectory(s)) {
			thePrefs.shareddir_list.AddTail(s);
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

	if (InterlockedCompareExchange(&g_tpStopping, 0, 0) != 0)
		return; // Stopping, do not rearm

	if (ioResult == ERROR_OPERATION_ABORTED)
		return; // Canceled during shutdown

	// Use the byte count from I/O completion to safely walk the buffer.
	const DWORD cbData = (bytes < (ULONG_PTR)pW->cbBuf) ? (DWORD)bytes : pW->cbBuf;

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

					if (thePrefs.GetAutoShareSubdirs() && g_tpNewSharedDirsCSInit) {
						if (full.Right(1) != _T("\\"))
							full += _T("\\");

						EnterCriticalSection(&g_tpNewSharedDirsCS);
						g_tpNewSharedDirs.push_back(full);
						LeaveCriticalSection(&g_tpNewSharedDirsCS);
					}

					break; // One is enough per batch
				}
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

	BB_CollectDeletedFromBuffer(pW, cbData);

	// Debounced GUI refresh via UploadTimer (300 ms)
	if (InterlockedCompareExchange(&g_tpStopping, 0, 0) == 0) {
		InterlockedExchange(&g_tpRefreshPending, 1);
		g_dwRefreshDueAt = ::GetTickCount() + 300;
	}

	// Rearm immediately for subsequent changes
	RearmWatch(pW);
}

void CemuleApp::StartDirWatchTP()
{
	StopDirWatchTP();
	InterlockedExchange(&g_tpStopping, 0);
	InterlockedExchange(&g_tpRefreshPending, 0);
	InterlockedExchange(&g_tpCleanupPending, 0);
	g_dwRefreshDueAt = g_dwCleanupDueAt = 0;
	InterlockedExchange(&g_tpTimerArmed, 0);

	// Init per-run queue for auto-added subdirs (if feature enabled)
	if (!g_tpNewSharedDirsCSInit) {
		InitializeCriticalSection(&g_tpNewSharedDirsCS);
		g_tpNewSharedDirsCSInit = true;
	}

	if (g_tpNewSharedDirsCSInit) {
		EnterCriticalSection(&g_tpNewSharedDirsCS);
		g_tpNewSharedDirs.clear();
		LeaveCriticalSection(&g_tpNewSharedDirsCS);
	}

	// Build unique root list (incoming, categories, shared dirs)
	std::vector<CString> roots;
	CString cur;

	// Incoming
	cur = thePrefs.GetMuleDirectory(EMULE_INCOMINGDIR);
	if (!cur.IsEmpty())
		roots.push_back(cur);

	// Categories
	for (INT_PTR i = 0; i < thePrefs.GetCatCount(); ++i) {
		const CString& p = thePrefs.GetCatPath(i);
		bool dup = false;

		for (size_t k = 0; k < roots.size(); ++k) 
			if (roots[k].CompareNoCase(p) == 0) {
				dup = true;
				break;
			}

		if (!dup && !p.IsEmpty()) 
			roots.push_back(p);
	}

	// Shared directories list
	POSITION pos = thePrefs.shareddir_list.GetHeadPosition();
	while (pos) {
		CString p = thePrefs.shareddir_list.GetNext(pos);
		bool dup = false;
		for (size_t k = 0; k < roots.size(); ++k)
			if (roots[k].CompareNoCase(p) == 0) {
				dup = true;
				break;
			}

		if (!dup && !p.IsEmpty())
			roots.push_back(p);
	}

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
			::CloseHandle(h);
			delete pW;
			continue;
		}

		// Create TP I/O and hEvent, then rearm
		pW->tpIo = BB_CreateThreadpoolIo(pW->hDir, DirWatchIoCb, pW, NULL);
		if (pW->tpIo == NULL) {
			free(pW->pBuf);
			::CloseHandle(h);
			delete pW;
			continue;
		}

		if (!RearmWatch(pW)) {
			// Runtime failure while arming: close TP I/O for this watch (not exiting)
			BB_CloseThreadpoolIoRealX(&pW->tpIo);  
			free(pW->pBuf);
			if (pW->hEvent)
				::CloseHandle(pW->hEvent);
			::CloseHandle(h);
			delete pW;
			continue;
		}

		g_tpWatches.push_back(pW);
	}

	if (!g_tpWatches.empty())
		TRACE2(_T("Shared Files Watcher (TP I/O): %u roots armed\n"), (UINT)g_tpWatches.size());
}

void CemuleApp::StopDirWatchTP()
{
	InterlockedExchange(&g_tpStopping, 1);
	InterlockedExchange(&g_tpRefreshPending, 0);
	InterlockedExchange(&g_tpCleanupPending, 0);
	InterlockedExchange(&g_tpTimerArmed, 0);

	// Ensure cleanup CS is initialized even if StartDirWatchTP has not created it yet.
	if (!g_tpCleanupCSInit) {
		InitializeCriticalSection(&g_tpCleanupCS);
		g_tpCleanupCSInit = true;
	}

	// Drop any deferred cleanup items to avoid dangling pointers during shutdown.
	if (g_tpCleanupCSInit) {
		EnterCriticalSection(&g_tpCleanupCS);
		g_tpCleanup.clear();
		LeaveCriticalSection(&g_tpCleanupCS);
	}

	// Synchronous, deterministic teardown of all watches to avoid post-snapshot leaks.
	for (size_t i = 0; i < g_tpWatches.size(); ++i) {
		TPIODirWatch* pW = g_tpWatches[i];
		if (!pW)
			continue;

		// Cancel outstanding I/O and drain callbacks deterministically.
		// Order: ensure event -> CancelThreadpoolIo -> CancelIoEx -> wait event (short) -> WaitForThreadpoolIoCallbacks(FALSE) -> CloseThreadpoolIo.
		if (pW->hEvent == NULL)
			pW->hEvent = ::CreateEvent(NULL, TRUE, FALSE, NULL);

		if (pW->tpIo != NULL)
			BB_CancelThreadpoolIo(pW->tpIo);

		if (pW->hDir != INVALID_HANDLE_VALUE)
			BB_CancelIoEx(pW->hDir, &pW->ov);

		// Give the kernel a tiny window to signal the overlapped if it was in flight.
		if (pW->hEvent)
			::WaitForSingleObject(pW->hEvent, 50);

		if (pW->tpIo != NULL) {
			// Wait until no callback is in-flight and I/O is not armed.
			DWORD start = ::GetTickCount();
			for (;;) {
				const LONG inFlight = InterlockedCompareExchange(&pW->nCallbacks, 0, 0);
				const LONG armed = InterlockedCompareExchange(&pW->nArmed, 0, 0);
				if (inFlight == 0 && armed == 0)
					break;
				if ((LONG)(::GetTickCount() - start) > 1000) // 1s safety cap
					break;
				::Sleep(0);
			}

			// Always avoid OS CloseThreadpoolIo here to prevent sporadic INVALID_PARAMETER during teardown.
			// Runtime rebuilds are handled by deferred cleanup which calls the RealX variant outside shutdown.
			BB_CloseThreadpoolIoX(&pW->tpIo);
		}

		// Close handles and free buffers.
		BB_SafeCloseHandle(&pW->hDir);
		if (pW->hEvent) {
			::CloseHandle(pW->hEvent);
			pW->hEvent = NULL;
		}

		if (pW->pBuf) {
			free(pW->pBuf);
			pW->pBuf = NULL;
		}

		delete pW;
	}
	g_tpWatches.clear();

	// Reset per-run queue
	if (g_tpNewSharedDirsCSInit) {
		EnterCriticalSection(&g_tpNewSharedDirsCS);
		g_tpNewSharedDirs.clear();
		LeaveCriticalSection(&g_tpNewSharedDirsCS);
		DeleteCriticalSection(&g_tpNewSharedDirsCS);
		g_tpNewSharedDirsCSInit = false;
	}

	// Delete cleanup CS if we created it.
	if (g_tpCleanupCSInit) {
		DeleteCriticalSection(&g_tpCleanupCS);
		g_tpCleanupCSInit = false;
	}

	// Delete deleted-paths CS if it was initialized anywhere (defensive: initialize here on demand)
	if (g_tpCsInit) {
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
				todo.swap(g_tpCleanup);
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
						if (pW->tpIo != NULL) BB_CloseThreadpoolIoRealX(&pW->tpIo);
						BB_SafeCloseHandle(&pW->hDir);
						if (pW->pBuf) { free(pW->pBuf); pW->pBuf = NULL; }
						delete pW;
					} else
						remaining.push_back(pW);
				}

				if (!remaining.empty()) {
					if (g_tpCleanupCSInit) {
						EnterCriticalSection(&g_tpCleanupCS);
						for (size_t i = 0; i < remaining.size(); ++i) g_tpCleanup.push_back(remaining[i]);
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
	DrainAutoSharedNewDirs(); // Drain auto-added dirs (if any) without blocking UI
}

void CemuleApp::OnUploadTick_5s_DirWatch() noexcept
{
	DirWatchRootsTimerCb(NULL, NULL, NULL); // Check for root set changes and rebuild watcher if needed
}
