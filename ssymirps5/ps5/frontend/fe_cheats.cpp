// SSYmirPS5 frontend: the cheats (fe_cheats.h).
//
// SPDX-License-Identifier: MIT
#include "fe_cheats.h"

#include "fe_artmatch.h"
#include "fe_backsources.h"
#include "fe_games.h"
#include "fe_romheader.h"
#include "fe_tlshttp.h"

#include "OrbisPaths.h"
#include "ProsperoThread.h"
#include "libretro.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <mutex>

#define YMIR_INCBIN(sym, path)                                                                                \
	__asm__(".section .rodata\n"                                                                               \
			".balign 16\n"                                                                                     \
			".global " #sym "_begin\n" #sym "_begin:\n"                                                        \
			".incbin \"" path "\"\n"                                                                           \
			".global " #sym "_end\n" #sym "_end:\n"                                                            \
			".previous\n");                                                                                    \
	extern "C" const char sym##_begin[];                                                                       \
	extern "C" const char sym##_end[];

YMIR_INCBIN(ymir_saturn_cheats, SATURN_CHEATS_TXT)

namespace fe
{
namespace cheats
{
namespace
{
const char* const kAlmarUrl = "https://gist.githubusercontent.com/t3chnaztea/0f2ea9755ee331d2ff74227190a107d4/raw/"
	"03170f5d1084c706b7e16328e4f64118417910aa/sega-saturn-action-replay-codes.md";
const char* const kBaseUrl = "https://raw.githubusercontent.com/libretro/libretro-database/master/cht/Sega%20-%20Saturn/";

std::mutex g_mx;
State g_state = State::Idle;
std::vector<Cheat> g_list;
std::string g_file; // the .cht in use
std::string g_text; // its text as read (written back with the "on" flags changed)
std::atomic<bool> g_dirty{false}; // the core has not been given the list as it is now
std::atomic<int> g_generation{0}; // a newer Begin / End: an older lookup's result is thrown away
ps5::BigThread* g_thread = nullptr;

const std::vector<std::string>& Names()
{
	static const std::vector<std::string> names = [] {
		std::vector<std::string> v;
		std::string line;
		for (const char* p = ymir_saturn_cheats_begin; p < ymir_saturn_cheats_end; p++)
		{
			if (*p == '\n' || *p == '\r')
			{
				if (!line.empty())
					v.push_back(line);
				line.clear();
			}
			else
				line += *p;
		}
		if (!line.empty())
			v.push_back(line);
		return v;
	}();
	return names;
}

std::string Unquote(std::string v)
{
	while (!v.empty() && (v.back() == ' ' || v.back() == '\t' || v.back() == '\r'))
		v.pop_back();
	size_t i = 0;
	while (i < v.size() && (v[i] == ' ' || v[i] == '\t'))
		i++;
	v = v.substr(i);
	if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
		v = v.substr(1, v.size() - 2);
	return v;
}

bool ReadFile(const std::string& path, std::string& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.clear();
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && out.size() < (4u << 20))
		out.append(buf, n);
	fclose(f);
	return true;
}

bool WriteFile(const std::string& path, const std::string& text)
{
	const std::string tmp = path + ".tmp";
	FILE* f = fopen(tmp.c_str(), "wb");
	if (!f)
		return false;
	const bool ok = fwrite(text.data(), 1, text.size(), f) == text.size();
	if (fclose(f) != 0 || !ok)
	{
		remove(tmp.c_str());
		return false;
	}
	return rename(tmp.c_str(), path.c_str()) == 0;
}

std::string BaseName(const std::string& path)
{
	std::string f = path.substr(path.find_last_of('/') + 1);
	const size_t dot = f.find_last_of('.');
	return dot == std::string::npos ? f : f.substr(0, dot);
}

// libretro's file names keep characters a FAT/exFAT USB drive refuses (':' in "Street Fighter Alpha 2: ...")
std::string SafeFileName(std::string n)
{
	for (char& c : n)
		if (c == ':' || c == '/' || c == '\\' || c == '*' || c == '?' || c == '"' || c == '<' || c == '>' || c == '|')
			c = '-';
	return n;
}

void Load(const std::string& path, const std::string& text, int gen)
{
	std::vector<Cheat> list;
	Parse(text, list);
	std::lock_guard<std::mutex> l(g_mx);
	if (gen != g_generation)
		return;
	g_file = path;
	g_text = text;
	g_list = list;
	g_state = list.empty() ? State::None : State::Ready;
	g_dirty = true;
	OrbisLog("[cheats] %zu cheats from %s", list.size(), path.c_str());
}

// The libretro file of a game, downloaded once (kept as cheats/Saturn/<its name>.cht). "" when none or offline;
// *path gets where it is kept, *offline whether it could not be had (no network).
std::string LibretroText(const std::string& name, std::string* path, bool* offline)
{
	// 3.9.4: kept in cheats/Saturn/libretro (cheats/Saturn itself is for your own files)
	const std::string sat = OrbisDir("cheats") + "/Saturn/libretro";
	*path = sat + "/" + SafeFileName(name) + ".cht";
	std::string text;
	if (ReadFile(*path, text))
		return text;
	std::vector<uint8_t> body;
	int status;
	{
		TlsHttp http;
		status = http.Get(UrlFor(name), body, 1u << 20);
	}
	OrbisLog("[cheats] GET %s -> %d, %zu bytes", UrlFor(name).c_str(), status, body.size());
	if (status == 200 && !body.empty())
	{
		text.assign(body.begin(), body.end());
		std::vector<Cheat> probe;
		if (Parse(text, probe) && OrbisMkdirs(sat) && WriteFile(*path, text))
			return text;
	}
	*offline = true;
	return "";
}

// 3.9.2: Almar's Guides' Saturn Action Replay codes (388 pages, Japanese games too), one file for all the games:
// downloaded once to cheats/Saturn/almar-codes.md, read once per run.
std::mutex g_almar_mx;
bool g_almar_read = false;
std::vector<DbGame> g_almar;

const std::vector<DbGame>* Almar(bool* offline)
{
	std::lock_guard<std::mutex> l(g_almar_mx);
	if (g_almar_read)
		return &g_almar;
	const std::string sat = OrbisDir("cheats") + "/Saturn";
	const std::string path = sat + "/almar-codes.md";
	std::string text;
	if (!ReadFile(path, text))
	{
		std::vector<uint8_t> body;
		int status;
		const std::string url = getenv("YMIR_ALMAR_URL") ? getenv("YMIR_ALMAR_URL") : kAlmarUrl;
		{
			TlsHttp http;
			status = http.Get(url, body, 8u << 20);
		}
		OrbisLog("[cheats] GET %s -> %d, %zu bytes", url.c_str(), status, body.size());
		if (status != 200 || body.size() < 1000)
		{
			*offline = true;
			return nullptr; // tried again at the next game
		}
		text.assign(body.begin(), body.end());
		if (OrbisMkdirs(sat))
			WriteFile(path, text);
	}
	g_almar = ParseAlmar(text);
	g_almar_read = true;
	OrbisLog("[cheats] Almar's Guides: %zu games", g_almar.size());
	return &g_almar;
}

std::string HexOf(const std::string& code)
{
	std::string h;
	for (char c : code)
		if (std::isxdigit(static_cast<unsigned char>(c)))
			h += char(std::toupper(static_cast<unsigned char>(c)));
	return h;
}

std::string Quote(const std::string& v)
{
	std::string o;
	for (char c : v)
		o += c == '"' ? '\'' : c;
	return "\"" + o + "\"";
}

// 3.9.4: the .cht files you copied (cheats/Saturn and cheats, not their sub-folders): name -> path. GameHacking.org's
// exports are named by the game's title with its own region words ("(Jpn)"), so those are read as the usual ones.
std::vector<std::pair<std::string, std::string>> DroppedFiles()
{
	std::vector<std::pair<std::string, std::string>> out;
	for (const std::string& d : {OrbisDir("cheats") + "/Saturn", OrbisDir("cheats")})
	{
		DIR* dir = opendir(d.c_str());
		if (!dir)
			continue;
		while (dirent* e = readdir(dir))
		{
			const std::string n = e->d_name;
			if (n.size() < 5 || strcasecmp(n.c_str() + n.size() - 4, ".cht") != 0 || n[0] == '.')
				continue;
			const std::string path = d + "/" + n;
			struct stat st;
			if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
				continue;
			std::string name = n.substr(0, n.size() - 4);
			for (const auto& r : {std::make_pair("(Jpn)", "(Japan)"), std::make_pair("(Euro)", "(Europe)"), std::make_pair("(Eur)", "(Europe)"),
					 std::make_pair("(Jap)", "(Japan)"), std::make_pair("(US)", "(USA)"), std::make_pair("(EU)", "(Europe)"),
					 std::make_pair("(JP)", "(Japan)"), std::make_pair("(Kor)", "(Korea)")})
			{
				const size_t at = name.find(r.first);
				if (at != std::string::npos)
					name.replace(at, strlen(r.first), r.second);
			}
			out.push_back({name, path});
		}
		closedir(dir);
	}
	return out;
}

void Merge(std::vector<Cheat>& list, std::vector<std::string>& have, const std::string& desc, const std::string& code, bool on)
{
	const std::string h = HexOf(code);
	if (h.empty())
		return;
	for (const std::string& x : have)
		if (x == h)
			return;
	have.push_back(h);
	Cheat c;
	c.desc = desc;
	c.code = code;
	c.on = on;
	list.push_back(c);
}

void Lookup(std::string game_path, int gen)
{
	const std::string sat = OrbisDir("cheats") + "/Saturn";
	const std::string base = BaseName(game_path);
	std::string product;
	{
		romheader::Info info;
		System sys = System::Saturn;
		if (ReadRomHeader(game_path, sys, &info))
			product = info.other;
	}
	bool offline = false;
	// 1. libretro's file
	const std::string name = MatchName(base, product);
	std::string lr_path, lr_text;
	if (!name.empty())
		lr_text = LibretroText(name, &lr_path, &offline);
	// 2. Almar's Guides
	const DbGame* game = nullptr;
	if (const std::vector<DbGame>* db = Almar(&offline))
	{
		const int i = MatchDbGame(*db, base, product);
		if (i >= 0)
			game = &(*db)[size_t(i)];
	}
	// 3. your own files (GameHacking.org's exports...): the one named as the game's file, else the one its title is
	std::string mine_path;
	{
		const auto files = DroppedFiles();
		auto alnum = [](const std::string& v) {
			std::string k;
			for (char c : v)
				if (std::isalnum(static_cast<unsigned char>(c)))
					k += char(std::toupper(static_cast<unsigned char>(c)));
			return k;
		};
		for (const auto& f : files)
			if (BaseName(f.second) == base)
				mine_path = f.second;
		// named by the disc's product number ("MK81029-50.cht" for MK-81029-50)
		if (mine_path.empty() && alnum(product).size() >= 5)
			for (const auto& f : files)
				if (alnum(BaseName(f.second)) == alnum(product))
					mine_path = f.second;
		if (mine_path.empty() && !files.empty())
		{
			std::vector<DbGame> names;
			for (const auto& f : files)
				names.push_back({f.first, {}});
			const int i = MatchDbGame(names, base, product, true);
			if (i >= 0)
				mine_path = files[size_t(i)].second;
		}
	}
	OrbisLog("[cheats] %s (product %s): libretro %s, Almar's Guides %s, yours %s", base.c_str(),
		product.empty() ? "?" : product.c_str(), name.empty() ? "-" : name.c_str(), game ? game->name.c_str() : "-",
		mine_path.empty() ? "-" : mine_path.c_str());
	// one list: libretro's, Almar's, yours, the codes already there not twice
	std::vector<Cheat> list;
	std::vector<std::string> have;
	std::vector<Cheat> part;
	if (!lr_text.empty() && Parse(lr_text, part))
		for (const Cheat& c : part)
			Merge(list, have, c.desc, c.code, c.on);
	if (game)
		for (const auto& dc : game->cheats)
			Merge(list, have, dc.first, dc.second, false);
	std::string text;
	if (!mine_path.empty() && ReadFile(mine_path, text) && Parse(text, part))
		for (const Cheat& c : part)
			Merge(list, have, c.desc, c.code, c.on);
	if (list.empty())
	{
		std::lock_guard<std::mutex> l(g_mx);
		if (gen == g_generation)
			g_state = offline ? State::Failed : State::None;
		return;
	}
	// which ones were on: kept in cheats/Saturn/games/<the game's file name>.cht (made again at every start, so a
	// file you add is in it the next time)
	const std::string kept = sat + "/games/" + SafeFileName(base) + ".cht";
	{
		std::string old;
		std::vector<Cheat> before;
		if (ReadFile(kept, old) && Parse(old, before))
			for (Cheat& c : list)
				for (const Cheat& b : before)
					if (HexOf(b.code) == HexOf(c.code))
						c.on = b.on;
	}
	std::string out = "# SSYmirPS5: the cheats of " + base + " (made at each start: libretro-database " +
		(name.empty() ? "-" : name) + ", Almar's Guides " + (game ? game->name : "-") + ", " +
		(mine_path.empty() ? "-" : mine_path.substr(mine_path.find_last_of('/') + 1)) + ")\ncheats = " + std::to_string(list.size()) + "\n";
	for (size_t i = 0; i < list.size(); i++)
	{
		const std::string k = "cheat" + std::to_string(i);
		out += "\n" + k + "_desc = " + Quote(list[i].desc) + "\n" + k + "_code = " + Quote(list[i].code) + "\n" + k +
			"_enable = " + (list[i].on ? "true" : "false") + "\n";
	}
	OrbisMkdirs(sat + "/games");
	if (!WriteFile(kept, out))
		OrbisLog("[cheats] could not write %s", kept.c_str());
	Load(kept, out, gen);
}

void JoinThread()
{
	if (g_thread)
	{
		g_thread->join();
		delete g_thread;
		g_thread = nullptr;
	}
}
} // namespace

bool Usable(const std::string& code)
{
	if (code.empty() || code.find('?') != std::string::npos)
		return false;
	// groups of hex digits: 12 at a time, or 8 then 4
	std::vector<size_t> runs;
	size_t run = 0;
	for (char c : code)
	{
		if (std::isxdigit(static_cast<unsigned char>(c)))
			run++;
		else if (run)
		{
			runs.push_back(run);
			run = 0;
		}
	}
	if (run)
		runs.push_back(run);
	if (runs.empty())
		return false;
	for (size_t i = 0; i < runs.size(); i++)
	{
		if (runs[i] % 12 == 0)
			continue;
		if (runs[i] == 8 && i + 1 < runs.size() && runs[i + 1] == 4)
		{
			i++;
			continue;
		}
		return false;
	}
	return true;
}

bool Parse(const std::string& text, std::vector<Cheat>& out)
{
	out.clear();
	int count = -1;
	std::vector<Cheat> by_index;
	size_t pos = 0;
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = text.substr(pos, end - pos);
		pos = end + 1;
		const size_t eq = line.find('=');
		if (eq == std::string::npos)
			continue;
		const std::string key = Unquote(line.substr(0, eq));
		const std::string value = Unquote(line.substr(eq + 1));
		if (key == "cheats")
		{
			count = atoi(value.c_str());
			if (count < 0 || count > 4096)
				return false;
			continue;
		}
		if (key.compare(0, 5, "cheat") != 0)
			continue;
		const char* p = key.c_str() + 5;
		char* after = nullptr;
		const long i = strtol(p, &after, 10);
		if (after == p || i < 0 || i >= 4096 || *after != '_')
			continue;
		if (by_index.size() <= size_t(i))
			by_index.resize(size_t(i) + 1);
		Cheat& c = by_index[size_t(i)];
		const std::string field = after + 1;
		if (field == "desc")
			c.desc = value;
		else if (field == "code")
			c.code = value;
		else if (field == "enable")
			c.on = value == "true" || value == "1";
		else if (field == "address")
			c.retroarch = true;
	}
	if (count >= 0 && by_index.size() > size_t(count))
		by_index.resize(size_t(count));
	for (Cheat& c : by_index)
	{
		{
			// only master codes: F6000914 / B6002800 (with their values), nothing else
			std::string hex;
			for (char ch : c.code)
				if (std::isxdigit(static_cast<unsigned char>(ch)))
					hex += char(std::toupper(static_cast<unsigned char>(ch)));
			bool master = !hex.empty() && hex.size() % 12 == 0;
			for (size_t i = 0; master && i < hex.size(); i += 12)
				master = hex[i] == 'F' || hex[i] == 'B'; // F6000914 C305, F60B6C12 C305, B6002800 0000...
			c.hidden = master;
		}
		c.usable = Usable(c.code);
		if (!c.usable)
			c.on = false;
		if (c.desc.empty())
			c.desc = c.code;
		out.push_back(c);
	}
	return count >= 0 || !out.empty();
}

