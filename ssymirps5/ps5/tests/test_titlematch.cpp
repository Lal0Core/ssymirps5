// The cover and border matcher over SSYmirPS5's data (no PS5 SDK needed): fe_titlematch.h, fe_artmatch.h,
// fe_dbart.h, fe_romheader.h and the tables built into the app (frontend/data). Ported from Snes9x PS5 2.7's test (pull
// request #3 by thatboialex); 3.11.10: with the Saturn's lists. Run from ps5/.
// SPDX-License-Identifier: MIT
#include "../frontend/fe_titlematch.h"
#include "../frontend/fe_artmatch.h"
#include "../frontend/fe_dbart.h"
#include "../frontend/fe_romheader.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <map>
#include <set>
#include <string>
#include <vector>

static int g_failed = 0, g_checked = 0;
#define CHECK(cond, what)                                                       \
	do                                                                          \
	{                                                                           \
		g_checked++;                                                            \
		if (!(cond))                                                            \
		{                                                                       \
			g_failed++;                                                         \
			printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, what);               \
		}                                                                       \
	} while (0)

// A "#@ <system>" sectioned list (cover-index.txt, border-index.txt): system -> names.
static std::map<std::string, std::vector<std::string>> Sections(const char* path)
{
	std::map<std::string, std::vector<std::string>> out;
	std::ifstream f(path);
	if (!f.good())
		printf("FAIL cannot read %s (run from ps5/)\n", path), g_failed++;
	std::string line, cur;
	while (std::getline(f, line))
	{
		if (line.compare(0, 3, "#@ ") == 0)
		{
			cur = line.substr(3, line.find(' ', 3) - 3);
			out[cur];
			continue;
		}
		if (!line.empty() && line[0] != '#')
			out[cur].push_back(line);
	}
	return out;
}

// The data lines of a table file.
static std::vector<std::string> Lines(const char* path)
{
	std::ifstream f(path);
	std::vector<std::string> out;
	std::string line;
	while (std::getline(f, line))
		if (!line.empty() && line[0] != '#')
			out.push_back(line);
	return out;
}

static bool Has(const std::vector<std::string>& list, const std::string& name)
{
	return std::find(list.begin(), list.end(), name) != list.end();
}

