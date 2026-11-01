/* Core-local "C" locale layer and unwinder stubs for the PS5 port of the Ymir core.
 *
 * The core links libc++ privately. On FreeBSD (the PS5 target) libc++ calls the
 * xlocale *_l functions, reads _DefaultRuneLocale, and needs a few message
 * catalog and unwinder entry points. PS5_RetroArch's title binds none of these
 * names for cores (checked against an installed eboot.bin), so the core defines
 * them itself, hidden, on top of calls the title does bind (vsnprintf, strtod,
 * mbrtowc, wcrtomb, ...). Only the "C" locale exists here: libretro cores never
 * switch locales, and the console's libc runs in "C" as well. */
#include <errno.h>
#include <limits.h>
#include <runetype.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>

#include "ps5_rune_tables.h"

#define HIDDEN __attribute__((visibility("hidden")))

typedef struct _xlocale *ps5_locale_t;

/* ---- rune tables ---------------------------------------------------------- */
/* libc++'s ctype<char>::classic_table() and the FreeBSD ctype inlines read these. */
HIDDEN const _RuneLocale _DefaultRuneLocale = {
    .__magic = "RuneMagi",
    .__encoding = "NONE",
    .__invalid_rune = 0xFFFD,
    .__runetype = {PS5_C_RUNETYPE},
    .__maplower = {PS5_C_MAPLOWER},
    .__mapupper = {PS5_C_MAPUPPER},
};

HIDDEN const _RuneLocale *__runes_for_locale(ps5_locale_t locale, int *mb_sb_limit) {
    (void)locale;
    if (mb_sb_limit)
        *mb_sb_limit = 256;
    return &_DefaultRuneLocale;
}

HIDDEN int ___mb_cur_max_l(ps5_locale_t locale) {
    (void)locale;
    return 1;
}

HIDDEN void freelocale(ps5_locale_t locale) {
    (void)locale;
}

/* ---- formatted I/O ---------------------------------------------------------- */
HIDDEN int snprintf_l(char *buffer, size_t size, ps5_locale_t locale, const char *format, ...) {
    (void)locale;
    va_list args;
    va_start(args, format);
    const int result = vsnprintf(buffer, size, format, args);
    va_end(args);
    return result;
}

HIDDEN int asprintf_l(char **out, ps5_locale_t locale, const char *format, ...) {
    (void)locale;
    va_list args;
    va_start(args, format);
    const int result = vasprintf(out, format, args);
    va_end(args);
    return result;
}

/* libc++ uses sscanf_l only for num_get<>::do_get(void*&), with "%p". The title
 * binds no vsscanf to forward other formats to, so only that one is parsed. */
HIDDEN int sscanf_l(const char *string, ps5_locale_t locale, const char *format, ...) {
    (void)locale;
    if (strcmp(format, "%p") != 0)
        return 0;
    while (*string == ' ' || (*string >= '\t' && *string <= '\r'))
        ++string;
    if (*string == '\0')
        return EOF;
    char *end = NULL;
    const unsigned long long value = strtoull(string, &end, 16);
    if (end == string)
        return 0;
    va_list args;
    va_start(args, format);
    *va_arg(args, void **) = (void *)(uintptr_t)value;
    va_end(args);
    return 1;
}

/* ---- number parsing ----------------------------------------------------------- */
HIDDEN double strtod_l(const char *s, char **end, ps5_locale_t locale) {
    (void)locale;
    return strtod(s, end);
}

HIDDEN float strtof_l(const char *s, char **end, ps5_locale_t locale) {
    (void)locale;
    return strtof(s, end);
}

/* No strtold in the title's table: parsed at double precision. */
HIDDEN long double strtold_l(const char *s, char **end, ps5_locale_t locale) {
    (void)locale;
    return strtod(s, end);
}

HIDDEN long long strtoll_l(const char *s, char **end, int base, ps5_locale_t locale) {
    (void)locale;
    return strtoll(s, end, base);
}