std::string Write(const std::string& original, const std::vector<Cheat>& list)
{
	// the original's lines, with each cheatN_enable set as the list says (added after its code when missing)
	std::vector<bool> written(list.size(), false);
	std::string out;
	size_t pos = 0;
	while (pos < original.size())
	{
		size_t end = original.find('\n', pos);
		const bool nl = end != std::string::npos;
		if (!nl)
			end = original.size();
		std::string line = original.substr(pos, end - pos);
		pos = end + 1;
		const size_t eq = line.find('=');
		if (eq != std::string::npos)
		{
			const std::string key = Unquote(line.substr(0, eq));
			int i = -1;
			char field[16] = {};
			if (sscanf(key.c_str(), "cheat%d_%15s", &i, field) == 2 && i >= 0 && size_t(i) < list.size())
			{
				if (!strcmp(field, "enable"))
				{
					line = key + " = " + (list[size_t(i)].on ? "true" : "false");
					written[size_t(i)] = true;
				}
			}
		}
		out += line;
		if (nl)
			out += '\n';
	}
	for (size_t i = 0; i < list.size(); i++)
		if (!written[i] && list[i].on)
			out += "cheat" + std::to_string(i) + "_enable = true\n";
	return out;
}

// The name of `ix` a game is: by its file name; else by its title without the subtitle, when a name of the list is
// that title alone ("Albert Odyssey - Legend of Eldean (USA)" is "Albert Odyssey (USA)"); else by the title its
// product number names. "" when none.
// loose (3.9.4, for your own files only): at the end, the one name of the list whose main title (before the subtitle)
// and region are the game's ("Metal Slug - Kakuchou Ram... (Jpn)" for "Metal Slug - Super Vehicle-001 (Japan)")
std::string ResolveGame(const artmatch::Index& ix, const std::string& file_name, const std::string& product, bool loose = false,
	const std::vector<std::string>* names = nullptr)
{
	std::string hit = artmatch::Resolve(ix, file_name, "", true);
	auto by_main = [&](const std::string& name) -> std::string {
		const artmatch::Parsed p = artmatch::Parse(name);
		const std::string main = artmatch::MainOf(p.title);
		if (main.size() >= p.title.size() || main.size() < 3)
			return "";
		std::string m = main;
		while (!m.empty() && m.back() == ' ')
			m.pop_back();
		const std::string h = artmatch::Resolve(ix, p.region.empty() ? m : m + " (" + p.region + ")", p.region, true);
		// only a name that is that title alone (not "Dragon Ball Z" for "Dragon Ball Z - Super Butouden 2")
		if (h.empty() || artmatch::Lower(artmatch::Parse(h).title) != artmatch::Lower(m))
			return "";
		return h;
	};
	if (hit.empty())
		hit = by_main(file_name);
	if (hit.empty() && !product.empty())
	{
		std::string title, region;
		if (backs::SerialTitle(product, &title, &region))
		{
			const std::string q = region.empty() ? title : title + " (" + region + ")";
			hit = artmatch::Resolve(ix, q, region, true);
			if (hit.empty())
				hit = by_main(q);
		}
	}
	if (hit.empty() && loose && names)
	{
		auto key = [](const std::string& n, std::string* region) {
			const artmatch::Parsed p = artmatch::Parse(n);
			*region = p.region;
			std::string k;
			for (char c : artmatch::Lower(artmatch::MainOf(p.title)))
				if (std::isalnum(static_cast<unsigned char>(c)))
					k += c;
			return k;
		};
		std::string region;
		const std::string want = key(file_name, &region);
		int found = 0;
		std::string one;
		for (const std::string& n : *names)
		{
			std::string r;
			if (want.size() >= 3 && key(n, &r) == want && (r.empty() || region.empty() || r == region))
			{
				found++;
				one = n;
			}
		}
		if (found == 1)
			hit = one;
	}
	return hit;
}

