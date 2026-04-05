//This file is part of eMule AI
//Copyright (C)2026 eMule AI

#pragma once
// Minimal, SDK-agnostic wrappers for Windows Thread Pool I/O and timers.
// - Late-bound via GetProcAddress to avoid SDK/OS version issues.
// - Provides opaque typedefs and thin wrappers.
// - Kept lean and warning-free.

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

// ----- Opaque aliases -----
typedef PVOID BB_PTP_ENV;
typedef PVOID BB_PTP_IO;
typedef PVOID BB_PTP_TIMER;

// Match Windows PTP_WIN32_IO_CALLBACK (stdcall, 6 parameters)
typedef VOID(NTAPI* BB_PTP_WIN32_IO_CALLBACK)(PVOID pInstance, PVOID pContext, PVOID pOverlapped, ULONG IoResult, ULONG_PTR NumberOfBytesTransferred, PVOID pTpIo);

// ----- Lazy symbol resolver -----
static HMODULE g_hKernel32_TPW = NULL;

static __forceinline FARPROC BB_GetProc(LPCSTR name)
{
	if (!g_hKernel32_TPW)
		g_hKernel32_TPW = ::GetModuleHandleW(L"kernel32.dll");
	return g_hKernel32_TPW ? ::GetProcAddress(g_hKernel32_TPW, name) : NULL;
}

// ----- Safe handle helper -----
static void BB_SafeCloseHandle(HANDLE* ph)
{
	// Close a handle capturing any SEH from kernel transitions; never throws.
	if (ph == NULL)
		return;

	HANDLE h = *ph;
	if (h == NULL || h == INVALID_HANDLE_VALUE)
		return;

	__try {
		::CloseHandle(h);
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// Swallow; best-effort close.
	}

	*ph = INVALID_HANDLE_VALUE;
}

// =============================
// Thread pool I/O: wrappers
// =============================
static __forceinline BB_PTP_IO BB_CreateThreadpoolIo(HANDLE h, BB_PTP_WIN32_IO_CALLBACK cb, PVOID ctx, BB_PTP_ENV env)
{
	typedef PVOID(WINAPI* PFN_CreateThreadpoolIo)(HANDLE, BB_PTP_WIN32_IO_CALLBACK, PVOID, PVOID);
	PFN_CreateThreadpoolIo p = reinterpret_cast<PFN_CreateThreadpoolIo>(BB_GetProc("CreateThreadpoolIo"));

	if (!p)		
		return NULL; // API not available on this OS; behave as if creation failed.

	return p(h, cb, ctx, env);
}

static __forceinline VOID BB_StartThreadpoolIo(BB_PTP_IO io)
{
	typedef VOID(WINAPI* PFN_StartThreadpoolIo)(PVOID);
	PFN_StartThreadpoolIo p = reinterpret_cast<PFN_StartThreadpoolIo>(BB_GetProc("StartThreadpoolIo"));
	if (p && io) p(io);
}

static __forceinline VOID BB_CancelThreadpoolIo(BB_PTP_IO io)
{
	typedef VOID(WINAPI* PFN_CancelThreadpoolIo)(PVOID);
	PFN_CancelThreadpoolIo p = reinterpret_cast<PFN_CancelThreadpoolIo>(BB_GetProc("CancelThreadpoolIo"));
	if (p && io) {
		__try {
			p(io);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// Best-effort cancel during teardown; swallow invalid-state SEH from the OS.
		}
	}
}

static __forceinline VOID BB_CloseThreadpoolIo(BB_PTP_IO io)
{
	// Intentionally a no-op.
	// Rationale: CloseThreadpoolIo sporadically raises STATUS_INVALID_PARAMETER on various
	// Windows builds during late shutdown despite correct draining. We cancel/drain and drop
	// the pointer; OS will reclaim at process exit. Keep this as no-op for safety.
	(void)io;
}

// Atomically close a TP_IO once and null out the caller's slot.
// Use this in shutdown paths to avoid double-close races.
static __forceinline VOID BB_CloseThreadpoolIoX(BB_PTP_IO* pIo)
{
	if (pIo == NULL)
		return;
	// Atomically null out caller's slot; do not call CloseThreadpoolIo for safety.
	(void)InterlockedExchangePointer(reinterpret_cast<PVOID*>(pIo), NULL);
}

