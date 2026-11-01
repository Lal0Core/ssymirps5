#!/usr/bin/env bash
# Fetch the pinned Emir source (Ymir's libretro core) and apply the PS5 patch.
# Usage: tools/prepare-source.sh [target-directory]   (default: ./source)
set -euo pipefail
root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)
target=${1:-$root/source}
url=https://github.com/warmenhoven/Emir.git
revision=8174cbbeea4070654841cdd4604361b3eee8fdd1   # branch libretro, 2026-10-08
# Only the submodules the libretro core builds (no vcpkg, imgui or wil).
submodules=(vendor/concurrentqueue/concurrentqueue vendor/fmt vendor/libchdr/libchdr
            vendor/lz4/lz4 vendor/mio vendor/stb/stb vendor/xxHash/xxHash)

if [[ -e $target ]]; then
    echo "error: $target exists; remove it to prepare a fresh tree" >&2
    exit 2
fi
git init -q "$target"
git -C "$target" remote add origin "$url"
git -C "$target" fetch -q --depth 1 origin "$revision"
git -C "$target" checkout -q FETCH_HEAD
git -C "$target" submodule update -q --init --depth 1 -- "${submodules[@]}"
git -C "$target" apply --whitespace=nowarn "$root/tooling/ymir/native.patch"
echo "==> [ymir] source at $revision with the PS5 patch: $target"
