// SSYmirPS5 frontend: the cheats (3.9). Saturn Action Replay codes in RetroArch's .cht files.
//
// Where they come from, best first:
//   1. cheats/Saturn/<the game's file name>.cht or cheats/<the game's file name>.cht: your own
//   2. cheats/Saturn/<a libretro name>.cht: downloaded once, when the game starts, from libretro-database's
//      "Sega - Saturn" folder (192 games). The game is matched to its name there by its file name, else by the title
//      its product number names (the built-in serial table), as the covers are.
// The codes on are written to memory every frame by the core (retro_cheat_set: 1xxxxxxx word, 3xxxxxxx byte,
// Dxxxxxxx "if equal"); which ones are on is kept in the .cht itself (cheatN_enable), so they are on the next time.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace fe
{
namespace cheats
{
struct Cheat
{
	std::string desc;
	std::string code;
	bool on = false;
	bool usable = true; // false: the code has a value to fill in ("??") or is not one the core reads
	bool retroarch = false; // 3.9.1: RetroArch's own format (an address and a value, no code): not read here
	bool hidden = false; // only the master code (F6000914 C305...): a real cartridge needs it, the emulator does not
};

enum class State
{
	Idle, // no game
	Looking, // reading the disc's header, downloading
	Ready, // the list is loaded (it may be empty: none for this game)
	None, // no cheats for this game
	Failed, // the download failed (no network): tried again at the next start
};

// A game starts: finds (or downloads, in the background) its cheats and turns on the ones that were on.
void Begin(const std::string& game_path);
// The game closes.
void End();
State Status();
// The cheats of the game (after Ready), and where they are kept ("" when none).
std::vector<Cheat> List();
std::string File();
// Turns one cheat on or off (or all: index -1), saves which ones are on in the .cht, and gives the core the codes.
void Set(int index, bool on);
// Called on the emulator's thread each frame: hands the core the codes when they changed (the download finished, a
// cheat was turned on).
void Pump();

// For the tests.
bool Parse(const std::string& text, std::vector<Cheat>& out);
std::string Write(const std::string& original, const std::vector<Cheat>& list);
bool Usable(const std::string& code);
// The libretro name of a game ("" if it has none): from its file name, else from the title of its product number.
std::string MatchName(const std::string& file_name, const std::string& product);
std::string UrlFor(const std::string& name);
// 3.9.2: Almar's Guides' list (one Markdown file for every game): the games, each with its cheats (name, code).
struct DbGame
{
	std::string name; // "Astal (USA)"
	std::vector<std::pair<std::string, std::string>> cheats;
};
std::vector<DbGame> ParseAlmar(const std::string& text);
// The game of the list a file is (by its name, else by its product number's title): its index, or -1.
// loose: also by the main title alone when one name has it (your own files)
int MatchDbGame(const std::vector<DbGame>& db, const std::string& file_name, const std::string& product, bool loose = false);
} // namespace cheats
} // namespace fe
