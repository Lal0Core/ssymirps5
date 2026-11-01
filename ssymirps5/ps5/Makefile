# SSYmirPS5: a native PS5 app that plays Sega Saturn discs (the Ymir core, through its libretro port)
#   make ps5    -> build/ps5/SSYmirPS5.elf   the payload to send with the PS5 Payload Manager / ELF loader:
#                  it installs or updates the dashboard app /data/homebrew/$(TITLE_ID)/ (SSYmirPS5 itself,
#                  icon included) and stays running as the helper that lets the app reach /data
#   make app    -> build/app/$(TITLE_ID)/     the app folder on its own (eboot.bin, sce_module/, sce_sys/)
#   make dist   -> build/dist/SSYmirPS5-v$(VERSION).elf + ssymirps5-v$(VERSION)-src.zip (release names)
#   make send   -> sends SSYmirPS5.elf to $(PS5_HOST):$(PS5_PORT) with the SDK's prospero-deploy
#
# The layout is PS5SX2's (and Snes9x PS5's): eboot.bin is the emulator, a native PS5 title,
# and the payload is its installer and helper:
#   1. build/ps5/SSYmirPS5-helper.elf   the helper alone (payload); the app sends it to the ELF loader
#   2. build/app/$(TITLE_ID)/          eboot.bin = Ymir's core (static libraries, see YMIR_BUILD) + the PS5 frontend +
#                                      the helper from step 1 (native title, signed); sce_module/libc.prx; sce_sys/
#   3. build/ps5/SSYmirPS5.elf          installer + helper, with the app folder from step 2 built in
#
# Ymir's core (Emir, the libretro branch of ymir-emu/Ymir, GPL-3.0, with the PS5 patch: no exceptions) is built
# with CMake and the PS5 toolchain (tools/build-ymir-core.sh) into $(YMIR_BUILD): libymir-core.a, its vendored
# libraries and apps/ymir-libretro's three objects, linked here as they are.
#
# Dependencies: the ps5-payload-dev SDK (v0.42 or newer, https://github.com/ps5-payload-dev/sdk) in
# $(PS5_PAYLOAD_SDK), with clang/lld 18 on the host; PS5_VULKAN for the Vulkan (RADV) renderer and FSR.

PS5_PAYLOAD_SDK ?= /opt/ps5-payload-sdk
PS5_HOST ?= ps5
PS5_PORT ?= 9021
# The app's version: raise it for every build that is handed out (CHANGELOG.md says what each one changed). It goes
# into the app (the shelf, Settings, the game log), param.json's contentVersion and the release file names.
VERSION ?= 1.0
VERSION_STAMP := build/gen/version.stamp
CORE_VERSION := 0.4.0
# the GPU's shader tables, generated from shaders/presets.txt (see "shaders:" below)
SLANG_CPP := build/gen/slang_presets.cpp
TITLE_ID ?= PPSA99378
# where the app looks for its updates (Settings, SYSTEM, Updates): this repository's GitHub releases
UPDATE_REPO ?= Lal0Core/ssymirps5
APP_OUT := build/app/$(TITLE_ID)

ROOT := ..

# Ymir's core: the CMake build's outputs (tools/build-ymir-core.sh), and its source for libretro.h and libchdr's header
YMIR_SRC ?= $(abspath ../../ymir-ps5/source)
YMIR_BUILD ?= $(abspath ../build-core/ps5)
YMIR_LIBRETRO_OBJ := $(addprefix $(YMIR_BUILD)/apps/ymir-libretro/CMakeFiles/ymir_libretro.dir/src/,libretro.cpp.o cdrom_loader.cpp.o vfs.cpp.o)
YMIR_ARCHIVES := $(YMIR_BUILD)/libs/ymir-core/libymir-core.a $(wildcard $(YMIR_BUILD)/libs/ymir-core/libymir-core-shaders.a) \
	$(YMIR_BUILD)/vendor/libchdr/libchdr-static.a $(YMIR_BUILD)/vendor/libchdr/libchdr/deps/zstd-1.5.7/libzstd.a \
	$(YMIR_BUILD)/vendor/libchdr/libchdr/deps/miniz-3.1.1/libminiz.a \
	$(YMIR_BUILD)/vendor/libchdr/libchdr/deps/lzma-25.01/libchdr-lzma.a $(YMIR_BUILD)/vendor/fmt/libfmt.a \
	$(YMIR_BUILD)/vendor/xxHash/libxxHash.a $(wildcard $(YMIR_BUILD)/vendor/lz4/liblz4*.a)
CHD_INC := $(YMIR_SRC)/vendor/libchdr/libchdr/include
# zlib 1.3.1 (zlib licence) for the frontend (minizip, the covers' CRCs); the GPU build uses RADV's copy of the same release
ZLIB := third_party/zlib
ZLIB_SRC := $(addprefix $(ZLIB)/,adler32.c compress.c crc32.c deflate.c infback.c inffast.c inflate.c inftrees.c trees.c \
	uncompr.c zutil.c)

# The console's libc gives a native title no working getaddrinfo, and its fcntl may refuse O_NONBLOCK on a socket: the
# app's HTTPS downloads (fe_tlshttp.cpp) reach coreorbis/orbis-shims/ProsperoNetShims.cpp's versions through --wrap.
NET_WRAPPED_PS5 := fcntl getaddrinfo freeaddrinfo
NET_WRAP_LLD := $(foreach s,$(NET_WRAPPED_PS5),--wrap=$(s))

# the app (eboot.bin): the PS5 layer and the frontend
EMU_SRC := coreorbis/main-boot.cpp \
	coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoNotify.cpp \
	coreorbis/orbis-shims/ProsperoVideo.cpp coreorbis/orbis-shims/ProsperoAudio.cpp \
	coreorbis/orbis-shims/ProsperoInput.cpp coreorbis/orbis-shims/ProsperoJailbreak.cpp \
	coreorbis/orbis-shims/ProsperoCrash.cpp coreorbis/orbis-shims/ProsperoCrt.cpp coreorbis/orbis-shims/ProsperoGpu.cpp \
	coreorbis/orbis-shims/ProsperoUpdate.cpp coreorbis/orbis-shims/ProsperoSlangExt.cpp \
	frontend/fe_emu.cpp frontend/fe_menu.cpp frontend/fe_settings.cpp frontend/fe_i18n.cpp frontend/fe_chdcache.cpp frontend/fe_text.cpp \
	frontend/fe_games.cpp frontend/fe_http.cpp frontend/fe_covers.cpp frontend/fe_shelf.cpp \
	frontend/fe_prefetch.cpp frontend/fe_coverfetch.cpp frontend/fe_coverworker.cpp \
	frontend/fe_osk.cpp frontend/fe_ui.cpp frontend/fe_tlshttp.cpp frontend/fe_gamelog.cpp frontend/fe_caseview.cpp frontend/fe_backsources.cpp frontend/fe_launchbox.cpp frontend/fe_style.cpp frontend/fe_cheats.cpp frontend/fe_recent.cpp frontend/fe_playtime.cpp \
	coreorbis/orbis-shims/ProsperoNetShims.cpp