HIDDEN unsigned long long strtoull_l(const char *s, char **end, int base, ps5_locale_t locale) {
    (void)locale;
    return strtoull(s, end, base);
}

/* ---- time ------------------------------------------------------------------------- */
HIDDEN size_t strftime_l(char *buffer, size_t size, const char *format, const struct tm *time,
                         ps5_locale_t locale) {
    (void)locale;
    return strftime(buffer, size, format, time);
}

/* ---- multibyte conversions ----------------------------------------------------------- */
static mbstate_t *state_or(mbstate_t *ps, mbstate_t *fallback) {
    return ps ? ps : fallback;
}

HIDDEN size_t mbrtowc_l(wchar_t *pwc, const char *s, size_t n, mbstate_t *ps, ps5_locale_t locale) {
    (void)locale;
    return mbrtowc(pwc, s, n, ps);
}

HIDDEN size_t wcrtomb_l(char *s, wchar_t wc, mbstate_t *ps, ps5_locale_t locale) {
    (void)locale;
    return wcrtomb(s, wc, ps);
}

HIDDEN size_t mbsrtowcs_l(wchar_t *dst, const char **src, size_t len, mbstate_t *ps, ps5_locale_t locale) {
    (void)locale;
    return mbsrtowcs(dst, src, len, ps);
}

HIDDEN size_t mbrlen_l(const char *s, size_t n, mbstate_t *ps, ps5_locale_t locale) {
    static mbstate_t internal;
    (void)locale;
    return mbrtowc(NULL, s, n, state_or(ps, &internal));
}

HIDDEN int mbtowc_l(wchar_t *pwc, const char *s, size_t n, ps5_locale_t locale) {
    static mbstate_t internal;
    (void)locale;
    if (s == NULL) {
        memset(&internal, 0, sizeof(internal));
        return 0; /* stateless encoding */
    }
    const size_t result = mbrtowc(pwc, s, n, &internal);
    if (result == (size_t)-1 || result == (size_t)-2) {
        memset(&internal, 0, sizeof(internal));
        errno = EILSEQ;
        return -1;
    }
    return (int)result;
}

/* FreeBSD's __mbsnrtowcs_std, over mbrtowc. */
HIDDEN size_t mbsnrtowcs_l(wchar_t *dst, const char **src, size_t nms, size_t len, mbstate_t *ps,
                           ps5_locale_t locale) {
    static mbstate_t internal;
    (void)locale;
    ps = state_or(ps, &internal);
    const char *s = *src;
    size_t nchr = 0, nb;
    wchar_t wc;
    if (dst == NULL) {
        for (;;) {
            if ((nb = mbrtowc(&wc, s, nms, ps)) == (size_t)-1)
                return (size_t)-1; /* invalid sequence; errno set by mbrtowc */
            if (nb == 0 || nb == (size_t)-2)
                return nchr;
            s += nb;
            nms -= nb;
            nchr++;
        }
    }
    while (len-- > 0) {
        if ((nb = mbrtowc(dst, s, nms, ps)) == (size_t)-1) {
            *src = s;
            return (size_t)-1;
        } else if (nb == (size_t)-2) {
            *src = s + nms;
            return nchr;
        } else if (nb == 0) {
            *src = NULL;
            return nchr;
        }
        s += nb;
        nms -= nb;
        nchr++;
        dst++;
    }
    *src = s;
    return nchr;
}

