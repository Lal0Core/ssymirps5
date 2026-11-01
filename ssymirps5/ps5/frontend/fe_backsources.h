// SSYmirPS5 frontend: which game a Saturn disc is, and where its back cover is online (for fe_caseview.cpp).
//
// The disc's product number names the game for sure: data/saturn-serials.tsv (tools/make_saturn_serials.py, from
// niemasd/GameDB-Saturn: Redump, GameFAQs, Sega Retro) gives the game of 2426 product numbers. That title -- not the
// file's name -- is what the back cover is looked up by, so a renamed or badly named file still gets its own game's
// back. A disc whose number the table lacks (or a .chd, whose header can't be read) falls back to its file's name.
//
// Two places have back covers:
//   * LaunchBox Games DB (gamesdb.launchbox-app.com): its public database (Metadata.zip) lists a "Box - Back" picture,
//     by region, for many Saturn games. It is downloaded once, the first time a case is opened, and only the Saturn
//     back covers are kept (covers/Saturn/launchbox-backs.tsv: name, region, picture); the picture itself comes from
//     images.launchbox-app.com when that game's case is opened.
//   * Chromagi (chromagi.com): a small collection, its back covers named <page>.boxback.avif. A page is found by the
//     disc's product number among the numbers Chromagi lists for it (data/chromagi-saturn.tsv: "FVIR16SSC" is
//     "MK81014-50"), else by the game's title -- never by a number alone that could be another game's.
// Titles are compared as the covers are (fe_artmatch.h: the same words, every number the same; a doubtful match is
// no match), the disc's region first.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace fe
{
namespace backs
{
// What the case view knows of the disc.
struct Disc
{
	std::string product; // the header's product number ("MK-81014"), "" if none
	std::string areas; // the header's area codes ("JTUE")
	std::vector<std::string> names; // other names of the game, best first (the No-Intro / Redump name, the file's, the header's)
};

// The game the product number is, from the built-in table: "Virtua Fighter 2" and "USA"; false if it isn't there.
bool SerialTitle(const std::string& product, std::string* title, std::string* region);

// The region to prefer for the disc's back cover: "Japan", "USA", "Europe" or "".
std::string PreferredRegion(const Disc& d);

// The names to look the disc up by, best first: the table's title of its product number (only it, when there is one:
// the number is sure, a file name is not), else its other names.
std::vector<std::string> LookupNames(const Disc& d);

struct Candidate
{
	std::string url;
	std::string label; // "LaunchBox: Virtua Fighter 2 (Europe)"
};

// Chromagi's back cover addresses for the disc: the page whose numbers include the disc's, else the page of the same
// game (by title, the disc's region first).
std::vector<Candidate> ChromagiCandidates(const Disc& d);

// LaunchBox's index of Saturn box art (front, back, spine), made from its database (`metadata_zip`: a Metadata.zip
// file). Returns how many pictures it has, -1 when the file is not a LaunchBox database.
constexpr const char* kLaunchBoxFormat = "# SSYmirPS5 LaunchBox index, format 3"; // 3.10: with the discs
int BuildLaunchBoxIndex(const std::string& metadata_zip, const std::string& index_path);
// An index of this format (2.3's had only back covers: read again).
bool LaunchBoxIndexValid(const std::string& index_path);
// The LaunchBox pictures of the disc's game (kind: "front", "back", "spine" or "disc"), its region first.
std::vector<Candidate> LaunchBoxCandidates(const Disc& d, const std::string& index_path, const std::string& image_base,
	const std::string& kind);

// For the tests: the index made from a Metadata.xml's text, fed in pieces.
class LaunchBoxParser
{
public:
	void Feed(const char* data, size_t n);
	// The Saturn back covers found: "name<TAB>region<TAB>file" lines.
	std::string Finish();
	size_t games() const { return m_saturn.size(); }

private:
	void Block(const std::string& tag, const std::string& body);
	struct Image
	{
		int id;
		std::string file, region;
		const char* kind; // "front", "back", "spine"
	};
	struct Alt
	{
		int id;
		std::string name, region;
	};
	std::string m_buf;
	std::vector<std::pair<int, std::string>> m_saturn; // database id, name
	std::vector<Image> m_images; // every "Box - Back" (the games may come after them)
	std::vector<Alt> m_alts;
};
} // namespace backs
} // namespace fe