EMU_SRC += frontend/fe_border.cpp frontend/fe_borderimg.cpp frontend/fe_borderfetch.cpp
# minizip (zlib licence), built as C with the zlib above
EMU_C_SRC := frontend/third_party/unzip.c frontend/third_party/ioapi.c
# 2.2: games inside a .7z (fe_7z.cpp), with the LZMA SDK 26.05's C 7z decoder (public domain; third_party/lzma7z/README.md):
# single-threaded, no AES (an encrypted archive is refused), no assembler. libchdr links an older LZMA SDK (24.05): every
# external name of this copy is prefixed fe7z_ (fe7z_names.h, force-included), so the two can't meet, and lzma7z-names
# checks the objects after each build.
EMU_SRC += frontend/fe_7z.cpp frontend/fe_screenshot.cpp
LZMA7Z := third_party/lzma7z
LZMA7Z_SRC := $(sort $(wildcard $(LZMA7Z)/*.c))
LZMA7Z_DEFS := -DZ7_ST -DZ7_PPMD_SUPPORT -DZ7_EXTRACT_ONLY
LZMA7Z_FLAGS := $(LZMA7Z_DEFS) -include $(LZMA7Z)/fe7z_names.h -I$(LZMA7Z)
%/port/frontend/fe_7z.o: EXTRA_FLAGS = $(LZMA7Z_DEFS) -isystem $(LZMA7Z)
%/port/frontend/fe_7z.o: $(LZMA7Z)/fe7z_names.h
# every global name the objects define must carry the prefix (a name the SDK adds in a later version, left out of
# fe7z_names.h, would otherwise clash with libchdr's copy, or silently mix the two versions)
define LZMA7Z_NAMES_CHECK
	@bad=$$($(1) -g --defined-only $(2) | awk 'NF==3 && $$3 !~ /^(fe7z_|__odr_asan\.fe7z_)/ {print $$3}'); \
	if [ -n "$$bad" ]; then echo "lzma7z: names without the fe7z_ prefix: $$bad (add them to $(LZMA7Z)/fe7z_names.h)"; exit 1; fi
	@touch $@
endef
# 2.1: the back covers of the case view (fe_caseview.cpp) are AVIF files (Chromagi's): libavif 1.3 (BSD-2-Clause) with
# dav1d (BSD-2-Clause, built as a static library by tools/build-dav1d.sh, no assembler)
AVIF := third_party/libavif
AVIF_SRC := $(addprefix $(AVIF)/src/,alpha.c avif.c codec_dav1d.c colr.c colrconvert.c diag.c exif.c gainmap.c io.c mem.c \
	obu.c properties.c rawdata.c read.c reformat.c reformat_libsharpyuv.c reformat_libyuv.c scale_noyuv.c \
	stream.c utils.c write.c)
DAV1D := third_party/dav1d
AVIF_FLAGS := -I$(AVIF)/include -isystem $(DAV1D)/include -DAVIF_CODEC_DAV1D=1
%/port/frontend/fe_caseview.o: EXTRA_FLAGS = -isystem $(AVIF)/include
# 2.3: the product number -> game table (tools/make_saturn_serials.py, GameDB-Saturn) and Chromagi's pages, built in
%/port/frontend/fe_backsources.o: EXTRA_FLAGS = -DSATURN_SERIALS_TSV='"$(abspath frontend/data/saturn-serials.tsv)"' \
	-DCHROMAGI_TSV='"$(abspath frontend/data/chromagi-saturn.tsv)"'
%/port/frontend/fe_backsources.o: frontend/data/saturn-serials.tsv frontend/data/chromagi-saturn.tsv
%/port/frontend/fe_cheats.o: EXTRA_FLAGS = -DSATURN_CHEATS_TXT='"$(abspath frontend/data/saturn-cheats.txt)"'
%/port/frontend/fe_cheats.o: frontend/data/saturn-cheats.txt
BRIDGE_C_SRC :=
# the payloads: the helper, and the installer (= helper + install)
# (the helper also downloads the covers in the background: fe_coverworker.cpp, with the app's HTTPS code)
HELPER_SRC := coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoNotify.cpp \
	coreorbis/orbis-shims/ProsperoHelper.cpp coreorbis/orbis-shims/ProsperoCrash.cpp coreorbis/orbis-shims/ProsperoMigrate.cpp \
	coreorbis/orbis-shims/ProsperoUpdate.cpp \
	frontend/fe_http.cpp frontend/fe_coverfetch.cpp frontend/fe_coverworker.cpp frontend/fe_tlshttp.cpp
# the helper's own HTTPS (fe_tlshttp.cpp): Mbed TLS 3.6 (Apache-2.0), with fe_mbedtls_config.h on its default settings
MBEDTLS := frontend/third_party/mbedtls
MBEDTLS_SRC := $(sort $(wildcard $(MBEDTLS)/library/*.c))
MBEDTLS_FLAGS := -I$(MBEDTLS)/include -Ifrontend '-DMBEDTLS_USER_CONFIG_FILE="fe_mbedtls_config.h"'
INSTALLER_SRC := installer/installer_main.cpp $(HELPER_SRC) coreorbis/orbis-shims/ProsperoInstall.cpp

INCS := -Icoreorbis/include-orbis -Icoreorbis/orbis-shims -Ifrontend -isystem $(YMIR_SRC)/apps/ymir-libretro/include \
	-isystem $(ZLIB)
DEFS := -DSSYMIRPS5_VERSION='"$(VERSION)"' -DYMIR_CORE_VERSION='"$(CORE_VERSION)"' -DYMIR_TITLE_ID='"$(TITLE_ID)"' -DYMIR_UPDATE_REPO='"$(UPDATE_REPO)"'
MINIZIP_FLAGS := -DNOUNCRYPT -I$(ZLIB) -w
PORT_WARN := -Wall -Wextra -Werror -Wno-unused-parameter -Wno-missing-field-initializers

# the game-name table built into fe_games.o (tools/make_gamedb.py), the palettes in fe_config.o
# (2.1: with the dumps No-Intro lacks and the games an online database has art of, tools/make_extra_crc.py, make_db_art.py)
GAMEDB_DEFS := -DGAMEDB_TSV='"$(abspath frontend/data/nointro.tsv)"' -DEXTRACRC_TSV='"$(abspath frontend/data/extra-crc.tsv)"' \
	-DDBART_TSV='"$(abspath frontend/data/db-art.tsv)"'
%/port/frontend/fe_games.o: EXTRA_FLAGS = $(GAMEDB_DEFS) -isystem $(CHD_INC)
%/port/frontend/fe_games.o: frontend/data/nointro.tsv frontend/data/extra-crc.tsv frontend/data/db-art.tsv
# stb_image in fe_covers.o has warnings of its own; libretro-thumbnails' box art names are built in (tools/make_cover_index.py)
%/port/frontend/fe_covers.o: EXTRA_FLAGS = -Wno-error -Wno-sign-compare -Wno-unused-but-set-variable -Wno-implicit-fallthrough \
	-DCOVER_INDEX_TXT='"$(abspath frontend/data/cover-index.txt)"'
%/port/frontend/fe_covers.o: frontend/data/cover-index.txt
# the borders (2.1): the built-in frame is embedded in fe_border.o (tools/make_default_border.py), the names of the borders The
# Bezel Project has in fe_borderfetch.o (tools/make_border_index.py; the pictures are downloaded); stb_image in fe_borderimg.o
%/port/frontend/fe_border.o: EXTRA_FLAGS = -DBORDER_DEFAULT_PNG='"$(abspath frontend/assets/borders/default.png)"'
%/port/frontend/fe_border.o: frontend/assets/borders/default.png
%/port/frontend/fe_borderfetch.o: EXTRA_FLAGS = -DBORDER_INDEX_TXT='"$(abspath frontend/data/border-index.txt)"'
%/port/frontend/fe_borderfetch.o: frontend/data/border-index.txt
%/port/frontend/fe_borderimg.o: EXTRA_FLAGS = -Wno-error -Wno-sign-compare -Wno-unused-but-set-variable -Wno-implicit-fallthrough
# the UI fonts (PS5SX2's: PCSX2's Roboto Regular and PromptFont; Font Awesome's brands) built into fe_text.o
%/port/frontend/fe_text.o: EXTRA_FLAGS = -DFE_FONT_DIR='"$(abspath frontend/assets/fonts)"' -Wno-error -Wno-sign-compare -Wno-unused-function
%/port/frontend/fe_text.o: frontend/assets/fonts/Inter-Regular.otf frontend/assets/fonts/Inter-Medium.otf frontend/assets/fonts/Inter-SemiBold.otf frontend/assets/fonts/promptfont.otf \
	frontend/assets/fonts/fa-brands-400.otf
# the helper's HTTPS client: Mbed TLS's headers, and Mozilla's CA list built in
%/port/frontend/fe_tlshttp.o: EXTRA_FLAGS = $(MBEDTLS_FLAGS) -DCACERT_PEM='"$(abspath frontend/data/cacert.pem)"'
%/port/frontend/fe_tlshttp.o: frontend/data/cacert.pem frontend/fe_mbedtls_config.h
# the CHD read-ahead (fe_chdcache.cpp) sees libchdr's header; the link wraps chd_read and chd_close with it
%/port/frontend/fe_chdcache.o: EXTRA_FLAGS = -I$(CHD_INC)
CHD_WRAP := --wrap=chd_read --wrap=chd_close
# the helper payload is installer_main.cpp without the install step
%/port/installer/helper_main.o: EXTRA_FLAGS = -DYMIR_HELPER_ONLY

# glslang (third_party/glslang, 15.1.0, BSD-3-Clause and others: its LICENSE.txt): the user's own .slangp presets
# compiled on the console (ProsperoSlangExt.cpp). Built without HLSL and without SPIRV-Tools.
GLSLANG := third_party/glslang
GLSLANG_SRC := $(wildcard $(GLSLANG)/glslang/MachineIndependent/*.cpp $(GLSLANG)/glslang/MachineIndependent/preprocessor/*.cpp \
	$(GLSLANG)/glslang/GenericCodeGen/*.cpp) $(GLSLANG)/glslang/OSDependent/Unix/ossource.cpp \
	$(GLSLANG)/glslang/ResourceLimits/ResourceLimits.cpp $(addprefix $(GLSLANG)/SPIRV/,GlslangToSpv.cpp SpvBuilder.cpp \
	SpvPostProcess.cpp InReadableOrder.cpp Logger.cpp SpvTools.cpp disassemble.cpp doc.cpp)
GLSLANG_FLAGS := -std=c++17 -I$(GLSLANG) -w
%/port/coreorbis/orbis-shims/ProsperoSlangExt.o: EXTRA_FLAGS = -I$(GLSLANG) $(PS5_GPU_DEFS)

# ---------------- the GPU (optional) ----------------
# PS5_VULKAN=<a PS5_Vulkan checkout> (github.com/mihawk-99/PS5_Vulkan, with tools/setup-native-dependencies.sh and
# tools/build-radv.sh release run in it) builds the app with RADV inside: the GPU draws the picture and runs the
# slang shaders. Without it the app is the CPU-only build of 1.8. Use that checkout's payload SDK fork too
# (PS5_PAYLOAD_SDK=$(PS5_VULKAN)/.deps/native/ps5-payload-sdk): RADV is built against it.
PS5_VULKAN ?=
empty :=
space := $(empty) $(empty)
ifneq ($(PS5_VULKAN),)
RADV_DIR ?= $(PS5_VULKAN)/.deps/native/radv-release
RADV_ARCHIVE ?= $(RADV_DIR)/lib/libvulkan_radeon.ps5.a
PS5_GPU_DEFS := -DYMIR_GPU -isystem $(RADV_DIR)/include
endif

# ---------------- PS5 ----------------
PS5_CC  := $(PS5_PAYLOAD_SDK)/bin/prospero-clang
PS5_CXX := $(PS5_PAYLOAD_SDK)/bin/prospero-clang++
PS5_OUT := build/ps5
# The PS5's CPU is Zen 2 (PS5SX2 builds with the same -march).
PS5_OPT := -O3 -fPIC -march=znver2 -fno-strict-aliasing
# the payloads: notifications, and the kernel access the helper needs (libkernel_sys: sceKernelGetAppInfo)
# the payload SDK's network libraries (its http2_get sample's), for the helper's cover downloads
PS5_PAYLOAD_LIBS := -lkernel_sys -lSceNet -lSceNetCtl -lSceSsl -lSceHttp2 -lpthread

PS5_ZLIB_OBJ := $(patsubst $(ZLIB)/%.c,$(PS5_OUT)/zlib/%.o,$(ZLIB_SRC))
PS5_CORE_OBJ := $(YMIR_LIBRETRO_OBJ) $(YMIR_ARCHIVES) $(PS5_ZLIB_OBJ)
PS5_EMU_OBJ := $(patsubst %.cpp,$(PS5_OUT)/port/%.o,$(EMU_SRC)) $(patsubst %.c,$(PS5_OUT)/port/%.o,$(EMU_C_SRC)) \
	$(patsubst %.c,$(PS5_OUT)/bridge/%.o,$(BRIDGE_C_SRC))
PS5_MBEDTLS_OBJ := $(patsubst $(MBEDTLS)/library/%.c,$(PS5_OUT)/mbedtls/%.o,$(MBEDTLS_SRC))
PS5_HELPER_OBJ := $(PS5_OUT)/port/installer/helper_main.o $(patsubst %.cpp,$(PS5_OUT)/port/%.o,$(HELPER_SRC)) $(PS5_MBEDTLS_OBJ)
PS5_INSTALLER_OBJ := $(patsubst %.cpp,$(PS5_OUT)/port/%.o,$(INSTALLER_SRC)) $(PS5_MBEDTLS_OBJ)
PS5_LZMA7Z_OBJ := $(patsubst $(LZMA7Z)/%.c,$(PS5_OUT)/lzma7z/%.o,$(LZMA7Z_SRC))
PS5_AVIF_OBJ := $(patsubst $(AVIF)/src/%.c,$(PS5_OUT)/avif/%.o,$(AVIF_SRC))
PS5_DAV1D_LIB := $(PS5_OUT)/dav1d/src/libdav1d.a

ps5: $(PS5_OUT)/SSYmirPS5.elf
# the core alone (its CMake build)
ps5-core:
	PS5_PAYLOAD_SDK=$(PS5_PAYLOAD_SDK) bash tools/build-ymir-core.sh $(YMIR_SRC) $(YMIR_BUILD)
$(YMIR_LIBRETRO_OBJ) $(YMIR_ARCHIVES):
	@echo "error: $@ is missing: build Ymir's core first (make ps5-core)" >&2; exit 1

$(PS5_OUT)/zlib/%.o: $(ZLIB)/%.c
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) -DHAVE_UNISTD_H -w -c -o $@ $<

$(PS5_OUT)/mbedtls/%.o: $(MBEDTLS)/library/%.c frontend/fe_mbedtls_config.h
	@mkdir -p $(dir $@)
	$(PS5_CC) -Os -fPIC -march=znver2 $(MBEDTLS_FLAGS) -c -o $@ $<

$(PS5_OUT)/port/%.o: %.c
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) $(MINIZIP_FLAGS) -c -o $@ $<

$(PS5_OUT)/port/%.o: %.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h frontend/*.h $(VERSION_STAMP)
	@mkdir -p $(dir $@)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(PORT_WARN) $(EXTRA_FLAGS) -c -o $@ $<

$(PS5_OUT)/avif/%.o: $(AVIF)/src/%.c
	@mkdir -p $(dir $@)
	$(PS5_CC) -O2 -fPIC -march=znver2 $(AVIF_FLAGS) -w -c -o $@ $<
$(PS5_DAV1D_LIB):
	PS5_PAYLOAD_SDK=$(PS5_PAYLOAD_SDK) bash tools/build-dav1d.sh $(PS5_OUT)/dav1d ps5

$(PS5_OUT)/lzma7z/%.o: $(LZMA7Z)/%.c $(wildcard $(LZMA7Z)/*.h)
	@mkdir -p $(dir $@)
	$(PS5_CC) $(PS5_OPT) $(LZMA7Z_FLAGS) -c -o $@ $<
$(PS5_OUT)/lzma7z/names.ok: $(PS5_LZMA7Z_OBJ)
	$(call LZMA7Z_NAMES_CHECK,$(PS5_PAYLOAD_SDK)/bin/llvm-nm,$^)
$(PS5_OUT)/port/coreorbis/orbis-shims/ProsperoGpu.o: EXTRA_FLAGS = $(PS5_GPU_DEFS)

$(PS5_OUT)/port/installer/helper_main.o: installer/installer_main.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h $(VERSION_STAMP)
	@mkdir -p $(dir $@)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(PORT_WARN) $(EXTRA_FLAGS) -c -o $@ $<

# 1. the helper payload (built into the app, which sends it to the ELF loader when no helper answers)
$(PS5_OUT)/SSYmirPS5-helper.elf: $(PS5_HELPER_OBJ)
	$(PS5_CXX) -o $@ $^ $(PS5_PAYLOAD_LIBS)

$(PS5_OUT)/helper_data.o: coreorbis/orbis-shims/helper_data.cpp coreorbis/orbis-shims/ProsperoJailbreak.h $(PS5_OUT)/SSYmirPS5-helper.elf
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(PORT_WARN) -DHELPER_ELF='"$(abspath $(PS5_OUT)/SSYmirPS5-helper.elf)"' -c -o $@ $<

# 3. the installer + helper, with the app folder built in
APP_FILES = $(APP_OUT)/eboot.bin $(APP_OUT)/sce_sys/param.json $(APP_OUT)/sce_sys/icon0.png $(APP_OUT)/sce_sys/pic0.dds \
	$(APP_OUT)/sce_sys/pic1.dds \
	$(APP_OUT)/sce_module/libc.prx
APP_DATA_DEFS = -DAPP_EBOOT='"$(abspath $(APP_OUT)/eboot.bin)"' -DAPP_PARAM='"$(abspath $(APP_OUT)/sce_sys/param.json)"' \
	-DAPP_ICON0='"$(abspath $(APP_OUT)/sce_sys/icon0.png)"' -DAPP_PIC0='"$(abspath $(APP_OUT)/sce_sys/pic0.dds)"' \
	-DAPP_PIC1='"$(abspath $(APP_OUT)/sce_sys/pic1.dds)"' \
	-DAPP_LIBC='"$(abspath $(APP_OUT)/sce_module/libc.prx)"' \
	$(if $(shell cmp -s app/sce_sys/pic0.dds app/sce_sys/pic1.dds && echo same),-DAPP_PIC1_IS_PIC0)

$(PS5_OUT)/install_data.o: coreorbis/orbis-shims/install_data.cpp coreorbis/orbis-shims/ProsperoInstall.h $(APP_FILES)
	$(PS5_CXX) -std=c++17 $(PS5_OPT) $(DEFS) $(INCS) $(PORT_WARN) $(APP_DATA_DEFS) -c -o $@ $<

$(PS5_OUT)/SSYmirPS5.elf: $(PS5_INSTALLER_OBJ) $(PS5_OUT)/install_data.o
	$(PS5_CXX) -o $@ $^ $(PS5_PAYLOAD_LIBS)
	@echo "built $@ ($$(stat -c %s $@) bytes)"

$(PS5_OUT)/SSYmirPS5-stripped.elf: $(PS5_OUT)/SSYmirPS5.elf
	$(PS5_PAYLOAD_SDK)/bin/prospero-strip -o $@ $<

strip: $(PS5_OUT)/SSYmirPS5-stripped.elf

# the release files, named with the version: build/dist/SSYmirPS5-v$(VERSION).elf and the sources zip
DIST := build/dist
# the source zip takes the core with its PS5 patch from here (ymir-ps5/ beside the app folder)
YMIR_PS5_DIR ?= $(abspath ../../ymir-ps5)
# + the app folder as a stored zip with its manifest (SSYmirPS5-v$(VERSION)-$(TITLE_ID).zip): attach it to the
# GitHub release tagged v$(VERSION), so the app can update itself (Settings, SYSTEM, Updates)
# the folder for FTP (3.9.7): ppsa99378-SSYmirPS5-v3.9.7.zip
FTP_ZIP = $(shell echo $(TITLE_ID) | tr A-Z a-z)-SSYmirPS5-v$(VERSION).zip
dist: $(PS5_OUT)/SSYmirPS5.elf $(APP_FILES)
	@mkdir -p $(DIST)
	@# 3.11.2: only this version's files are kept: the earlier versions' are removed (anything else in build/dist,
	@# such as an unzipped app folder, is left alone)
	rm -f $(DIST)/SSYmirPS5-v*.elf $(DIST)/SSYmirPS5-v*-$(TITLE_ID).zip $(DIST)/ssymirps5-v*-src.zip \
		$(DIST)/$(shell echo $(TITLE_ID) | tr A-Z a-z)-SSYmirPS5-v*.zip
	cp $< $(DIST)/SSYmirPS5-v$(VERSION).elf
	@# 1.0: the licences go with the app (its folder, so the update zip and the FTP zip carry them)
	mkdir -p $(APP_OUT)/licenses
	cp ../LICENSE $(APP_OUT)/licenses/LICENSE.txt
	cp ../THIRD_PARTY_NOTICES.md $(APP_OUT)/licenses/THIRD_PARTY_NOTICES.md
	cp THIRD_PARTY_SHADERS.md $(APP_OUT)/licenses/THIRD_PARTY_SHADERS.md
	python3 tools/make_release_zip.py $(APP_OUT) $(TITLE_ID) $(DIST)/SSYmirPS5-v$(VERSION)-$(TITLE_ID).zip
	@# the complete source (GPL-3.0): the app, Ymir's core with its PS5 patch, PS5_Vulkan and the build scripts
	python3 tools/make_source_zip.py $(DIST)/ssymirps5-v$(VERSION)-src.zip ..=ssymirps5 $(YMIR_PS5_DIR)=ymir-ps5 \
		$(if $(PS5_VULKAN),$(abspath $(PS5_VULKAN))=PS5_Vulkan) ../../build.sh=build.sh ../../setup.sh=setup.sh
	rm -f "$(DIST)/$(FTP_ZIP)"
	cp ../README.md build/app/LEEME-SSYmirPS5.md
	cp ../CHANGELOG.md build/app/CAMBIOS-SSYmirPS5.md
	cp ../LICENSE build/app/LICENCIA-SSYmirPS5.txt
	cd build/app && zip -qr -9 "$(abspath $(DIST))/$(FTP_ZIP)" $(TITLE_ID) LEEME-SSYmirPS5.md CAMBIOS-SSYmirPS5.md \
		LICENCIA-SSYmirPS5.txt
	@ls -l $(DIST)

send: $(PS5_OUT)/SSYmirPS5.elf
	$(PS5_PAYLOAD_SDK)/bin/prospero-deploy -h $(PS5_HOST) -p $(PS5_PORT) $<

# ---------------- the app (eboot.bin = Ymir's core and the frontend) ----------------
# 2. A native PS5 title linked the way PS5SX2 links its eboot (ps5/coreorbis/link-vk.sh): the boilerplate's
# app_crt.cpp (_start -> _init_env -> main) and app_cpp_runtime.cpp, the emulator's objects (the same ones,
# compiled once), a PIE laid out by ps5-pie.ld (+ PS5SX2's ehframe.ld) against the SDK's system-module stubs
# and its static libc++/libc++abi/libunwind/libc, every defined symbol local (PS5SX2's genmap.py result:
# localize.map); then ps5-native-tool link (SCE dynamic tables, process parameters) and self --sign.
# The folder carries sce_module/libc.prx, the C runtime module a native title needs (without it the console
# stops the app with PRX_SCE_MODULE_LOAD_ERROR, 0xa0020102).
NATIVE := proto/native
NATIVE_TOOL := $(NATIVE)/build/host/ps5-native-tool
LIBC_BUILDER := $(NATIVE)/build/host/libc-builder
LLD ?= ld.lld
NATIVE_CLANG = PS5_PAYLOAD_SDK=$(PS5_PAYLOAD_SDK) sh $(NATIVE)/tooling/prospero-clang18
SDK_LIB := $(PS5_PAYLOAD_SDK)/target/lib
# every system-module stub but the two PS5SX2 leaves out (link-vk.sh)
APP_STUBS = $(filter-out %/libScePosixForWebKit.so %/libSceGLSlimVSH.so,$(sort $(wildcard $(SDK_LIB)/*.so)))

$(NATIVE_TOOL): $(NATIVE)/tooling/native/native_app_builder.cpp $(NATIVE)/tooling/native/elf_object.cpp \
		$(NATIVE)/tooling/native/self_container.cpp $(NATIVE)/tooling/native/sce_module_writer.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX_NATIVE) -std=c++20 -O2 -o $@ $^ -lz
HOST_CXX_NATIVE ?= clang++

$(LIBC_BUILDER): $(NATIVE)/tooling/native/libc_builder.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX_NATIVE) -std=c++20 -O2 -Wall -Wextra -Werror -o $@ $<

# sce_module/libc.prx, deterministic: it must match the published digest (proto/native/libc.prx.sha256)
build/app/libc.prx: $(LIBC_BUILDER) $(NATIVE_TOOL) $(wildcard $(NATIVE)/tooling/native/runtime/*.txt)
	@mkdir -p $(dir $@)
	$(LIBC_BUILDER) $(NATIVE)/tooling/native/runtime/api-surface.txt $(NATIVE)/tooling/native/runtime/imports.txt build/app/libc.raw.elf
	$(NATIVE_TOOL) self --sign --in build/app/libc.raw.elf --out $@
	cd build/app && sha256sum --check --strict $(abspath $(NATIVE)/libc.prx.sha256)

$(APP_OUT)/sce_module/libc.prx: build/app/libc.prx
	@mkdir -p $(dir $@)
	cp $< $@

app: $(APP_FILES)

build/app/app_crt.o build/app/app_cpp_runtime.o: build/app/%.o: $(NATIVE)/tooling/native/%.cpp
	@mkdir -p $(dir $@)
	$(NATIVE_CLANG) -std=c++20 -O2 -Wall -Wextra -fno-exceptions -fno-rtti -ffunction-sections -fdata-sections \
		-c -o $@ $<

EBOOT_OBJS = build/app/app_crt.o build/app/app_cpp_runtime.o $(PS5_CORE_OBJ) $(PS5_EMU_OBJ) $(PS5_MBEDTLS_OBJ) \
	$(PS5_LZMA7Z_OBJ) $(PS5_AVIF_OBJ) $(PS5_DAV1D_LIB) $(PS5_OUT)/helper_data.o
ifeq ($(PS5_VULKAN),)
build/app/ssymirps5-pie.elf: $(EBOOT_OBJS) $(NATIVE)/tooling/native/ps5-pie.ld $(NATIVE)/tooling/native/ehframe.ld \
		$(NATIVE)/tooling/native/app-symbols.map $(NATIVE)/tooling/native/localize.map $(PS5_OUT)/lzma7z/names.ok
	$(LLD) -m elf_x86_64 -pie -z max-page-size=0x4000 --hash-style=gnu \
		-T $(NATIVE)/tooling/native/ps5-pie.ld -T $(NATIVE)/tooling/native/ehframe.ld --eh-frame-hdr \
		--version-script $(NATIVE)/tooling/native/app-symbols.map --version-script $(NATIVE)/tooling/native/localize.map \
		-e _start --no-dynamic-linker $(CHD_WRAP) $(NET_WRAP_LLD) -o $@ $(EBOOT_OBJS) \
		--as-needed $(APP_STUBS) -L $(SDK_LIB) \
		$(SDK_LIB)/libc++.a $(SDK_LIB)/libc++abi.a $(SDK_LIB)/libunwind.a $(SDK_LIB)/libc.a -lpthread
EBOOT_EXTRA_STUBS =
else
# With RADV: PS5_Vulkan's recipe (tools/radv_link_args.sh), its AGC import stubs, and the shader tables. RADV's
# archive carries zlib 1.3.1 (its shader cache's), the same release as ours: ours is left out.
EBOOT_OBJS := $(filter-out $(PS5_ZLIB_OBJ),$(EBOOT_OBJS)) \
	$(PS5_OUT)/slang_presets.o $(patsubst %.cpp,$(PS5_OUT)/glslang/%.o,$(GLSLANG_SRC))
$(PS5_OUT)/glslang/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(PS5_CXX) $(GLSLANG_FLAGS) -O2 -fPIC -march=znver2 -c -o $@ $<
AGC_STUBS := build/app/stubs/libSceAgc.so build/app/stubs/libSceAgcDriver.so
build/app/stubs/libSceAgc.so: $(PS5_VULKAN)/vendor/ps5/sdk/stubs/agc_canary_link_stub.c
build/app/stubs/libSceAgcDriver.so: $(PS5_VULKAN)/vendor/ps5/sdk/stubs/agc_driver_canary_link_stub.c
$(AGC_STUBS):
	@mkdir -p $(dir $@)
	$(PS5_CC) -std=c11 -O2 -fPIC -c -o $@.o $<
	$(PS5_PAYLOAD_SDK)/bin/prospero-lld --shared -soname $(basename $(notdir $@)).prx -o $@ $@.o
# The GPU build: PS5_Vulkan's link recipe points some libc names at its own versions (--defsym=getaddrinfo=ps5_getaddrinfo
# ...). With --wrap, lld lets such a --defsym replace the app's __wrap_ function itself (every getaddrinfo then failed):
# the --defsym of a name the app wraps (NET_WRAPPED_PS5) is left out. The file depends on the Makefile too: one left by
# an older build keeps that build's filtering. ProsperoNetShims.cpp then defines those names as the platform's versions.
ifneq ($(PS5_VULKAN),)
$(PS5_OUT)/port/coreorbis/orbis-shims/ProsperoNetShims.o: EXTRA_FLAGS = -DNET_CHAIN_PS5PLATFORM
endif
build/app/radv-link.args: tools/radv_link_args.sh $(RADV_ARCHIVE) Makefile
	@mkdir -p $(dir $@)
	bash tools/radv_link_args.sh $(abspath $(PS5_VULKAN)) $(abspath $(PS5_PAYLOAD_SDK)) $(abspath $(RADV_ARCHIVE)) > $@.recipe
	sed $(foreach s,$(NET_WRAPPED_PS5),-e '/^--defsym=$(s)=/d') $@.recipe > $@.tmp
	! grep -E '^--defsym=($(subst $(space),|,$(strip $(NET_WRAPPED_PS5))))=' $@.tmp
	mv $@.tmp $@
$(PS5_OUT)/slang_presets.o: $(SLANG_CPP) coreorbis/orbis-shims/ProsperoSlang.h
	@mkdir -p $(dir $@)
	$(PS5_CXX) -std=c++17 -O0 -fPIC -Icoreorbis/orbis-shims -c -o $@ $<
build/app/ssymirps5-pie.elf: $(EBOOT_OBJS) $(AGC_STUBS) build/app/radv-link.args $(NATIVE)/tooling/native/app-symbols.map \
		$(NATIVE)/tooling/native/localize.map $(PS5_OUT)/lzma7z/names.ok
	$(PS5_PAYLOAD_SDK)/bin/prospero-lld --eh-frame-hdr --no-dynamic-linker -z nodynamic-undefined-weak --version-script $(NATIVE)/tooling/native/app-symbols.map \
		--version-script $(NATIVE)/tooling/native/localize.map --exclude-libs=ALL $(CHD_WRAP) $(NET_WRAP_LLD) \
		-e _start -o $@ $(EBOOT_OBJS) $(AGC_STUBS) $$(cat build/app/radv-link.args) \
		--as-needed $(APP_STUBS)
EBOOT_EXTRA_STUBS = $(foreach s,$(AGC_STUBS),--stub $(s))
endif

build/app/eboot.elf: build/app/ssymirps5-pie.elf $(NATIVE_TOOL)
	$(NATIVE_TOOL) link --in $< --out $@ --stub-dir $(SDK_LIB) $(EBOOT_EXTRA_STUBS) --module-sdk 0x02000009 \
		--companion-sdk 0x08050001 --file-name eboot.elf
	python3 tools/prepare_console_elf.py --input $@ --output build/app/eboot-console-ready.elf

$(APP_OUT)/eboot.bin: build/app/eboot.elf $(NATIVE_TOOL)
	@mkdir -p $(dir $@)
	$(NATIVE_TOOL) self --sign --in $< --out $@ --magic 0x1D3D154F
	$(NATIVE_TOOL) self --inspect --file $@

# the version (and the title ID) the objects were compiled with: when either changes, everything that has them built in is
# compiled again (the -D flags are not files make could see change)
$(VERSION_STAMP): FORCE
	@mkdir -p $(dir $@)
	@echo '$(VERSION) $(TITLE_ID) $(UPDATE_REPO)' | cmp -s - $@ || echo '$(VERSION) $(TITLE_ID) $(UPDATE_REPO)' > $@
.PHONY: FORCE
FORCE:

# contentVersion "MM.mmp.000" from VERSION (1.3 -> 01.030.000, 2.2.1 -> 02.021.000)
CONTENT_VERSION = $(shell echo $(VERSION) | awk -F. '{printf "%02d.0%d%d.000", $$1, $$2, $$3}')
$(APP_OUT)/sce_sys/param.json: app/sce_sys/param.json Makefile
	@mkdir -p $(dir $@)
	sed -e 's/PPSA99378/$(TITLE_ID)/g' -e 's/"contentVersion": "[0-9.]*"/"contentVersion": "$(CONTENT_VERSION)"/' $< > $@

$(APP_OUT)/sce_sys/%.png: app/sce_sys/%.png
	@mkdir -p $(dir $@)
	cp $< $@

# pic0.dds (the home screen's background while the icon is selected) and pic1.dds (the launch background):
# 3840x2160 BC7_UNORM DX10 DDS without mipmaps, made from app/sce_sys/background-source.png with the
# ps5-native-app-boilerplate's tools/prepare-assets.sh --background (pinned bc7enc_rdo).
$(APP_OUT)/sce_sys/%.dds: app/sce_sys/%.dds
	@mkdir -p $(dir $@)
	cp $< $@

# ---------------- host ----------------
HOST_CC ?= gcc
HOST_CXX ?= g++
HOST_OUT := build/host
# the Vulkan headers for the host's GPU renderer (the system's, else RADV's copy in a PS5_Vulkan checkout)
HOST_VK_INC ?= $(if $(wildcard /usr/include/vulkan/vulkan.h),,-isystem $(HOME)/PS5_Vulkan/.deps/native/radv-release/include)
HOST_VK_LIB ?= $(if $(wildcard /usr/lib/x86_64-linux-gnu/libvulkan.so),-lvulkan,-l:libvulkan.so.1)
HOST_SAN ?= -fsanitize=address,undefined -fno-omit-frame-pointer
# The core under AddressSanitizer only (UBSan would report its deliberate shifts and unaligned reads).
HOST_CORE_SAN ?= -fsanitize=address -fno-omit-frame-pointer
HOST_OPT := -O1 -g -mavx2 -fno-strict-aliasing -U_FORTIFY_SOURCE
HOST_CORE_OPT := -O2 -g1 -mavx2 -U_FORTIFY_SOURCE

# Ymir's core for the host: the same CMake build for Linux (YMIR_HOST_BUILD; tools/build-ymir-core.sh's settings, the
# host compiler, no exceptions)
YMIR_HOST_BUILD ?= $(abspath ../../ymir-ps5/build/host-noexc)
HOST_ZLIB_OBJ := $(patsubst $(ZLIB)/%.c,$(HOST_OUT)/zlib/%.o,$(ZLIB_SRC))
HOST_CORE_OBJ := $(subst $(YMIR_BUILD),$(YMIR_HOST_BUILD),$(YMIR_LIBRETRO_OBJ) $(YMIR_ARCHIVES)) $(HOST_ZLIB_OBJ)
HOST_EMU_OBJ := $(patsubst %.cpp,$(HOST_OUT)/port/%.o,$(EMU_SRC) host/sce_host.cpp) $(patsubst %.c,$(HOST_OUT)/port/%.o,$(EMU_C_SRC)) \
	$(patsubst %.c,$(HOST_OUT)/bridge/%.o,$(BRIDGE_C_SRC))
HOST_MBEDTLS_OBJ := $(patsubst $(MBEDTLS)/library/%.c,$(HOST_OUT)/mbedtls/%.o,$(MBEDTLS_SRC))
HOST_HELPER_OBJ := $(HOST_OUT)/port/installer/helper_main.o $(patsubst %.cpp,$(HOST_OUT)/port/%.o,$(HELPER_SRC) host/sce_host.cpp) \
	$(HOST_MBEDTLS_OBJ)
HOST_INSTALLER_OBJ := $(patsubst %.cpp,$(HOST_OUT)/port/%.o,$(INSTALLER_SRC) host/sce_host.cpp) $(HOST_MBEDTLS_OBJ)
HOST_LZMA7Z_OBJ := $(patsubst $(LZMA7Z)/%.c,$(HOST_OUT)/lzma7z/%.o,$(LZMA7Z_SRC))
HOST_AVIF_OBJ := $(patsubst $(AVIF)/src/%.c,$(HOST_OUT)/avif/%.o,$(AVIF_SRC))
HOST_DAV1D_LIB := $(HOST_OUT)/dav1d/src/libdav1d.a

host-core: $(HOST_CORE_OBJ)
host: $(HOST_OUT)/ssymirps5-host $(HOST_OUT)/ssymirps5-installer $(HOST_OUT)/ssymirps5-helper

$(HOST_OUT)/zlib/%.o: $(ZLIB)/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -fPIC $(HOST_CORE_OPT) -DHAVE_UNISTD_H -w -c -o $@ $<

$(HOST_OUT)/mbedtls/%.o: $(MBEDTLS)/library/%.c frontend/fe_mbedtls_config.h
	@mkdir -p $(dir $@)
	$(HOST_CC) -O2 -g1 $(MBEDTLS_FLAGS) -c -o $@ $<

$(HOST_OUT)/port/%.o: %.c
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_OPT) $(MINIZIP_FLAGS) -c -o $@ $<

$(HOST_OUT)/port/%.o: %.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h frontend/*.h $(VERSION_STAMP)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(HOST_SAN) $(DEFS) $(INCS) $(HOST_VK_INC) $(PORT_WARN) $(EXTRA_FLAGS) -c -o $@ $<

# the 7z decoder under AddressSanitizer too (it reads archives from anywhere; UBSan would report its unaligned reads)
$(HOST_OUT)/avif/%.o: $(AVIF)/src/%.c
	@mkdir -p $(dir $@)
	$(HOST_CC) -O2 -g1 -fPIC $(AVIF_FLAGS) -w -c -o $@ $<
$(HOST_DAV1D_LIB):
	bash tools/build-dav1d.sh $(HOST_OUT)/dav1d host

$(HOST_OUT)/lzma7z/%.o: $(LZMA7Z)/%.c $(wildcard $(LZMA7Z)/*.h)
	@mkdir -p $(dir $@)
	$(HOST_CC) -fPIC $(HOST_OPT) $(HOST_CORE_SAN) $(LZMA7Z_FLAGS) -c -o $@ $<
$(HOST_OUT)/lzma7z/names.ok: $(HOST_LZMA7Z_OBJ)
	$(call LZMA7Z_NAMES_CHECK,nm,$^)

$(HOST_OUT)/port/installer/helper_main.o: installer/installer_main.cpp coreorbis/include-orbis/*.h coreorbis/orbis-shims/*.h $(VERSION_STAMP)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(HOST_SAN) $(DEFS) $(INCS) $(PORT_WARN) $(EXTRA_FLAGS) -c -o $@ $<

# The host installer carries a stand-in app folder (tests/fake-eboot.bin, tests/fake-libc.prx + the real
# param/icon/pic); the host app carries the host helper, which it hands to a fake ELF loader in the tests.
HOST_APP_DEFS = -DAPP_EBOOT='"$(abspath tests/fake-eboot.bin)"' -DAPP_PARAM='"$(abspath app/sce_sys/param.json)"' \
	-DAPP_ICON0='"$(abspath app/sce_sys/icon0.png)"' -DAPP_PIC0='"$(abspath app/sce_sys/pic0.dds)"' \
	-DAPP_PIC1='"$(abspath app/sce_sys/pic1.dds)"' \
	-DAPP_LIBC='"$(abspath tests/fake-libc.prx)"'

$(HOST_OUT)/install_data.o: coreorbis/orbis-shims/install_data.cpp coreorbis/orbis-shims/ProsperoInstall.h \
		tests/fake-eboot.bin tests/fake-libc.prx app/sce_sys/param.json app/sce_sys/icon0.png app/sce_sys/pic0.dds app/sce_sys/pic1.dds
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(DEFS) $(INCS) $(PORT_WARN) $(HOST_APP_DEFS) -c -o $@ $<

$(HOST_OUT)/helper_data.o: coreorbis/orbis-shims/helper_data.cpp coreorbis/orbis-shims/ProsperoJailbreak.h $(HOST_OUT)/ssymirps5-helper
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(DEFS) $(INCS) $(PORT_WARN) -DHELPER_ELF='"$(abspath $(HOST_OUT)/ssymirps5-helper)"' -c -o $@ $<

$(HOST_OUT)/ssymirps5-helper: $(HOST_HELPER_OBJ)
	$(HOST_CXX) $(HOST_SAN) -o $@ $^ -lpthread

$(HOST_OUT)/ssymirps5-installer: $(HOST_INSTALLER_OBJ) $(HOST_OUT)/install_data.o
	$(HOST_CXX) $(HOST_SAN) -o $@ $^ -lpthread

# the shader tables: data only, no sanitizer, no optimisation (a large file)
$(HOST_OUT)/slang_presets.o: $(SLANG_CPP) coreorbis/orbis-shims/ProsperoSlang.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -O0 -Icoreorbis/orbis-shims -c -o $@ $<

HOST_GLSLANG_OBJ := $(patsubst %.cpp,$(HOST_OUT)/glslang/%.o,$(GLSLANG_SRC))
$(HOST_OUT)/glslang/%.o: %.cpp
	@mkdir -p $(dir $@)
	$(HOST_CXX) $(GLSLANG_FLAGS) -O2 -g1 -c -o $@ $<

$(HOST_OUT)/ssymirps5-host: $(HOST_CORE_OBJ) $(HOST_EMU_OBJ) $(HOST_MBEDTLS_OBJ) $(HOST_LZMA7Z_OBJ) \
		$(HOST_AVIF_OBJ) $(HOST_DAV1D_LIB) \
		$(HOST_OUT)/helper_data.o $(HOST_OUT)/slang_presets.o $(HOST_GLSLANG_OBJ) | $(HOST_OUT)/lzma7z/names.ok
	$(HOST_CXX) $(HOST_SAN) -o $@ $(filter %.o,$^) -Wl,--start-group $(filter %.a,$^) -Wl,--end-group -Wl,--wrap=chd_read -Wl,--wrap=chd_close -lpthread -lm $(HOST_VK_LIB)

# the shelf's storage tabs (frontend/fe_tabs.h): header only, so no app code is linked
TABS_TEST := $(HOST_OUT)/tabs-test
$(TABS_TEST): tests/test_tabs.cpp frontend/fe_tabs.h frontend/fe_games.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(HOST_SAN) $(PORT_WARN) -Ifrontend -o $@ tests/test_tabs.cpp

test-tabs: $(TABS_TEST)
	$(TABS_TEST)

# 3.11.3: fast forward's pace (frontend/fe_ffpace.h): header only
FFPACE_TEST := $(HOST_OUT)/ffpace-test
$(FFPACE_TEST): tests/test_ffpace.cpp frontend/fe_ffpace.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(HOST_SAN) $(PORT_WARN) -Ifrontend -o $@ tests/test_ffpace.cpp

test-ffpace: $(FFPACE_TEST)
	$(FFPACE_TEST)

# `make test`: the host's unit tests
test: test-tabs test-ffpace test-backsources unit-tests

# 2.1's unit tests, ported from Snes9x PS5 2.7:
# the cover and border matcher over the built-in tables (headers only)
TITLEMATCH_TEST := $(HOST_OUT)/titlematch-test
$(TITLEMATCH_TEST): tests/test_titlematch.cpp frontend/fe_titlematch.h frontend/fe_artmatch.h frontend/fe_dbart.h frontend/fe_romheader.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -O2 -g $(HOST_SAN) $(PORT_WARN) -Ifrontend -o $@ tests/test_titlematch.cpp
# the border pack code (fe_border.cpp, fe_borderimg.cpp, fe_borderfetch.cpp) with the renderer, the cover matcher and the game
# naming stood in for by the test: generated PNGs through the lookup order, the window, the backdrop and the rim
BORDER_TEST := $(HOST_OUT)/border-pack-test
BORDER_TEST_OBJ := $(patsubst %.cpp,$(HOST_OUT)/port/%.o,tests/test_border_pack.cpp frontend/fe_border.cpp frontend/fe_borderimg.cpp \
	frontend/fe_borderfetch.cpp frontend/fe_coverfetch.cpp frontend/fe_settings.cpp coreorbis/orbis-shims/orbis_paths.cpp)
$(BORDER_TEST): $(BORDER_TEST_OBJ)
	$(HOST_CXX) $(HOST_SAN) -o $@ $^ -lpthread -lz
# the border in the renderer (ProsperoVideo.cpp) on the CPU and for the GPU (stood in for), against the host's video output
BORDER_RENDER_TEST := $(HOST_OUT)/border-render-test
BORDER_RENDER_OBJ := $(patsubst %.cpp,$(HOST_OUT)/port/%.o,tests/test_border_render.cpp coreorbis/orbis-shims/ProsperoVideo.cpp \
	coreorbis/orbis-shims/ProsperoCrt.cpp coreorbis/orbis-shims/ProsperoCrash.cpp coreorbis/orbis-shims/orbis_paths.cpp host/sce_host.cpp)
$(BORDER_RENDER_TEST): $(BORDER_RENDER_OBJ)
	$(HOST_CXX) $(HOST_SAN) -o $@ $^ -lpthread
# the helper's download loop with its covers and borders lanes (fe_coverworker.cpp), against a fake HTTP client
DOWNLOAD_WORKER_TEST := $(HOST_OUT)/download-worker-test
DOWNLOAD_WORKER_OBJ := $(patsubst %.cpp,$(HOST_OUT)/port/%.o,tests/test_download_worker.cpp frontend/fe_coverworker.cpp frontend/fe_coverfetch.cpp \
	frontend/fe_http.cpp coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoCrash.cpp host/sce_host.cpp)
$(DOWNLOAD_WORKER_TEST): $(DOWNLOAD_WORKER_OBJ)
	$(HOST_CXX) $(HOST_SAN) -o $@ $^ -lpthread
# 2.2: the 7z reader (fe_7z.cpp and the decoder) on the archives in tests/7z/
SEVENZIP_TEST := $(HOST_OUT)/7z-test
$(SEVENZIP_TEST): $(HOST_OUT)/port/tests/test_7z.o $(HOST_OUT)/port/frontend/fe_7z.o $(HOST_LZMA7Z_OBJ) | $(HOST_OUT)/lzma7z/names.ok
	$(HOST_CXX) $(HOST_SAN) -o $@ $(filter %.o,$^) -lz
test-7z: $(SEVENZIP_TEST)
	$(SEVENZIP_TEST) tests/7z
unit-tests: $(TITLEMATCH_TEST) $(BORDER_TEST) $(BORDER_RENDER_TEST) $(DOWNLOAD_WORKER_TEST) $(SEVENZIP_TEST)
	$(TITLEMATCH_TEST)
	$(BORDER_TEST)
	@# (its GPU-surface checks fail since 2.7's 4K surface, before 3.11.10: shown, but they do not stop the others)
	-$(BORDER_RENDER_TEST)
	$(DOWNLOAD_WORKER_TEST)
	$(SEVENZIP_TEST) tests/7z

# The GPU's shaders: shaders/presets.txt (RetroArch slang presets, in shaders/slang/) compiled to SPIR-V tables
# with their reflection and textures (tools/slang_compile.py, which needs glslangValidator).
$(SLANG_CPP): shaders/presets.txt tools/slang_compile.py shaders/plain.slang shaders/ui.slang
	@mkdir -p $(dir $@)
	python3 tools/slang_compile.py $@ --list shaders/presets.txt
shaders: $(SLANG_CPP)

# the CRT shaders' drawing time on this computer, built with the PS5's flags (tools/shader_bench.cpp)
shader-bench: $(HOST_OUT)/shader-bench
	$(HOST_OUT)/shader-bench 300
	$(HOST_OUT)/shader-bench 300 720

$(HOST_OUT)/shader-bench: tools/shader_bench.cpp coreorbis/orbis-shims/ProsperoCrt.cpp coreorbis/orbis-shims/ProsperoCrt.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -O3 -march=znver2 -fno-strict-aliasing $(INCS) -o $@ tools/shader_bench.cpp \
		coreorbis/orbis-shims/ProsperoCrt.cpp -lpthread


clean:
	rm -rf build



.PHONY: test-7z
.PHONY: shaders shader-bench ps5 ps5-core host-core host test test-tabs unit-tests send strip app dist clean
.SECONDARY:

# 3.9: the Saturn cheats (fe_cheats.cpp; the libretro files: YMIR_TEST_CHT_DIR)
test-cheats-saturn: $(HOST_OUT)/test_cheats_saturn
	$(HOST_OUT)/test_cheats_saturn $(HOST_OUT)/cheats-saturn-test
$(HOST_OUT)/test_cheats_saturn: tests/test_cheats_saturn.cpp frontend/fe_cheats.cpp frontend/fe_cheats.h frontend/fe_backsources.cpp \
		frontend/fe_artmatch.h frontend/data/saturn-cheats.txt frontend/data/saturn-serials.tsv frontend/data/chromagi-saturn.tsv \
		$(HOST_OUT)/port/frontend/third_party/unzip.o $(HOST_OUT)/port/frontend/third_party/ioapi.o $(HOST_ZLIB_OBJ)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(HOST_SAN) $(INCS) -DSATURN_CHEATS_TXT='"$(abspath frontend/data/saturn-cheats.txt)"' \
		-DSATURN_SERIALS_TSV='"$(abspath frontend/data/saturn-serials.tsv)"' -DCHROMAGI_TSV='"$(abspath frontend/data/chromagi-saturn.tsv)"' \
		-o $@ tests/test_cheats_saturn.cpp frontend/fe_cheats.cpp frontend/fe_backsources.cpp \
		$(HOST_OUT)/port/frontend/third_party/unzip.o $(HOST_OUT)/port/frontend/third_party/ioapi.o $(HOST_ZLIB_OBJ) -lpthread

# 2.3: the back covers' game lookup (product numbers, Chromagi's pages, a LaunchBox database)
test-backsources: $(HOST_OUT)/test_backsources
	$(HOST_OUT)/test_backsources $(HOST_OUT)
$(HOST_OUT)/test_backsources: tests/test_backsources.cpp frontend/fe_backsources.cpp frontend/fe_backsources.h frontend/fe_artmatch.h \
		frontend/data/saturn-serials.tsv frontend/data/chromagi-saturn.tsv $(HOST_OUT)/port/frontend/third_party/unzip.o \
		$(HOST_OUT)/port/frontend/third_party/ioapi.o $(HOST_ZLIB_OBJ)
	$(HOST_CXX) -std=c++17 $(HOST_OPT) $(HOST_SAN) $(INCS) -DSATURN_SERIALS_TSV='"$(abspath frontend/data/saturn-serials.tsv)"' \
		-DCHROMAGI_TSV='"$(abspath frontend/data/chromagi-saturn.tsv)"' -o $@ tests/test_backsources.cpp frontend/fe_backsources.cpp \
		$(HOST_OUT)/port/frontend/third_party/unzip.o $(HOST_OUT)/port/frontend/third_party/ioapi.o $(HOST_ZLIB_OBJ)

# Full-screen capture regression tests (CPU/GPU channel order, hotkey and async writer).
.PHONY: test-screenshots
build/tests/test-screenshots: tests/test_screenshots.cpp frontend/fe_screenshot.cpp frontend/fe_screenshot_io.h frontend/fe_screenshot.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror $(HOST_SAN) -g $(INCS) -o $@ tests/test_screenshots.cpp frontend/fe_screenshot.cpp -lz -pthread
test-screenshots: build/tests/test-screenshots
	@tmp=$$(mktemp -d /tmp/ymir-screenshots-XXXXXX); $< "$$tmp"
test: test-screenshots

# Independent Vulkan final-composition test; uses lavapipe and glslangValidator, no Saturn BIOS or game.
.PHONY: test-screenshot-gpu
build/tests/test-screenshot-gpu: HOST_VK_INC = $(if $(wildcard /usr/include/vulkan/vulkan.h),,-isystem $(abspath ../../PS5_Vulkan)/.deps/native/radv-release/include)
build/tests/test-screenshot-gpu: tests/test_screenshot_gpu.cpp coreorbis/orbis-shims/ProsperoGpu.cpp frontend/fe_screenshot.cpp $(SLANG_CPP)
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -O1 -g $(INCS) $(HOST_VK_INC) -o $@ $^ -lz -pthread $(HOST_VK_LIB)
test-screenshot-gpu: build/tests/test-screenshot-gpu
	@tmp=$$(mktemp -d /tmp/ymir-gpu-shot-XXXXXX); VK_ICD_FILENAMES=/usr/share/vulkan/icd.d/lvp_icd.json $< "$$tmp"

# Geometric navigation icons with real fonts, at 1080p and 4K.
.PHONY: test-navigation-icons
build/tests/test-navigation-icons: tests/test_navigation_icons.cpp frontend/fe_style.cpp frontend/fe_style.h frontend/fe_text.cpp frontend/fe_text.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -O1 -g $(HOST_SAN) -Ifrontend -Icoreorbis/orbis-shims -DFE_FONT_DIR=\"$(abspath frontend/assets/fonts)\" tests/test_navigation_icons.cpp frontend/fe_style.cpp frontend/fe_text.cpp -o $@
test-navigation-icons: build/tests/test-navigation-icons
	$< build/tests
test: test-navigation-icons

# Reserved case actions and bounded animated text, five languages at 1080p/4K.
.PHONY: test-case-panel
build/tests/test-case-panel: tests/test_case_panel.cpp frontend/fe_casepanel.h frontend/fe_style.cpp frontend/fe_text.cpp frontend/fe_text.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -O1 -g $(HOST_SAN) -Ifrontend -Icoreorbis/orbis-shims -DFE_FONT_DIR=\"$(abspath frontend/assets/fonts)\" tests/test_case_panel.cpp frontend/fe_style.cpp frontend/fe_text.cpp -o $@
test-case-panel: build/tests/test-case-panel
	$< build/tests
test: test-case-panel


# Brand identity: data/USB roots and update archive filtering; no network or console needed.
.PHONY: test-identity
build/tests/test-identity: tests/test_identity.cpp coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoUpdate.cpp coreorbis/include-orbis/OrbisPaths.h coreorbis/orbis-shims/ProsperoUpdate.h
	@mkdir -p $(dir $@)
	$(HOST_CXX) -std=c++17 -Wall -Wextra -Werror $(HOST_SAN) -g $(INCS) -o $@ tests/test_identity.cpp coreorbis/orbis-shims/orbis_paths.cpp coreorbis/orbis-shims/ProsperoUpdate.cpp -pthread
test-identity: build/tests/test-identity
	$< --default
	@tmp=$$(mktemp -d /tmp/ssymirps5-identity-XXXXXX); $< "$$tmp"
test: test-identity
