import argparse
import re
from pathlib import Path

CONFIGCONTENT = r'''/* Generated for the eMuleAI MSVC build. */
#ifndef GNUTLS_CONFIG_H_INCLUDED
#define GNUTLS_CONFIG_H_INCLUDED
#define _GL_CONFIG_H_INCLUDED 1

#include <stdbool.h>
#include <limits.h>

#ifndef SSIZE_MAX
#if defined(_WIN64)
#define SSIZE_MAX _I64_MAX
#else
#define SSIZE_MAX INT_MAX
#endif
#endif

#ifndef __GNUC__
#define __attribute__(x)
#endif

#define PACKAGE "gnutls"
#define PACKAGE_NAME "GnuTLS"
#define PACKAGE_TARNAME "gnutls"
#define PACKAGE_VERSION "$version"
#define PACKAGE_STRING "GnuTLS $version"
#define VERSION "$version"
#define DEFAULT_PRIORITY_STRING "NORMAL"
#define HW_FEATURES ""
#define TLS_FEATURES "tls1.0,tls1.1,tls1.2,tls1.3,dtls1.0,dtls1.2"
#define SYSTEM_PRIORITY_FILE ""
#define PKCS12_ITER_COUNT 600000
#if defined(_WIN64)
#define GNUTLS_POINTER_TO_INT_CAST (__int64)
#else
#define GNUTLS_POINTER_TO_INT_CAST /**/
#endif

#define ENABLE_ALPN 1
#define ENABLE_ANON 1
#define ENABLE_DHE 1
#define ENABLE_DSA 1
#define ENABLE_DTLS_SRTP 1
#define ENABLE_ECDHE 1
#define ENABLE_HEARTBEAT 1
#define ENABLE_NON_SUITEB_CURVES 1
#define ENABLE_OCSP 1
#define ENABLE_PSK 1
#define ENABLE_SRP 1
#define ENABLE_MINITASN1 1
#define ENABLE_NETTLE 1

#define HAVE_BCRYPT_ALG_HANDLE 1
#define HAVE_C_BOOL 1
#define HAVE_C_STATIC_ASSERT 1
#define HAVE_DECL_FREEADDRINFO 1
#define HAVE_DECL_GAI_STRERROR 1
#define HAVE_DECL_GAI_STRERRORA 1
#define HAVE_DECL_GETADDRINFO 1
#define HAVE_DECL_HTONL 1
#define HAVE_DECL_HTONS 1
#define HAVE_DECL_INET_NTOP 1
#define HAVE_DECL_INET_PTON 1
#define HAVE_DECL_NTOHL 1
#define HAVE_DECL_NTOHS 1
#define HAVE_DECL_SNPRINTF 1
#define HAVE_DECL_STRDUP 1
#define HAVE_DECL_STRNCASECMP 0
#define HAVE_DECL_STRNDUP 0
#define HAVE_DECL_STRNLEN 1
#define HAVE_DECL_VSNPRINTF 1
#define HAVE_INLINE 1
#define HAVE_INTMAX_T 1
#define HAVE_INTTYPES_H 1
#define HAVE_IPV4 1
#define HAVE_IPV6 1
#define HAVE_LIBUNISTRING 1
#define HAVE_LIMITS_H 1
#define HAVE_LONG_LONG_INT 1
#define HAVE_MALLOC_0_NONNULL 1
#define HAVE_MALLOC_PTRDIFF 1
#define HAVE_MBSTATE_T 1
#define HAVE_MEMSET_S 0
#define HAVE_MEMSET_S_SUPPORTS_ZERO 0
#define HAVE_MSVC_INVALID_PARAMETER_HANDLER 1
#define HAVE_STDINT_H 1
#define HAVE_STDINT_H_WITH_UINTMAX 1
#define HAVE_STDBOOL_H 1
#define HAVE_STDIO_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_WINDOWS_LOCALE_T 1
#define HAVE_WINSOCK2_H 1
#define HAVE_WS2TCPIP_H 1

#define HAVE_NETTLE_CMAC_KUZNYECHIK_UPDATE 0
#define HAVE_NETTLE_CMAC_MAGMA_UPDATE 0
#define HAVE_NETTLE_GOST28147_SET_KEY 0
#define HAVE_NETTLE_KUZNYECHIK_SET_KEY 0
#define HAVE_NETTLE_MAGMA_SET_KEY 0

#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_UNSIGNED_INT 4
#define SIZEOF_UNSIGNED_LONG_INT 4
#if defined(_WIN64)
#define SIZEOF_VOID_P 8
#define SIZEOF_TIME_T 8
#define BITSIZEOF_SIZE_T 64
#else
#define SIZEOF_VOID_P 4
#define SIZEOF_TIME_T 8
#define BITSIZEOF_SIZE_T 32
#endif
#define BITSIZEOF_PTRDIFF_T BITSIZEOF_SIZE_T
#define BITSIZEOF_SIG_ATOMIC_T 32
#define BITSIZEOF_WCHAR_T 16
#define BITSIZEOF_WINT_T 16
#define WORD_BIT 32
#define LONG_BIT 32

#define GNULIB_UNISTR_U8_CHECK 1
#define GNULIB_UNISTR_U8_CPY 1
#define GNULIB_UNISTR_U8_MBTOUC 1
#define GNULIB_UNISTR_U8_MBTOUC_UNSAFE 1
#define GNULIB_UNISTR_U8_MBTOUCR 1
#define GNULIB_UNISTR_U8_TO_U16 1
#define GNULIB_UNISTR_U8_TO_U32 1
#define GNULIB_UNISTR_U8_UCTOMB 1
#define GNULIB_UNINORM_U8_NORMALIZE 1

#define _GL_INLINE static inline
#define _GL_EXTERN_INLINE static inline
#define C_CTYPE_INLINE _GL_INLINE
#define _GL_INLINE_HEADER_BEGIN
#define _GL_INLINE_HEADER_END
#define _GL_BEGIN_C_LINKAGE
#define _GL_END_C_LINKAGE
#define _GL_ARG_NONNULL(args)
#define _GL_WARN_ON_USE(function, message)
#define _GL_ATTRIBUTE_MALLOC
#define _GL_ATTRIBUTE_ALLOC_SIZE(args)
#define _GL_ATTRIBUTE_ALWAYS_INLINE
#define _GL_ATTRIBUTE_ARTIFICIAL
#define _GL_ATTRIBUTE_COLD
#define _GL_ATTRIBUTE_DEALLOC(f, i)
#define _GL_ATTRIBUTE_DEALLOC_FREE
#define _GL_ATTRIBUTE_CONST
#define _GL_ATTRIBUTE_ERROR(msg)
#define _GL_ATTRIBUTE_EXTERNALLY_VISIBLE
#define _GL_ATTRIBUTE_FALLTHROUGH
#define _GL_ATTRIBUTE_FORMAT(spec)
#define _GL_ATTRIBUTE_FORMAT_PRINTF(a, b)
#define _GL_ATTRIBUTE_LEAF
#define _GL_ATTRIBUTE_MAY_ALIAS
#define _GL_ATTRIBUTE_MAYBE_UNUSED
#define _GL_ATTRIBUTE_NONNULL(args)
#define _GL_ATTRIBUTE_NONNULL_IF_NONZERO(arg1, arg2)
#define _GL_ATTRIBUTE_NODISCARD
#define _GL_ATTRIBUTE_NOINLINE
#define _GL_ATTRIBUTE_NONSTRING
#define _GL_ATTRIBUTE_NOTHROW
#define _GL_ATTRIBUTE_PACKED
#define _GL_ATTRIBUTE_PURE
#define _GL_ATTRIBUTE_REPRODUCIBLE
#define _GL_ATTRIBUTE_RETURNS_NONNULL
#define _GL_ATTRIBUTE_SENTINEL(pos)
#define _GL_ATTRIBUTE_UNSEQUENCED
#define _GL_ATTRIBUTE_WARNING(msg)
#define _GL_ATTRIBUTE_NONNULL(args)
#define _GL_ATTRIBUTE_RETURNS_NONNULL
#define _GL_ATTRIBUTE_DEPRECATED
#define _GL_UNUSED
#define _GL_UNNAMED(name) name
#define _GL_CMP(n1, n2) (((n1) > (n2)) - ((n1) < (n2)))

#endif'''

