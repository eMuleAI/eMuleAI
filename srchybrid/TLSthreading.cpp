#include "stdafx.h"

#include "TLSthreading.h"
#include "mbedtls/error.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#undef THIS_FILE
static char THIS_FILE[] = __FILE__;
#endif

int threading_mutex_init_alt(mbedtls_platform_mutex_t *mutex) noexcept
{
	if (!mutex)
		return MBEDTLS_ERR_THREADING_USAGE_ERROR;
	InitializeCriticalSection(&mutex->cs);
	mutex->is_valid = 1;
	return 0;
}

void threading_mutex_destroy_alt(mbedtls_platform_mutex_t *mutex) noexcept
{
	if (mutex && mutex->is_valid) {
		DeleteCriticalSection(&mutex->cs);
		mutex->is_valid = 0;
	}
}

int threading_mutex_lock_alt(mbedtls_platform_mutex_t *mutex) noexcept
{
	if (mutex == NULL || !mutex->is_valid)
		return MBEDTLS_ERR_THREADING_USAGE_ERROR;
	EnterCriticalSection(&mutex->cs);
	return 0;
}

int threading_mutex_unlock_alt(mbedtls_platform_mutex_t *mutex) noexcept
{
	if (mutex == NULL || !mutex->is_valid)
		return MBEDTLS_ERR_THREADING_USAGE_ERROR;
	LeaveCriticalSection(&mutex->cs);
	return 0;
}

int cond_init_alt(mbedtls_platform_condition_variable_t *cond) noexcept
{
	UNREFERENCED_PARAMETER(cond);
	return 0;
}

void cond_destroy_alt(mbedtls_platform_condition_variable_t *cond) noexcept
{
	UNREFERENCED_PARAMETER(cond);
}

int cond_signal_alt(mbedtls_platform_condition_variable_t *cond) noexcept
{
	UNREFERENCED_PARAMETER(cond);
	return 0;
}

int cond_broadcast_alt(mbedtls_platform_condition_variable_t *cond) noexcept
{
	UNREFERENCED_PARAMETER(cond);
	return 0;
}

int cond_wait_alt(mbedtls_platform_condition_variable_t *cond, mbedtls_platform_mutex_t *mutex) noexcept
{
	UNREFERENCED_PARAMETER(cond);
	UNREFERENCED_PARAMETER(mutex);
	return 0;
}

CString SSLerror(int ret)
{
	char buf[256];
	mbedtls_strerror(ret, buf, sizeof buf);
	buf[sizeof buf - 1] = '\0';
	return CString(buf);
}
