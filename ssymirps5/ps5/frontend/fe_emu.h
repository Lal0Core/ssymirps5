// SSYmirPS5 frontend: the Ymir Sega Saturn core behind a small API.
//
// The core is driven through its libretro interface (apps/ymir-libretro, statically linked): the
// frontend is the libretro "frontend" -- it answers the core's environment calls (folders, options, log), gets
// each frame (XRGB8888) and its sound (44.1 kHz) through callbacks, and reports the pads. One retro_run() is one
// frame, called from the frontend's thread, as SSYmirPS5 runs its core.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace emu
{
// The choices the settings menu offers.
struct Choice
{
	const char* name;
	const char* value; // the core option's value ("" where the frontend handles it)
};
extern const Choice kAspects[]; // the core's (4:3 TV), square pixels, 4:3, 16:9
extern const int kAspectCount;
extern const Choice kRegions[]; // ymir_region
extern const int kRegionCount;
extern const Choice kCartridges[]; // ymir_cartridge
extern const int kCartridgeCount;
extern const Choice kCdSpeeds[]; // ymir_cd_speed
extern const int kCdSpeedCount;
extern const Choice kSh2Clocks[]; // ymir_sh2_clock
extern const int kSh2ClockCount;
constexpr int kSh2ClockDefault = 3; // 100%
extern const Choice kAudioInterps[]; // ymir_audio_interpolation
extern const int kAudioInterpCount;
extern const Choice kUpscalers[]; // the frontend's: none, AMD FSR 1.0 (EASU + RCAS, on the GPU)
extern const int kUpscalerCount;
extern const Choice kPadTypes[]; // Saturn pad, 3D Control Pad, Arcade Racer, Mission Stick
extern const int kPadTypeCount;
// The GPU shader (ps5gpu index) the picture uses: FSR when upscaling is on and the GPU draws, else the chosen one.
int EffectiveShader();
// The folder the Saturn BIOS was found in ("" when there is none), looked up again every 2 s at most.
std::string BiosFolder();

// The buttons of the console's pad the player can move, and the PS5 buttons they can go to.
struct PadButton
{
	const char* name; // "Square"
	unsigned bit; // SCE_PAD_BUTTON_*; 0 = not assigned
};
extern const PadButton kPs5Buttons[]; // Cross, Circle, Square, Triangle, L1, R1, L2, R2, OPTIONS, (none)
extern const int kPs5ButtonCount;
struct ConsoleButton
{
	const char* name; // "A", "B"...
	const char* key; // its key in ssymirps5.ini: "btn_a"
	unsigned retro_id; // the RetroPad button Ymir reads for it
	int default_ps5; // index in kPs5Buttons
};
constexpr int kConsoleButtonCount = 9; // A, B, C, X, Y, Z, L, R, Start
extern const ConsoleButton kConsoleButtons[kConsoleButtonCount];

bool InitCore();
void DeinitCore();

// Starts the game. On failure, *error says why (a missing Saturn BIOS names the files and where they go).
bool LoadGame(const std::string& path, std::string* error);
void CloseGame(); // writes the battery save and ends the game
bool GameLoaded();
std::string GameName(); // the file name without its extension
std::string SystemName(); // "Saturn"

// Settings from fe::Config() -> the core's options and the frontend's picture/sound settings.
void ApplySettings();

enum class FrameResult
{
	Continue,
	OpenMenu, // touchpad released after at most 1 second
	Stopped, // no game
};
// One frame: the pads and hot keys, retro_run(), the picture (and the messages) on the screen, the sound to
// the audio ring, paced by the display (60 Hz games) or by the sound (50 Hz games). Hot keys (the Saturn pad uses
// every face and shoulder button): touchpad + Up / Down = save / load the state slot, touchpad + Left / Right =
// change the slot, R3 held = fast forward, L3 held = rewind, a touchpad press of at most 1 second = the menu.
FrameResult RunFrame();

void Pause(); // nothing runs between frames; kept for the menus' symmetry
void Resume();

bool SaveState(int slot); // 1..10, /data/ssymirps5/states/<game>.state<slot>
bool LoadState(int slot);
bool StateExists(int slot);
// The state the slot held before its last save (<state>.prev), kept when a state is written over another.
bool LoadPreviousState(int slot);
bool PreviousStateExists(int slot);
// 3.11: the picture of the game when the slot's state (or the one before it) was saved: <state>.thumb, written with
// the state, in the surface's format (row-major). False when there is no such state; px left empty when it has no
// picture (saved before 3.11). *when: the state's time (time()).
bool StateThumb(int slot, bool previous, std::vector<uint32_t>& px, int& w, int& h, long long* when);
// When a state was saved, for the screen: "Saved today at 14:32", "Saved 3 days ago"; short: "today 14:32".
std::string StateWhen(long long when, bool short_form);
// 3.9.9: "Continue where you left off": a state of its own (states/<system>/<game>.auto, none of the 10 slots), written
// when the player leaves the game and offered by the shelf the next time it starts.
std::string AutoStatePathFor(const std::string& rom); // the file, also without a game running
bool SaveAutoState();
bool LoadAutoState();
void Reset();
// Games of several discs (an .m3u, or a disc set the shelf grouped): how many, which one is in, and swapping it (the
// lid opened and closed, as on the console: do it when the game asks for the next disc).
int DiscCount(); // 0 or 1: one disc
// Empties (formats) the internal backup memory (0) or the backup cartridge's (1), writes it to its file, and resets
// the game (it reads the memory when it starts). False: no game, no backup cartridge in the slot.
bool FormatBackup(int which);
bool HasBackupCart(); // a backup memory cartridge is in the slot
int DiscIndex();
bool ChangeDisc(int index);
void PowerCycle(); // the game loaded again, as switching the console off and on
void Osd(const std::string& text); // a message over the game for a few seconds

// Redraws the last frame into the surface (the pause menu draws over it).
void RedrawLastFrame();
} // namespace emu