BYTESWAPCONTENT = r'''#ifndef EMULEAI_BYTESWAP_H
#define EMULEAI_BYTESWAP_H

#include <stdlib.h>
#include <stdint.h>

#define bswap_16(x) _byteswap_ushort((unsigned short)(x))
#define bswap_32(x) _byteswap_ulong((unsigned long)(x))
#define bswap_64(x) _byteswap_uint64((unsigned long long)(x))

#endif'''

DIRENTCONTENT = r'''#ifndef EMULEAI_DIRENT_H
#define EMULEAI_DIRENT_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <tchar.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#define DT_UNKNOWN 0
#define DT_DIR 4
#define DT_REG 8
#define DT_LNK 10

struct _tdirent {
    _TCHAR d_name[MAX_PATH];
    size_t d_namlen;
    unsigned char d_type;
};

typedef struct _emuleai_tdir {
    HANDLE handle;
    WIN32_FIND_DATA data;
    int first;
    struct _tdirent entry;
    _TCHAR *pattern;
} _TDIR;

static inline int _emuleai_dirent_is_dot_name(const _TCHAR *name)
{
    return _tcscmp(name, _T(".")) == 0 || _tcscmp(name, _T("..")) == 0;
}

static inline _TCHAR *_emuleai_dirent_make_pattern(const _TCHAR *dirname)
{
    size_t length = _tcslen(dirname);
    int needs_separator = length != 0 && dirname[length - 1] != _T('\\') && dirname[length - 1] != _T('/') && dirname[length - 1] != _T(':');
    size_t pattern_length = length + (needs_separator ? 1 : 0) + 2;
    _TCHAR *pattern = (_TCHAR *)malloc((pattern_length + 1) * sizeof(_TCHAR));

    if (pattern == NULL) {
        return NULL;
    }

    _tcscpy_s(pattern, pattern_length + 1, dirname);
    if (needs_separator) {
        _tcscat_s(pattern, pattern_length + 1, _T("\\"));
    }
    _tcscat_s(pattern, pattern_length + 1, _T("*"));
    return pattern;
}

static inline void _emuleai_dirent_fill_entry(_TDIR *dir)
{
    _tcsncpy_s(dir->entry.d_name, MAX_PATH, dir->data.cFileName, _TRUNCATE);
    dir->entry.d_namlen = _tcslen(dir->entry.d_name);
    dir->entry.d_type = (dir->data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) ? DT_DIR : DT_REG;
}

static inline _TDIR *_topendir(const _TCHAR *dirname)
{
    _TDIR *dir = (_TDIR *)calloc(1, sizeof(_TDIR));
    if (dir == NULL) {
        return NULL;
    }

    dir->pattern = _emuleai_dirent_make_pattern(dirname);
    if (dir->pattern == NULL) {
        free(dir);
        return NULL;
    }

    dir->handle = FindFirstFile(dir->pattern, &dir->data);
    if (dir->handle == INVALID_HANDLE_VALUE) {
        free(dir->pattern);
        free(dir);
        return NULL;
    }

    dir->first = 1;
    return dir;
}

static inline struct _tdirent *_treaddir(_TDIR *dir)
{
    if (dir == NULL) {
        return NULL;
    }

    for (;;) {
        BOOL ok;
        if (dir->first) {
            dir->first = 0;
            ok = TRUE;
        } else {
            ok = FindNextFile(dir->handle, &dir->data);
        }

        if (!ok) {
            return NULL;
        }

        if (_emuleai_dirent_is_dot_name(dir->data.cFileName)) {
            continue;
        }

        _emuleai_dirent_fill_entry(dir);
        return &dir->entry;
    }
}

static inline int _tclosedir(_TDIR *dir)
{
    if (dir == NULL) {
        return -1;
    }

    if (dir->handle != INVALID_HANDLE_VALUE) {
        FindClose(dir->handle);
    }
    free(dir->pattern);
    free(dir);
    return 0;
}

#endif'''