// Atomically null out and really call CloseThreadpoolIo; use only when not exiting process.
static __forceinline VOID BB_CloseThreadpoolIoRealX(BB_PTP_IO* pIo)
{
	if (pIo == NULL)
		return;

	PVOID io = InterlockedExchangePointer(reinterpret_cast<PVOID*>(pIo), NULL);
	if (io == NULL)
		return;

	typedef VOID(WINAPI* PFN_CloseThreadpoolIo)(PVOID);
	PFN_CloseThreadpoolIo p = reinterpret_cast<PFN_CloseThreadpoolIo>(BB_GetProc("CloseThreadpoolIo"));
	if (p != NULL) {
		__try {
			p(io);
		} __except (EXCEPTION_EXECUTE_HANDLER) {
			// Best-effort runtime cleanup; swallow invalid-state SEH from the OS.
		}
	}
}

static __forceinline BOOL BB_WaitForThreadpoolIoCallbacks(BB_PTP_IO io, BOOL fCancelPendingCallbacks)
{
	typedef VOID(WINAPI* PFN_WaitForThreadpoolIoCallbacks)(PVOID, BOOL);
	PFN_WaitForThreadpoolIoCallbacks p = reinterpret_cast<PFN_WaitForThreadpoolIoCallbacks>(BB_GetProc("WaitForThreadpoolIoCallbacks"));
	if (io == NULL)
		return TRUE;

	if (p == NULL)
		return FALSE;

	__try {
		p(io, fCancelPendingCallbacks);
		return TRUE;
	} __except (EXCEPTION_EXECUTE_HANDLER) {
		// Best-effort wait during teardown; swallow invalid-state SEH from the OS.
	}

	return FALSE;
}

static __forceinline BOOL BB_CancelIoEx(HANDLE h, LPOVERLAPPED pov)
{
	typedef BOOL(WINAPI* PFN_CancelIoEx)(HANDLE, LPOVERLAPPED);
	PFN_CancelIoEx p = reinterpret_cast<PFN_CancelIoEx>(BB_GetProc("CancelIoEx"));
	return p ? p(h, pov) : FALSE;
}

// =============================
// Thread pool timers: wrappers
// (retained for compatibility;
// new code is driven by UploadTimer)
// =============================
static __forceinline BB_PTP_TIMER BB_CreateThreadpoolTimer(VOID(NTAPI* cb)(PVOID, PVOID, PVOID), PVOID ctx, BB_PTP_ENV env)
{
	typedef PVOID(WINAPI* PFN_CreateThreadpoolTimer)(VOID(NTAPI*)(PVOID, PVOID, PVOID), PVOID, PVOID);
	PFN_CreateThreadpoolTimer p = reinterpret_cast<PFN_CreateThreadpoolTimer>(BB_GetProc("CreateThreadpoolTimer"));
	return p ? p(cb, ctx, env) : NULL;
}

static __forceinline VOID BB_SetThreadpoolTimer(BB_PTP_TIMER t, const FILETIME* pDueTime, DWORD msPeriod, DWORD msWindowLength)
{
	typedef VOID(WINAPI* PFN_SetThreadpoolTimer)(PVOID, const FILETIME*, DWORD, DWORD);
	PFN_SetThreadpoolTimer p = reinterpret_cast<PFN_SetThreadpoolTimer>(BB_GetProc("SetThreadpoolTimer"));
	if (p && t) p(t, pDueTime, msPeriod, msWindowLength);
}

static __forceinline VOID BB_WaitForThreadpoolTimerCallbacks(BB_PTP_TIMER t, BOOL fCancelPending)
{
	typedef VOID(WINAPI* PFN_WaitForThreadpoolTimerCallbacks)(PVOID, BOOL);
	PFN_WaitForThreadpoolTimerCallbacks p = reinterpret_cast<PFN_WaitForThreadpoolTimerCallbacks>(BB_GetProc("WaitForThreadpoolTimerCallbacks"));
	if (p && t) p(t, fCancelPending);
}

static __forceinline VOID BB_CloseThreadpoolTimer(BB_PTP_TIMER t)
{
	typedef VOID(WINAPI* PFN_CloseThreadpoolTimer)(PVOID);
	PFN_CloseThreadpoolTimer p = reinterpret_cast<PFN_CloseThreadpoolTimer>(BB_GetProc("CloseThreadpoolTimer"));
	if (p && t) p(t);
}
