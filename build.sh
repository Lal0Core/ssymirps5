#!/usr/bin/env bash
# SSYmirPS5: compila todo con un comando.
#   bash build.sh          -> núcleo de Ymir para PS5 (solo la 1.ª vez o con --core) + la app: make dist
#   bash build.sh --core   -> vuelve a compilar el núcleo de Ymir (tras cambiar ymir-ps5/source)
#   bash build.sh --host   -> además la versión de PC para pruebas (make host) y su núcleo
# El resultado queda en ssymirps5/ps5/build/dist/ (el .elf y ppsa99378-SSYmirPS5-v<versión>.zip para FTP).
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
export PATH=$HOME/.local/bin:$PATH
export LLVM_CONFIG=$(command -v llvm-config-18)   # el SDK usa la versión 18 aunque haya otras instaladas
JOBS=${JOBS:-$(nproc)}
VK=$here/PS5_Vulkan
SDK=$VK/.deps/native/ps5-payload-sdk
APP=$here/ssymirps5/ps5
CORE_OUT=$here/ssymirps5/build-core/ps5
HOST_CORE=$here/ymir-ps5/build/host-noexc
core=0 host=0
for a in "$@"; do
    case $a in --core) core=1 ;; --host) host=1 ;; *) echo "opción desconocida: $a"; exit 1 ;; esac
done
[[ -d $SDK ]] || { echo "Falta el SDK: ejecuta primero  bash setup.sh"; exit 1; }
t0=$(date +%s)

if [[ $core == 1 || ! -f $CORE_OUT/libs/ymir-core/libymir-core.a ]]; then
    echo "==> Núcleo de Ymir para PS5 ($JOBS hilos)"
    JOBS=$JOBS YMIR_TOOLING=$here/ymir-ps5/tooling/ymir PS5_RETROARCH_DIR=$here/PS5_RetroArch PS5_PAYLOAD_SDK=$SDK \
        bash "$APP/tools/build-ymir-core.sh" "$here/ymir-ps5/source" "$CORE_OUT"
fi

if [[ $host == 1 ]]; then
    if [[ $core == 1 || ! -f $HOST_CORE/libs/ymir-core/libymir-core.a ]]; then
        echo "==> Núcleo de Ymir para PC"
        cmake -S "$here/ymir-ps5/source" -B "$HOST_CORE" -G Ninja -DCMAKE_BUILD_TYPE=Release \
            -DCMAKE_C_COMPILER=clang-18 -DCMAKE_CXX_COMPILER=clang++-18 \
            -DCMAKE_CXX_FLAGS="-fno-exceptions -DFMT_EXCEPTIONS=0" \
            -DYmir_ENABLE_LIBRETRO=ON -DYmir_DEV_BUILD=OFF -DYmir_ENABLE_GPU_API=OFF -DYmir_ENABLE_IPO=OFF \
            -DYmir_AVX2=OFF -DYmir_ENABLE_TESTS=OFF -DYmir_ENABLE_SANDBOX=OFF -DYmir_ENABLE_YMDASM=OFF
        obj=apps/ymir-libretro/CMakeFiles/ymir_libretro.dir/src
        ninja -C "$HOST_CORE" -j "$JOBS" ymir-core "$obj/libretro.cpp.o" "$obj/cdrom_loader.cpp.o" "$obj/vfs.cpp.o"
    fi
    echo "==> App para PC (make host)"
    make -C "$APP" host -j"$JOBS"
fi

echo "==> App para PS5 (make dist, $JOBS hilos)"
make -C "$APP" dist PS5_VULKAN="$VK" PS5_PAYLOAD_SDK="$SDK" -j"$JOBS"

echo
echo "==> Terminado en $(( $(date +%s) - t0 )) s. Archivos:"
ls -la "$APP/build/dist"