COMPATHEADERCONTENT = r'''#ifndef EMULEAI_GNUTLS_COMPAT_H
#define EMULEAI_GNUTLS_COMPAT_H

#include <BaseTsd.h>
#include <direct.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>

#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

#ifndef _OFF_T_DEFINED
typedef long long off_t;
#define _OFF_T_DEFINED
#endif

#define mkdir(path, mode) _mkdir(path)

#ifdef __cplusplus
extern "C" {
#endif

void *memmem(const void *haystack, size_t haystack_len, const void *needle, size_t needle_len);
char *secure_getenv(const char *name);
int strcasecmp(const char *left, const char *right);
int strncasecmp(const char *left, const char *right, size_t count);
int strverscmp(const char *left, const char *right);
char *stpcpy(char *destination, const char *source);
void explicit_bzero(void *data, size_t size);
char *strtok_r(char *str, const char *delimiters, char **context);
ssize_t getline(char **line, size_t *capacity, FILE *stream);
struct tm *gmtime_r(const time_t *timer, struct tm *result);
char *read_file(const char *filename, int flags, size_t *length);
int vasprintf(char **result, const char *format, va_list args);
int asprintf(char **result, const char *format, ...);

#ifdef __cplusplus
}
#endif

#endif'''

COMPATSOURCECONTENT = r'''#include "compat.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef va_copy
#define va_copy(destination, source) ((destination) = (source))
#endif

void *memmem(const void *haystack, size_t haystack_len, const void *needle, size_t needle_len)
{
    const unsigned char *h = (const unsigned char *)haystack;
    const unsigned char *n = (const unsigned char *)needle;
    size_t i;

    if (needle_len == 0) {
        return (void *)h;
    }
    if (haystack_len < needle_len) {
        return NULL;
    }

    for (i = 0; i <= haystack_len - needle_len; ++i) {
        if (h[i] == n[0] && memcmp(h + i, n, needle_len) == 0) {
            return (void *)(h + i);
        }
    }

    return NULL;
}

char *secure_getenv(const char *name)
{
    return getenv(name);
}

int strcasecmp(const char *left, const char *right)
{
    return _stricmp(left, right);
}

int strncasecmp(const char *left, const char *right, size_t count)
{
    return _strnicmp(left, right, count);
}

char *stpcpy(char *destination, const char *source)
{
    size_t length = strlen(source);
    memcpy(destination, source, length + 1);
    return destination + length;
}

void explicit_bzero(void *data, size_t size)
{
    volatile unsigned char *p = (volatile unsigned char *)data;
    while (size-- != 0) {
        *p++ = 0;
    }
}

char *strtok_r(char *str, const char *delimiters, char **context)
{
    return strtok_s(str, delimiters, context);
}

ssize_t getline(char **line, size_t *capacity, FILE *stream)
{
    size_t length = 0;
    int ch;

    if (line == NULL || capacity == NULL || stream == NULL) {
        return -1;
    }

    if (*line == NULL || *capacity == 0) {
        *capacity = 128;
        *line = (char *)malloc(*capacity);
        if (*line == NULL) {
            *capacity = 0;
            return -1;
        }
    }

    while ((ch = fgetc(stream)) != EOF) {
        if (length + 1 >= *capacity) {
            size_t new_capacity = *capacity * 2;
            char *new_line = (char *)realloc(*line, new_capacity);
            if (new_line == NULL) {
                return -1;
            }
            *line = new_line;
            *capacity = new_capacity;
        }

        (*line)[length++] = (char)ch;
        if (ch == '\n') {
            break;
        }
    }

    if (length == 0 && ch == EOF) {
        return -1;
    }

    (*line)[length] = '\0';
    return (ssize_t)length;
}

struct tm *gmtime_r(const time_t *timer, struct tm *result)
{
    return gmtime_s(result, timer) == 0 ? result : NULL;
}

char *read_file(const char *filename, int flags, size_t *length)
{
    FILE *stream = NULL;
    char *data = NULL;
    __int64 file_size;
    size_t read_size;
    const int rf_binary = 0x1;
    const int rf_sensitive = 0x2;

    if (filename == NULL || length == NULL) {
        return NULL;
    }

    if (fopen_s(&stream, filename, (flags & rf_binary) ? "rb" : "r") != 0 || stream == NULL) {
        return NULL;
    }

    if (_fseeki64(stream, 0, SEEK_END) != 0) {
        fclose(stream);
        return NULL;
    }

    file_size = _ftelli64(stream);
    if (file_size < 0 || (unsigned long long)file_size > (unsigned long long)SIZE_MAX - 1ULL) {
        fclose(stream);
        return NULL;
    }

    if (_fseeki64(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }

    data = (char *)malloc((size_t)file_size + 1U);
    if (data == NULL) {
        fclose(stream);
        return NULL;
    }

    read_size = fread(data, 1, (size_t)file_size, stream);
    if (read_size != (size_t)file_size) {
        if (flags & rf_sensitive) {
            explicit_bzero(data, (size_t)file_size + 1U);
        }
        free(data);
        fclose(stream);
        return NULL;
    }

    data[read_size] = '\0';
    *length = read_size;
    fclose(stream);
    return data;
}

static int compare_version_number(const char **left, const char **right)
{
    unsigned long long left_value = 0;
    unsigned long long right_value = 0;

    while (**left == '0') {
        ++(*left);
    }
    while (**right == '0') {
        ++(*right);
    }

    while (isdigit((unsigned char)**left)) {
        left_value = (left_value * 10) + (unsigned int)(**left - '0');
        ++(*left);
    }
    while (isdigit((unsigned char)**right)) {
        right_value = (right_value * 10) + (unsigned int)(**right - '0');
        ++(*right);
    }

    if (left_value < right_value) {
        return -1;
    }
    if (left_value > right_value) {
        return 1;
    }

    return 0;
}

int strverscmp(const char *left, const char *right)
{
    while (*left != '\0' || *right != '\0') {
        if (isdigit((unsigned char)*left) && isdigit((unsigned char)*right)) {
            int cmp = compare_version_number(&left, &right);
            if (cmp != 0) {
                return cmp;
            }
        } else {
            unsigned char l = (unsigned char)*left++;
            unsigned char r = (unsigned char)*right++;
            if (l != r) {
                return (int)l - (int)r;
            }
        }
    }

    return 0;
}

int vasprintf(char **result, const char *format, va_list args)
{
    int length;
    va_list args_copy;

    va_copy(args_copy, args);
    length = _vscprintf(format, args_copy);
    va_end(args_copy);
    if (length < 0) {
        *result = NULL;
        return -1;
    }

    *result = (char *)malloc((size_t)length + 1);
    if (*result == NULL) {
        return -1;
    }

    va_copy(args_copy, args);
    if (vsnprintf_s(*result, (size_t)length + 1, _TRUNCATE, format, args_copy) < 0) {
        va_end(args_copy);
        free(*result);
        *result = NULL;
        return -1;
    }
    va_end(args_copy);

    return length;
}

int asprintf(char **result, const char *format, ...)
{
    int ret;
    va_list args;

    va_start(args, format);
    ret = vasprintf(result, format, args);
    va_end(args);

    return ret;
}'''

