# Included by apps/ymir-libretro/CMakeLists.txt when PS5=ON.
#
# PS5_RetroArch's loader resolves a core's imports only from a table built into
# the title from the cores it shipped with. A C++20 core importing libc++ from
# the title would only load on a title rebuilt with it, so this core carries
# libc++/libc++abi privately (hidden) and imports only plain C functions the
# title binds. What libc++ needs beyond those (the xlocale *_l calls, rune
# tables, message catalogs, the unwinder entry points) is defined in the core:
# ps5_core_locale.c. The unwinder itself is left out: the core has no
# exceptions, so an exception raised inside libc++ ends in std::terminate.
set(PS5_SDK "$ENV{PS5_PAYLOAD_SDK}")

target_sources(ymir_libretro PRIVATE
    "${PS5_RETROARCH_DIR}/tooling/native/core_cxx_runtime.cpp"
    "${PS5_PORT_DIR}/ps5_core_shims.c"
    "${PS5_PORT_DIR}/ps5_core_locale.c")
target_include_directories(ymir_libretro PRIVATE "${PS5_PORT_DIR}")

target_link_options(ymir_libretro PRIVATE
    -Wl,--version-script=${PS5_PORT_DIR}/exports.map
    -Wl,--exclude-libs=ALL)

# Archives after the objects (target_link_libraries keeps that order).
target_link_libraries(ymir_libretro PRIVATE
    -Wl,--start-group
    "${PS5_SDK}/target/lib/libc++.a"
    "${PS5_SDK}/target/lib/libc++abi.a"
    ${PS5_COMPILER_BUILTINS}
    -Wl,--end-group
    -Wl,--no-as-needed -lkernel_web -lSceLibcInternal -lScePosixForWebKit)
