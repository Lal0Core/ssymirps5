#!/usr/bin/env bash
# Ymir (Sega Saturn) libretro core for PS5_RetroArch, built as a native shared
# core with the software VDP renderer. Upstream: warmenhoven/Emir (libretro
# branch of ymir-emu/Ymir), GPL-3.0.
#
# Usage: tools/build-ymir.sh
# Environment:
#   PS5_RETROARCH_DIR  checkout of mihawk-99/PS5_RetroArch with `make deps` done
#                      (default: ../PS5_RetroArch next to this repo)
#   YMIR_SOURCE        prepared Emir source tree (default: ./source, created
#                      by tools/prepare-source.sh when missing)
#   PS5_TITLE_EBOOT    optional eboot.bin of the installed title: checks that it
#                      binds every import of the core
#   JOBS               parallel jobs (default: nproc)
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
retroarch=${PS5_RETROARCH_DIR:-$root/../PS5_RetroArch}
retroarch=$(cd -- "$retroarch" && pwd)
source_dir=${YMIR_SOURCE:-$root/source}
[[ -d $source_dir ]] || bash "$root/tools/prepare-source.sh" "$source_dir"
sdk="$retroarch/.deps/native/ps5-payload-sdk"
[[ -x $sdk/bin/prospero-clang ]] || { echo "error: run 'make deps' in $retroarch first" >&2; exit 2; }
for tool in cmake ninja readelf llvm-objdump; do
    command -v "$tool" >/dev/null || { echo "error: $tool is required" >&2; exit 2; }
done
# compiler-rt builtins, when installed (libclang-rt-18-dev): only needed if the
# compiler emits a helper call such as 128-bit division; the import check below
# reports any such call left unresolved.
builtins="$(clang-18 --print-resource-dir)/lib/linux/libclang_rt.builtins-x86_64.a"
[[ -f $builtins ]] || builtins=""

work="$root/build/ymir"
mkdir -p "$work/empty-libs" "$root/dist/cores" "$root/dist/info"
# CMake and the vendored libraries ask for these; the console runtime has them.
for name in m rt pthread dl util c++ c++abi; do
    [[ -f $work/empty-libs/lib$name.a ]] || "$sdk/bin/prospero-ar" rc "$work/empty-libs/lib$name.a"
done

export PS5_PAYLOAD_SDK="$sdk" PS5_CLANG=$(command -v clang-18) PS5_YMIR_WORK="$work"
export PS5_CORE_LD="$root/tooling/ymir/ps5-core.ld"
cmake -S "$source_dir" -B "$work/ps5-build" -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE="$root/tooling/ymir/ps5-toolchain.cmake" \
    -DCMAKE_BUILD_TYPE=Release -DPS5=ON \
    -DPS5_PORT_DIR="$root/tooling/ymir" -DPS5_RETROARCH_DIR="$retroarch" \
    -DPS5_COMPILER_BUILTINS="$builtins" \
    -DYmir_ENABLE_LIBRETRO=ON -DYmir_DEV_BUILD=OFF -DYmir_ENABLE_GPU_API=OFF \
    -DYmir_ENABLE_IPO=OFF -DYmir_EXTRA_INLINING=OFF -DYmir_AVX2=ON \
    -DYmir_ENABLE_TESTS=OFF -DYmir_ENABLE_SANDBOX=OFF -DYmir_ENABLE_YMDASM=OFF
cmake --build "$work/ps5-build" --target ymir_libretro --parallel "${JOBS:-$(nproc)}"

built="$work/ps5-build/apps/ymir-libretro/ymir_libretro.so"
python3 "$retroarch/tools/check-core.py" "$built" --report "$work/abi.json"
cp -- "$built" "$root/dist/cores/ymir_libretro.so"
cp -- "$root/tooling/ymir/ymir_libretro.info" "$root/dist/info/ymir_libretro.info"
cp -- "$source_dir/LICENSE" "$root/dist/info/ymir-LICENSE.txt"
python3 "$root/tools/check-imports.py" "$root/dist/cores/ymir_libretro.so" \
    ${PS5_TITLE_EBOOT:+--title "$PS5_TITLE_EBOOT"}
echo '==> [ymir] shared core built and ABI checked; runtime behavior requires console validation'