UNISTDCONTENT = r'''#ifndef EMULEAI_UNISTD_H
#define EMULEAI_UNISTD_H

#include <BaseTsd.h>
#include <direct.h>
#include <io.h>
#include <process.h>

#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

#define access _access
#define close _close
#define getpid _getpid
#define lseek _lseeki64
#define read _read
#define write _write

#endif'''

SYSTYPESCONTENT = r'''#ifndef EMULEAI_SYS_TYPES_H
#define EMULEAI_SYS_TYPES_H

#include <BaseTsd.h>
#include <corecrt.h>
#include <stddef.h>

#ifndef _INO_T_DEFINED
typedef unsigned short _ino_t;
typedef _ino_t ino_t;
#define _INO_T_DEFINED
#endif

#ifndef _DEV_T_DEFINED
typedef unsigned int _dev_t;
typedef _dev_t dev_t;
#define _DEV_T_DEFINED
#endif

#ifndef _OFF_T_DEFINED
typedef long _off_t;
typedef _off_t off_t;
#define _OFF_T_DEFINED
#endif

#ifndef _SSIZE_T_DEFINED
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

#endif'''

SYSSOCKETCONTENT = r'''#ifndef EMULEAI_SYS_SOCKET_H
#define EMULEAI_SYS_SOCKET_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#ifndef _SSIZE_T_DEFINED
#include <BaseTsd.h>
typedef SSIZE_T ssize_t;
#define _SSIZE_T_DEFINED
#endif

#ifndef _SOCKLEN_T_DEFINED
typedef int socklen_t;
#define _SOCKLEN_T_DEFINED
#endif

#endif'''

