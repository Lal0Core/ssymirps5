#!/usr/bin/env bash
# dav1d (BSD-2-Clause, third_party/dav1d) as a static library, for the AVIF images (the back covers): the PS5 build with
# the SDK's meson cross file, and the host build. No assembly, 8- and 10-bit.
# Usage: tools/build-dav1d.sh <out-dir> [ps5|host]
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
out=$1
kind=${2:-ps5}
opts=(--default-library=static --buildtype=release -Denable_asm=false -Dbitdepths=8,16 -Denable_tools=false
      -Denable_tests=false -Denable_examples=false -Dlogging=false)
if [[ $kind == ps5 ]]; then
    "${PS5_PAYLOAD_SDK:?}/bin/prospero-meson" setup "$out" "$here/third_party/dav1d" "${opts[@]}" "-Dc_args=['-march=znver2','-fPIC']"
else
    meson setup "$out" "$here/third_party/dav1d" "${opts[@]}" "-Dc_args=['-fPIC']"
fi
ninja -C "$out"
