// SSYmirPS5 frontend: the game library -- every ROM under the ROM folders, with its system and official name.
//
// The system comes from the file extension (inside a .zip or a .7z, from the ROM in it). A game is then named the way
// libretro names it:
//   1. its file name, when it is already a Redump name ("Panzer Dragoon (Europe).cue");
//   2. otherwise the product number in its disc's header, looked up in the table built into the app
//      (data/saturn-serials.tsv), and the table of names (data/nointro.tsv: Redump's Saturn names;
//      tools/make_gamedb.py makes it);
//   3. otherwise the file name, loosely: "panzer dragoon.cue" -> the best entry with that title (USA first,
//      then World, Europe, Japan).
// A ROM none of that names (a fan translation, a hack) keeps its file name as the title. For the cover and border
// lookup it also carries what its CRC is called in the extra table (data/extra-crc.tsv) and the title in its own header
// (fe_romheader.h, fe_artmatch.h), as Snes9x PS5 2.7 does (pull request #3 by thatboialex).
//
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include "fe_romheader.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace fe
{
enum class System : uint8_t
{
	Saturn, // Sega Saturn
	Count
};

// The shelf's tabs.
enum class Family : uint8_t
{
	All,
	Saturn,
	Count
};

struct SystemInfo
{
	const char* id; // "ss": the table's and the cover cache's tag
	const char* name; // "Saturn"
	const char* thumbs; // the libretro-thumbnails repository
	Family family;
	const char* folder; // "Saturn": its folder in games/ and in covers/
};
const SystemInfo& Info(System s);
const char* FamilyName(Family f); // "All games", "Saturn"

// Creates roms/Saturn, covers/Saturn and borders/Saturn.
void PrepareFolders();

// ".cue" -> System::Saturn; false for anything that isn't a game we list.
bool SystemForExt(const std::string& ext, System* out);

struct GameInfo
{
	std::string path; // full path
	std::string file_base; // file name without extension
	std::string ext; // ".cue", ".chd"... lower case
	System system = System::Saturn;
	std::string nointro; // official name, "" until known (or unknown)
	std::string title; // what the shelf shows
	std::string region; // "USA", "Europe"... from the name's tags
	bool on_usb = false;
	bool name_by_crc = false; // the name came from the CRC (not the file name)
	uint32_t crc = 0; // the ROM's CRC32 (without a copier header) when the scan took it; 0 if not
	std::string crc_name; // the name the ROM's CRC has in the extra table (a translation, a hack), "" if none
	std::string header_title; // the ROM header's title, only for a ROM no name identified; "" otherwise
	bool header_japan = false; // that header says Japan only
	int disc_count = 0; // a disc set the scan grouped (its .m3u in cache/discs): how many discs; 0 otherwise
	int year = 0; // 3.11: the year its disc's header gives (the shelf's order by year); 0 when not known
};

// The table built into the app.
namespace gamedb
{
size_t Count();
// "" when unknown. *found_in: the system whose table knew it (one of `s`'s family).
std::string ByCrc(System s, uint32_t crc, System* found_in);
// Exact Redump name ("Panzer Dragoon (Europe)") -> itself, or "".
std::string Exact(System s, const std::string& name, System* found_in);
// Loose: title without tags, any case ("sonic the hedgehog") -> best regional entry, or "".
std::string Loose(System s, const std::string& file_base, System* found_in);
// "Panzer Dragoon (Europe)" -> "Panzer Dragoon"; "Ooze, The (...)" -> "The Ooze"
std::string Title(const std::string& nointro);
std::string Region(const std::string& nointro);
// The name of a dump No-Intro does not list (data/extra-crc.tsv: fan translations, hacks, homebrew, other dump sets),
// in s's family, or "". Not a No-Intro name: only the cover and border lookup reads it.
std::string ByExtraCrc(System s, uint32_t crc);
// Where an online database has the cover of a game libretro-thumbnails lacks (data/db-art.tsv, fe_dbart.h): addresses,
// tab-separated, found by the ROM's CRC or by the words of its name; "" when no row of its system fits.
std::string DatabaseCoverUrls(const GameInfo& game);
} // namespace gamedb

// The GameInfo of a ROM that is being loaded, named exactly as ScanGames names it (the border picks its picture by it).
// system: the one the loader already knows (a zip's, from the ROM inside), so a zip is not opened again; null: from the
// path (a zip's directory is read).
GameInfo DescribeGame(const std::string& path, const System* system = nullptr);

// The internal header of a Saturn disc (a .cue's first track, a .chd's first hunk); false if
// there is none to find (the other systems' headers have no title).
bool ReadRomHeader(const std::string& path, System system, romheader::Info* info);

// The ROM's CRC32s: of the whole file, and without its header (= whole when it has none). For a .zip, of the ROM
// inside (a .7z too); *inner_ext gets that ROM's extension. False if it can't be read (a disc image: named by file name).
// cancel (when given): stops the read when it turns true (false is returned).
bool RomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, std::string* inner_ext = nullptr,
	const std::atomic<bool>* cancel = nullptr);