SYSTIMECONTENT = r'''#ifndef EMULEAI_SYS_TIME_H
#define EMULEAI_SYS_TIME_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>

static inline int gettimeofday(struct timeval *tv, void *tz)
{
    FILETIME ft;
    unsigned long long t;
    (void)tz;
    GetSystemTimeAsFileTime(&ft);
    t = ((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime;
    t -= 116444736000000000ULL;
    tv->tv_sec = (long)(t / 10000000ULL);
    tv->tv_usec = (long)((t % 10000000ULL) / 10ULL);
    return 0;
}

#endif'''

NETINETINCONTENT = r'''#ifndef EMULEAI_NETINET_IN_H
#define EMULEAI_NETINET_IN_H
#include <sys/socket.h>
#endif'''

NETINETTCPCONTENT = r'''#ifndef EMULEAI_NETINET_TCP_H
#define EMULEAI_NETINET_TCP_H
#include <sys/socket.h>
#ifndef TCP_FASTOPEN
#define TCP_FASTOPEN 15
#endif
#endif'''

ARPAINETCONTENT = r'''#ifndef EMULEAI_ARPA_INET_H
#define EMULEAI_ARPA_INET_H
#include <sys/socket.h>
#endif'''


def to_crlf(content: str) -> str:
    return content.replace("\r\n", "\n").replace("\r", "\n").replace("\n", "\r\n")


