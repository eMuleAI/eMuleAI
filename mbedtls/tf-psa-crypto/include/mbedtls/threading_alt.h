#pragma once
#ifdef _MSC_VER
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>

typedef struct
{
	CRITICAL_SECTION cs;
	char is_valid;
} mbedtls_platform_mutex_t;

typedef struct
{
	int cond;
} mbedtls_platform_condition_variable_t;

#endif