std::string MatchName(const std::string& file_name, const std::string& product)
{
	const std::vector<std::string>& names = Names();
	static const artmatch::Index ix = [&] {
		artmatch::Index i;
		artmatch::Build(i, names);
		return i;
	}();
	std::string hit = ResolveGame(ix, file_name, product);
	// 3.9.1: libretro has some games twice: the Action Replay file and a "(RetroArch)" one in RetroArch's own format
	// (which this does not read). The plain one when it is there.
	const size_t ra = hit.find(" (RetroArch)");
	if (ra != std::string::npos)
	{
		const std::string plain = hit.substr(0, ra);
		for (const std::string& n : names)
			if (n == plain)
				return plain;
	}
	return hit;
}

std::vector<DbGame> ParseAlmar(const std::string& text)
{
	// "## [Astal (USA)](https://...)", "Master code: `...`", "| Cheat | Code |", "| --- | --- |", "| Unlimited Power | `360DBB76 00FF` |".
	// A page of more codes of a game, "Albert Odyssey - Unlimited Item (USA) / UnlimitedItemCodes", joins the game's
	// ("Albert Odyssey (USA)"), its cheats named "Unlimited Item: ...".
	std::vector<DbGame> out;
	std::map<std::string, size_t> by_name;
	size_t cur = std::string::npos; // the game the rows go to
	std::string prefix;
	size_t pos = 0;
	auto trim = [](std::string v) {
		while (!v.empty() && (v.back() == ' ' || v.back() == '\r' || v.back() == '\t'))
			v.pop_back();
		size_t i = 0;
		while (i < v.size() && (v[i] == ' ' || v[i] == '\t'))
			i++;
		return v.substr(i);
	};
	while (pos < text.size())
	{
		size_t end = text.find('\n', pos);
		if (end == std::string::npos)
			end = text.size();
		const std::string line = trim(text.substr(pos, end - pos));
		pos = end + 1;
		if (line.compare(0, 3, "## ") == 0)
		{
			std::string h = trim(line.substr(3));
			if (!h.empty() && h[0] == '[')
			{
				const size_t close = h.find("](");
				h = close == std::string::npos ? h.substr(1) : h.substr(1, close - 1);
			}
			prefix.clear();
			const size_t slash = h.find(" / ");
			if (slash != std::string::npos)
			{
				h = trim(h.substr(0, slash));
				// "Albert Odyssey - Unlimited Item (USA)": the game, its region, and what this page is
				const size_t dash = h.rfind(" - ");
				const size_t paren = h.rfind(" (");
				if (dash != std::string::npos && paren != std::string::npos && paren > dash)
				{
					prefix = trim(h.substr(dash + 3, paren - dash - 3));
					h = h.substr(0, dash) + h.substr(paren);
				}
			}
			auto it = by_name.find(h);
			if (it == by_name.end())
			{
				cur = out.size();
				by_name[h] = cur;
				out.push_back({h, {}});
			}
			else
				cur = it->second;
			continue;
		}
		if (cur == std::string::npos || line.size() < 3 || line[0] != '|')
			continue;
		// a table row: the cheat, then the code(s) in backticks
		std::vector<std::string> cells;
		size_t i = 1;
		while (i < line.size())
		{
			size_t bar = line.find('|', i);
			if (bar == std::string::npos)
				bar = line.size();
			cells.push_back(trim(line.substr(i, bar - i)));
			i = bar + 1;
		}
		if (cells.size() < 2 || cells[0].empty() || cells[0] == "Cheat" || cells[0].find("---") != std::string::npos)
			continue;
		std::string code, cell;
		{
			// the codes are in backticks ("`160BC5FA FFFF`<br>`...`"); without them, the cell with its tags left out
			bool in = false, any = cells[1].find('`') != std::string::npos, tag = false;
			for (char c : cells[1])
			{
				if (any)
				{
					if (c == '`')
					{
						in = !in;
						cell += ' ';
					}
					else if (in)
						cell += c;
				}
				else if (c == '<')
					tag = true;
				else if (c == '>')
				{
					tag = false;
					cell += ' ';
				}
				else if (!tag)
					cell += c;
			}
		}
		for (char c : cell)
		{
			if (std::isxdigit(static_cast<unsigned char>(c)) || c == '?')
				code += char(std::toupper(static_cast<unsigned char>(c)));
			else if (!code.empty() && code.back() != '+')
				code += '+';
		}
		while (!code.empty() && code.back() == '+')
			code.pop_back();
		if (code.empty())
			continue;
		out[cur].cheats.push_back({prefix.empty() ? cells[0] : prefix + ": " + cells[0], code});
	}
	// the pages without a code
	std::vector<DbGame> kept;
	for (DbGame& g : out)
		if (!g.cheats.empty())
			kept.push_back(std::move(g));
	return kept;
}

