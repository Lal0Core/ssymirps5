// SSYmirPS5 frontend: settings kept in /data/ssymirps5/ssymirps5.ini (key=value lines).
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace fe
{
struct Settings
{
	// video
	int renderer = 1;            // 1 Vulkan (the GPU, RADV; the default when the build has it), 0 CPU; read at start
	int upscaler = 0;            // emu::kUpscalers: 0 off, 1 AMD FSR 1.0, 2 SMAA + FSR 1.0 (the GPU only); 1.0: off by default
	int fsr_sharpness = 2;       // FSR's RCAS sharpening, in tenths of a stop: 0 the sharpest .. 20 the softest
	int scale = 0; // ps5video::Scale: fit, integer, stretch
	// the GPU's (or the CPU's) shader when upscaling is off: 0 off. -1 (1.0): none chosen yet, the first start; once the
	// video is up, main-boot.cpp makes it CRT Easymode (the picture for 2D games), found by its name
	int shader = -1;
	int aspect = 0; // emu::kAspects: the core's (TV), square pixels, 4:3, 16:9
	int border = 0; // art around the picture (fe_border.h): 0 Off, 1 Auto (the game's own, else a default), 2 Default frame
	bool border_download = false; // Auto also downloads the game's border from The Bezel Project
	bool smooth = true; // bilinear when the scale isn't whole (the CPU and the plain picture)
	bool scanlines = false;
	bool deinterlace = false; // ymir_deinterlace: the high-resolution interlaced modes drawn progressive
	bool transparent_meshes = false; // ymir_transparent_meshes: mesh (checkerboard) sprites drawn half-transparent
	bool show_fps = false;
	bool screenshots = true; // full-screen PNG: hold touchpad over 1 second to capture once; short release opens menu
	bool launch_anim = true; // 3.10: opening a game: the shelf fades to black and the game's disc spins (fe_shelf.cpp)
	// audio
	bool audio = true;
	int volume = 100; // 0..100
	int audio_interp = 0; // emu::kAudioInterps
	bool threaded_scsp = false; // ymir_threaded_scsp
	// emulation
	int ff_speed = 0; // % while R3 is held: 150, 200, or 0 = as fast as possible (3.11.5: the default; 300% and up are gone)
	bool rewind = false; // L3 held rewinds (a Saturn state is several MB: off by default)
	bool resume = true; // 3.9.9: a state when leaving a game, and the shelf offers to continue from it
	int region = 0; // emu::kRegions: auto, Japan, North America, Europe
	int cartridge = 0; // emu::kCartridges: auto (the game database's, else backup memory), none, 1 MB / 4 MB DRAM, KOF95 / Ultraman ROM, backup memory
	bool cart_shared = false; // the backup cartridge: one for every game (as the console's) instead of one per game
	int cd_speed = 0; // emu::kCdSpeeds: 2x, 4x, 8x
	int sh2_clock = 3; // emu::kSh2Clocks: 100%
	bool sh2_cache = false; // ymir_sh2_cache
	bool cdblock_lle = false; // ymir_cdblock_lle: the CD block's own ROM (bios/cdb/), more accurate, slower
	int state_slot = 1; // 1..10
	// controls
	int pad_type = 0; // emu::kPadTypes: Saturn pad, 3D Control Pad, Arcade Racer, Mission Stick (player 1)
	// the PS5 button (index in emu::kPs5Buttons) of each console button, in emu::kConsoleButtons' order:
	// A, B, C, X, Y, Z, L, R, Start. By position by default: Square, Cross, Circle, L1, Triangle, R1, L2, R2, OPTIONS.
	static constexpr int kButtons = 9;
	static constexpr int kDefaultButtons[kButtons] = {2, 0, 1, 4, 3, 5, 6, 7, 8};
	int buttons[kButtons] = {2, 0, 1, 4, 3, 5, 6, 7, 8};
	void DefaultButtons();
	// library
	bool covers_download = true; // fetch box art from libretro-thumbnails
	int language = 1;            // fe::Language: 0 English, 1 Spanish (the default), 2 Portuguese (Brazil), 3 French, 4 German
	bool perf_log = false;       // logs/perf-<system>-<game>.csv, a row a second in a game (fe_emu.cpp, Perf)
	bool debug_logs = false;     // boot.log and the others in /data/ssymirps5/logs (OrbisLogSetEnabled)
	int shelf_family = 0; // fe::Family the shelf shows (before 2.0)
	int shelf_letter = 0; // the shelf's letter filter: 0 all, 1 "#", 2..27 A..Z
	int shelf_sort = 0; // 3.11: the shelf's order: 0 name, 1 last played, 2 most played, 3 year (the Recent tab: by time)
	int last_tab = 0; // the shelf's tab (fe_tabs.h): 0 Internal, 1 USB, 2 Recent
	std::string last_rom; // the shelf puts the selection on this game

	void Load();
	// Writes the settings: the global file, and -- while a game with its own settings runs -- that game's file
	// (the game's keys go there, the global file keeps the global ones).
	void Save() const;

	// The settings parser and writer, for any file. game_keys_only: only the keys a game can have of its own.
	bool LoadFrom(const std::string& path);
	bool SaveTo(const std::string& path, bool game_keys_only) const;
};

Settings& Config();

// Per-game settings (/data/ssymirps5/games/<system>/<game>.ini): the picture, sound, emulation and control settings a
// game can keep of its own; the library, logs, fast forward and rewind stay global.
// BeginGame: when the game has its own file, its settings replace the global ones until EndGame.
void BeginGame(const std::string& game_ini);
void EndGame();
bool GameHasOwnSettings();
// Makes the running game use its own settings (starting from the current ones), or the global ones again (its
// file removed). False when no game runs or the file can't be written.
bool SetGameOwnSettings(bool own);
} // namespace fe
