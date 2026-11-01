/* Host test stand-in for FreeBSD's runetype.h (same layout as the PS5 SDK's). */
#pragma once
#include <stddef.h>
typedef int __rune_t;
typedef size_t __size_t;
#define _CACHED_RUNES 256
typedef struct { __rune_t __min, __max, __map; unsigned long *__types; } _RuneEntry;
typedef struct { int __nranges; _RuneEntry *__ranges; } _RuneRange;
typedef struct {
    char __magic[8]; char __encoding[32];
    __rune_t (*__sgetrune)(const char *, __size_t, char const **);
    int (*__sputrune)(__rune_t, char *, __size_t, char **);
    __rune_t __invalid_rune;
    unsigned long __runetype[_CACHED_RUNES];
    __rune_t __maplower[_CACHED_RUNES];
    __rune_t __mapupper[_CACHED_RUNES];
    _RuneRange __runetype_ext, __maplower_ext, __mapupper_ext;
    void *__variable; int __variable_len;
} _RuneLocale;
