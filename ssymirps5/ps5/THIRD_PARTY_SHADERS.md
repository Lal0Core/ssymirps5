# Third-party CRT shaders

Ymir (GPL-3.0) is ported to PS5 with its GPU renderer running RetroArch
slang presets. This document tracks the licenses of included third-party shaders
to ensure GPL-3.0 compliance.

## License Compatibility

This project is licensed under GPL-3.0. All third-party code must be compatible:

- **Public domain / CC0 / Unlicense**: ✅ Fully compatible
- **MIT**: ✅ Fully compatible
- **GPL-2.0-or-later / GPL-3.0**: ✅ Fully compatible (copyleft chain)
- **Non-commercial or proprietary**: ❌ Not compatible (excluded)

## CPU-rendered shaders (original C++ rewrites)

Drawn on the CPU by `coreorbis/orbis-shims/ProsperoCrt.cpp`: C++ rewrites of shaders from libretro's slang-shaders
collection (https://github.com/libretro/slang-shaders, folder `crt/`), kept with their default parameters.

| Shader | Source in slang-shaders | Author | Licence |
|---|---|---|---|
| crt-lottes | `crt/shaders/crt-lottes.slang` | Timothy Lottes | public domain |
| crt-lottes-fast | `crt/shaders/crt-lottes-fast.slang` ([CRTS] 20180120b, adapted for RetroArch by hunterk) | Timothy Lottes | Unlicense (public domain) |
| crt-1tap | `crt/shaders/crt-Ntap/crt-1tap.slang` (v1.4) | fishku | public domain (CC0) |
| crt-2tap | `crt/shaders/crt-Ntap/crt-2tap.slang` (v1.0) | fishku | public domain (CC0) |
| monoCRT | `crt/shaders/monoCRT.slang` | hunterk | public domain |
| newpixie-mini | `crt/shaders/newpixie-mini/newpixie-mini.slang` (adapted for slang by hunterk) | Mattias Gustavsson | Unlicense or MIT (used here under the Unlicense) |
| crt-hyllian-fast | `crt/shaders/hyllian/crt-hyllian-fast.slang` (ported to GLSL/slang by DariusG & hunterk) | Hyllian | MIT |
| crt-nobody | `crt/shaders/crt-nobody.slang` | Hyllian | MIT |
| crt-blurPi | `crt/shaders/crt-blurPi.slang` | Oriol Ferrer Mesià (armadillu) | MIT |

All are licensed under public domain, Unlicense, CC0, or MIT. "CRT Easymode style"
is original code (MIT) inspired by but not derived from crt-easymode.

## GPU-rendered shaders (libretro slang-shaders)

Run by the GPU renderer (`coreorbis/orbis-shims/ProsperoGpu.cpp`, with the RADV build) from `shaders/slang/`,
compiled to SPIR-V at build time (`tools/slang_compile.py`, `shaders/presets.txt`). Each file keeps the licence
written at its top.

| Preset (`shaders/presets.txt`) | Licence of its shaders |
|---|---|
| crt-lottes, crt-lottes-fast, crt-1tap, crt-2tap, monoCRT | public domain / Unlicense / CC0 |
| newpixie-mini, crt-hyllian-fast, crt-hyllian, crt-nobody, crt-blurPi-sharp/soft, ScaleFX | MIT |
| crt-easymode, crt-easymode-halation, crt-aperture | GPL, no version given (the GPL lets any published version be chosen: used under GPL-3.0) |
| crt-guest-advanced (and -fast, -ntsc), crt-consumer | GPL-2.0-or-later |
| crt-geom, crt-pi, zfast-crt | GPL-2.0-or-later |
| crt-royale | GPL-2.0-or-later |
| crt-yah | MIT (its core files, `crt-yah.stage-f.core.h` and `crt-yah.stage-v.core.h`; its other passes carry no licence header) |
| Mega Bezel (Potato, Standard) | GPL-3.0-or-later and the licences of the shaders it includes (Guest, Dogway, Hyllian...) |

The plain picture and the menu overlay (`shaders/plain.slang`, `shaders/ui.slang`) are this port's own (MIT).

Shaders are used as-is from libretro's slang-shaders (commit e1d75632a205c70f14a4cc947c46d5abb7b3f7f1, 8 October 2026).
All listed shaders are GPL-3.0 compatible and are distributed under this project's
GPL-3.0 license, consistent with their original licenses.

### Note on GPL shaders

GPL-licensed shaders (crt-easymode, crt-guest-advanced, Mega Bezel, etc.)
are fully compatible with Ymir's GPL-3.0 license. No legal conflict exists.

## Licence notices

The MIT licence asks that its copyright notice and permission notice be kept with every copy; the notices of the
shaders above follow.

### Unlicense (crt-lottes-fast, newpixie-mini)

This is free and unencumbered software released into the public domain.

Anyone is free to copy, modify, publish, use, compile, sell, or distribute this software, either in source code
form or as a compiled binary, for any purpose, commercial or non-commercial, and by any means.

In jurisdictions that recognize copyright laws, the author or authors of this software dedicate any and all
copyright interest in the software to the public domain. We make this dedication for the benefit of the public at
large and to the detriment of our heirs and successors. We intend this dedication to be an overt act of
relinquishment in perpetuity of all present and future rights to this software under copyright law.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS BE
LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### MIT (crt-hyllian-fast, crt-nobody, crt-blurPi)

crt-hyllian-fast: Copyright (C) 2011-2015 Hyllian - sergiogdb@gmail.com
crt-nobody: Copyright (C) 2011-2025 Hyllian - sergiogdb@gmail.com
crt-blurPi: Made by Oriol Ferrer Mesià (armadillu), http://uri.cat

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated
documentation files (the "Software"), to deal in the Software without restriction, including without limitation the
rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to
permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of
the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE
WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR
COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

### CC0 (crt-1tap, crt-2tap) and public domain (crt-lottes, monoCRT)

crt-1tap and crt-2tap: "Copyright (C) 2023-2026, Public domain license (CC0)" -- fishku. crt-lottes: "PUBLIC
DOMAIN CRT STYLED SCAN-LINE SHADER by Timothy Lottes". monoCRT: "by hunterk, license: public domain".