/* FreeBSD's __wcsnrtombs_std, over wcrtomb. */
HIDDEN size_t wcsnrtombs_l(char *dst, const wchar_t **src, size_t nwc, size_t len, mbstate_t *ps,
                           ps5_locale_t locale) {
    static mbstate_t internal;
    enum { BUF = 16 }; /* >= MB_LEN_MAX of any locale this libc ships */
    (void)locale;
    ps = state_or(ps, &internal);
    char buf[BUF];
    const wchar_t *s = *src;
    size_t nbytes = 0, nb;
    if (dst == NULL) {
        while (nwc-- > 0) {
            if ((nb = wcrtomb(buf, *s, ps)) == (size_t)-1)
                return (size_t)-1;
            if (*s == L'\0')
                return nbytes + nb - 1;
            s++;
            nbytes += nb;
        }
        return nbytes;
    }
    while (len > 0 && nwc-- > 0) {
        if (len > BUF) {
            /* Enough space to translate in place. */
            if ((nb = wcrtomb(dst, *s, ps)) == (size_t)-1) {
                *src = s;
                return (size_t)-1;
            }
        } else {
            /* May not be enough space: translate into a buffer first. */
            const mbstate_t saved = *ps;
            if ((nb = wcrtomb(buf, *s, ps)) == (size_t)-1) {
                *src = s;
                return (size_t)-1;
            }
            if (nb > len) {
                *ps = saved; /* doesn't fit: back out */
                break;
            }
            memcpy(dst, buf, nb);
        }
        if (*s == L'\0') {
            *src = NULL;
            return nbytes + nb - 1;
        }
        s++;
        dst += nb;
        len -= nb;
        nbytes += nb;
    }
    *src = s;
    return nbytes;
}

/* ---- message catalogs: none on the console ------------------------------------------ */
typedef void *ps5_nl_catd;

HIDDEN ps5_nl_catd catopen(const char *name, int flag) {
    (void)name, (void)flag;
    errno = ENOENT;
    return (ps5_nl_catd)-1;
}

HIDDEN char *catgets(ps5_nl_catd catd, int set, int message, const char *fallback) {
    (void)catd, (void)set, (void)message;
    return (char *)fallback;
}

HIDDEN int catclose(ps5_nl_catd catd) {
    (void)catd;
    errno = EBADF;
    return -1;
}

/* ---- unwinder: the core is built without exceptions ----------------------------------
 * libc++abi's __cxa_throw still references the Itanium unwinder. With no handler
 * anywhere in the core, an exception thrown inside libc++ must end in
 * std::terminate: _Unwind_RaiseException reports the end of the stack, after
 * which __cxa_throw calls std::terminate itself. The personality-routine helpers
 * are never reached. */
struct _Unwind_Exception;
typedef void (*ps5_unwind_cleanup)(int, struct _Unwind_Exception *);
struct _Unwind_Exception {
    uint64_t exception_class;
    ps5_unwind_cleanup exception_cleanup;
    uintptr_t private_1, private_2;
} __attribute__((aligned(16)));
struct _Unwind_Context;
enum { URC_FOREIGN_EXCEPTION_CAUGHT = 1, URC_END_OF_STACK = 5 };

HIDDEN int _Unwind_RaiseException(struct _Unwind_Exception *exception) {
    (void)exception;
    return URC_END_OF_STACK;
}

HIDDEN void _Unwind_DeleteException(struct _Unwind_Exception *exception) {
    if (exception->exception_cleanup)
        exception->exception_cleanup(URC_FOREIGN_EXCEPTION_CAUGHT, exception);
}

HIDDEN void _Unwind_Resume(struct _Unwind_Exception *exception) {
    (void)exception;
    abort();
}

HIDDEN uintptr_t _Unwind_GetIP(struct _Unwind_Context *context) {
    (void)context;
    abort();
}

HIDDEN void _Unwind_SetIP(struct _Unwind_Context *context, uintptr_t value) {
    (void)context, (void)value;
    abort();
}

HIDDEN void _Unwind_SetGR(struct _Unwind_Context *context, int index, uintptr_t value) {
    (void)context, (void)index, (void)value;
    abort();
}

HIDDEN uintptr_t _Unwind_GetLanguageSpecificData(struct _Unwind_Context *context) {
    (void)context;
    abort();
}

HIDDEN uintptr_t _Unwind_GetRegionStart(struct _Unwind_Context *context) {
    (void)context;
    abort();
}