int MatchDbGame(const std::vector<DbGame>& db, const std::string& file_name, const std::string& product, bool loose)
{
	static std::mutex mx;
	static std::vector<std::string> names;
	static artmatch::Index ix;
	std::lock_guard<std::mutex> l(mx);
	bool same = names.size() == db.size();
	for (size_t i = 0; same && i < db.size(); i++)
		same = names[i] == db[i].name;
	if (!same)
	{
		names.clear();
		for (const DbGame& g : db)
			names.push_back(g.name);
		ix = artmatch::Index();
		artmatch::Build(ix, names);
	}
	const std::string hit = ResolveGame(ix, file_name, product, loose, &names);
	for (size_t i = 0; i < names.size() && !hit.empty(); i++)
		if (names[i] == hit)
			return int(i);
	return -1;
}

std::string UrlFor(const std::string& name)
{
	static const char* const hex = "0123456789ABCDEF";
	std::string url = getenv("YMIR_CHEATS_URL") ? getenv("YMIR_CHEATS_URL") : kBaseUrl;
	for (unsigned char c : name + ".cht")
	{
		if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
			url += char(c);
		else
		{
			url += '%';
			url += hex[c >> 4];
			url += hex[c & 15];
		}
	}
	return url;
}

void Begin(const std::string& game_path)
{
	End();
	int gen;
	{
		std::lock_guard<std::mutex> l(g_mx);
		gen = ++g_generation;
		g_state = State::Looking;
	}
	g_thread = new ps5::BigThread([game_path, gen] { Lookup(game_path, gen); }, size_t(1) << 20);
}

