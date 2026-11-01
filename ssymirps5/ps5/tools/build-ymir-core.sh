#!/usr/bin/env bash
# Builds Ymir's core (Emir, the libretro branch of ymir-emu/Ymir, with the PS5 patch) as static libraries for the
# app's eboot: libymir-core.a, its vendored libraries (libchdr with zstd/miniz/LZMA, fmt, xxHash) and the three
# objects of apps/ymir-libretro (the libretro API the frontend drives).
#
# Usage: tools/build-ymir-core.sh <ymir-source> <build-dir>
#   <ymir-source>  the patched source tree (../../ymir-ps5/tools/prepare-source.sh makes it)
#   <build-dir>    where CMake builds (the Makefile's YMIR_BUILD)
# Environment: PS5_PAYLOAD_SDK (the same SDK as the app: PS5_Vulkan's for the GPU build), JOBS.
#
# SPDX-License-Identifier: GPL-3.0-or-later
set -euo pipefail
src=$(cd -- "$1" && pwd)
out=$2
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
tooling=${YMIR_TOOLING:-$here/ymir}
sdk=${PS5_PAYLOAD_SDK:?set PS5_PAYLOAD_SDK}
mkdir -p "$out/../empty-libs"
for n in m rt pthread dl util c++ c++abi; do
    [[ -f $out/../empty-libs/lib$n.a ]] || "$sdk/bin/prospero-ar" rc "$out/../empty-libs/lib$n.a"
done
export PS5_PAYLOAD_SDK=$sdk PS5_YMIR_WORK=$(cd -- "$out/.." && pwd) PS5_CORE_LD=$tooling/ps5-core.ld
cmake -S "$src" -B "$out" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$tooling/ps5-toolchain.cmake" -DCMAKE_BUILD_TYPE=Release -DPS5=ON \
    -DPS5_PORT_DIR="$tooling" -DPS5_RETROARCH_DIR="${PS5_RETROARCH_DIR:-$tooling}" \
    -DYmir_ENABLE_LIBRETRO=ON -DYmir_DEV_BUILD=OFF -DYmir_ENABLE_GPU_API=OFF -DYmir_ENABLE_IPO=OFF \
    -DYmir_EXTRA_INLINING=ON -DYmir_AVX2=ON -DYmir_ENABLE_TESTS=OFF -DYmir_ENABLE_SANDBOX=OFF -DYmir_ENABLE_YMDASM=OFF
obj=apps/ymir-libretro/CMakeFiles/ymir_libretro.dir/src
ninja -C "$out" -j "${JOBS:-$(nproc)}" ymir-core "$obj/libretro.cpp.o" "$obj/cdrom_loader.cpp.o" "$obj/vfs.cpp.o"
echo "==> Ymir's core built in $out"
