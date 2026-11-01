#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <wchar.h>
/* Rename the shim's libc-shadowing names so the host libc stays intact. */
#define snprintf_l t_snprintf_l
#define asprintf_l t_asprintf_l
#define sscanf_l t_sscanf_l
#define strtod_l t_strtod_l
#define strtof_l t_strtof_l
#define strtold_l t_strtold_l
#define strtoll_l t_strtoll_l
#define strtoull_l t_strtoull_l
#define strftime_l t_strftime_l
#define mbrtowc_l t_mbrtowc_l
#define wcrtomb_l t_wcrtomb_l
#define mbsrtowcs_l t_mbsrtowcs_l
#define mbrlen_l t_mbrlen_l
#define mbtowc_l t_mbtowc_l
#define mbsnrtowcs_l t_mbsnrtowcs_l
#define wcsnrtombs_l t_wcsnrtombs_l
#define freelocale t_freelocale
#define catopen t_catopen
#define catgets t_catgets
#define catclose t_catclose
#define _Unwind_RaiseException t_URE
#define _Unwind_DeleteException t_UDE
#define _Unwind_Resume t_UR
#define _Unwind_GetIP t_UGIP
#define _Unwind_SetIP t_USIP
#define _Unwind_SetGR t_USGR
#define _Unwind_GetLanguageSpecificData t_ULSDA
#define _Unwind_GetRegionStart t_URS
#include "../tooling/ymir/ps5_core_locale.c"

static int bad;
#define CHECK(c, ...) do { if (!(c)) { bad++; printf("FAIL: " __VA_ARGS__); puts(""); } } while (0)

int main(void) {
    /* 1. Rune table vs glibc "C" classification. */
    setlocale(LC_ALL, "C");
    const unsigned long *t = _DefaultRuneLocale.__runetype;
    for (int c = 0; c < 256; c++) {
        int ok = (!!(t[c] & 0x100) == !!isalpha(c)) && (!!(t[c] & 0x200) == !!iscntrl(c)) &&
                 (!!(t[c] & 0x400) == !!isdigit(c)) && (!!(t[c] & 0x800) == !!isgraph(c)) &&
                 (!!(t[c] & 0x1000) == !!islower(c)) && (!!(t[c] & 0x2000) == !!ispunct(c)) &&
                 (!!(t[c] & 0x4000) == !!isspace(c)) && (!!(t[c] & 0x8000) == !!isupper(c)) &&
                 (!!(t[c] & 0x10000) == !!isxdigit(c)) && (!!(t[c] & 0x20000) == !!isblank(c)) &&
                 (!!(t[c] & 0x40000) == !!isprint(c)) &&
                 _DefaultRuneLocale.__maplower[c] == tolower(c) && _DefaultRuneLocale.__mapupper[c] == toupper(c);
        CHECK(ok, "ctype mismatch at %d", c);
    }
    CHECK((t['7'] & 0xFF) == 7 && (t['f'] & 0xFF) == 15 && (t['C'] & 0xFF) == 12, "digit values");

    /* 2. Multibyte conversions vs glibc, in a UTF-8 locale (the harder case). */
    setlocale(LC_ALL, "C.UTF-8");
    const char *inputs[] = {"", "abc", "Ñandú café", "サターン!", "x\xF0\x9F\x98\x80y", "bad\xFFtail", "cut\xE3\x82"};
    for (size_t i = 0; i < sizeof inputs / sizeof *inputs; i++) {
        for (size_t nms = 0; nms <= strlen(inputs[i]) + 1; nms++) {
            for (size_t len = 0; len <= 12; len++) {
                wchar_t a[16] = {0}, b[16] = {0};
                const char *sa = inputs[i], *sb = inputs[i];
                mbstate_t ma = {0}, mb = {0};
                size_t ra = t_mbsnrtowcs_l(a, &sa, nms, len, &ma, NULL);
                size_t rb = mbsnrtowcs(b, &sb, nms, len, &mb);
                CHECK(ra == rb && sa == sb && !memcmp(a, b, sizeof a), "mbsnrtowcs input %zu nms %zu len %zu: %zd/%zd",
                      i, nms, len, (ssize_t)ra, (ssize_t)rb);
            }
            const char *sa = inputs[i], *sb = inputs[i];
            mbstate_t ma = {0}, mb = {0};
            CHECK(t_mbsnrtowcs_l(NULL, &sa, nms, 0, &ma, NULL) == mbsnrtowcs(NULL, &sb, nms, 0, &mb), "count mode");
        }
    }
    const wchar_t *winputs[] = {L"", L"abc", L"Ñandú café", L"サターン!", L"x\U0001F600y"};
    for (size_t i = 0; i < sizeof winputs / sizeof *winputs; i++) {
        for (size_t nwc = 0; nwc <= wcslen(winputs[i]) + 1; nwc++) {
            for (size_t len = 0; len <= 24; len++) {
                char a[32] = {0}, b[32] = {0};
                const wchar_t *sa = winputs[i], *sb = winputs[i];
                mbstate_t ma = {0}, mb = {0};
                size_t ra = t_wcsnrtombs_l(a, &sa, nwc, len, &ma, NULL);
                size_t rb = wcsnrtombs(b, &sb, nwc, len, &mb);
                CHECK(ra == rb && sa == sb && !memcmp(a, b, sizeof a), "wcsnrtombs input %zu nwc %zu len %zu: %zd/%zd",
                      i, nwc, len, (ssize_t)ra, (ssize_t)rb);
            }
        }
    }

    /* 3. sscanf_l "%p", formatted output, numbers. */
    void *p = NULL;
    CHECK(t_sscanf_l("0x7ffdeadbeef0", NULL, "%p", &p) == 1 && p == (void *)0x7ffdeadbeef0, "sscanf %%p");
    CHECK(t_sscanf_l("  zz", NULL, "%p", &p) == 0, "sscanf %%p junk");
    CHECK(t_sscanf_l("", NULL, "%p", &p) == EOF, "sscanf %%p empty");
    char buf[32];
    CHECK(t_snprintf_l(buf, sizeof buf, NULL, "%d-%s-%.2f", 42, "ok", 1.5) == 10 && !strcmp(buf, "42-ok-1.50"), "snprintf_l");
    CHECK(t_strtoll_l("-123", NULL, 10, NULL) == -123 && t_strtold_l("2.5", NULL, NULL) == 2.5L, "strto*_l");
    CHECK(t_catopen("x", 0) == (void *)-1 && !strcmp(t_catgets((void *)-1, 1, 1, "dflt"), "dflt"), "catalogs");
    printf("locale layer tests: %s\n", bad ? "FAIL" : "PASS");
    return bad != 0;
}