void End()
{
	{
		std::lock_guard<std::mutex> l(g_mx);
		++g_generation;
		g_state = State::Idle;
		g_list.clear();
		g_file.clear();
		g_text.clear();
	}
	JoinThread();
	retro_cheat_reset();
	g_dirty = false;
}

State Status()
{
	std::lock_guard<std::mutex> l(g_mx);
	return g_state;
}

std::vector<Cheat> List()
{
	std::lock_guard<std::mutex> l(g_mx);
	return g_list;
}

std::string File()
{
	std::lock_guard<std::mutex> l(g_mx);
	return g_file;
}

void Set(int index, bool on)
{
	std::string path, text;
	{
		std::lock_guard<std::mutex> l(g_mx);
		if (g_state != State::Ready)
			return;
		for (size_t i = 0; i < g_list.size(); i++)
			if ((index < 0 || int(i) == index) && g_list[i].usable)
				g_list[i].on = on;
		g_text = Write(g_text, g_list);
		path = g_file;
		text = g_text;
	}
	g_dirty = true;
	if (!WriteFile(path, text))
		OrbisLog("[cheats] could not save %s", path.c_str());
}

void Pump()
{
	if (!g_dirty.exchange(false))
		return;
	std::vector<Cheat> list = List();
	retro_cheat_reset();
	int on = 0;
	for (size_t i = 0; i < list.size(); i++)
		if (list[i].on && list[i].usable)
		{
			retro_cheat_set(unsigned(i), true, list[i].code.c_str());
			on++;
		}
	if (on)
		OrbisLog("[cheats] %d on", on);
}
} // namespace cheats
} // namespace fe
