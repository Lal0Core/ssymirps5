# PS5 (Prospero) toolchain for the Ymir libretro core, built as a native shared
# core for mihawk-99/PS5_RetroArch's loader. Modeled on tooling/melondsds.
set(CMAKE_SYSTEM_NAME FreeBSD)
set(CMAKE_SYSTEM_PROCESSOR x86_64)
set(PS5_SDK "$ENV{PS5_PAYLOAD_SDK}")
set(PS5_YMIR_WORK "$ENV{PS5_YMIR_WORK}")
set(CMAKE_C_COMPILER "${PS5_SDK}/bin/prospero-clang")
set(CMAKE_CXX_COMPILER "${PS5_SDK}/bin/prospero-clang++")
set(CMAKE_AR "${PS5_SDK}/bin/prospero-ar")
set(CMAKE_RANLIB "${PS5_SDK}/bin/prospero-ranlib")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_FIND_ROOT_PATH "${PS5_SDK}/target")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# Zen 2: the PS5 CPU has AVX2. No exceptions: the core carries its own libc++
# and must not depend on the title's unwinder.
set(CMAKE_C_FLAGS_INIT "-fPIC -DPS5=1 -march=znver2 -ffunction-sections -fdata-sections")
set(CMAKE_CXX_FLAGS_INIT "${CMAKE_C_FLAGS_INIT} -fno-exceptions -DFMT_EXCEPTIONS=0 -D_LIBCPP_NO_EXCEPTIONS")

# Shared-core link: the title's loader resolves every import by name, so the core
# links libc++/libc++abi privately (hidden) and imports only the C runtime.
set(CMAKE_SHARED_LINKER_FLAGS_INIT
    "-nostdlib -nodefaultlibs -Wl,-m,elf_x86_64_fbsd -Wl,-z,undefs -Wl,--build-id=sha1 -Wl,--gc-sections -Wl,-T,$ENV{PS5_CORE_LD} -L${PS5_YMIR_WORK}/empty-libs")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${CMAKE_SHARED_LINKER_FLAGS_INIT}")

# No C++20 modules in Ymir; the SDK has no clang-scan-deps.
set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
