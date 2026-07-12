//This file is part of eMule AI
//Copyright (C)2026 eMule AI

// PCH MUST be first to avoid PCH warnings.
#include "stdafx.h"

#include "DebugLeakHelper.h"

#ifdef _DEBUG

#include <crtdbg.h>
#include <tchar.h>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdarg>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <shellapi.h>
#include <set>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <dbghelp.h>

#ifndef _countof
#define _countof(a) (sizeof(a) / sizeof((a)[0]))
#endif

// --- Global symbol requested by Emule.cpp (intentionally in global namespace). ---
CRITICAL_SECTION g_allocCS; // Initialized in DebugLeakHelper::EarlyInit()/Init()

namespace DebugLeakHelper
{
    // Snapshot taken at Init() and used as a safe stop-point for dumps.
    static _CrtMemState g_stateAtInit = {};
    static bool g_hasStateAtInit = false;

    // Break-allocation ids (supports multiple ids even though CRT breaks on one at a time).
    static std::set<unsigned long> g_breakIds;
	// Debug-only forced break id when env vars are unreliable (update as needed).
	static unsigned long g_forcedBreakAlloc = 0;
	// Optional large-allocation break (size threshold in bytes).
	static size_t g_breakLargeThreshold = 0;
	static bool g_breakLargeOnce = false;
	// Optional heap integrity check (every N allocations).
	static unsigned long g_heapCheckInterval = 0;
	static unsigned long g_heapCheckCounter = 0;
	static bool g_heapCheckInProgress = false;
	// Optional: ids to report stacks for at shutdown (from EMULE_CRT_REPORT_IDS).
	static std::set<unsigned long> g_reportIds;

	static const unsigned short kDefaultStackDepth = 12;
	static const unsigned short kMinStackDepth = 4;
	static const unsigned short kMaxStackDepth = 32;
	static const unsigned short kStackSkipFrames = 4;
	static const DWORD kMaxSymbolNameLen = 256;
	static const unsigned long kPreInitTraceCap = 1024;

	static bool LoadUserDebuggerEnvironment(CString& outEnv)
	{
		static bool s_loaded = false;
		static CString s_env;
		if (s_loaded) {
			outEnv = s_env;
			return !outEnv.IsEmpty();
		}
		s_loaded = true;
		s_env.Empty();

		TCHAR exePath[MAX_PATH] = {};
		const DWORD len = ::GetModuleFileName(nullptr, exePath, _countof(exePath));
		if (len == 0 || len >= _countof(exePath))
			return false;

		CString dir(exePath);
		const int lastSlash = dir.ReverseFind(_T('\\'));
		if (lastSlash <= 0)
			return false;
		dir = dir.Left(lastSlash);

		CString path;
		CFileStatus status = {};
		bool found = false;
		for (int depth = 0; depth < 6; ++depth) {
			path = dir + _T("\\_emule.vcxproj.user");
			if (CFile::GetStatus(path, status)) {
				found = true;
				break;
			}

			const int pos = dir.ReverseFind(_T('\\'));
			if (pos <= 0)
				break;
			dir = dir.Left(pos);
		}

		if (!found)
			return false;

		CFile file;
		if (!file.Open(path, CFile::modeRead | CFile::shareDenyNone))
			return false;

		const ULONGLONG kMaxSize = 1024ull * 1024ull;
		const ULONGLONG size = file.GetLength();
		if (size == 0 || size > kMaxSize)
			return false;

		std::string data;
		data.resize(static_cast<size_t>(size));
		const UINT readSize = file.Read(&data[0], static_cast<UINT>(data.size()));
		data.resize(readSize);

		const char* tagStart = "<LocalDebuggerEnvironment>";
		const char* tagEnd = "</LocalDebuggerEnvironment>";
		const char* p = std::strstr(data.c_str(), tagStart);
		if (!p)
			return false;
		p += std::strlen(tagStart);
		const char* q = std::strstr(p, tagEnd);
		if (!q || q <= p)
			return false;

		std::string raw(p, q - p);
		while (!raw.empty() && (raw.back() == '\r' || raw.back() == '\n' || raw.back() == ' ' || raw.back() == '\t'))
			raw.pop_back();
		size_t start = 0;
		while (start < raw.size() && (raw[start] == ' ' || raw[start] == '\t'))
			++start;
		if (start > 0)
			raw.erase(0, start);
		if (raw.empty())
			return false;

		const int wlen = ::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), nullptr, 0);
		if (wlen <= 0)
			return false;
		std::wstring wide;
		wide.resize(static_cast<size_t>(wlen));
		::MultiByteToWideChar(CP_UTF8, 0, raw.c_str(), static_cast<int>(raw.size()), &wide[0], wlen);

		s_env = wide.c_str();
		outEnv = s_env;
		TRACE(_T("[DebugLeak][EnvFallback] Loaded LocalDebuggerEnvironment from _emule.vcxproj.user.\n"));
		return !outEnv.IsEmpty();
	}

	static bool TryGetEnvValue(LPCTSTR pszName, CString& outValue)
	{
		TCHAR szValue[512] = {};
		DWORD n = ::GetEnvironmentVariable(pszName, szValue, _countof(szValue));
		if (n > 0 && n < _countof(szValue)) {
			outValue = szValue;
			return true;
		}

		CString envString;
		if (!LoadUserDebuggerEnvironment(envString))
			return false;

		int pos = 0;
		CString token = envString.Tokenize(_T(";"), pos);
		while (!token.IsEmpty()) {
			token.Trim();
			const int eq = token.Find(_T('='));
			if (eq > 0) {
				CString key = token.Left(eq);
				key.Trim();
				if (key.CompareNoCase(pszName) == 0) {
					outValue = token.Mid(eq + 1);
					outValue.Trim();
					return !outValue.IsEmpty();
				}
			}
			token = envString.Tokenize(_T(";"), pos);
		}
		return false;
	}

	struct RequestTrace
	{
		const unsigned char* pszFileName = nullptr;
		int nLine = 0;
		size_t nSize = 0;
		unsigned short nBlockUse = 0;
		void* pCaller = nullptr;
		void* Stack[kMaxStackDepth] = {};
		unsigned short nStackDepth = 0;
		bool bValid = false;
	};

	struct LargeAllocInfo
	{
		bool bValid = false;
		unsigned long nRequest = 0;
		size_t nSize = 0;
		unsigned short nBlockUse = 0;
		const unsigned char* pszFileName = nullptr;
		int nLine = 0;
		void* pCaller = nullptr;
		void* Stack[kMaxStackDepth] = {};
		unsigned short nStackDepth = 0;
	};

	struct ResolveKey
	{
		const unsigned char* pszFileName = nullptr;
		int nLine = 0;
		void* pCaller = nullptr;
		void* pStack0 = nullptr;
		void* pStack1 = nullptr;
		void* pStack2 = nullptr;
	};

	struct ResolveKeyHash
	{
		size_t operator()(const ResolveKey& key) const
		{
			size_t h = reinterpret_cast<size_t>(key.pszFileName);
			h ^= (reinterpret_cast<size_t>(key.pCaller) + (h << 6) + (h >> 2));
			h ^= (reinterpret_cast<size_t>(key.pStack0) + (h << 6) + (h >> 2));
			h ^= (reinterpret_cast<size_t>(key.pStack1) + (h << 6) + (h >> 2));
			h ^= (reinterpret_cast<size_t>(key.pStack2) + (h << 6) + (h >> 2));
			h ^= (static_cast<size_t>(static_cast<unsigned int>(key.nLine)) + (h << 6) + (h >> 2));
			return h;
		}
	};

	struct ResolveKeyEq
	{
		bool operator()(const ResolveKey& a, const ResolveKey& b) const
		{
			return a.pszFileName == b.pszFileName
				&& a.nLine == b.nLine
				&& a.pCaller == b.pCaller
				&& a.pStack0 == b.pStack0
				&& a.pStack1 == b.pStack1
				&& a.pStack2 == b.pStack2;
		}
	};

	struct ResolveInfo
	{
		size_t nCount = 0;
		size_t nBytes = 0;
		unsigned long nFirstId = 0;
	};

	// Intentionally leaked to keep tracking data alive for atexit leak dumps.
	static std::vector<RequestTrace>* g_requestTrace = nullptr;
	static RequestTrace g_preInitTrace[kPreInitTraceCap] = {};
	static bool g_bTrackAllocs = true;
	static bool g_bCaptureCaller = false;
	static bool g_bCaptureStack = false;
	static bool g_bEmitLeakStacks = false;
	static bool g_bEmitResolveStacks = false;
	static bool g_bDumpAllocs = false;
	static bool g_bEarlyInitDone = false;
	static bool g_bInitDone = false;
	static bool g_bDumpRegistered = false;
	static bool g_bDumped = false;
	static const size_t kMaxPreDumpHooks = 8;
	static PreDumpHook g_preDumpHooks[kMaxPreDumpHooks] = {};
	static size_t g_preDumpHookCount = 0;
	static unsigned short g_nStackDepth = kDefaultStackDepth;
	static unsigned long g_nTrackRequestLimit = 2000000;
	static unsigned long g_nLeakStackLimit = 64;
	static unsigned long g_nResolveStackLimit = 8;
	static unsigned long g_nFirstTrackedRequest = 0;
	static unsigned long g_nMaxTrackedRequest = 0;
	static size_t g_nDroppedTraceRecords = 0;
	static volatile LONG g_bLeakDumpInProgress = 0;
	static bool g_bGlobalReportHookInstalled = false;
	static LargeAllocInfo g_largeAlloc = {};
	static const size_t kLargeAllocThreshold = 50ull * 1024ull * 1024ull;

	static bool IsExitCommandInvocation()
	{
		int argc = 0;
		LPWSTR* argv = ::CommandLineToArgvW(::GetCommandLineW(), &argc);
		if (argv == NULL)
			return false;

		bool bIsExitCommand = false;
		if (argc >= 2 && _wcsicmp(argv[1], L"exit") == 0)
			bIsExitCommand = true;

		::LocalFree(argv);
		return bIsExitCommand;
	}

	static bool ShouldSkipLeakDumpRegistration()
	{
		CString value;
		if (TryGetEnvValue(_T("EMULE_CRT_SKIP_DUMP"), value)) {
			value.Trim();
			if (!value.IsEmpty() && value[0] != _T('0'))
				return true;
		}

		return IsExitCommandInvocation();
	}

	static void ReportDebugLeakLineV(LPCTSTR pszFormat, va_list args)
	{
		CString strLine;
		strLine.FormatV(pszFormat, args);
		if (strLine.IsEmpty())
			return;
		if (strLine[strLine.GetLength() - 1] != _T('\n'))
			strLine += _T("\n");

		TRACE(_T("%s"), static_cast<LPCTSTR>(strLine));
	#ifdef _UNICODE
		_CrtDbgReportW(_CRT_WARN, NULL, 0, NULL, L"%ls", static_cast<LPCWSTR>(strLine));
	#else
		_CrtDbgReport(_CRT_WARN, NULL, 0, NULL, "%s", static_cast<LPCSTR>(strLine));
	#endif
	}

	static void ReportDebugLeakLine(LPCTSTR pszFormat, ...)
	{
		va_list args;
		va_start(args, pszFormat);
		ReportDebugLeakLineV(pszFormat, args);
		va_end(args);
	}

	static void MergePreInitTraces()
	{
		if (!g_requestTrace)
			return;

		unsigned long maxRequestId = 0;
		for (unsigned long requestId = 1; requestId < kPreInitTraceCap; ++requestId) {
			if (g_preInitTrace[requestId].bValid)
				maxRequestId = requestId;
		}

		if (maxRequestId == 0)
			return;

		if (maxRequestId >= g_requestTrace->size())
			g_requestTrace->resize(static_cast<size_t>(maxRequestId) + 1u);

		for (unsigned long requestId = 1; requestId <= maxRequestId; ++requestId) {
			if (g_preInitTrace[requestId].bValid)
				(*g_requestTrace)[requestId] = g_preInitTrace[requestId];
		}
	}

