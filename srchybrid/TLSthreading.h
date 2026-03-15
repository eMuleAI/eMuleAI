#pragma once

#include "mbedtls/build_info.h"
#include "mbedtls/threading.h"

int threading_mutex_init_alt(mbedtls_platform_mutex_t *mutex) noexcept;
void threading_mutex_destroy_alt(mbedtls_platform_mutex_t *mutex) noexcept;
int threading_mutex_lock_alt(mbedtls_platform_mutex_t *mutex) noexcept;
int threading_mutex_unlock_alt(mbedtls_platform_mutex_t *mutex) noexcept;
int cond_init_alt(mbedtls_platform_condition_variable_t *cond) noexcept;
void cond_destroy_alt(mbedtls_platform_condition_variable_t *cond) noexcept;
int cond_signal_alt(mbedtls_platform_condition_variable_t *cond) noexcept;
int cond_broadcast_alt(mbedtls_platform_condition_variable_t *cond) noexcept;
int cond_wait_alt(mbedtls_platform_condition_variable_t *cond, mbedtls_platform_mutex_t *mutex) noexcept;
CString SSLerror(int ret);
