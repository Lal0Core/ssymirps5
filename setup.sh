#!/usr/bin/env bash
# SSYmirPS5: prepara el entorno de compilación en Ubuntu 24.04 (WSL2 en Windows).
# Uso: bash setup.sh   (una sola vez; pide la contraseña de sudo para instalar paquetes)
set -euo pipefail
here=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)

echo "==> 1/3 Paquetes del sistema (clang 18, lld 18, cmake, ninja, nasm, python)"
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
    build-essential make git clang-18 lld-18 llvm-18 llvm-18-dev cmake ninja-build nasm pkg-config \
    python3 python3-pip python3-numpy python3-pil zip unzip xz-utils ca-certificates \
    glslang-tools libclang-rt-18-dev
# el mismo meson que el entorno original (1.12.1)
python3 -m pip install --user --break-system-packages meson==1.12.1 >/dev/null
# los nombres sin versión (clang, ld.lld, llvm-ar...) apuntan a la 18, como en el entorno original
for t in clang clang++ ld.lld llvm-ar llvm-nm llvm-objcopy llvm-ranlib llvm-strip llvm-config; do
    v=$(command -v "$t-18" || true)
    [[ -n $v && ! -e /usr/local/bin/$t ]] && sudo ln -s "$v" "/usr/local/bin/$t"
done

echo "==> 2/3 Driver Vulkan (RADV) + PS5 payload SDK ya compilados -> PS5_Vulkan/.deps/native"
if [[ ! -d $here/PS5_Vulkan/.deps/native/ps5-payload-sdk ]]; then
    (cd "$here/deps" && cat ps5_vulkan_native.tar.xz.part* > /tmp/ps5_vulkan_native.tar.xz)
    (cd /tmp && sha256sum -c "$here/deps/SHA256SUMS")
    tar -C "$here/PS5_Vulkan" -xJf /tmp/ps5_vulkan_native.tar.xz
    rm -f /tmp/ps5_vulkan_native.tar.xz
else
    echo "    ya estaba"
fi

echo "==> 3/3 Comprobación"
export PATH=$HOME/.local/bin:$PATH
clang-18 --version | head -1
ld.lld-18 --version
cmake --version | head -1
glslangValidator --version | head -1
echo "ninja $(ninja --version)   meson $(meson --version)   $(nasm -v)"
echo "Núcleos disponibles: $(nproc)"
echo
echo "Listo. Ahora compila con:  bash build.sh"