#ifdef _UNICODE
	static wchar_t* g_dumpBufW = nullptr;
	static std::size_t g_dumpBufWLen = 0;
	static const std::size_t kDumpBufWCap = 1024u * 1024u; // characters
#else
	static char* g_dumpBufA = nullptr;
	static std::size_t g_dumpBufALen = 0;
	static const std::size_t kDumpBufACap = 1024u * 1024u; // bytes
#endif

	typedef USHORT(WINAPI* PRtlCaptureStackBackTraceFn)(ULONG, ULONG, PVOID*, PULONG);
	typedef BOOL(WINAPI* PSymInitialize)(HANDLE, PCSTR, BOOL);
	typedef BOOL(WINAPI* PSymCleanup)(HANDLE);
	typedef BOOL(WINAPI* PSymFromAddr)(HANDLE, DWORD64, PDWORD64, PSYMBOL_INFO);
	typedef BOOL(WINAPI* PSymGetLineFromAddr64)(HANDLE, DWORD64, PDWORD, PIMAGEHLP_LINE64);
	typedef DWORD(WINAPI* PSymSetOptions)(DWORD);

	static void* CaptureCallerAddress()
	{
		static PRtlCaptureStackBackTraceFn pCapture = reinterpret_cast<PRtlCaptureStackBackTraceFn>(::GetProcAddress(::GetModuleHandle(_T("kernel32.dll")), "RtlCaptureStackBackTrace"));
		if (!pCapture)
			return nullptr;

		void* frame = nullptr;
		if (pCapture(3, 1, &frame, NULL) == 0)
			return nullptr;

		return frame;
	}

	static unsigned short CaptureStackFrames(unsigned long skip, unsigned long maxFrames, void** frames)
	{
		static PRtlCaptureStackBackTraceFn pCapture = reinterpret_cast<PRtlCaptureStackBackTraceFn>(::GetProcAddress(::GetModuleHandle(_T("kernel32.dll")), "RtlCaptureStackBackTrace"));
		if (!pCapture || !frames || maxFrames == 0)
			return 0;

		const ULONG nFrames = static_cast<ULONG>(maxFrames);
		return pCapture(static_cast<ULONG>(skip), nFrames, frames, NULL);
	}

	static void StoreTraceRecord(RequestTrace& rec, size_t nSize, unsigned short blockType, const unsigned char* pszFileName, int nLine)
	{
		rec.pszFileName = pszFileName;
		rec.nLine = nLine;
		rec.nSize = nSize;
		rec.nBlockUse = blockType;
		rec.pCaller = g_bCaptureCaller ? CaptureCallerAddress() : nullptr;
		rec.nStackDepth = 0;
		if (g_bCaptureStack)
			rec.nStackDepth = CaptureStackFrames(kStackSkipFrames, g_nStackDepth, rec.Stack);
		rec.bValid = true;
	}

	struct DbgHelpState
	{
		HMODULE hDbgHelp = NULL;
		PSymInitialize pSymInitialize = nullptr;
		PSymCleanup pSymCleanup = nullptr;
		PSymFromAddr pSymFromAddr = nullptr;
		PSymGetLineFromAddr64 pSymGetLineFromAddr64 = nullptr;
		PSymSetOptions pSymSetOptions = nullptr;
		BOOL bReady = FALSE;
		BOOL bFailed = FALSE;
		BOOL bInitialized = FALSE;
	};

	static DbgHelpState g_dbgHelp = {};

	static BOOL EnsureDbgHelp()
	{
		if (g_dbgHelp.bInitialized)
			return g_dbgHelp.bReady;

		g_dbgHelp.bInitialized = TRUE;
		g_dbgHelp.hDbgHelp = ::LoadLibrary(_T("DBGHELP.DLL"));
		if (!g_dbgHelp.hDbgHelp) {
			g_dbgHelp.bFailed = TRUE;
			return FALSE;
		}

		g_dbgHelp.pSymInitialize = reinterpret_cast<PSymInitialize>(::GetProcAddress(g_dbgHelp.hDbgHelp, "SymInitialize"));
		g_dbgHelp.pSymCleanup = reinterpret_cast<PSymCleanup>(::GetProcAddress(g_dbgHelp.hDbgHelp, "SymCleanup"));
		g_dbgHelp.pSymFromAddr = reinterpret_cast<PSymFromAddr>(::GetProcAddress(g_dbgHelp.hDbgHelp, "SymFromAddr"));
		g_dbgHelp.pSymGetLineFromAddr64 = reinterpret_cast<PSymGetLineFromAddr64>(::GetProcAddress(g_dbgHelp.hDbgHelp, "SymGetLineFromAddr64"));
		g_dbgHelp.pSymSetOptions = reinterpret_cast<PSymSetOptions>(::GetProcAddress(g_dbgHelp.hDbgHelp, "SymSetOptions"));

		if (!g_dbgHelp.pSymInitialize || !g_dbgHelp.pSymCleanup || !g_dbgHelp.pSymFromAddr || !g_dbgHelp.pSymGetLineFromAddr64 || !g_dbgHelp.pSymSetOptions) {
			::FreeLibrary(g_dbgHelp.hDbgHelp);
			g_dbgHelp = {};
			g_dbgHelp.bFailed = TRUE;
			return FALSE;
		}

		g_dbgHelp.pSymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		if (!g_dbgHelp.pSymInitialize(::GetCurrentProcess(), NULL, TRUE)) {
			::FreeLibrary(g_dbgHelp.hDbgHelp);
			g_dbgHelp = {};
			g_dbgHelp.bFailed = TRUE;
			return FALSE;
		}

		g_dbgHelp.bReady = TRUE;
		return TRUE;
	}

	static void CleanupDbgHelp()
	{
		if (!g_dbgHelp.bReady || !g_dbgHelp.hDbgHelp)
			return;

		if (g_dbgHelp.pSymCleanup)
			g_dbgHelp.pSymCleanup(::GetCurrentProcess());
		::FreeLibrary(g_dbgHelp.hDbgHelp);
		g_dbgHelp = {};
	}

	static void RecordLargeAllocIfNeeded(int mode, size_t nSize, unsigned short blockType, long lRequest, const unsigned char* pszFileName, int nLine)
	{
		if (g_largeAlloc.bValid)
			return;
		if (mode != _HOOK_ALLOC && mode != _HOOK_REALLOC)
			return;
		if (lRequest <= 0)
			return;
		if (blockType == _IGNORE_BLOCK || blockType == _FREE_BLOCK)
			return;
		if (nSize < kLargeAllocThreshold)
			return;

		g_largeAlloc.bValid = true;
		g_largeAlloc.nRequest = static_cast<unsigned long>(lRequest);
		g_largeAlloc.nSize = nSize;
		g_largeAlloc.nBlockUse = blockType;
		g_largeAlloc.pszFileName = pszFileName;
		g_largeAlloc.nLine = nLine;
		g_largeAlloc.pCaller = g_bCaptureCaller ? CaptureCallerAddress() : nullptr;
		g_largeAlloc.nStackDepth = 0;
		if (g_bCaptureStack)
			g_largeAlloc.nStackDepth = CaptureStackFrames(kStackSkipFrames, g_nStackDepth, g_largeAlloc.Stack);
	}

	static void EmitLargeAllocInfo()
	{
		if (!g_largeAlloc.bValid)
			return;

		if (g_largeAlloc.pszFileName && g_largeAlloc.nLine > 0)
			ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] id=%lu size=%Iu block=%u file=%hs line=%d"), g_largeAlloc.nRequest, g_largeAlloc.nSize, g_largeAlloc.nBlockUse, g_largeAlloc.pszFileName, g_largeAlloc.nLine);
		else
			ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] id=%lu size=%Iu block=%u"), g_largeAlloc.nRequest, g_largeAlloc.nSize, g_largeAlloc.nBlockUse);

		if (g_largeAlloc.pCaller)
			ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] caller=0x%p"), g_largeAlloc.pCaller);

		if (!g_largeAlloc.nStackDepth) {
			if (g_bCaptureStack)
				ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] no stack captured."));
			return;
		}

		const BOOL bSymbols = EnsureDbgHelp();
		const HANDLE hProcess = ::GetCurrentProcess();
		for (unsigned short i = 0; i < g_largeAlloc.nStackDepth; ++i) {
			const DWORD64 addr = reinterpret_cast<DWORD64>(g_largeAlloc.Stack[i]);
			if (!bSymbols) {
				ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] #%u addr=0x%p"), i, g_largeAlloc.Stack[i]);
				continue;
			}

			char symBuffer[sizeof(SYMBOL_INFO) + kMaxSymbolNameLen] = {};
			SYMBOL_INFO* pSym = reinterpret_cast<SYMBOL_INFO*>(symBuffer);
			pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
			pSym->MaxNameLen = kMaxSymbolNameLen - 1;
			DWORD64 displacement = 0;
			IMAGEHLP_LINE64 lineInfo = {};
			lineInfo.SizeOfStruct = sizeof(lineInfo);
			DWORD lineDisp = 0;

			const BOOL bSym = g_dbgHelp.pSymFromAddr ? g_dbgHelp.pSymFromAddr(hProcess, addr, &displacement, pSym) : FALSE;
			const BOOL bLine = g_dbgHelp.pSymGetLineFromAddr64 ? g_dbgHelp.pSymGetLineFromAddr64(hProcess, addr, &lineDisp, &lineInfo) : FALSE;

			if (bSym && bLine)
				ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] #%u %hs %hs:%lu addr=0x%p"), i, pSym->Name, lineInfo.FileName, lineInfo.LineNumber, g_largeAlloc.Stack[i]);
			else if (bSym)
				ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] #%u %hs addr=0x%p"), i, pSym->Name, g_largeAlloc.Stack[i]);
			else
				ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] #%u addr=0x%p"), i, g_largeAlloc.Stack[i]);
		}
	}

	static void EmitStackForId(unsigned long id)
	{
		if (!g_requestTrace || id >= g_requestTrace->size() || !(*g_requestTrace)[id].bValid) {
			if (g_nFirstTrackedRequest != 0 && id < g_nFirstTrackedRequest)
				ReportDebugLeakLine(_T("[DebugLeak][Stack] id=%lu not tracked (pre-hook allocation, first_tracked=%lu)."), id, g_nFirstTrackedRequest);
			else
				ReportDebugLeakLine(_T("[DebugLeak][Stack] id=%lu not tracked."), id);
			return;
		}

		const RequestTrace& rec = (*g_requestTrace)[id];
		if (!rec.nStackDepth) {
			ReportDebugLeakLine(_T("[DebugLeak][Stack] id=%lu no stack captured."), id);
			return;
		}

		if (rec.pszFileName && rec.nLine > 0)
			ReportDebugLeakLine(_T("[DebugLeak][Stack] id=%lu size=%Iu block=%u frames=%u file=%hs line=%d"), id, rec.nSize, rec.nBlockUse, rec.nStackDepth, rec.pszFileName, rec.nLine);
		else
			ReportDebugLeakLine(_T("[DebugLeak][Stack] id=%lu size=%Iu block=%u frames=%u"), id, rec.nSize, rec.nBlockUse, rec.nStackDepth);

		const BOOL bSymbols = EnsureDbgHelp();
		const HANDLE hProcess = ::GetCurrentProcess();
		for (unsigned short i = 0; i < rec.nStackDepth; ++i) {
			const DWORD64 addr = reinterpret_cast<DWORD64>(rec.Stack[i]);
			if (!bSymbols) {
				ReportDebugLeakLine(_T("[DebugLeak][Stack] #%u addr=0x%p"), i, rec.Stack[i]);
				continue;
			}

			char symBuffer[sizeof(SYMBOL_INFO) + kMaxSymbolNameLen] = {};
			SYMBOL_INFO* pSym = reinterpret_cast<SYMBOL_INFO*>(symBuffer);
			pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
			pSym->MaxNameLen = kMaxSymbolNameLen - 1;
			DWORD64 displacement = 0;
			IMAGEHLP_LINE64 lineInfo = {};
			lineInfo.SizeOfStruct = sizeof(lineInfo);
			DWORD lineDisp = 0;

			const BOOL bSym = g_dbgHelp.pSymFromAddr ? g_dbgHelp.pSymFromAddr(hProcess, addr, &displacement, pSym) : FALSE;
			const BOOL bLine = g_dbgHelp.pSymGetLineFromAddr64 ? g_dbgHelp.pSymGetLineFromAddr64(hProcess, addr, &lineDisp, &lineInfo) : FALSE;

			if (bSym && bLine)
				ReportDebugLeakLine(_T("[DebugLeak][Stack] #%u %hs %hs:%lu addr=0x%p"), i, pSym->Name, lineInfo.FileName, lineInfo.LineNumber, rec.Stack[i]);
			else if (bSym)
				ReportDebugLeakLine(_T("[DebugLeak][Stack] #%u %hs addr=0x%p"), i, pSym->Name, rec.Stack[i]);
			else
				ReportDebugLeakLine(_T("[DebugLeak][Stack] #%u addr=0x%p"), i, rec.Stack[i]);
		}
	}

	static bool ReadEnvBool(LPCTSTR pszName, bool bDefault)
	{
		CString value;
		if (!TryGetEnvValue(pszName, value))
			return bDefault;

		value.Trim();
		if (value.IsEmpty())
			return bDefault;
		const TCHAR ch = value[0];
		if (ch == _T('0') || ch == _T('f') || ch == _T('F') || ch == _T('n') || ch == _T('N'))
			return false;
		return true;
	}

	static unsigned long ReadEnvUnsignedLong(LPCTSTR pszName, unsigned long nDefault, unsigned long nMin, unsigned long nMax)
	{
		if (nMin > nMax)
			return nDefault;

		CString value;
		if (!TryGetEnvValue(pszName, value))
			return nDefault;

		value.Trim();
		if (value.IsEmpty())
			return nDefault;

		CString temp = value;
		TCHAR* pStart = temp.GetBuffer();
		while (*pStart == _T(' ') || *pStart == _T('\t'))
			++pStart;

		TCHAR* pTrim = pStart + _tcslen(pStart);
		while (pTrim > pStart) {
			const TCHAR ch = pTrim[-1];
			if (ch == _T(';') || ch == _T(' ') || ch == _T('\t')) {
				--pTrim;
				*pTrim = _T('\0');
				continue;
			}
			break;
		}
		if (*pStart == _T('\0'))
		{
			temp.ReleaseBuffer();
			return nDefault;
		}

		TCHAR* pEnd = nullptr;
		unsigned long parsed = _tcstoul(pStart, &pEnd, 10);
		temp.ReleaseBuffer();
		if (pEnd == pStart || *pEnd != _T('\0'))
			return nDefault;

		if (parsed < nMin)
			return nMin;
		if (parsed > nMax)
			return nMax;
		return parsed;
	}

	void RegisterPreDumpHook(PreDumpHook hook)
	{
		if (!hook)
			return;
		for (size_t i = 0; i < g_preDumpHookCount; ++i) {
			if (g_preDumpHooks[i] == hook)
				return;
		}
		if (g_preDumpHookCount < kMaxPreDumpHooks)
			g_preDumpHooks[g_preDumpHookCount++] = hook;
	}

	static void ParseLeakIdsFromDump(std::set<unsigned long>& ids)
	{
#ifdef _UNICODE
		const wchar_t* p = g_dumpBufW ? g_dumpBufW : L"";
		const wchar_t* end = p + g_dumpBufWLen;
		while (p < end) {
			const wchar_t* b = wcschr(p, L'{');
			if (!b || b >= end)
				break;

			++b;
			unsigned long id = 0;
			bool hasDigits = false;
			const wchar_t* q = b;
			while (q < end && *q >= L'0' && *q <= L'9') {
				hasDigits = true;
				id = id * 10ul + static_cast<unsigned long>(*q - L'0');
				++q;
			}
			if (hasDigits && q < end && *q == L'}')
				ids.insert(id);
			p = q + 1;
		}
#else
		const char* p = g_dumpBufA ? g_dumpBufA : "";
		const char* end = p + g_dumpBufALen;
		while (p < end) {
			const char* b = static_cast<const char*>(memchr(p, '{', static_cast<size_t>(end - p)));
			if (!b || b >= end)
				break;

			++b;
			unsigned long id = 0;
			bool hasDigits = false;
			const char* q = b;
			while (q < end && *q >= '0' && *q <= '9') {
				hasDigits = true;
				id = id * 10ul + static_cast<unsigned long>(*q - '0');
				++q;
			}
			if (hasDigits && q < end && *q == '}')
				ids.insert(id);
			p = q + 1;
		}
#endif
	}

	static char NormalizePathChar(char ch)
	{
		if (ch >= 'A' && ch <= 'Z')
			ch = static_cast<char>(ch - 'A' + 'a');
		if (ch == '\\')
			ch = '/';
		return ch;
	}

	static bool PathEndsWithIgnoreCase(const char* value, const char* suffix)
	{
		if (value == nullptr || suffix == nullptr)
			return false;

		const size_t valueLen = strlen(value);
		const size_t suffixLen = strlen(suffix);
		if (suffixLen > valueLen)
			return false;

		const char* valueTail = value + valueLen - suffixLen;
		for (size_t i = 0; i < suffixLen; ++i) {
			if (NormalizePathChar(valueTail[i]) != NormalizePathChar(suffix[i]))
				return false;
		}

		return true;
	}

	static bool StackContainsSourceLine(const RequestTrace& rec, const char* fileSuffix, DWORD lineNumber)
	{
		if (!rec.nStackDepth)
			return false;
		if (!EnsureDbgHelp())
			return false;

		const HANDLE hProcess = ::GetCurrentProcess();
		for (unsigned short i = 0; i < rec.nStackDepth; ++i) {
			const DWORD64 addr = reinterpret_cast<DWORD64>(rec.Stack[i]);
			IMAGEHLP_LINE64 lineInfo = {};
			lineInfo.SizeOfStruct = sizeof(lineInfo);
			DWORD lineDisp = 0;
			if (!g_dbgHelp.pSymGetLineFromAddr64 || !g_dbgHelp.pSymGetLineFromAddr64(hProcess, addr, &lineDisp, &lineInfo))
				continue;
			if (lineInfo.LineNumber != lineNumber)
				continue;
			if (PathEndsWithIgnoreCase(lineInfo.FileName, fileSuffix))
				return true;
		}

		return false;
	}

	static bool StackContainsSymbol(const RequestTrace& rec, const char* symbolName)
	{
		if (!rec.nStackDepth || symbolName == nullptr || *symbolName == '\0')
			return false;
		if (!EnsureDbgHelp())
			return false;

		const HANDLE hProcess = ::GetCurrentProcess();
		for (unsigned short i = 0; i < rec.nStackDepth; ++i) {
			const DWORD64 addr = reinterpret_cast<DWORD64>(rec.Stack[i]);
			char symBuffer[sizeof(SYMBOL_INFO) + kMaxSymbolNameLen] = {};
			SYMBOL_INFO* pSym = reinterpret_cast<SYMBOL_INFO*>(symBuffer);
			pSym->SizeOfStruct = sizeof(SYMBOL_INFO);
			pSym->MaxNameLen = kMaxSymbolNameLen - 1;
			DWORD64 displacement = 0;
			if (!g_dbgHelp.pSymFromAddr || !g_dbgHelp.pSymFromAddr(hProcess, addr, &displacement, pSym))
				continue;
			if (strcmp(pSym->Name, symbolName) == 0)
				return true;
		}

		return false;
	}

	static bool IsKnownInstrumentationLeak(const RequestTrace& rec)
	{
		return StackContainsSymbol(rec, "ATL::CTrace::TraceV")
			&& StackContainsSymbol(rec, "_AfxTraceMsg")
			&& ((StackContainsSymbol(rec, "CWnd::SendMessageToDescendants") && StackContainsSymbol(rec, "CWinApp::OnIdle"))
				|| (StackContainsSymbol(rec, "AfxWndProc") && StackContainsSymbol(rec, "CallWindowProcW") && StackContainsSymbol(rec, "SendMessageW")));
	}

	static bool IsKnownTruncatedStartupStaticLeak(const RequestTrace& rec)
	{
		return StackContainsSymbol(rec, "ZenLib::Ztring::Ztring")
			&& StackContainsSymbol(rec, "std::construct_at<ZenLib::Ztring,ZenLib::Ztring const &>")
			&& StackContainsSymbol(rec, "std::_Default_allocator_traits<std::allocator<ZenLib::Ztring> >::construct<ZenLib::Ztring,ZenLib::Ztring const &>")
			&& StackContainsSymbol(rec, "std::vector<ZenLib::Ztring,std::allocator<ZenLib::Ztring> >::_Emplace_reallocate<ZenLib::Ztring const &>")
			&& StackContainsSymbol(rec, "std::vector<ZenLib::Ztring,std::allocator<ZenLib::Ztring> >::_Emplace_one_at_back<ZenLib::Ztring const &>");
	}

	static bool IsKnownStartupStaticLeak(const RequestTrace& rec)
	{
		return StackContainsSourceLine(rec, "MediaInfoLib/Source/MediaInfo/MediaInfo_Config.cpp", 143)
			|| StackContainsSourceLine(rec, "MediaInfoLib/Source/MediaInfo/MediaInfo_Config.cpp", 144)
			|| StackContainsSourceLine(rec, "MediaInfoLib/Source/MediaInfo/MediaInfo_Config.cpp", 145)
			|| StackContainsSourceLine(rec, "MediaInfoLib/Source/MediaInfo/MediaInfo_Config.cpp", 352)
			|| StackContainsSourceLine(rec, "MediaInfoLib/Source/MediaInfo/MediaInfo_Internal.cpp", 908)
			|| StackContainsSourceLine(rec, "MediaInfoLib/Source/MediaInfo/MediaInfo_Internal.cpp", 909)
			|| StackContainsSourceLine(rec, "MediaInfoLib/Source/MediaInfo/ExternalCommandHelpers.cpp", 39)
			|| StackContainsSourceLine(rec, "ZenLib/ZenLib/InfoMap.cpp", 31)
			|| StackContainsSourceLine(rec, "ZenLib/ZenLib/Ztring.cpp", 154)
			|| StackContainsSourceLine(rec, "cryptopp/integer.cpp", 4895)
			|| StackContainsSourceLine(rec, "cryptopp/integer.cpp", 4896)
			|| StackContainsSourceLine(rec, "cryptopp/integer.cpp", 4897)
			|| StackContainsSourceLine(rec, "cryptopp/gf2n.cpp", 33)
			|| StackContainsSourceLine(rec, "cryptopp/ecp.cpp", 27);
	}

	static bool ShouldIgnoreTrackedLeak(unsigned long id)
	{
		if (!g_requestTrace || id >= g_requestTrace->size() || !(*g_requestTrace)[id].bValid)
			return false;

		const RequestTrace& rec = (*g_requestTrace)[id];
		return IsKnownStartupStaticLeak(rec) || IsKnownTruncatedStartupStaticLeak(rec) || IsKnownInstrumentationLeak(rec);
	}

	static size_t FilterKnownStartupStaticLeaks(std::set<unsigned long>& leakIds)
	{
		size_t ignored = 0;
		for (std::set<unsigned long>::iterator it = leakIds.begin(); it != leakIds.end(); ) {
			if (ShouldIgnoreTrackedLeak(*it)) {
				it = leakIds.erase(it);
				++ignored;
				continue;
			}

			++it;
		}

		return ignored;
	}

	static size_t FilterPreHookLeakIds(std::set<unsigned long>& leakIds)
	{
		if (g_nFirstTrackedRequest == 0)
			return 0;

		size_t ignored = 0;
		for (std::set<unsigned long>::iterator it = leakIds.begin(); it != leakIds.end(); ) {
			if (*it < g_nFirstTrackedRequest) {
				it = leakIds.erase(it);
				++ignored;
				continue;
			}

			++it;
		}

		return ignored;
	}

	static size_t FilterMissingTraceLeakIds(std::set<unsigned long>& leakIds)
	{
		if (!g_requestTrace)
			return leakIds.size();

		size_t ignored = 0;
		for (std::set<unsigned long>::iterator it = leakIds.begin(); it != leakIds.end(); ) {
			const unsigned long id = *it;
			if (id >= g_requestTrace->size() || !(*g_requestTrace)[id].bValid) {
				it = leakIds.erase(it);
				++ignored;
				continue;
			}

			++it;
		}

		return ignored;
	}

	static bool ParseLargestLeakFromDump(unsigned long& outId, size_t& outBytes)
	{
		outId = 0;
		outBytes = 0;
#ifdef _UNICODE
		const wchar_t* p = g_dumpBufW ? g_dumpBufW : L"";
		const wchar_t* end = p + g_dumpBufWLen;
		while (p < end) {
			const wchar_t* lineStart = p;
			while (p < end && *p != L'\n')
				++p;
			const wchar_t* lineEnd = p;

			unsigned long id = 0;
			bool hasId = false;
			for (const wchar_t* q = lineStart; q < lineEnd; ++q) {
				if (*q != L'{')
					continue;
				const wchar_t* s = q + 1;
				bool digits = false;
				unsigned long value = 0;
				while (s < lineEnd && *s >= L'0' && *s <= L'9') {
					digits = true;
					value = value * 10ul + static_cast<unsigned long>(*s - L'0');
					++s;
				}
				if (digits && s < lineEnd && *s == L'}') {
					id = value;
					hasId = true;
				}
				break;
			}

			const wchar_t* bytesPos = nullptr;
			static const wchar_t kBytesPattern[] = L"bytes long";
			const size_t kBytesPatternLen = _countof(kBytesPattern) - 1;
			for (const wchar_t* q = lineStart; (q + kBytesPatternLen) <= lineEnd; ++q) {
				if (wcsncmp(q, kBytesPattern, kBytesPatternLen) == 0) {
					bytesPos = q;
					break;
				}
			}

			if (hasId && bytesPos) {
				const wchar_t* digitEnd = bytesPos;
				while (digitEnd > lineStart && (*(digitEnd - 1) < L'0' || *(digitEnd - 1) > L'9'))
					--digitEnd;
				const wchar_t* digitStart = digitEnd;
				while (digitStart > lineStart && (*(digitStart - 1) >= L'0' && *(digitStart - 1) <= L'9'))
					--digitStart;

				if (digitStart < digitEnd) {
					size_t bytes = 0;
					for (const wchar_t* t = digitStart; t < digitEnd; ++t)
						bytes = (bytes * 10u) + static_cast<size_t>(*t - L'0');
					if (bytes > outBytes) {
						outBytes = bytes;
						outId = id;
					}
				}
			}

			if (p < end)
				++p;
		}
#else
		const char* p = g_dumpBufA ? g_dumpBufA : "";
		const char* end = p + g_dumpBufALen;
		while (p < end) {
			const char* lineStart = p;
			while (p < end && *p != '\n')
				++p;
			const char* lineEnd = p;

			unsigned long id = 0;
			bool hasId = false;
			for (const char* q = lineStart; q < lineEnd; ++q) {
				if (*q != '{')
					continue;
				const char* s = q + 1;
				bool digits = false;
				unsigned long value = 0;
				while (s < lineEnd && *s >= '0' && *s <= '9') {
					digits = true;
					value = value * 10ul + static_cast<unsigned long>(*s - '0');
					++s;
				}
				if (digits && s < lineEnd && *s == '}') {
					id = value;
					hasId = true;
				}
				break;
			}

			const char* bytesPos = nullptr;
			static const char kBytesPattern[] = "bytes long";
			const size_t kBytesPatternLen = _countof(kBytesPattern) - 1;
			for (const char* q = lineStart; (q + kBytesPatternLen) <= lineEnd; ++q) {
				if (strncmp(q, kBytesPattern, kBytesPatternLen) == 0) {
					bytesPos = q;
					break;
				}
			}

			if (hasId && bytesPos) {
				const char* digitEnd = bytesPos;
				while (digitEnd > lineStart && (*(digitEnd - 1) < '0' || *(digitEnd - 1) > '9'))
					--digitEnd;
				const char* digitStart = digitEnd;
				while (digitStart > lineStart && (*(digitStart - 1) >= '0' && *(digitStart - 1) <= '9'))
					--digitStart;

				if (digitStart < digitEnd) {
					size_t bytes = 0;
					for (const char* t = digitStart; t < digitEnd; ++t)
						bytes = (bytes * 10u) + static_cast<size_t>(*t - '0');
					if (bytes > outBytes) {
						outBytes = bytes;
						outId = id;
					}
				}
			}

			if (p < end)
				++p;
		}
#endif
		return outId != 0;
	}

	static bool IsBogusLargestLeakMarker(unsigned long id, size_t dumpBytes, size_t* trackedBytes = nullptr)
	{
		if (trackedBytes != nullptr)
			*trackedBytes = 0;
		if (id == 0 || dumpBytes < kLargeAllocThreshold)
			return false;
		if (!g_requestTrace || id >= g_requestTrace->size() || !(*g_requestTrace)[id].bValid)
			return false;

		const RequestTrace& rec = (*g_requestTrace)[id];
		if (trackedBytes != nullptr)
			*trackedBytes = rec.nSize;
		if (rec.nSize == dumpBytes)
			return false;
		if (rec.nSize >= kLargeAllocThreshold)
			return false;

		return rec.nSize < dumpBytes;
	}

	static size_t FilterBogusLargestLeakMarker(std::set<unsigned long>& leakIds)
	{
		unsigned long largestId = 0;
		size_t largestBytes = 0;
		if (!ParseLargestLeakFromDump(largestId, largestBytes))
			return 0;
		if (!IsBogusLargestLeakMarker(largestId, largestBytes))
			return 0;

		const size_t erased = leakIds.erase(largestId);
		return erased;
	}

	struct LeakDumpGuard
	{
		LeakDumpGuard() { ::InterlockedExchange(&g_bLeakDumpInProgress, 1); }
		~LeakDumpGuard() { ::InterlockedExchange(&g_bLeakDumpInProgress, 0); }
	};

	bool IsLeakDumpInProgress()
	{
		return ::InterlockedCompareExchange(&g_bLeakDumpInProgress, 0, 0) != 0;
	}

	static void EmitStacksForLeakIds(const std::set<unsigned long>& leakIds);
	static void ParseReportIdsFromEnv();
	static void EmitLargestLeakFromDump();

	static void EmitResolvedLeakOrigins()
	{
		std::set<unsigned long> leakIds;
		ParseLeakIdsFromDump(leakIds);
		const size_t ignoredBogusBigAlloc = FilterBogusLargestLeakMarker(leakIds);
		if (ignoredBogusBigAlloc != 0)
			ReportDebugLeakLine(_T("[DebugLeak][Filter] ignored=%llu bogus BigAlloc marker from dump."), static_cast<unsigned long long>(ignoredBogusBigAlloc));
		const size_t ignoredKnownStatics = FilterKnownStartupStaticLeaks(leakIds);
		if (ignoredKnownStatics != 0)
			ReportDebugLeakLine(_T("[DebugLeak][Filter] ignored=%llu known MediaInfoLib/ZenLib/CryptoPP startup-static leak ids."), static_cast<unsigned long long>(ignoredKnownStatics));
		const size_t ignoredPreHook = FilterPreHookLeakIds(leakIds);
		if (ignoredPreHook != 0)
			ReportDebugLeakLine(_T("[DebugLeak][Filter] ignored=%llu pre-hook leak ids before alloc tracking started (first_tracked=%lu)."), static_cast<unsigned long long>(ignoredPreHook), g_nFirstTrackedRequest);
		const size_t ignoredMissingTrace = FilterMissingTraceLeakIds(leakIds);
		if (ignoredMissingTrace != 0)
			ReportDebugLeakLine(_T("[DebugLeak][Filter] ignored=%llu leak ids without captured trace records."), static_cast<unsigned long long>(ignoredMissingTrace));
		if (leakIds.empty()) {
			TRACE(_T("[DebugLeak][Resolve] No leak ids parsed from current dump.\n"));
			return;
		}

		std::unordered_map<ResolveKey, ResolveInfo, ResolveKeyHash, ResolveKeyEq> grouped;
		size_t unresolved = 0;
		std::vector<unsigned long> unresolvedSamples;
		unresolvedSamples.reserve(16);

		for (std::set<unsigned long>::const_iterator it = leakIds.begin(); it != leakIds.end(); ++it) {
			const unsigned long id = *it;
			if (id >= g_requestTrace->size() || !(*g_requestTrace)[id].bValid) {
				++unresolved;
				if (unresolvedSamples.size() < 16)
					unresolvedSamples.push_back(id);
				continue;
			}

			const RequestTrace& rec = (*g_requestTrace)[id];
			ResolveKey key = {};
			if (rec.pszFileName != nullptr && rec.nLine > 0) {
				key.pszFileName = rec.pszFileName;
				key.nLine = rec.nLine;
				key.pCaller = rec.pCaller;
			} else if (rec.nStackDepth > 0) {
				key.pStack0 = rec.Stack[0];
				if (rec.nStackDepth > 1)
					key.pStack1 = rec.Stack[1];
				if (rec.nStackDepth > 2)
					key.pStack2 = rec.Stack[2];
			} else if (rec.pCaller != nullptr) {
				key.pCaller = rec.pCaller;
			} else {
				++unresolved;
				if (unresolvedSamples.size() < 16)
					unresolvedSamples.push_back(id);
				continue;
			}

			ResolveInfo& info = grouped[key];
			++info.nCount;
			info.nBytes += rec.nSize;
			if (info.nFirstId == 0)
				info.nFirstId = id;
		}

		std::vector<std::pair<ResolveKey, ResolveInfo>> ordered;
		ordered.reserve(grouped.size());
		for (std::unordered_map<ResolveKey, ResolveInfo, ResolveKeyHash, ResolveKeyEq>::const_iterator it = grouped.begin(); it != grouped.end(); ++it)
			ordered.push_back(*it);

		std::sort(ordered.begin(), ordered.end(), [](const std::pair<ResolveKey, ResolveInfo>& a, const std::pair<ResolveKey, ResolveInfo>& b) {
			if (a.second.nBytes != b.second.nBytes)
				return a.second.nBytes > b.second.nBytes;
			if (a.second.nCount != b.second.nCount)
				return a.second.nCount > b.second.nCount;
			return a.second.nFirstId < b.second.nFirstId;
		});

		const size_t resolved = leakIds.size() - unresolved;
		ReportDebugLeakLine(_T("[DebugLeak][Resolve] leaked=%Iu resolved=%Iu unresolved=%Iu tracked_max_id=%lu dropped=%Iu"), leakIds.size(), resolved, unresolved, g_nMaxTrackedRequest, g_nDroppedTraceRecords);

		const size_t kMaxLines = 64;
		const size_t lines = ordered.size() < kMaxLines ? ordered.size() : kMaxLines;
		for (size_t i = 0; i < lines; ++i) {
			const ResolveKey& key = ordered[i].first;
			const ResolveInfo& info = ordered[i].second;
			if (key.pszFileName) {
				if (key.pCaller)
					TRACE(_T("[DebugLeak][Resolve] #%Iu count=%Iu bytes=%Iu firstId=%lu file=%hs line=%d caller=0x%p\n"), i + 1, info.nCount, info.nBytes, info.nFirstId, key.pszFileName, key.nLine, key.pCaller);
				else
					TRACE(_T("[DebugLeak][Resolve] #%Iu count=%Iu bytes=%Iu firstId=%lu file=%hs line=%d\n"), i + 1, info.nCount, info.nBytes, info.nFirstId, key.pszFileName, key.nLine);
			} else if (key.pStack0) {
				TRACE(_T("[DebugLeak][Resolve] #%Iu count=%Iu bytes=%Iu firstId=%lu stack0=0x%p\n"), i + 1, info.nCount, info.nBytes, info.nFirstId, key.pStack0);
			} else if (key.pCaller) {
				TRACE(_T("[DebugLeak][Resolve] #%Iu count=%Iu bytes=%Iu firstId=%lu caller=0x%p\n"), i + 1, info.nCount, info.nBytes, info.nFirstId, key.pCaller);
			}
		}

		if (g_bCaptureStack && g_bEmitResolveStacks) {
			const size_t maxStacks = (g_nResolveStackLimit < lines) ? g_nResolveStackLimit : lines;
			for (size_t i = 0; i < maxStacks; ++i) {
				const ResolveInfo& info = ordered[i].second;
				if (info.nFirstId != 0) {
					TRACE(_T("[DebugLeak][Resolve] stack for group #%Iu (firstId=%lu)\n"), i + 1, info.nFirstId);
					EmitStackForId(info.nFirstId);
				}
			}
		}

		for (size_t i = 0; i < unresolvedSamples.size(); ++i)
			TRACE(_T("[DebugLeak][Resolve] unresolved sample id=%lu\n"), unresolvedSamples[i]);

		if (!g_reportIds.empty()) {
			TRACE(_T("[DebugLeak][Report] reporting %Iu requested ids.\n"), g_reportIds.size());
			for (std::set<unsigned long>::const_iterator it = g_reportIds.begin(); it != g_reportIds.end(); ++it)
				EmitStackForId(*it);
		}

		if (g_bCaptureStack) {
			for (size_t i = 0; i < unresolvedSamples.size(); ++i)
				EmitStackForId(unresolvedSamples[i]);
			if (g_bEmitLeakStacks)
				EmitStacksForLeakIds(leakIds);
		} else
			TRACE(_T("[DebugLeak][Stack] Stack capture disabled. Set EMULE_CRT_CAPTURE_STACK=1 to enable.\n"));
	}

	static void EmitLargestLeakFromDump()
	{
		unsigned long id = 0;
		size_t bytes = 0;
		if (!ParseLargestLeakFromDump(id, bytes))
			return;
		if (bytes < kLargeAllocThreshold)
			return;

		size_t trackedBytes = 0;
		if (IsBogusLargestLeakMarker(id, bytes, &trackedBytes)) {
			ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] ignored bogus marker id=%lu dump_size=%Iu tracked_size=%Iu."), id, bytes, trackedBytes);
			return;
		}

		ReportDebugLeakLine(_T("[DebugLeak][BigAlloc] largest id=%lu size=%Iu (from dump)"), id, bytes);
		EmitStackForId(id);
	}

	static void EmitStacksForLeakIds(const std::set<unsigned long>& leakIds)
	{
		if (leakIds.empty())
			return;

		size_t emitted = 0;
		size_t skippedNoTrace = 0;
		size_t skippedNoStack = 0;

			for (std::set<unsigned long>::const_iterator it = leakIds.begin(); it != leakIds.end(); ++it) {
				if (emitted >= g_nLeakStackLimit)
					break;

				const unsigned long id = *it;
				if (!g_requestTrace || id >= g_requestTrace->size() || !(*g_requestTrace)[id].bValid) {
					++skippedNoTrace;
					continue;
				}

				if (!(*g_requestTrace)[id].nStackDepth) {
					++skippedNoStack;
					continue;
				}

			EmitStackForId(id);
			++emitted;
		}

		ReportDebugLeakLine(_T("[DebugLeak][Stack] leakIds=%Iu emitted=%Iu skippedNoTrace=%Iu skippedNoStack=%Iu limit=%lu"),
			leakIds.size(), emitted, skippedNoTrace, skippedNoStack, g_nLeakStackLimit);

		if (emitted >= g_nLeakStackLimit && leakIds.size() > emitted)
			TRACE(_T("[DebugLeak][Stack] leak stack output truncated. Increase EMULE_CRT_LEAK_STACK_LIMIT if needed.\n"));
	}

	bool TryDescribeTrackedAlloc(unsigned long allocId, LPTSTR pszBuffer, size_t cchBuffer)
	{
		if (pszBuffer == NULL || cchBuffer == 0)
			return false;

		pszBuffer[0] = _T('\0');
		if (!g_requestTrace || allocId >= g_requestTrace->size() || !(*g_requestTrace)[allocId].bValid) {
			if (g_nFirstTrackedRequest != 0 && allocId < g_nFirstTrackedRequest)
				_sntprintf_s(pszBuffer, cchBuffer, _TRUNCATE, _T("[DebugLeak][AutoDump] id=%lu not tracked (pre-hook allocation, first_tracked=%lu)."), allocId, g_nFirstTrackedRequest);
			else
				_sntprintf_s(pszBuffer, cchBuffer, _TRUNCATE, _T("[DebugLeak][AutoDump] id=%lu not tracked."), allocId);
			return true;
		}

		const RequestTrace& rec = (*g_requestTrace)[allocId];
		if (rec.pszFileName != NULL && rec.nLine > 0)
			_sntprintf_s(pszBuffer, cchBuffer, _TRUNCATE, _T("[DebugLeak][AutoDump] id=%lu tracked_size=%llu block=%u file=%hs line=%d"), allocId, static_cast<unsigned long long>(rec.nSize), rec.nBlockUse, rec.pszFileName, rec.nLine);
		else
			_sntprintf_s(pszBuffer, cchBuffer, _TRUNCATE, _T("[DebugLeak][AutoDump] id=%lu tracked_size=%llu block=%u"), allocId, static_cast<unsigned long long>(rec.nSize), rec.nBlockUse);
		return true;
	}

    static bool g_suppressMfcPoolLines = true;
    static bool ShouldSuppressLine(const wchar_t* s) { return g_suppressMfcPoolLines && s && (wcsstr(s, L"plex.cpp") || wcsstr(s, L"afxtempl.h") || wcsstr(s, L"tooltip.cpp") || wcsstr(s, L"map_sp.cpp") || wcsstr(s, L"array_s.cpp")); }
    static bool ShouldSuppressLineA(const char* s) { return g_suppressMfcPoolLines && s && (strstr(s, "plex.cpp") || strstr(s, "afxtempl.h") || strstr(s, "tooltip.cpp") || strstr(s, "map_sp.cpp") || strstr(s, "array_s.cpp")); }

    // Allocates/clears the capture buffer without using the CRT heap.
    static void EnsureBuffer()
    {
#ifdef _UNICODE
        if (!g_dumpBufW)
            g_dumpBufW = static_cast<wchar_t*>(::VirtualAlloc(nullptr, kDumpBufWCap * sizeof(wchar_t), MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

        g_dumpBufWLen = 0;
        if (g_dumpBufW) 
            g_dumpBufW[0] = L'\0';
#else
        if (!g_dumpBufA)
            g_dumpBufA = static_cast<char*>(::VirtualAlloc(nullptr, kDumpBufACap, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));

        g_dumpBufALen = 0;
        if (g_dumpBufA)
            g_dumpBufA[0] = '\0';
#endif
    }

	static bool IsCrtDumpSourceLocationLineW(const wchar_t* message)
	{
		if (message == nullptr || wcsstr(message, L") :") == nullptr)
			return false;
		return wcsstr(message, L".cpp(") != nullptr || wcsstr(message, L".h(") != nullptr || wcsstr(message, L".hpp(") != nullptr || wcsstr(message, L".inl(") != nullptr;
	}

	static bool IsCrtDumpSourceLocationLineA(const char* message)
	{
		if (message == nullptr || strstr(message, ") :") == nullptr)
			return false;
		return strstr(message, ".cpp(") != nullptr || strstr(message, ".h(") != nullptr || strstr(message, ".hpp(") != nullptr || strstr(message, ".inl(") != nullptr;
	}

	static bool ShouldSuppressRawLeakDumpMessageW(const wchar_t* message)
	{
		if (message == nullptr || *message == L'\0')
			return false;
		if (::InterlockedCompareExchange(&g_bLeakDumpInProgress, 0, 0) != 0 && IsCrtDumpSourceLocationLineW(message))
			return true;
		return wcsstr(message, L"Detected memory leaks!") != nullptr
			|| wcsstr(message, L"Dumping objects ->") != nullptr
			|| wcsstr(message, L"Object dump complete.") != nullptr
			|| (wcschr(message, L'{') != nullptr && wcschr(message, L'}') != nullptr)
			|| wcsstr(message, L" bytes long.") != nullptr
			|| wcsstr(message, L" block at 0x") != nullptr
			|| wcsstr(message, L" normal block at ") != nullptr
			|| wcsstr(message, L" client block at ") != nullptr
			|| wcsstr(message, L" free block at ") != nullptr
			|| wcsstr(message, L" ignore block at ") != nullptr
			|| wcsstr(message, L" Data: <") != nullptr;
	}

	static bool ShouldSuppressRawLeakDumpMessageA(const char* message)
	{
		if (message == nullptr || *message == '\0')
			return false;
		if (::InterlockedCompareExchange(&g_bLeakDumpInProgress, 0, 0) != 0 && IsCrtDumpSourceLocationLineA(message))
			return true;
		return strstr(message, "Detected memory leaks!") != nullptr
			|| strstr(message, "Dumping objects ->") != nullptr
			|| strstr(message, "Object dump complete.") != nullptr
			|| (strchr(message, '{') != nullptr && strchr(message, '}') != nullptr)
			|| strstr(message, " bytes long.") != nullptr
			|| strstr(message, " block at 0x") != nullptr
			|| strstr(message, " normal block at ") != nullptr
			|| strstr(message, " client block at ") != nullptr
			|| strstr(message, " free block at ") != nullptr
			|| strstr(message, " ignore block at ") != nullptr
			|| strstr(message, " Data: <") != nullptr;
	}

#ifdef _UNICODE
	static int __cdecl GlobalReportHookW(int reportType, wchar_t* message, int* /*returnValue*/)
	{
		(void)reportType;
		return ShouldSuppressRawLeakDumpMessageW(message) ? TRUE : FALSE;
	}

    // CRT report hook (wide)
    static int __cdecl ReportHookW(int reportType, wchar_t* message, int* /*returnValue*/)
    {
        (void)reportType;
        if (!g_dumpBufW || !message)
			return TRUE;

        std::size_t msgLen = wcslen(message);
        if (!msgLen)
			return TRUE;
		const bool bLeakSourceLocation = ::InterlockedCompareExchange(&g_bLeakDumpInProgress, 0, 0) != 0 && IsCrtDumpSourceLocationLineW(message);
		if (ShouldSuppressRawLeakDumpMessageW(message)) {
			if (!bLeakSourceLocation) {
				std::size_t cap = kDumpBufWCap;
				std::size_t remaining = (g_dumpBufWLen < cap) ? (cap - g_dumpBufWLen - 1) : 0;
				if (remaining) {
					std::size_t toCopy = msgLen < remaining ? msgLen : remaining;
					wmemcpy(g_dumpBufW + g_dumpBufWLen, message, toCopy);
					g_dumpBufWLen += toCopy;
					g_dumpBufW[g_dumpBufWLen] = L'\0';
				}
			}
			return TRUE;
		}

        // Optionally ignore benign MFC CPlex/afxtempl pool lines.
        if (ShouldSuppressLine(message))
			return TRUE;

        std::size_t cap = kDumpBufWCap;
        std::size_t remaining = (g_dumpBufWLen < cap) ? (cap - g_dumpBufWLen - 1) : 0;
        if (!remaining)
			return TRUE;

        std::size_t toCopy = msgLen < remaining ? msgLen : remaining;
        wmemcpy(g_dumpBufW + g_dumpBufWLen, message, toCopy);
        g_dumpBufWLen += toCopy;
        g_dumpBufW[g_dumpBufWLen] = L'\0';
		return TRUE; // keep raw CRT dump out of the debugger log; emit sanitized lines later
    }
#else
	static int __cdecl GlobalReportHookA(int reportType, char* message, int* /*returnValue*/)
	{
		(void)reportType;
		return ShouldSuppressRawLeakDumpMessageA(message) ? TRUE : FALSE;
	}

    // CRT report hook (ANSI)
    static int __cdecl ReportHookA(int reportType, char* message, int* /*returnValue*/)
    {
        (void)reportType;
        if (!g_dumpBufA || !message)
			return TRUE;

        std::size_t msgLen = strlen(message);
        if (!msgLen)
			return TRUE;
		const bool bLeakSourceLocation = ::InterlockedCompareExchange(&g_bLeakDumpInProgress, 0, 0) != 0 && IsCrtDumpSourceLocationLineA(message);
		if (ShouldSuppressRawLeakDumpMessageA(message)) {
			if (!bLeakSourceLocation) {
				std::size_t cap = kDumpBufACap;
				std::size_t remaining = (g_dumpBufALen < cap) ? (cap - g_dumpBufALen - 1) : 0;
				if (remaining) {
					std::size_t toCopy = msgLen < remaining ? msgLen : remaining;
					memcpy(g_dumpBufA + g_dumpBufALen, message, toCopy);
					g_dumpBufALen += toCopy;
					g_dumpBufA[g_dumpBufALen] = '\0';
				}
			}
			return TRUE;
		}

        std::size_t cap = kDumpBufACap;
        std::size_t remaining = (g_dumpBufALen < cap) ? (cap - g_dumpBufALen - 1) : 0;
        if (!remaining)
			return TRUE;

        // Optionally ignore benign MFC CPlex/afxtempl pool lines.
        if (ShouldSuppressLineA(message))
			return TRUE;

        std::size_t toCopy = msgLen < remaining ? msgLen : remaining;
        memcpy(g_dumpBufA + g_dumpBufALen, message, toCopy);
        g_dumpBufALen += toCopy;
        g_dumpBufA[g_dumpBufALen] = '\0';
		return TRUE;
    }
#endif

    // Emits small hints back to the CRT output by scanning "{123}" markers in the dump.
    static void EmitHintsFromDump()
    {
#ifdef _UNICODE
        const wchar_t* p = g_dumpBufW ? g_dumpBufW : L"";
        const wchar_t* end = p + g_dumpBufWLen;
        wchar_t temp[64] = {};
        unsigned printed = 0;

        while (p < end && printed < 8) {
            const wchar_t* b = wcschr(p, L'{');

            if (!b)
                break;

            ++b;
            unsigned id = 0;
            const wchar_t* q = b;
            // Parse consecutive digits: increment 'q' in the for-loop header to avoid falling out of the loop body.
            for (; q < end && *q >= L'0' && *q <= L'9'; ++q)
                id = id * 10u + (*q - L'0');

            if (q < end && *q == L'}') {
                if (!printed)
                    OutputDebugStringW(L"\n[DebugLeak] Allocation IDs in leak dump: ");
                wsprintfW(temp, L"%u ", id);
                OutputDebugStringW(temp);
                ++printed;
                p = q + 1;
            } else
                p = q + 1;
        }

        if (printed)
			OutputDebugStringW(L"\n[DebugLeak] Tip: set EMULE_CRT_BREAKALLOC=<id> (or EMULE_CRT_BREAKALLOCS=<id1,id2>) and restart to break on allocation.\n");
#else
        const char* p = g_dumpBufA ? g_dumpBufA : "";
        const char* end = p + g_dumpBufALen;
        char temp[64] = {};
        unsigned printed = 0;

        while (p < end && printed < 8) {
            const char* b = strchr(p, '{');
            if (!b)
                break;
            ++b;

            unsigned id = 0;
            const char* q = b;
            // Parse consecutive digits: increment 'q' in the for-loop header to avoid falling out of the loop body.
            for (; q < end && *q >= '0' && *q <= '9'; ++q)
                id = id * 10u + (*q - '0');

            if (q < end && *q == '}') {
                if (!printed) OutputDebugStringA("\n[DebugLeak] Allocation IDs in leak dump: ");
                _snprintf_s(temp, _TRUNCATE, "%u ", id);
                OutputDebugStringA(temp);
                ++printed;
                p = q + 1;
            } else p = q + 1;
        }

        if (printed)
			OutputDebugStringA("\n[DebugLeak] Tip: set EMULE_CRT_BREAKALLOC=<id> (or EMULE_CRT_BREAKALLOCS=<id1,id2>) and restart to break on allocation.\n");
#endif
    }

	// Parses a TCHAR list like "12, 34;56 78" into the provided set.
	static void ParseIdList(const TCHAR* s, std::set<unsigned long>& out)
	{
		if (!s || !*s)
			return;

        unsigned long acc = 0;
        bool inNum = false;

        for (const TCHAR* p = s; ; ++p) {
            TCHAR ch = *p;
            if (ch >= _T('0') && ch <= _T('9')) {
                acc = (acc * 10ul) + static_cast<unsigned long>(ch - _T('0'));
                inNum = true;
            } else {
                if (inNum) {
					out.insert(acc);
					acc = 0; inNum = false; 
				}

                if (ch == _T('\0'))
                    break;
            }
        }
    }

	void EarlyInit()
	{
		if (g_bEarlyInitDone)
			return;
		g_bEarlyInitDone = true;

		InitializeCriticalSection(&g_allocCS);

		int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
		flags |= _CRTDBG_ALLOC_MEM_DF;
		flags &= ~_CRTDBG_LEAK_CHECK_DF;
		_CrtSetDbgFlag(flags);

		if (!g_bGlobalReportHookInstalled) {
			g_bGlobalReportHookInstalled = true;
#ifdef _UNICODE
			_CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &GlobalReportHookW);
#else
			_CrtSetReportHookA2(_CRT_RPTHOOK_INSTALL, &GlobalReportHookA);
#endif
		}

		g_bTrackAllocs = true;
		g_bCaptureCaller = false;
		g_bCaptureStack = true;
		g_bEmitLeakStacks = true;
		g_bEmitResolveStacks = false;
		g_nStackDepth = kDefaultStackDepth;
	}

    void Init()
    {
		EarlyInit();

		if (g_bInitDone)
			return;
		g_bInitDone = true;

        // Ensure CRT debug heap is on.
        int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);
        flags |= _CRTDBG_ALLOC_MEM_DF;
		flags &= ~_CRTDBG_LEAK_CHECK_DF;
        _CrtSetDbgFlag(flags);

        // Enable optional suppression via env or compile-time macro.
#ifdef DEBUGLEAKHELPER_IGNORE_MFC
        g_suppressMfcPoolLines = true;
#endif
        TCHAR buf[8] = { 0 };
        if (GetEnvironmentVariable(_T("EMULE_CRT_IGNORE_MFC"), buf, _countof(buf)) > 0)
            g_suppressMfcPoolLines = (buf[0] != _T('0'));

		g_bTrackAllocs = ReadEnvBool(_T("EMULE_CRT_TRACK_ALLOCS"), true);
		g_bCaptureCaller = ReadEnvBool(_T("EMULE_CRT_CAPTURE_CALLER"), false);
		g_bCaptureStack = ReadEnvBool(_T("EMULE_CRT_CAPTURE_STACK"), false);
		g_bEmitLeakStacks = ReadEnvBool(_T("EMULE_CRT_LEAK_STACKS"), g_bCaptureStack);
		g_bEmitResolveStacks = ReadEnvBool(_T("EMULE_CRT_RESOLVE_STACKS"), false);
		// Force stack capture when env injection is unreliable.
		g_bCaptureStack = true;
		g_bEmitLeakStacks = true;
		g_nStackDepth = static_cast<unsigned short>(ReadEnvUnsignedLong(_T("EMULE_CRT_STACK_DEPTH"), kDefaultStackDepth, kMinStackDepth, kMaxStackDepth));
		g_bDumpAllocs = ReadEnvBool(_T("EMULE_CRT_DUMP_ALL"), false);
		g_nTrackRequestLimit = ReadEnvUnsignedLong(_T("EMULE_CRT_TRACK_LIMIT"), 2000000ul, 16384ul, 50000000ul);
		g_nLeakStackLimit = ReadEnvUnsignedLong(_T("EMULE_CRT_LEAK_STACK_LIMIT"), 64ul, 1ul, 5000ul);
		g_nResolveStackLimit = ReadEnvUnsignedLong(_T("EMULE_CRT_RESOLVE_STACK_LIMIT"), 8ul, 1ul, 128ul);
		const unsigned long breakLargeMb = ReadEnvUnsignedLong(_T("EMULE_CRT_BREAK_LARGE_MB"), 0ul, 0ul, 4096ul);
		g_breakLargeThreshold = static_cast<size_t>(breakLargeMb) * 1024u * 1024u;
		g_breakLargeOnce = false;
		if (g_breakLargeThreshold != 0)
			TRACE(_T("[DebugLeak][BreakLarge] threshold=%Iu bytes (%lu MB)\n"), g_breakLargeThreshold, breakLargeMb);
		g_heapCheckInterval = ReadEnvUnsignedLong(_T("EMULE_CRT_HEAP_CHECK_INTERVAL"), 0ul, 0ul, 1000000ul);
		g_heapCheckCounter = 0;
		g_heapCheckInProgress = false;
		if (g_heapCheckInterval != 0)
			TRACE(_T("[DebugLeak][HeapCheck] interval=%lu\n"), g_heapCheckInterval);
		g_nDroppedTraceRecords = 0;
		if (!g_requestTrace)
			g_requestTrace = new std::vector<RequestTrace>();
		g_requestTrace->clear();
		MergePreInitTraces();
		g_largeAlloc = {};

        // Capture a start snapshot to avoid walking with a NULL stop pointer later.
        _CrtMemCheckpoint(&g_stateAtInit);
        g_hasStateAtInit = true;

		// Apply env-provided break ids.
		ParseAndApplyBreakAllocsFromEnv();
		ParseReportIdsFromEnv();

		if (!g_bDumpRegistered && !ShouldSkipLeakDumpRegistration()) {
			g_bDumpRegistered = true;
			::atexit(&DumpLeaksToCrt);
		}
    }

    void LateInit()
    {
        // Idempotent: make sure we still have a valid snapshot and flags.
        int flags = _CrtSetDbgFlag(_CRTDBG_REPORT_FLAG);

		int updatedFlags = flags | _CRTDBG_ALLOC_MEM_DF;
		updatedFlags &= ~_CRTDBG_LEAK_CHECK_DF;
		if (updatedFlags != flags)
			_CrtSetDbgFlag(updatedFlags);

        if (!g_hasStateAtInit) {
            _CrtMemCheckpoint(&g_stateAtInit);
            g_hasStateAtInit = true;
        }

		ParseReportIdsFromEnv();
    }

    void ParseAndApplyBreakAllocsFromEnv()
    {
		bool hadEnv = false;
		g_breakIds.clear();

		CString buf;
		if (TryGetEnvValue(_T("EMULE_CRT_BREAKALLOC_FORCE"), buf)) {
			hadEnv = true;
			TRACE(_T("[DebugLeak][BreakAlloc] EMULE_CRT_BREAKALLOC_FORCE='%s' (override)\n"), static_cast<LPCTSTR>(buf));
			ParseIdList(buf, g_breakIds);
		} else {
			if (TryGetEnvValue(_T("EMULE_CRT_BREAKALLOCS"), buf)) {
				hadEnv = true;
				TRACE(_T("[DebugLeak][BreakAlloc] EMULE_CRT_BREAKALLOCS='%s'\n"), static_cast<LPCTSTR>(buf));
				ParseIdList(buf, g_breakIds);
			}

			buf.Empty();
			if (TryGetEnvValue(_T("EMULE_CRT_BREAKALLOC"), buf)) {
				hadEnv = true;
				TRACE(_T("[DebugLeak][BreakAlloc] EMULE_CRT_BREAKALLOC='%s'\n"), static_cast<LPCTSTR>(buf));
				ParseIdList(buf, g_breakIds);
			}
		}

        // CRT can hold a single break id; choose the smallest one for determinism.
		if (g_forcedBreakAlloc != 0) {
			g_breakIds.clear();
			g_breakIds.insert(g_forcedBreakAlloc);
			_CrtSetBreakAlloc(static_cast<long>(g_forcedBreakAlloc));
			TRACE(_T("[DebugLeak][BreakAlloc] forced=%lu (code override)\n"), g_forcedBreakAlloc);
		} else if (!g_breakIds.empty()) {
			unsigned long first = *g_breakIds.begin();
			_CrtSetBreakAlloc(static_cast<long>(first));
			TRACE(_T("[DebugLeak][BreakAlloc] parsed=%Iu first=%lu\n"), g_breakIds.size(), first);
		} else {
			_CrtSetBreakAlloc(-1);
			if (hadEnv)
				TRACE(_T("[DebugLeak][BreakAlloc] no valid ids parsed from env; cleared.\n"));
			else
				TRACE(_T("[DebugLeak][BreakAlloc] cleared (no env/override).\n"));
        }
    }

	static void ParseReportIdsFromEnv()
	{
		TCHAR buf[512] = { 0 };
		DWORD n = ::GetEnvironmentVariable(_T("EMULE_CRT_REPORT_IDS"), buf, _countof(buf));
		if (!n || n >= _countof(buf))
			return;

		ParseIdList(buf, g_reportIds);
	}

    void AddBreakAlloc(std::uint32_t allocId)
    {
        g_breakIds.insert(static_cast<unsigned long>(allocId));
        _CrtSetBreakAlloc(static_cast<long>(allocId));
    }

    bool ShouldBreakAlloc(unsigned long allocId)
    {
        return g_breakIds.find(allocId) != g_breakIds.end();
    }

	void TrackAllocHookEvent(int mode, void* pUserData, size_t nSize, int nBlockUse, long lRequest, const unsigned char* pszFileName, int nLine)
	{
		(void)pUserData;
		if (!g_bTrackAllocs)
			return;
		if (lRequest <= 0)
			return;
		if (mode != _HOOK_ALLOC && mode != _HOOK_REALLOC)
			return;

		if (mode == _HOOK_ALLOC && g_heapCheckInterval != 0) {
			++g_heapCheckCounter;
			if (g_heapCheckCounter >= g_heapCheckInterval && !g_heapCheckInProgress) {
				g_heapCheckCounter = 0;
				g_heapCheckInProgress = true;
				const int ok = _CrtCheckMemory();
				g_heapCheckInProgress = false;
				if (!ok) {
					const char* fileName = pszFileName ? reinterpret_cast<const char*>(pszFileName) : "<null>";
					TRACE(_T("[DebugLeak][HeapCheck] failed request=%ld size=%Iu block=%d file=%hs line=%d\n"),
						lRequest, nSize, nBlockUse, fileName, nLine);
				#ifdef _MSC_VER
					__debugbreak();
				#else
					DebugBreak();
				#endif
				}
			}
		}

		if (!g_breakLargeOnce && g_breakLargeThreshold != 0 && nSize >= g_breakLargeThreshold) {
			g_breakLargeOnce = true;
			TRACE(_T("[DebugLeak][BreakLarge] nSize=%Iu request=%ld\n"), nSize, lRequest);
		#ifdef _MSC_VER
			__debugbreak();
		#else
			DebugBreak();
		#endif
		}

		const int blockType = _BLOCK_TYPE(nBlockUse);
		RecordLargeAllocIfNeeded(mode, nSize, static_cast<unsigned short>(blockType), lRequest, pszFileName, nLine);
		if (blockType == _IGNORE_BLOCK || blockType == _FREE_BLOCK)
			return;

		const unsigned long requestId = static_cast<unsigned long>(lRequest);
		if (g_nFirstTrackedRequest == 0)
			g_nFirstTrackedRequest = requestId;
		if (requestId > g_nMaxTrackedRequest)
			g_nMaxTrackedRequest = requestId;
		if (requestId > g_nTrackRequestLimit) {
			++g_nDroppedTraceRecords;
			return;
		}

		if (!g_requestTrace) {
			if (requestId < kPreInitTraceCap)
				StoreTraceRecord(g_preInitTrace[requestId], nSize, static_cast<unsigned short>(blockType), pszFileName, nLine);
			else
				++g_nDroppedTraceRecords;
			return;
		}

		if (requestId >= g_requestTrace->size()) {
			g_requestTrace->resize(static_cast<size_t>(requestId) + 1u);
		}

		RequestTrace& rec = (*g_requestTrace)[requestId];
		StoreTraceRecord(rec, nSize, static_cast<unsigned short>(blockType), pszFileName, nLine);
	}

    void DumpLeaksToCrt()
    {
		if (g_bDumped)
			return;
		g_bDumped = true;

		LeakDumpGuard guard;
        EnsureBuffer();

#ifdef _UNICODE
        _CrtSetReportHookW2(_CRT_RPTHOOK_INSTALL, &ReportHookW);
#else
        _CrtSetReportHookA2(_CRT_RPTHOOK_INSTALL, &ReportHookA);
#endif

		for (size_t i = g_preDumpHookCount; i > 0; --i) {
			PreDumpHook hook = g_preDumpHooks[i - 1];
			if (hook)
				hook();
		}

        // Default: dump actual leaks. Use EMULE_CRT_DUMP_ALL=1 to dump all allocations since Init().
		if (g_bDumpAllocs && g_hasStateAtInit)
			_CrtMemDumpAllObjectsSince(&g_stateAtInit);
		else
			_CrtDumpMemoryLeaks();

#ifdef _UNICODE
        _CrtSetReportHookW2(_CRT_RPTHOOK_REMOVE, &ReportHookW);
#else
        _CrtSetReportHookA2(_CRT_RPTHOOK_REMOVE, &ReportHookA);
#endif

        EmitHintsFromDump();
		EmitLargeAllocInfo();
		EmitLargestLeakFromDump();
		if (g_bTrackAllocs)
			EmitResolvedLeakOrigins();
		else
			TRACE(_T("[DebugLeak][Resolve] Allocation trace disabled. Set EMULE_CRT_TRACK_ALLOCS=1 to enable.\n"));

		CleanupDbgHelp();
    }
} // namespace DebugLeakHelper

#endif // _DEBUG