// RomCrc32 through the CRC cache the scan keeps (crc-cache.txt): the ROM is read only when the cache has no CRC for
// the file as it is now (and never when read is false). For the cheats' lookup at a game's start.
bool CachedRomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, bool read);

// The folders to look for games in: OrbisRomRoots().
std::vector<std::string> RomRoots();

// ".zip" and ".7z" (2.2): archives that hold a cartridge ROM (any case; ext as ExtOf gives it, with its dot).
bool IsArchiveExt(const std::string& ext);

// Why a ROM could not be taken out of an archive (the loader's message says it).
enum class ArchiveError
{
	None,
	NoGame, // no ROM inside that SSYmirPS5 knows (or a zip that can't be read)
	Disc, // a .7z that holds a disc image (.cue, .chd, .iso): discs are not read from inside a .7z
	Damaged, // a .7z that is broken or cut short, or not a 7z at all
	Unsupported, // a .7z packed with a method the decoder lacks (BZip2, Deflate...)
	Encrypted, // a password-protected .7z
	TooBig, // the ROM is over kMaxArchiveRom, or the .7z needs more memory than allowed
	BlockTooBig, // the .7z's solid block holding the ROM is over kMax7zBlock
	ReadError, // the file could not be read
};
// The largest ROM taken out of an archive (the core takes 10 MiB at most), and the largest solid block of a .7z that
// is decoded to get it (a solid block is decoded whole, in memory).
constexpr uint64_t kMaxArchiveRom = 16ull << 20;
constexpr uint64_t kMax7zBlock = 64ull << 20;

// The system of a .zip or .7z: the extension of the first ROM inside (archive order). False if none; a .7z that holds a
// disc image has none.
bool ArchiveSystem(const std::string& path, System* out);
// The first ROM inside a .zip or .7z (its name in the archive) and its bytes. False if none can be read; *why says why.
// cancel (when given): a .7z's read stops when it turns true.
bool ReadFromArchive(const std::string& path, std::string* inner, std::vector<uint8_t>* data, ArchiveError* why = nullptr,
	const std::atomic<bool>* cancel = nullptr);

// Every ROM under the ROM folders (sub-folders included, 4 levels), sorted by title. Names that need a
// CRC are resolved here too, with a cache (/data/ssymirps5/covers/crc-cache.txt) so each ROM is read once.
// progress (when given) is told how far the scan is: stage 0 while the folders are listed (done = games found
// so far, total = 0), stage 1 while each game is identified (done of total). It is called often: it decides itself
// how often to draw.
using ScanProgress = std::function<void(int stage, size_t done, size_t total)>;
// A cheap fingerprint of the game folders (OrbisRomRoots: the console's and the USB drives'):
// the folders and their sub-folders (4 levels), their modification times and file counts. It changes when a game is
// added, removed or renamed, or a drive is plugged in or out.
uint64_t LocalLibrarySignature();
// "Game (Disc 2)" -> 2 and "Game" (the tags of No-Intro/Redump, TOSEC and others). False: no disc number.
bool DiscTag(const std::string& name, int* number, std::string* base);
// A .m3u's discs as full paths: as each line says (relative to the .m3u, "\" or "/"), else a file of that name in the
// .m3u's folder or its sub-folders. *missing: the lines no file was found for; *changed: a path differs from the line's.
std::vector<std::string> ResolvePlaylist(const std::string& m3u, std::vector<std::string>* missing, bool* changed);
std::vector<GameInfo> ScanGames(const ScanProgress& progress = nullptr);
} // namespace fe