int main()
{
	namespace am = fe::artmatch;
	using fe::titles::Best;
	auto covers = Sections("frontend/data/cover-index.txt");
	auto borders = Sections("frontend/data/border-index.txt");
	CHECK(covers.size() == 1 && covers.count("ss") && covers["ss"].size() > 2000, "cover-index.txt: the Saturn's section, libretro-thumbnails' 2294 names");
	CHECK(borders.size() == 1 && borders.count("ss") && borders["ss"].size() > 1900, "border-index.txt: the Saturn's section, The Bezel Project's 1997");
	const std::vector<std::string>& ss = covers["ss"];
	const std::vector<std::string>& bss = borders["ss"];

	// ---- spelling (fe_titlematch.h) ----
	CHECK(Best(ss, "guardian heroes.cue").find("Guardian Heroes (") == 0, "a disc named loosely finds its game");
	CHECK(Best(ss, "Daytona-USA.chd").find("Daytona USA (") == 0, "hyphens and the extension do not matter");
	CHECK(Best(ss, "Guardian Heroes (USA)") == "Guardian Heroes (USA)", "an exact name is itself");
	CHECK(Best(ss, "Completely Different Game (USA)").empty(), "a game that is not there finds nothing");
	CHECK(am::NumbersConflict("Langrisser III", "Langrisser II (Japan)") && !am::NumbersConflict("Langrisser III", "Langrisser III (Japan) (1M)"),
		"a different number is another game");
	// the index (first four characters) gives the plain scan's answers, for every name of every list and its variants
	{
		size_t checked = 0;
		bool same = true;
		for (const auto* lists : {&covers, &borders})
			for (const auto& sec : *lists)
			{
				const fe::titles::Index& ix = fe::titles::IndexOf(sec.second);
				for (const std::string& n : sec.second)
				{
					std::string typo = n;
					if (typo.size() > 6)
						typo[typo.size() / 2] = 'x';
					for (const std::string& q : {n, n.substr(0, n.find(" (")), typo})
					{
						same = same && Best(ix, q) == Best(sec.second, q);
						checked++;
					}
				}
			}
		CHECK(same, "index lookups equal the plain scan");
		std::cout << "Index lookups equal to the plain scan: " << checked << "\n";
	}

	// ---- renamed and fan-translated discs (fe_artmatch.h) ----
	{
		const am::Index& ix = am::IndexOf(ss);
		CHECK(am::Parse("Langrisser III (English).cue").title == "Langrisser III" && am::Parse("Langrisser III (English).cue").translated,
			"a translation's name: the title, marked translated");
		CHECK(am::Parse("Panzer Dragoon Saga (ENG) # Saturn").title == "Panzer Dragoon Saga", "a frontend label goes");
		CHECK(am::Parse("Langrisser_III_English_Patched_v2.0").title == "Langrisser III", "underscores, language and version words go");
		CHECK(am::Parse("Langrisser III Rev 01 (Japan)[tr en Team]").title == "Langrisser III",
			"a revision written out (TOSEC's \"Rev 01\") is not part of the title");
		CHECK(am::Parse("Guardian Heroes (U) [!]").region == "USA" && am::Parse("Dragon Force (J)").region == "Japan", "GoodTools region codes");
		CHECK(am::Resolve(ix, "Langrisser III [T-Eng]").find("Langrisser III (Japan)") == 0, "a fan translation finds the Japanese game's art");
		CHECK(am::Resolve(ix, "Langrisser III (Japan)[tr en Langrisser Reincarnation]").find("Langrisser III (Japan)") == 0, "so does its TOSEC name");
		CHECK(am::Resolve(ix, "Dragon Force (English)") == "Dragon Force (USA)", "no region: the USA release");
		CHECK(am::Resolve(ix, "Albert Odyssey (English)") == "Albert Odyssey - Legend of Eldean (USA)", "a title without its subtitle");
		CHECK(am::Resolve(ix, "Completely Different Game (English)").empty(), "a game that is not in the list: nothing");
		CHECK(am::Resolve(ix, "Dragon (English)").empty(), "a single common word: nothing");
		CHECK(am::Resolve(ix, "Dragon Force 2 (English)") == "Dragon Force II - Kami Sarishi Daichi ni (Japan)", "a number is its roman numeral");
		CHECK(am::TitleKey(am::Resolve(ix, "Langrisser 3 (English)")) == am::TitleKey("Langrisser III"), "and the other way round");
		// every name of each list, written the ways a translator or a library tool writes it, finds its own game (and never another)
		size_t variants = 0;
		bool own = true;
		for (const auto* lists : {&covers, &borders})
			for (const auto& sec : *lists)
			{
				const am::Index& lx = am::IndexOf(sec.second);
				for (const std::string& name : sec.second)
				{
					const std::string base = am::Parse(name).title;
					std::string underscored = base;
					std::replace(underscored.begin(), underscored.end(), ' ', '_');
					for (const std::string& variant : {base + " (English)", am::Lower(base) + " [T+Eng v1.1]", underscored + "_English", name + ".cue"})
					{
						++variants;
						const std::string found = am::Resolve(lx, variant);
						if (am::TitleKey(found) != am::TitleKey(name))
						{
							own = false;
							std::cout << sec.first << ": Resolve(\"" << variant << "\") = \"" << found << "\"\n";
						}
					}
				}
			}
		CHECK(own, "every name, renamed four ways, finds its own game");
		std::cout << "Word matcher: " << variants << " renamed variants\n";
	}

	// ---- the extra CRC table (data/extra-crc.tsv): dumps the name table lacks; the Saturn's has none yet ----
	{
		bool shape = true;
		for (const std::string& line : Lines("frontend/data/extra-crc.tsv"))
		{
			const size_t t1 = line.find('\t');
			shape = shape && t1 != std::string::npos && line.size() > t1 + 10 && line[t1 + 9] == '\t' && line.substr(0, t1) == "ss";
		}
		CHECK(shape, "extra-crc.tsv: every line (if any) is a Saturn dump: system, CRC32, name");
	}

	// ---- the border index (tools/make_border_index.py) ----
	{
		CHECK(std::set<std::string>(bss.begin(), bss.end()).size() == bss.size(), "no name twice");
		CHECK(Has(bss, "Nights into Dreams... (Europe)") && Has(bss, "Arcade Gears Vol. 4 - ImageFight & XMultiply (Japan)"),
			"names as the repository spells them");
		std::ifstream f("frontend/data/border-index.txt");
		std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		CHECK(text.find("#@ ss bezelproject-Saturn/retroarch/overlay/GameBezels/Saturn/\n") != std::string::npos,
			"the section names its repository and folder");
		// a retranslation that fits a game's USA and Japanese entries alike: the cover matcher declines, the border matcher
		// (usa_on_tie) takes the USA one; a name that is not a tie is unchanged
		const am::Index& ix = am::IndexOf(bss);
		CHECK(am::Resolve(ix, "3D Baseball - Retranslation").empty(), "a toss-up between two games: no cover");
		CHECK(am::Resolve(ix, "3D Baseball - Retranslation", "", true) == "3D Baseball (USA)", "the border takes the USA one");
		for (const char* name : {"Langrisser III (English)", "Dragon Force (English)", "Guardian Heroes (Europe)", "Daytona USA (English)"})
			CHECK(am::Resolve(ix, name, "", true) == am::Resolve(ix, name), "the tie-break changes nothing else");
		CHECK(am::Resolve(ix, "Guardian Heroes (Japan)").find("Guardian Heroes (Japan)") == 0 &&
				am::Resolve(ix, "Guardian Heroes (Europe)") == "Guardian Heroes (Europe)",
			"a game takes its own region's border");
		// most releases of the name table (not betas, prototypes, demos) have a border by name
		size_t releases = 0, bordered = 0;
		const std::set<std::string> bset(bss.begin(), bss.end());
		for (const std::string& line : Lines("frontend/data/nointro.tsv"))
		{
			const size_t t1 = line.find('\t'), t2 = line.find('\t', t1 + 1);
			const std::string name = line.substr(t2 + 1);
			bool odd = false;
			for (const char* tag : {"(Beta", "(Proto", "(Sample", "(Unl", "(Demo", "(Program", "[b]"})
				odd = odd || name.find(tag) != std::string::npos;
			if (odd)
				continue;
			++releases;
			bordered += bset.count(name);
		}
		std::cout << "Border index: " << bordered << " of " << releases << " releases have a border by name\n";
		CHECK(releases > 1900 && bordered * 100 >= releases * 80, "four in five releases have a border by name (the matcher finds more)");
	}

	// ---- the database covers (fe_dbart.h): the rows of a system ----
	{
		namespace da = fe::dbart;
		const std::string fixture = "# test\n"
									"ss\tSome Homebrew\thAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA t123\tDEADBEEF,0BADF00D\n"
									"zz\tSome Homebrew\thBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB\tDEADBEEF\n"
									"ss\tKing of Fighters 95\tt27252\t12345678\n"
									"ss\tbroken line\n"
									"ss\tNo Sources\t\t11111111\n";
		da::Table t, other;
		da::Parse(fixture.data(), fixture.data() + fixture.size(), "ss", t);
		da::Parse(fixture.data(), fixture.data() + fixture.size(), "zz", other);
		CHECK(t.rows.size() == 2 && other.rows.size() == 1, "each system reads its own rows; malformed rows and rows without a source are skipped");
		const da::Row* hb = da::Find(t, 0x0BADF00D, "whatever");
		CHECK(hb && hb->name == "Some Homebrew" && da::Urls(*hb, "https://h/", "https://t/") == "https://h/AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA\thttps://t/123-1.jpg",
			"found by CRC, with the Hasheous image first and TheGamesDB's box art after it");
		CHECK(da::Find(other, 0xDEADBEEF, "") && da::Urls(*da::Find(other, 0xDEADBEEF, ""), "https://h/", "https://t/") == "https://h/BBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBBB",
			"the same CRC on another system is that system's row");
		CHECK(da::Find(t, 0, "King of Fighters 95 [T-Eng]") && !da::Find(t, 0, "King of Fighters 96"), "by the words of the name, and a different number never");
		CHECK(!da::Find(t, 0xCAFEBABE, "Completely Different Game"), "nothing for a game it does not list");
		std::ifstream f("frontend/data/db-art.tsv", std::ios::binary);
		const std::string text((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
		da::Table table;
		da::Parse(text.data(), text.data() + text.size(), "ss", table);
		std::cout << "Database covers: " << table.rows.size() << " games in db-art.tsv\n";
	}

	// ---- the Saturn disc's header (fe_romheader.h) ----
	{
		namespace rh = fe::romheader;
		// the system area: "SEGA SEGASATURN ", the product number at $20, the date at $30, "CD-1/2" at $38, the areas at
		// $40, the title at $60
		auto header = [](const char* product, const char* date, const char* disc, const char* areas, const char* title) {
			std::vector<uint8_t> h(0x800, ' ');
			memcpy(&h[0x00], "SEGA SEGASATURN ", 16);
			memcpy(&h[0x20], product, strlen(product));
			memcpy(&h[0x30], date, strlen(date));
			memcpy(&h[0x38], disc, strlen(disc));
			memcpy(&h[0x40], areas, strlen(areas));
			memcpy(&h[0x60], title, strlen(title));
			return h;
		};
		auto reader = [](const std::vector<uint8_t>& img) {
			return [&img](uint64_t at, uint8_t* out, size_t n) {
				if (at + n > img.size())
					return false;
				memcpy(out, img.data() + at, n);
				return true;
			};
		};
		rh::Info info;
		const std::vector<uint8_t> iso = header("MK-81009", "19950310", "CD-1/1", "JTUE", "PANZER DRAGOON");
		CHECK(rh::ReadDisc(reader(iso), &info) && info.title == "PANZER DRAGOON" && info.other == "MK-81009" && info.date == "19950310" &&
				info.areas == "JTUE" && !info.japan() && info.disc == 1 && info.discs == 1,
			"an .iso: its title, product number, date, areas and disc");
		std::vector<uint8_t> raw(16, 0);
		raw.insert(raw.end(), iso.begin(), iso.end());
		CHECK(rh::ReadDisc(reader(raw), &info) && info.title == "PANZER DRAGOON", "a raw 2352-byte track: 16 bytes in");
		const std::vector<uint8_t> jp = header("T-4507G", "19961122", "CD-2/2", "J", "GRANDIA");
		CHECK(rh::ReadDisc(reader(jp), &info) && info.japan() && info.disc == 2 && info.discs == 2, "a Japanese disc 2 of 2");
		const std::vector<uint8_t> none(0x800, 0x41);
		CHECK(!rh::ReadDisc(reader(none), &info), "no SEGA SEGASATURN: no header");
		CHECK(!rh::ReadCart(reader(iso), iso.size(), &info), "the Saturn has no cartridges");
		// the header's title finds the art (ResolveHeader)
		const am::Index& ix = am::IndexOf(ss);
		CHECK(am::ResolveHeader(ix, "GUARDIAN HEROES", true) == "Guardian Heroes (Japan)" &&
				am::ResolveHeader(ix, "GUARDIAN HEROES", false) == "Guardian Heroes (USA)",
			"a header finds its region's game");
		CHECK(am::ResolveHeader(ix, "LANGRISSER 3", true).find("Langrisser III (Japan)") == 0, "a header's digits are its numerals");
		CHECK(am::ResolveHeader(ix, "SEGA SATURN", false).empty(), "a header that names only the console finds nothing");
	}

	printf("%d checks, %d failed\n", g_checked, g_failed);
	return g_failed ? 1 : 0;
}