def write_ascii(path: Path, content: str) -> None:
    path.write_bytes(to_crlf(content).encode("ascii", errors="replace"))


def convert_unistring_header(source: Path, destination: Path) -> None:
    text = source.read_text(encoding="utf-8")
    text = text.replace("@HAVE_UNISTRING_WOE32DLL_H@", "0")
    text = re.sub(r"@[^@]+_DLL_VARIABLE@", "", text)
    text = re.sub(r"@[^@]+@", "0", text)
    write_ascii(destination, text)


def main() -> None:
    parser = argparse.ArgumentParser(description="Generate GnuTLS MSVC compatibility headers and sources.")
    parser.add_argument("-ProjectDir", required=True)
    parser.add_argument("-SourceRoot", default="")
    parser.add_argument("-GeneratedDir", default="")
    args = parser.parse_args()

    project_dir = Path(args.ProjectDir).resolve()
    source_root = Path(args.SourceRoot).resolve() if args.SourceRoot.strip() else project_dir.parent.resolve()
    gnutls_dir = source_root / "gnutls"
    generated_dir = Path(args.GeneratedDir) if args.GeneratedDir.strip() else source_root / "_Build" / "gnutls" / "Generated"
    generated_gnutls_dir = generated_dir / "gnutls"
    generated_sys_dir = generated_dir / "sys"
    generated_netinet_dir = generated_dir / "netinet"
    generated_arpa_dir = generated_dir / "arpa"
    for directory in (generated_gnutls_dir, generated_sys_dir, generated_netinet_dir, generated_arpa_dir):
        directory.mkdir(parents=True, exist_ok=True)

    configure = (gnutls_dir / "configure.ac").read_text(encoding="utf-8")
    match = re.search(r"AC_INIT\(\[GnuTLS\],\s*\[([0-9]+)\.([0-9]+)\.([0-9]+)", configure)
    if not match:
        raise RuntimeError("Cannot determine GnuTLS version from configure.ac")
    major, minor, patch = (int(value) for value in match.groups())
    version = f"{major}.{minor}.{patch}"
    number = f"0x{major:02x}{minor:02x}{patch:02x}"

    gnutls_header = (gnutls_dir / "lib" / "includes" / "gnutls" / "gnutls.h.in").read_text(encoding="utf-8")
    gnutls_header = gnutls_header.replace("@VERSION@", version)
    gnutls_header = gnutls_header.replace("@MAJOR_VERSION@", str(major))
    gnutls_header = gnutls_header.replace("@MINOR_VERSION@", str(minor))
    gnutls_header = gnutls_header.replace("@PATCH_VERSION@", str(patch))
    gnutls_header = gnutls_header.replace("@NUMBER_VERSION@", number)
    gnutls_header = gnutls_header.replace("@DEFINE_IOVEC_T@", "typedef struct { void *iov_base; size_t iov_len; } giovec_t;")
    gnutls_header = gnutls_header.replace("#if !defined(GNUTLS_INTERNAL_BUILD) && defined(_WIN32)", "#if !defined(GNUTLS_INTERNAL_BUILD) && !defined(GNUTLS_STATIC) && defined(_WIN32)")
    write_ascii(generated_gnutls_dir / "gnutls.h", gnutls_header)

    convert_unistring_header(gnutls_dir / "lib" / "unistring" / "unistr.in.h", generated_dir / "unistr.h")
    convert_unistring_header(gnutls_dir / "lib" / "unistring" / "uninorm.in.h", generated_dir / "uninorm.h")
    convert_unistring_header(gnutls_dir / "lib" / "unistring" / "unictype.in.h", generated_dir / "unictype.h")
    convert_unistring_header(gnutls_dir / "lib" / "unistring" / "unitypes.in.h", generated_dir / "unitypes.h")

    write_ascii(generated_dir / "config.h", CONFIGCONTENT.replace("$version", version))
    write_ascii(generated_dir / "byteswap.h", BYTESWAPCONTENT)
    write_ascii(generated_dir / "dirent.h", DIRENTCONTENT)
    write_ascii(generated_dir / "compat.h", COMPATHEADERCONTENT)
    write_ascii(generated_dir / "compat.c", COMPATSOURCECONTENT)
    write_ascii(generated_dir / "unistd.h", UNISTDCONTENT)
    write_ascii(generated_sys_dir / "types.h", SYSTYPESCONTENT)
    write_ascii(generated_sys_dir / "socket.h", SYSSOCKETCONTENT)
    write_ascii(generated_sys_dir / "time.h", SYSTIMECONTENT)
    write_ascii(generated_netinet_dir / "in.h", NETINETINCONTENT)
    write_ascii(generated_netinet_dir / "tcp.h", NETINETTCPCONTENT)
    write_ascii(generated_arpa_dir / "inet.h", ARPAINETCONTENT)


if __name__ == "__main__":
    main()
