// SSYmirPS5 frontend: the game library (fe_games.h).
// SPDX-License-Identifier: MIT

#include "fe_games.h"
#include "fe_backsources.h"
#include "fe_7z.h"
#include "fe_dbart.h"

#include "OrbisPaths.h"
#include "ProsperoThread.h"

#include "third_party/unzip.h"
#include "libchdr/chd.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <regex>
#include <mutex>
#include <set>
#include <unordered_map>

#define YMIR_INCBIN(sym, path)                                                                                \
	__asm__(".section .rodata\n"                                                                               \
			".balign 16\n"                                                                                     \
			".global " #sym "_begin\n" #sym "_begin:\n"                                                        \
			".incbin \"" path "\"\n"                                                                           \
			".global " #sym "_end\n" #sym "_end:\n"                                                            \
			".previous\n");                                                                                    \
	extern "C" const char sym##_begin[];                                                                       \
	extern "C" const char sym##_end[];

YMIR_INCBIN(ymir_gamedb, GAMEDB_TSV)
// the dumps No-Intro does not list, and the games an online database has a cover of (tools/make_extra_crc.py, make_db_art.py)
YMIR_INCBIN(ymir_extracrc, EXTRACRC_TSV)
YMIR_INCBIN(ymir_dbart, DBART_TSV)

namespace fe
{
namespace
{
const SystemInfo kSystems[size_t(System::Count)] = {
	{"ss", "Saturn", "Sega_-_Saturn", Family::Saturn, "Saturn"},
};

struct ExtSys
{
	const char* ext;
	System sys;
};
// What the shelf lists: the disc images Ymir opens (a .m3u lists the discs of a game with several).
const ExtSys kExts[] = {
	{".cue", System::Saturn}, {".chd", System::Saturn}, {".iso", System::Saturn}, {".mds", System::Saturn},
	{".ccd", System::Saturn}, {".m3u", System::Saturn},
};

uint32_t Crc(const uint8_t* p, size_t n)
{
	return uint32_t(crc32(0L, p, uInt(n)));
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(uint8_t(c)));
	return s;
}

std::string ExtOf(const std::string& name)
{
	const size_t dot = name.find_last_of('.');
	const size_t slash = name.find_last_of("/\\");
	if (dot == std::string::npos || dot == 0 || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(name.substr(dot));
}

// The systems searched together for a game of system s.
std::vector<System> Group(System s)
{
	return {s};
}

// "Super Mario World (USA) (Rev 1)" -> "super mario world"; also drops [tags] and trims.
std::string Key(const std::string& name)
{
	std::string out;
	int depth = 0;
	for (char c : name)
	{
		if (c == '(' || c == '[')
			depth++;
		else if ((c == ')' || c == ']') && depth > 0)
			depth--;
		else if (depth == 0)
			out += char(tolower(uint8_t(c)));
	}
	// collapse spaces, drop punctuation that file names often lose
	std::string k;
	bool space = false;
	for (char c : out)
	{
		if (c == '_' || c == ' ' || c == '.')
			space = !k.empty();
		else if (isalnum(uint8_t(c)) || c == '&' || c == '\'' || c == '-' || c == ',' || c == '!')
		{
			if (space)
				k += ' ';
			space = false;
			k += c;
		}
	}
	return k;
}

int RegionRank(const std::string& name)
{
	// prefer clean dumps: no Beta/Proto/Pirate/Virtual Console/etc.
	int penalty = 0;
	static const char* const bad[] = {"(Beta", "(Proto", "(Pirate", "(Virtual Console", "(Sample", "(Demo", "(Alt",
		"(Unl", "(Aftermarket", "(Arcade", "(Kiosk", "(Program", "(Hack", "(Switch Online", "(Collection"};
	for (const char* b : bad)
		if (name.find(b) != std::string::npos)
			penalty += 100;
	if (name.find("(Rev") != std::string::npos)
		penalty += 1;
	if (name.find("USA") != std::string::npos)
		return penalty + 0;
	if (name.find("World") != std::string::npos)
		return penalty + 2;
	if (name.find("Europe") != std::string::npos)
		return penalty + 4;
	if (name.find("Japan") != std::string::npos)
		return penalty + 6;
	return penalty + 8;
}

uint64_t CrcKey(System s, uint32_t crc)
{
	return (uint64_t(s) << 32) | crc;
}

std::string NameKey(System s, const std::string& k)
{
	return std::string(1, char('A' + int(s))) + k;
}

struct Db
{
	size_t entries = 0;
	std::unordered_map<uint64_t, std::string> by_crc;
	std::unordered_map<std::string, std::string> exact; // sys + lower-case name -> name
	std::unordered_map<std::string, std::string> loose; // sys + Key -> best name
	std::unordered_map<uint64_t, std::string> extra_by_crc; // sys + CRC -> the name of a dump No-Intro does not list
	dbart::Table covers[size_t(System::Count)]; // per system: games an online database has a cover for
	Db()
	{
		const char* dbart_begin = ymir_dbart_begin;
		const char* dbart_end = ymir_dbart_end;
#ifndef __PROSPERO__
		// the host tests give a table of their own (the built-in one may be empty until tools/make_db_art.py runs)
		std::string host_dbart;
		if (const char* path = getenv("YMIR_HOST_DBART"))
			if (FILE* f = fopen(path, "rb"))
			{
				char buf[4096];
				size_t n;
				while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
					host_dbart.append(buf, n);
				fclose(f);
				dbart_begin = host_dbart.data();
				dbart_end = host_dbart.data() + host_dbart.size();
			}
#endif
		for (int i = 0; i < int(System::Count); i++)
			dbart::Parse(dbart_begin, dbart_end, kSystems[i].id, covers[i]);
		// system<TAB>CRC32<TAB>name
		for (const char* q = ymir_extracrc_begin; q < ymir_extracrc_end;)
		{
			const char* nl = static_cast<const char*>(memchr(q, '\n', size_t(ymir_extracrc_end - q)));
			if (!nl)
				nl = ymir_extracrc_end;
			const std::string line(q, size_t(nl - q));
			q = nl + 1;
			const size_t t1 = line.find('\t');
			if (line.empty() || line[0] == '#' || t1 == std::string::npos || line.size() < t1 + 11 || line[t1 + 9] != '\t')
				continue;
			for (int i = 0; i < int(System::Count); i++)
				if (line.compare(0, t1, kSystems[i].id) == 0 && strlen(kSystems[i].id) == t1)
					extra_by_crc.emplace(CrcKey(System(i), uint32_t(strtoul(line.substr(t1 + 1, 8).c_str(), nullptr, 16))),
						line.substr(t1 + 10));
		}
		const char* p = ymir_gamedb_begin;
		const char* end = ymir_gamedb_end;
		while (p < end)
		{
			const char* nl = static_cast<const char*>(memchr(p, '\n', size_t(end - p)));
			if (!nl)
				nl = end;
			Line(std::string(p, size_t(nl - p)));
			p = nl + 1;
		}
	}
	void Line(const std::string& line)
	{
		if (line.empty() || line[0] == '#')
			return;
		const size_t t1 = line.find('\t');
		const size_t t2 = t1 == std::string::npos ? t1 : line.find('\t', t1 + 1);
		if (t2 == std::string::npos)
			return;
		const std::string id = line.substr(0, t1);
		const std::string crc = line.substr(t1 + 1, t2 - t1 - 1);
		const std::string name = line.substr(t2 + 1);
		System sys = System::Count;
		for (int i = 0; i < int(System::Count); i++)
			if (id == kSystems[i].id)
				sys = System(i);
		if (sys == System::Count || name.empty())
			return;
		entries++;
		if (crc != "-")
			by_crc.emplace(CrcKey(sys, uint32_t(strtoul(crc.c_str(), nullptr, 16))), name);
		exact.emplace(NameKey(sys, Lower(name)), name);
		const std::string k = NameKey(sys, Key(name));
		auto it = loose.find(k);
		if (it == loose.end() || RegionRank(name) < RegionRank(it->second))
			loose[k] = name;
	}
};

const Db& D()
{
	static const Db db;
	return db;
}

// ---- CRC cache: path \t size \t mtime \t whole \t body ----
// One per process: the scan and DescribeGame share it, each under the lock.
struct CrcCache
{
	std::mutex lock;
	std::unordered_map<std::string, std::string> lines; // path -> "size\tmtime\twhole\tbody"
	std::unordered_map<std::string, bool> seen; // the paths this scan looked up
	bool dirty = false;
	bool loaded = false;
	std::string file;
	// A new scan: reads the file the first time, forgets which paths the last scan saw.
	void Begin()
	{
		std::lock_guard<std::mutex> g(lock);
		seen.clear();
		LoadLocked();
	}
	// Reads the file the first time, keeps the scan's list (DescribeGame, a game being loaded).
	void Ensure()
	{
		std::lock_guard<std::mutex> g(lock);
		LoadLocked();
	}
	void LoadLocked()
	{
		if (loaded)
			return;
		loaded = true;
		file = OrbisDir("covers") + "/crc-cache.txt";
		FILE* f = fopen(file.c_str(), "r");
		if (!f)
			return;
		char line[2048];
		while (fgets(line, sizeof(line), f))
		{
			line[strcspn(line, "\r\n")] = 0;
			char* tab = strchr(line, '\t');
			if (tab)
			{
				*tab = 0;
				lines[line] = tab + 1;
			}
		}
		fclose(f);
	}
	// prune: the scan's end drops the entries of files that are gone; DescribeGame keeps all.
	void Save(bool prune)
	{
		std::lock_guard<std::mutex> g(lock);
		// entries of files that are gone are dropped (a USB drive that isn't plugged in keeps its own)
		for (auto it = lines.begin(); prune && it != lines.end();)
		{
			const std::string& key = it->first;
			if (!seen.count(key) && key.rfind("/mnt/", 0) != 0)
			{
				it = lines.erase(it);
				dirty = true;
			}
			else
				++it;
		}
		if (!dirty)
			return;
		const std::string tmp = file + ".part";
		FILE* f = fopen(tmp.c_str(), "w");
		if (!f)
			return;
		for (const auto& kv : lines)
			fprintf(f, "%s\t%s\n", kv.first.c_str(), kv.second.c_str());
		bool ok = ferror(f) == 0;
		ok = fclose(f) == 0 && ok;
		if (!ok || rename(tmp.c_str(), file.c_str()) != 0)
		{
			remove(tmp.c_str());
			return;
		}
		dirty = false;
	}
	void Put(const std::string& path, const std::string& val)
	{
		std::lock_guard<std::mutex> g(lock);
		lines[path] = val;
		dirty = true;
	}
};

CrcCache& SharedCrcCache()
{
	static CrcCache* c = new CrcCache; // never freed: used until the app exits
	return *c;
}

// read: false to use only a CRC already in the cache. known (if given): the cache holds an answer for this file as it
// is now, "could not be read" included.
bool CachedCrc(CrcCache& cache, const std::string& path, uint32_t* whole, uint32_t* body, bool read = true, bool* known = nullptr)
{
	if (known)
		*known = false;
	{
		std::lock_guard<std::mutex> g(cache.lock);
		cache.seen[path] = true;
	}
	struct stat st = {};
	if (stat(path.c_str(), &st) != 0)
		return false;
	char key[64];
	snprintf(key, sizeof(key), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	{
		std::lock_guard<std::mutex> g(cache.lock);
		auto it = cache.lines.find(path);
		if (it != cache.lines.end() && it->second.compare(0, strlen(key), key) == 0)
		{
			if (known)
				*known = true;
			if (it->second.compare(strlen(key), std::string::npos, "fail") == 0)
				return false; // couldn't be read last time, and hasn't changed since
			unsigned a = 0, b = 0;
			if (sscanf(it->second.c_str() + strlen(key), "%x\t%x", &a, &b) == 2)
			{
				*whole = a;
				*body = b;
				return true;
			}
		}
	}
	if (!read)
		return false;
	char val[96];
	if (!RomCrc32(path, whole, body))
	{
		// unreadable (or an empty archive): remembered, so the next scans don't read it again until it changes
		snprintf(val, sizeof(val), "%sfail", key);
		cache.Put(path, val);
		return false;
	}
	snprintf(val, sizeof(val), "%s%08X\t%08X", key, *whole, *body);
	cache.Put(path, val);
	return true;
}

// The offset of the ROM data after a header we know, or 0.
size_t HeaderSize(const std::vector<uint8_t>& d, const std::string& ext)
{
	(void)d, (void)ext; // disc images have no copier header
	return 0;
}

// The track files .cue sheets of this folder name (FILE "Game (Track 01).bin" BINARY): they belong to a disc,
// not to the shelf as games of their own. Each sheet's list is cached while the sheet hasn't changed, so a scan
// again doesn't read every sheet once more: "cue:<path>" -> "size\tmtime\t<track>\t<track>...". A sheet that could not be read whole is not cached.
std::vector<std::string> CueTracks(const std::string& dir, CrcCache& cache)
{
	std::vector<std::string> tracks;
	DIR* d = opendir(dir.c_str());
	if (!d)
		return tracks;
	while (dirent* e = readdir(d))
	{
		const std::string sheet_ext = ExtOf(e->d_name);
		if (sheet_ext != ".cue" && sheet_ext != ".m3u")
			continue;
		const bool playlist = sheet_ext == ".m3u";
		const std::string path = dir + "/" + e->d_name;
		struct stat st = {};
		if (stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
			continue;
		const std::string key = "cue:" + path;
		char stamp[64];
		snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
		bool cached = false;
		{
			std::lock_guard<std::mutex> g(cache.lock);
			cache.seen[key] = true;
			auto it = cache.lines.find(key);
			if (it != cache.lines.end() && it->second.compare(0, strlen(stamp), stamp) == 0)
			{
				cached = true;
				const std::string& v = it->second;
				for (size_t at = strlen(stamp); at < v.size();)
				{
					size_t tab = v.find('\t', at);
					if (tab == std::string::npos)
						tab = v.size();
					if (tab > at)
						tracks.push_back(v.substr(at, tab - at));
					at = tab + 1;
				}
			}
		}
		if (cached)
			continue;
		FILE* f = fopen(path.c_str(), "r");
		if (!f)
			continue;
		std::string val = stamp;
		bool first = true;
		char line[1024];
		while (fgets(line, sizeof(line), f))
		{
			const char* p = line;
			while (*p == ' ' || *p == '\t')
				p++;
			if (playlist)
			{
				// a playlist line names a disc: "Game (Disc 1).cue"; # starts a comment
				std::string file(p);
				while (!file.empty() && (file.back() == '\r' || file.back() == '\n' || file.back() == ' '))
					file.pop_back();
				const size_t slash = file.find_last_of("/\\");
				if (slash != std::string::npos)
					file = file.substr(slash + 1);
				if (!file.empty() && file[0] != '#')
				{
					tracks.push_back(Lower(file));
					val += (first ? "" : "\t") + tracks.back();
					first = false;
				}
				continue;
			}
			if (strncasecmp(p, "FILE", 4) != 0)
				continue;
			// FILE "Game (Track 01).bin" BINARY, or FILE track01.bin BINARY; a path keeps only its file name
			p += 4;
			while (*p == ' ' || *p == '\t')
				p++;
			std::string file;
			if (*p == '"')
			{
				const char* q2 = strchr(p + 1, '"');
				if (q2)
					file.assign(p + 1, q2);
			}
			else
			{
				const char* end = p;
				while (*end && *end != ' ' && *end != '\t' && *end != '\r' && *end != '\n')
					end++;
				file.assign(p, end);
			}
			const size_t slash = file.find_last_of("/\\");
			if (slash != std::string::npos)
				file = file.substr(slash + 1);
			if (!file.empty())
			{
				tracks.push_back(Lower(file));
				val += (first ? "" : "\t") + tracks.back();
				first = false;
			}
		}
		const bool read_ok = ferror(f) == 0;
		fclose(f);
		if (read_ok)
			cache.Put(key, val);
	}
	closedir(d);
	return tracks;
}

bool ArchiveSystemWhy(const std::string& path, System* out, ArchiveError* why, const std::atomic<bool>* cancel = nullptr);

// A zip's or 7z's system (the ROM inside it), from the cache while the archive hasn't changed: opening every archive
// at every start is slow on a USB drive with a big library. "sys:<path>" -> "size\tmtime\t<system>".
// read: false to use only the cache (*known then says whether the cache had it).
// *none (if given): false returned because the cache says, or now says, "none": the archive surely holds no ROM.
bool CachedArchiveSystem(CrcCache& cache, const std::string& path, const struct stat& st, System* out, bool read = true,
	bool* known = nullptr, const std::atomic<bool>* cancel = nullptr, bool* none = nullptr)
{
	if (none)
		*none = false;
	const std::string key = "sys:" + path;
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	if (known)
		*known = true;
	{
		std::lock_guard<std::mutex> g(cache.lock);
		cache.seen[key] = true;
		auto it = cache.lines.find(key);
		if (it != cache.lines.end() && it->second.compare(0, strlen(stamp), stamp) == 0)
		{
			const std::string v = it->second.substr(strlen(stamp));
			if (v == "none")
			{
				if (none)
					*none = true;
				return false;
			}
			const int n = atoi(v.c_str());
			if (n >= 0 && n < int(System::Count) && System(n) != System::Saturn)
			{
				*out = System(n);
				return true;
			}
		}
	}
	if (!read)
	{
		if (known)
			*known = false;
		return false;
	}
	ArchiveError why = ArchiveError::None;
	const bool ok = ArchiveSystemWhy(path, out, &why, cancel);
	// "none" hides the archive from the shelf until it changes
	(void)why;
	cache.Put(key, std::string(stamp) + (ok ? std::to_string(int(*out)) : std::string("none")));
	if (!ok && none)
		*none = true;
	return ok;
}

void Walk(const std::string& dir, int depth, bool usb, std::vector<GameInfo>& out, CrcCache& cache,
	const ScanProgress& progress)
{
	const std::vector<std::string> tracks = CueTracks(dir, cache);
	DIR* d = opendir(dir.c_str());
	if (!d)
		return;
	while (dirent* e = readdir(d))
	{
		if (e->d_name[0] == '.')
			continue;
		const std::string path = dir + "/" + e->d_name;
		struct stat st = {};
		if (stat(path.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
		{
			if (depth < 4)
				Walk(path, depth + 1, usb, out, cache, progress);
			continue;
		}
		if (!S_ISREG(st.st_mode))
			continue; // a FIFO or a device would block the CRC read (and the shelf) forever
		const std::string name = e->d_name;
		const std::string ext = ExtOf(name);
		if (ext.empty())
			continue;
		if (std::find(tracks.begin(), tracks.end(), Lower(name)) != tracks.end())
			continue; // a disc's track
		GameInfo g;
		if (IsArchiveExt(ext))
		{
			if (!CachedArchiveSystem(cache, path, st, &g.system))
				continue;
		}
		else if (!SystemForExt(ext, &g.system))
			continue;
		g.path = path;
		g.file_base = name.substr(0, name.size() - ext.size());
		g.ext = ext;
		g.on_usb = usb;
		out.push_back(std::move(g));
		if (progress)
			progress(0, out.size(), 0);
	}
	closedir(d);
}

// A ROM no name or CRC identified keeps its header's title for the cover and border lookup (Snes9x PS5 2.7's HeaderHint).
// Cached like the CRC (under the path plus "|hdr", with the size and mtime), so each such ROM is opened once:
// "size\tmtime\tTITLE\tJAPAN" or "...\tnone".
void LoadHeaderHint(CrcCache& cache, GameInfo& g)
{
	struct stat st = {};
	if (stat(g.path.c_str(), &st) != 0)
		return;
	const std::string key = g.path + "|hdr";
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	std::string val;
	bool cached = false;
	{
		std::lock_guard<std::mutex> lk(cache.lock);
		cache.seen[key] = true;
		auto it = cache.lines.find(key);
		if (it != cache.lines.end() && it->second.compare(0, strlen(stamp), stamp) == 0)
		{
			val = it->second.substr(strlen(stamp));
			cached = true;
		}
	}
	if (!cached)
	{
		romheader::Info info;
		val = ReadRomHeader(g.path, g.system, &info) ? info.title + "\t" + (info.japan() ? "1" : "0") : "none";
		cache.Put(key, stamp + val);
	}
	const size_t tab = val.find('\t');
	if (tab == std::string::npos)
		return;
	g.header_title = val.substr(0, tab);
	g.header_japan = val.compare(tab + 1, std::string::npos, "1") == 0;
}

// A Saturn disc's product number, from its header (cached like the header hint, under the path plus "|prod"): what
// names a disc for sure when its file's name doesn't (2.6: "Pu Li Ru La - Arcade Gears (1997)(Xing)(JP).ccd" is
// T-26106G, "Arcade Gears Vol. 1 - Pu-Li-Ru-La (Japan)"). "" when it can't be read.
std::string CachedProduct(CrcCache& cache, const GameInfo& g, int* disc, int* discs)
{
	*disc = *discs = 0;
	struct stat st = {};
	if (stat(g.path.c_str(), &st) != 0)
		return "";
	const std::string key = g.path + "|prod";
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	std::string v;
	bool cached = false;
	{
		std::lock_guard<std::mutex> lk(cache.lock);
		cache.seen[key] = true;
		auto it = cache.lines.find(key);
		if (it != cache.lines.end() && it->second.compare(0, strlen(stamp), stamp) == 0)
		{
			v = it->second.substr(strlen(stamp));
			cached = true;
		}
	}
	if (!cached)
	{
		romheader::Info info;
		v = ReadRomHeader(g.path, g.system, &info) && !info.other.empty()
			? info.other + "\t" + std::to_string(info.disc) + "\t" + std::to_string(info.discs) : std::string("none");
		cache.Put(key, stamp + v);
	}
	if (v == "none")
		return "";
	const size_t t1 = v.find('\t'), t2 = t1 == std::string::npos ? t1 : v.find('\t', t1 + 1);
	if (t2 == std::string::npos)
		return "";
	*disc = atoi(v.c_str() + t1 + 1);
	*discs = atoi(v.c_str() + t2 + 1);
	return v.substr(0, t1);
}

// 3.11: the year a Saturn disc's header gives (its date, "19950310"), cached like the product number under the path plus
// "|year". 0 when it can't be read or makes no sense.
int CachedYear(CrcCache& cache, const GameInfo& g)
{
	if (g.system != System::Saturn)
		return 0;
	struct stat st = {};
	if (stat(g.path.c_str(), &st) != 0)
		return 0;
	const std::string key = g.path + "|year";
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "%lld\t%lld\t", (long long)st.st_size, (long long)st.st_mtime);
	{
		std::lock_guard<std::mutex> lk(cache.lock);
		cache.seen[key] = true;
		auto it = cache.lines.find(key);
		if (it != cache.lines.end() && it->second.compare(0, strlen(stamp), stamp) == 0)
			return atoi(it->second.c_str() + strlen(stamp));
	}
	romheader::Info info;
	int year = 0;
	if (ReadRomHeader(g.path, g.system, &info) && info.date.size() >= 4)
		year = atoi(info.date.substr(0, 4).c_str());
	if (year < 1993 || year > 2010)
		year = 0; // the Saturn's years (a header with a stray date: not known)
	cache.Put(key, stamp + std::to_string(year));
	return year;
}

// The table's Redump name of a product number, for this disc of it: a game's discs share one number, and the table
// names its first ("Deep Fear (Japan) (Disc 1)"), so disc 2 (its header's "CD-2/2") is "... (Disc 2)". "" when the
// name that makes is not a known one.
std::string SerialName(System s, const std::string& product, int disc, int discs, System* found)
{
	std::string title;
	if (!backs::SerialTitle(product, &title, nullptr))
		return "";
	const size_t at = title.find(" (Disc ");
	if (at != std::string::npos)
	{
		const size_t end = title.find(')', at);
		if (end == std::string::npos || disc < 1 || discs < 2)
			return ""; // a disc of a set, but which one the header doesn't say
		title = title.substr(0, at) + " (Disc " + std::to_string(disc) + ")" + title.substr(end + 1);
	}
	else if (discs > 1)
		return ""; // the table names the game, not this disc of it
	return gamedb::Exact(s, title, found);
}

// Names one game, in the order fe_games.h describes: its file name, its CRC, the file name loosely; a ROM none of that
// names gets what its CRC is called in the extra table and its header's title. ScanGames and DescribeGame (a game being
// loaded) both come through here, so the shelf, the cover and the border name a game alike.
void NameGame(CrcCache& cache, GameInfo& g)
{
	System found = g.system;
	g.nointro = gamedb::Exact(g.system, g.file_base, &found);
	// a Saturn disc its file name doesn't name: the game its product number is (the built-in table's Redump name)
	if (g.nointro.empty() && g.system == System::Saturn)
	{
		int disc = 0, discs = 0;
		const std::string product = CachedProduct(cache, g, &disc, &discs);
		if (!product.empty())
		{
			g.nointro = SerialName(g.system, product, disc, discs, &found);
			if (!g.nointro.empty())
				OrbisLog("[games] %s: product %s (disc %d/%d) is %s", g.file_base.c_str(), product.c_str(), disc, discs, g.nointro.c_str());
		}
	}
	uint32_t whole = 0, body = 0;
	bool have_crc = false;
	if (g.nointro.empty())
	{
		if (CachedCrc(cache, g.path, &whole, &body))
		{
			have_crc = true;
			g.crc = body;
			g.nointro = gamedb::ByCrc(g.system, whole, &found);
			if (g.nointro.empty() && body != whole)
				g.nointro = gamedb::ByCrc(g.system, body, &found);
			g.name_by_crc = !g.nointro.empty();
		}
	}
	if (g.nointro.empty())
		g.nointro = gamedb::Loose(g.system, g.file_base, &found);
	if (!g.nointro.empty())
		g.system = found;
	else
	{
		if (have_crc)
		{
			g.crc_name = gamedb::ByExtraCrc(g.system, whole);
			if (g.crc_name.empty() && body != whole)
				g.crc_name = gamedb::ByExtraCrc(g.system, body);
		}
		LoadHeaderHint(cache, g);
	}
	g.title = g.nointro.empty() ? g.file_base : gamedb::Title(g.nointro);
	g.year = CachedYear(cache, g);
	g.region = gamedb::Region(g.nointro);
}

template <typename F>
std::string Search(System s, System* found_in, F&& find)
{
	for (System g : Group(s))
	{
		std::string n = find(g);
		if (!n.empty())
		{
			if (found_in)
				*found_in = g;
			return n;
		}
	}
	return "";
}
} // namespace

const SystemInfo& Info(System s)
{
	return kSystems[size_t(s) < size_t(System::Count) ? size_t(s) : 0];
}

const char* FamilyName(Family f)
{
	switch (f)
	{
		case Family::All: return "All games";
		case Family::Saturn: return "Saturn";
		default: return "?";
	}
}

bool SystemForExt(const std::string& ext, System* out)
{
	const std::string e = Lower(ext);
	for (const ExtSys& x : kExts)
		if (e == x.ext)
		{
			*out = x.sys;
			return true;
		}
	return false;
}

namespace
{
// Calls fn(name) for each file of the zip until it returns true; the zip is then positioned on that file.
template <typename F>
bool FindInZip(unzFile z, F&& fn)
{
	for (int r = unzGoToFirstFile(z); r == UNZ_OK; r = unzGoToNextFile(z))
	{
		unz_file_info info;
		char name[512];
		// minizip ends the name with '\0' only when it fits: keep the last byte for it, skip names that don't fit
		name[sizeof(name) - 1] = '\0';
		if (unzGetCurrentFileInfo(z, &info, name, sizeof(name) - 1, nullptr, 0, nullptr, 0) != UNZ_OK ||
			info.size_filename >= sizeof(name) - 1)
			continue;
		if (fn(std::string(name)))
			return true;
	}
	return false;
}
} // namespace

namespace
{
bool IsDiscExt(const std::string& ext)
{
	return ext == ".cue" || ext == ".chd" || ext == ".iso";
}

ArchiveError From7z(sevenzip::Status s)
{
	switch (s)
	{
		case sevenzip::Status::Ok: return ArchiveError::None;
		case sevenzip::Status::NotArchive:
		case sevenzip::Status::Damaged: return ArchiveError::Damaged;
		case sevenzip::Status::Unsupported: return ArchiveError::Unsupported;
		case sevenzip::Status::Encrypted: return ArchiveError::Encrypted;
		case sevenzip::Status::TooBig: return ArchiveError::TooBig;
		case sevenzip::Status::BlockTooBig: return ArchiveError::BlockTooBig;
		case sevenzip::Status::CantOpen:
		case sevenzip::Status::ReadError:
		case sevenzip::Status::Cancelled: return ArchiveError::ReadError;
	}
	return ArchiveError::Damaged;
}

// Opens a .7z and finds its first ROM in the archive's order, as FindInZip does in a zip: its index, or -1 (*why).
// A .7z that holds a disc image (.cue, .chd, .iso) has none: a disc is not read from inside a .7z (it would have to be
// unpacked whole), and its .bin tracks are no games of their own.
int First7zRom(sevenzip::Archive& a, const std::string& path, System* sys, ArchiveError* why, const std::atomic<bool>* cancel = nullptr)
{
	const sevenzip::Status st = a.Open(path, cancel);
	if (st != sevenzip::Status::Ok)
	{
		*why = From7z(st);
		if (st != sevenzip::Status::Cancelled)
			OrbisLog("[games] %s: 7z %s", path.c_str(), sevenzip::StatusText(st));
		return -1;
	}
	int found = -1;
	const std::vector<sevenzip::Entry>& entries = a.Entries();
	for (size_t i = 0; i < entries.size(); i++)
	{
		if (entries[i].dir)
			continue;
		const std::string ext = ExtOf(entries[i].name);
		if (IsDiscExt(ext))
		{
			*why = ArchiveError::Disc;
			OrbisLog("[games] %s: a disc image inside a 7z (%s): not read", path.c_str(), entries[i].name.c_str());
			return -1;
		}
		System s;
		if (found < 0 && SystemForExt(ext, &s) && s != System::Saturn)
		{
			found = int(i);
			*sys = s;
		}
	}
	if (found < 0)
		*why = ArchiveError::NoGame;
	return found;
}
} // namespace

bool IsArchiveExt(const std::string& ext)
{
	// Ymir reads a disc from its files, not from inside a .zip or .7z: archives are not listed.
	(void)ext;
	return false;
}

namespace
{
// ArchiveSystem, and why not (a zip: NoGame, whatever went wrong)
bool ArchiveSystemWhy(const std::string& path, System* out, ArchiveError* why, const std::atomic<bool>* cancel)
{
	*why = ArchiveError::None;
	if (ExtOf(path) == ".7z")
	{
		sevenzip::Archive a;
		return First7zRom(a, path, out, why, cancel) >= 0;
	}
	*why = ArchiveError::NoGame;
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	const bool ok = FindInZip(z, [&](const std::string& name) {
		System s;
		if (!SystemForExt(ExtOf(name), &s) || s == System::Saturn)
			return false;
		*out = s;
		return true;
	});
	unzClose(z);
	if (ok)
		*why = ArchiveError::None;
	return ok;
}
} // namespace

bool ArchiveSystem(const std::string& path, System* out)
{
	ArchiveError why;
	return ArchiveSystemWhy(path, out, &why);
}

namespace
{
// The CRC a zip or 7z stores for its ROM, when the ROM has no copier header to skip (its size isn't 512 bytes past a
// multiple of 1 KiB): true and the name inside; false when the data has to be read.
bool StoredArchiveCrc(const std::string& path, std::string* inner, uint32_t* crc)
{
	auto usable = [](uint64_t size) { return size > 0 && size <= kMaxArchiveRom && (size & 0x3ff) != 0x200; };
	if (ExtOf(path) == ".7z")
	{
		sevenzip::Archive a;
		System sys;
		ArchiveError why = ArchiveError::None;
		const int i = First7zRom(a, path, &sys, &why);
		if (i < 0)
			return false;
		const sevenzip::Entry& e = a.Entries()[size_t(i)];
		if (!e.has_crc || !usable(e.size))
			return false;
		*inner = e.name;
		*crc = e.crc;
		return true;
	}
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	bool ok = FindInZip(z, [&](const std::string& name) {
		System s;
		if (!SystemForExt(ExtOf(name), &s) || s == System::Saturn)
			return false;
		*inner = name;
		return true;
	});
	if (ok)
	{
		unz_file_info info;
		ok = unzGetCurrentFileInfo(z, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK && usable(info.uncompressed_size);
		if (ok)
			*crc = uint32_t(info.crc);
	}
	unzClose(z);
	return ok;
}
} // namespace

bool ReadFromArchive(const std::string& path, std::string* inner, std::vector<uint8_t>* data, ArchiveError* why,
	const std::atomic<bool>* cancel)
{
	ArchiveError dummy;
	if (!why)
		why = &dummy;
	*why = ArchiveError::None;
	data->clear();
	if (ExtOf(path) == ".7z")
	{
		sevenzip::Archive a;
		System sys;
		const int i = First7zRom(a, path, &sys, why, cancel);
		if (i < 0)
			return false;
		const sevenzip::Entry& e = a.Entries()[size_t(i)];
		*inner = e.name;
		if (e.size == 0)
		{
			*why = ArchiveError::NoGame;
			return false;
		}
		const sevenzip::Status st = a.Extract(size_t(i), data, kMaxArchiveRom, kMax7zBlock);
		if (st != sevenzip::Status::Ok)
		{
			*why = From7z(st);
			if (st != sevenzip::Status::Cancelled)
				OrbisLog("[games] %s: %s in the 7z: %s (%llu bytes, block of %llu)", path.c_str(), e.name.c_str(),
					sevenzip::StatusText(st), (unsigned long long)e.size, (unsigned long long)a.BlockSize(size_t(i)));
			data->clear();
			return false;
		}
		return true;
	}
	*why = ArchiveError::NoGame; // a zip: whatever went wrong
	unzFile z = unzOpen(path.c_str());
	if (!z)
		return false;
	bool ok = FindInZip(z, [&](const std::string& name) {
		System s;
		if (!SystemForExt(ExtOf(name), &s) || s == System::Saturn)
			return false;
		*inner = name;
		return true;
	});
	if (ok)
	{
		unz_file_info info;
		ok = unzGetCurrentFileInfo(z, &info, nullptr, 0, nullptr, 0, nullptr, 0) == UNZ_OK &&
			 info.uncompressed_size > 0 && info.uncompressed_size <= kMaxArchiveRom && unzOpenCurrentFile(z) == UNZ_OK;
		if (ok)
		{
			data->resize(info.uncompressed_size);
			ok = unzReadCurrentFile(z, data->data(), unsigned(data->size())) == int(data->size());
			unzCloseCurrentFile(z);
		}
	}
	unzClose(z);
	if (ok)
		*why = ArchiveError::None;
	else
		data->clear();
	return ok;
}

namespace gamedb
{
size_t Count()
{
	return D().entries;
}

std::string ByCrc(System s, uint32_t crc, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().by_crc.find(CrcKey(g, crc));
		return it == D().by_crc.end() ? std::string() : it->second;
	});
}

std::string Exact(System s, const std::string& name, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().exact.find(NameKey(g, Lower(name)));
		return it == D().exact.end() ? std::string() : it->second;
	});
}

std::string Loose(System s, const std::string& file_base, System* found_in)
{
	return Search(s, found_in, [&](System g) {
		auto it = D().loose.find(NameKey(g, Key(file_base)));
		return it == D().loose.end() ? std::string() : it->second;
	});
}

std::string Title(const std::string& nointro)
{
	std::string t = nointro.substr(0, nointro.find(" ("));
	// "Legend of Zelda, The - A Link to the Past" -> "The Legend of Zelda - A Link to the Past"
	static const char* const arts[] = {", The", ", A", ", An"};
	for (const char* a : arts)
	{
		const size_t pos = t.find(a);
		if (pos != std::string::npos)
		{
			const size_t end = pos + strlen(a);
			if (end == t.size() || t.compare(end, 3, " - ") == 0 || t[end] == ':')
			{
				const std::string art = std::string(a + 2);
				t = art + " " + t.substr(0, pos) + t.substr(end);
				break;
			}
		}
	}
	return t;
}

std::string ByExtraCrc(System s, uint32_t crc)
{
	return Search(s, nullptr, [&](System g) {
		auto it = D().extra_by_crc.find(CrcKey(g, crc));
		return it == D().extra_by_crc.end() ? std::string() : it->second;
	});
}

std::string DatabaseCoverUrls(const GameInfo& game)
{
	// the host tests point both at a local server
	const char* hasheous = getenv("YMIR_DBART_HASHEOUS");
	const char* thegamesdb = getenv("YMIR_DBART_THEGAMESDB");
	const dbart::Table& t = D().covers[size_t(game.system) < size_t(System::Count) ? size_t(game.system) : 0];
	const dbart::Row* row = dbart::Find(t, game.crc, game.nointro.empty() ? game.file_base : game.nointro);
	if (!row && game.nointro != game.file_base)
		row = dbart::Find(t, 0, game.file_base);
	if (!row && !game.crc_name.empty())
		row = dbart::Find(t, 0, game.crc_name);
	if (!row)
		return std::string();
	return dbart::Urls(*row, hasheous && *hasheous ? hasheous : "https://hasheous.org/api/v1/images/",
		thegamesdb && *thegamesdb ? thegamesdb : "https://cdn.thegamesdb.net/images/original/boxart/front/");
}

std::string Region(const std::string& nointro)
{
	const size_t a = nointro.find(" (");
	if (a == std::string::npos)
		return "";
	const size_t b = nointro.find(')', a);
	return b == std::string::npos ? "" : nointro.substr(a + 2, b - a - 2);
}
} // namespace gamedb

bool RomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, std::string* inner_ext, const std::atomic<bool>* cancel)
{
	auto stopped = [cancel] { return cancel && cancel->load(); };
	std::string ext = ExtOf(path);
	std::vector<uint8_t> data;
	System sys;
	if (IsArchiveExt(ext))
	{
		// a zip or 7z keeps its ROM's CRC: read only when a 512-byte copier header has to come off for the body's
		std::string inner;
		uint32_t stored = 0;
		if (StoredArchiveCrc(path, &inner, &stored))
		{
			if (inner_ext)
				*inner_ext = ExtOf(inner);
			*whole = *body = stored;
			return true;
		}
		if (!ReadFromArchive(path, &inner, &data, nullptr, cancel))
			return false;
		ext = ExtOf(inner);
	}
	else if (!SystemForExt(ext, &sys) || sys == System::Saturn)
		return false; // a disc: named by its file name only
	else
	{
		FILE* f = fopen(path.c_str(), "rb");
		if (!f)
			return false;
		fseek(f, 0, SEEK_END);
		const long size = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (size <= 0 || size > 64L * 1024 * 1024)
		{
			fclose(f);
			return false;
		}
		data.resize(size_t(size));
		// in pieces, so a cancel does not wait for a whole ROM
		size_t n = 0;
		while (n < data.size() && !stopped())
		{
			const size_t got = fread(data.data() + n, 1, std::min<size_t>(1 << 20, data.size() - n), f);
			if (got == 0)
				break;
			n += got;
		}
		fclose(f);
		if (n != data.size())
			return false;
	}
	if (inner_ext)
		*inner_ext = ext;
	*whole = Crc(data.data(), data.size());
	const size_t skip = HeaderSize(data, ext);
	*body = skip > 0 && skip < data.size() ? Crc(data.data() + skip, data.size() - skip) : *whole;
	return true;
}

void PrepareFolders()
{
	const std::string games = OrbisDir("games"), covers = OrbisDir("covers"), borders = OrbisDir("borders");
	for (const SystemInfo& s : kSystems)
	{
		OrbisMkdirs(games + "/" + s.folder);
		OrbisMkdirs(covers + "/" + s.folder);
		OrbisMkdirs(borders + "/" + s.folder); // the border pack (fe_border.h)
	}
	// covers cached by 1.0's first builds: covers/"ss - Panzer Dragoon (Europe).png" (and .missing)
	DIR* d = opendir(covers.c_str());
	if (!d)
		return;
	std::vector<std::pair<std::string, std::string>> moves;
	while (dirent* e = readdir(d))
	{
		const std::string name = e->d_name;
		for (const SystemInfo& s : kSystems)
		{
			const std::string tag = std::string(s.id) + " - ";
			if (name.size() > tag.size() && name.compare(0, tag.size(), tag) == 0)
			{
				moves.push_back({covers + "/" + name, covers + "/" + s.folder + "/" + name.substr(tag.size())});
				break;
			}
		}
	}
	closedir(d);
	for (const auto& m : moves)
	{
		struct stat st = {};
		if (stat(m.second.c_str(), &st) == 0)
			OrbisLog("[games] %s kept: %s is there already", m.first.c_str(), m.second.c_str()); // never overwritten
		else if (rename(m.first.c_str(), m.second.c_str()) != 0)
			OrbisLog("[games] can't move %s to %s", m.first.c_str(), m.second.c_str());
	}
	if (!moves.empty())
		OrbisLog("[games] moved %zu cached cover file(s) into covers/<system>/", moves.size());
}

namespace
{
void SignDir(const std::string& dir, int depth, uint64_t& h)
{
	auto mix = [&](const void* p, size_t n) {
		const uint8_t* b = static_cast<const uint8_t*>(p);
		for (size_t i = 0; i < n; i++)
			h = (h ^ b[i]) * 1099511628211ull;
	};
	struct stat st = {};
	if (stat(dir.c_str(), &st) != 0)
		return;
	mix(dir.data(), dir.size());
	const int64_t mt = int64_t(st.st_mtime);
	mix(&mt, sizeof(mt));
	DIR* d = opendir(dir.c_str());
	if (!d)
		return;
	uint32_t files = 0;
	std::vector<std::string> subs;
	while (dirent* e = readdir(d))
	{
		if (e->d_name[0] == '.')
			continue;
		if (e->d_type == DT_DIR)
			subs.push_back(dir + "/" + e->d_name);
		else
		{
			files++;
			mix(e->d_name, strlen(e->d_name));
		}
	}
	closedir(d);
	mix(&files, sizeof(files));
	if (depth < 4)
		for (const std::string& s : subs)
			SignDir(s, depth + 1, h);
}
} // namespace

uint64_t LocalLibrarySignature()
{
	uint64_t h = 1469598103934665603ull;
	for (const std::string& r : OrbisRomRoots())
		SignDir(r, 0, h);
	return h;
}

std::vector<std::string> RomRoots()
{
	return OrbisRomRoots();
}

namespace
{
// every file named `name` (any case) in `dir` and its sub-folders (2 levels), the folder's own first, then the
// sub-folders in name order ("Disc 1", "Disc 2"...)
void FindAllInTree(const std::string& dir, const std::string& name, int depth, std::vector<std::string>& out)
{
	DIR* d = opendir(dir.c_str());
	if (!d)
		return;
	const std::string want = Lower(name);
	std::vector<std::string> subs;
	while (dirent* e = readdir(d))
	{
		if (e->d_name[0] == '.')
			continue;
		const std::string path = dir + "/" + e->d_name;
		struct stat st = {};
		if (stat(path.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			subs.push_back(path);
		else if (S_ISREG(st.st_mode) && Lower(e->d_name) == want)
			out.push_back(path);
	}
	closedir(d);
	if (depth >= 2)
		return;
	std::sort(subs.begin(), subs.end(), [](const std::string& a, const std::string& b) {
		return strcasecmp(a.c_str(), b.c_str()) < 0;
	});
	for (const std::string& sd : subs)
		FindAllInTree(sd, name, depth + 1, out);
}

[[maybe_unused]] std::string FindInTree(const std::string& dir, const std::string& name, int depth)
{
	DIR* d = opendir(dir.c_str());
	if (!d)
		return "";
	const std::string want = Lower(name);
	std::vector<std::string> subs;
	std::string found;
	while (dirent* e = readdir(d))
	{
		if (e->d_name[0] == '.')
			continue;
		const std::string path = dir + "/" + e->d_name;
		struct stat st = {};
		if (stat(path.c_str(), &st) != 0)
			continue;
		if (S_ISDIR(st.st_mode))
			subs.push_back(path);
		else if (found.empty() && S_ISREG(st.st_mode) && Lower(e->d_name) == want)
			found = path;
	}
	closedir(d);
	if (!found.empty() || depth >= 2)
		return found;
	std::sort(subs.begin(), subs.end());
	for (const std::string& sd : subs)
	{
		found = FindInTree(sd, name, depth + 1);
		if (!found.empty())
			return found;
	}
	return "";
}
} // namespace

std::vector<std::string> ResolvePlaylist(const std::string& m3u, std::vector<std::string>* missing, bool* changed)
{
	std::vector<std::string> out;
	if (changed)
		*changed = false;
	FILE* f = fopen(m3u.c_str(), "r");
	if (!f)
		return out;
	const std::string dir = m3u.substr(0, m3u.find_last_of('/'));
	std::map<std::string, size_t> seen_name; // a name on several lines ("game.cue" in Disc 1/, Disc 2/...): the next one each time
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		std::string l = line;
		while (!l.empty() && (l.back() == '\r' || l.back() == '\n' || l.back() == ' ' || l.back() == '\t'))
			l.pop_back();
		size_t b = l.find_first_not_of(" \t");
		if (b == std::string::npos || l[b] == '#')
			continue;
		l = l.substr(b);
		const std::string as_written = l;
		for (char& c : l)
			if (c == '\\')
				c = '/';
		while (l.compare(0, 2, "./") == 0)
			l = l.substr(2);
		std::string full = l[0] == '/' ? l : dir + "/" + l;
		struct stat st = {};
		if (stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode))
		{
			// not where the line says: the same name in the .m3u's folder or its sub-folders, ignoring case
			// ("SAKURA TAISEN2_1.cue" in "Game/Disc 1/")
			const std::string name = l.substr(l.find_last_of('/') + 1);
			std::vector<std::string> all;
			FindAllInTree(dir, name, 0, all);
			size_t& k = seen_name[Lower(name)];
			if (k < all.size())
				full = all[k];
			else if (!all.empty())
				full = all.back();
			k++;
			if (all.empty() && missing)
				missing->push_back(as_written);
		}
		if (changed && full != (as_written[0] == '/' ? as_written : dir + "/" + as_written))
			*changed = true;
		out.push_back(full);
	}
	fclose(f);
	return out;
}

// The discs a .m3u lists, wherever they are (beside it, in sub-folders, by a name the line has without its folder,
// Windows' "\" too), are that game's: they leave the shelf, and the .m3u shows how many discs it has.
void HidePlaylistDiscs(std::vector<GameInfo>& games)
{
	std::set<std::string> listed;
	for (GameInfo& g : games)
	{
		if (g.ext != ".m3u")
			continue;
		const std::vector<std::string> discs = ResolvePlaylist(g.path, nullptr, nullptr);
		for (const std::string& d : discs)
			listed.insert(Lower(d));
		if (discs.size() > 1)
			g.disc_count = int(discs.size());
	}
	if (listed.empty())
		return;
	size_t hidden = 0;
	std::vector<GameInfo> kept;
	kept.reserve(games.size());
	for (GameInfo& g : games)
		if (g.ext != ".m3u" && listed.count(Lower(g.path)))
			hidden++;
		else
			kept.push_back(std::move(g));
	games.swap(kept);
	if (hidden)
		OrbisLog("[games] %zu disc(s) listed by .m3u playlists: shown as their playlist's game", hidden);
}

// ---- disc sets: "Game (Disc 1).cue", "Game (Disc 2).cue" -> one game on the shelf -------------------------------
// The tags a disc's number is written with (No-Intro/Redump "(Disc 1)", TOSEC "(Disc 1 of 2)", "[Disc 1]", "(CD1)",
// "(Disco 1)", " - Disc 1"...). The name without it is the set's name.
bool DiscTag(const std::string& name, int* number, std::string* base)
{
	static const std::regex kTag(
		R"((\s*[\(\[]\s*(?:disc|disk|disco|cd)\s*([0-9]{1,2})(?:\s*(?:of|de|/)\s*[0-9]{1,2})?\s*[\)\]])|(\s*[-_]?\s*\b(?:disc|disk|disco|cd)\s*([0-9]{1,2})(?:\s*(?:of|de)\s*[0-9]{1,2})?\s*$))",
		std::regex::icase | std::regex::ECMAScript);
	std::smatch m;
	if (!std::regex_search(name, m, kTag))
		return false;
	const std::string num = m[2].matched ? m[2].str() : m[4].str();
	*number = atoi(num.c_str());
	std::string b = name.substr(0, size_t(m.position(0))) + name.substr(size_t(m.position(0) + m.length(0)));
	while (!b.empty() && (b.back() == ' ' || b.back() == '-' || b.back() == '_'))
		b.pop_back();
	*base = b;
	return *number >= 1 && !b.empty();
}

std::string StripDiscTag(const std::string& title)
{
	int n = 0;
	std::string base;
	return DiscTag(title, &n, &base) ? base : title;
}

// Disc sets in one folder become one entry: its .m3u (written to cache/discs/, the discs by their full paths, in
// order), named and shown as disc 1 is but without the tag. Discs a .m3u of the user's already lists never got here.
namespace
{
struct SetDisc
{
	int number;
	size_t index;
};

// The folder a disc set is looked for in: the disc's own, or the one above when the disc is alone in a folder of its
// own ("Game/Disc 1/x.cue", "Game/CD2/y.cue", "Game/A/z.cue").
std::string SetFolder(const GameInfo& g, const std::map<std::string, int>& images_in)
{
	const std::string dir = g.path.substr(0, g.path.find_last_of('/'));
	auto it = images_in.find(dir);
	const bool alone = it != images_in.end() && it->second == 1;
	const std::string folder = dir.substr(dir.find_last_of('/') + 1);
	int fn = 0;
	std::string fbase;
	if (dir.find('/') != std::string::npos && dir.find('/') != dir.size() - 1 &&
		(alone || DiscTag("folder " + folder, &fn, &fbase)))
		return dir.substr(0, dir.find_last_of('/'));
	return dir;
}

// The Saturn header's product number, title and "CD-n/m" field of a local disc, read once per file version
struct HeaderInfo
{
	bool ok = false;
	std::string product, title;
	int disc = 0, discs = 0;
};
HeaderInfo DiscHeader(const GameInfo& g)
{
	static std::mutex lock;
	static std::unordered_map<std::string, HeaderInfo> cache;
	HeaderInfo h;
	if (g.ext == ".chd" || g.ext == ".m3u")
		return h;
	struct stat st = {};
	if (stat(g.path.c_str(), &st) != 0)
		return h;
	char stamp[64];
	snprintf(stamp, sizeof(stamp), "\t%lld\t%lld", (long long)st.st_size, (long long)st.st_mtime);
	const std::string key = g.path + stamp;
	{
		std::lock_guard<std::mutex> l(lock);
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;
	}
	romheader::Info info;
	if (ReadRomHeader(g.path, g.system, &info))
	{
		h.ok = true;
		h.product = info.other;
		h.title = info.title;
		h.disc = info.disc;
		h.discs = info.discs;
	}
	std::lock_guard<std::mutex> l(lock);
	cache[key] = h;
	return h;
}

// "SAKURA TAISEN2_1" -> 1 and "SAKURA TAISEN2"; "Game-2" -> 2. Only with a '_' or '-' before the number ("Virtua
// Fighter 2" is a game, not a disc).
bool NumberSuffix(const std::string& name, int* number, std::string* base)
{
	size_t e = name.size();
	while (e > 0 && name[e - 1] == ' ')
		e--;
	size_t d = e;
	while (d > 0 && isdigit(uint8_t(name[d - 1])))
		d--;
	if (d == e || e - d > 2)
		return false;
	size_t sep = d;
	while (sep > 0 && name[sep - 1] == ' ')
		sep--;
	if (sep == 0 || (name[sep - 1] != '_' && name[sep - 1] != '-'))
		return false;
	std::string b = name.substr(0, sep - 1);
	while (!b.empty() && (b.back() == ' ' || b.back() == '_' || b.back() == '-'))
		b.pop_back();
	if (b.size() < 2)
		return false;
	*number = atoi(name.substr(d, e - d).c_str());
	*base = b;
	return *number >= 1;
}

// the longest start the names share, without a dangling separator: "Shining Force III A", "Shining Force III B" ->
// "Shining Force III"
std::string CommonStart(const std::vector<std::string>& names)
{
	if (names.empty())
		return "";
	std::string p = names[0];
	for (const std::string& n : names)
	{
		size_t i = 0;
		while (i < p.size() && i < n.size() && tolower(uint8_t(p[i])) == tolower(uint8_t(n[i])))
			i++;
		p.resize(i);
	}
	while (!p.empty() && (p.back() == ' ' || p.back() == '_' || p.back() == '-' || p.back() == '(' || p.back() == '[' ||
							 isdigit(uint8_t(p.back()))))
		p.pop_back();
	return p;
}
} // namespace

// Disc sets become one entry each: its .m3u (written to cache/discs/, the discs by their full paths, in order), named
// and shown as disc 1 is but without the disc tag. A set is found, in this order, by
//   1. the discs' names: "(Disc 1)", "(Disc 1 of 2)", "[Disc 1]", "(CD1)", "(Disco 1)", " - Disc 1"...;
//   2. the discs' own headers: the same product number and title, and "CD-1/3", "CD-2/3"... (any file names:
//      "GAME1.cue", "Game A.cue", "x_1.cue"), when every disc number is there once;
//   3. a "_1", "-2" at the end of names otherwise the same (a .chd, whose header isn't read here), numbered 1, 2, 3...
// The discs are in one folder, or each alone in a folder of its own under the same one. Discs a .m3u of the user's
// lists never got here (HidePlaylistDiscs).
void GroupDiscSets(std::vector<GameInfo>& games)
{
	std::map<std::string, int> images_in;
	for (const GameInfo& g : games)
		if (g.ext != ".m3u")
			images_in[g.path.substr(0, g.path.find_last_of('/'))]++;

	std::vector<bool> used(games.size(), false);
	std::vector<bool> drop(games.size(), false);
	std::vector<GameInfo> added;
	const std::string out_dir = OrbisDir("cache") + "/discs";

	// writes the set's list and adds its entry; false (nothing done) when the list can't be written
	auto emit = [&](std::vector<SetDisc> discs, std::string name, const char* how) {
		std::sort(discs.begin(), discs.end(), [&](const SetDisc& a, const SetDisc& b) {
			return a.number != b.number ? a.number < b.number : games[a.index].path < games[b.index].path;
		});
		std::vector<SetDisc> unique;
		for (const SetDisc& d : discs)
			if (unique.empty() || unique.back().number != d.number)
				unique.push_back(d);
		if (unique.size() < 2)
			return false;
		std::string text = "# SSYmirPS5: the discs of \"" + name + "\" (" + how + ")\n";
		for (const SetDisc& d : unique)
			text += games[d.index].path + "\n";
		for (char& c : name)
			if (c == '/' || c == '\\' || c == ':' || uint8_t(c) < 0x20)
				c = '_';
		const std::string first = games[unique[0].index].path;
		std::string fname = name;
		OrbisMkdirs(out_dir);
		for (int attempt = 0; attempt < 8; attempt++)
		{
			const std::string target = out_dir + "/" + fname + ".m3u";
			std::string old;
			if (FILE* f = fopen(target.c_str(), "rb"))
			{
				char buf[4096];
				size_t n;
				while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
					old.append(buf, n);
				fclose(f);
			}
			if (old.empty() || old == text || old.find(first + "\n") != std::string::npos)
			{
				if (old != text)
				{
					FILE* f = fopen(target.c_str(), "wb");
					if (!f)
						return false;
					fwrite(text.data(), 1, text.size(), f);
					fclose(f);
				}
				GameInfo set = games[unique[0].index];
				set.path = target;
				set.ext = ".m3u";
				set.file_base = fname;
				set.title = StripDiscTag(set.title);
				if (set.title == games[unique[0].index].file_base) // no name of its own: the set's ("GRANDIA1" -> "GRANDIA")
					set.title = name;
				set.disc_count = int(unique.size());
				added.push_back(set);
				for (const SetDisc& d : discs)
				{
					drop[d.index] = true;
					used[d.index] = true;
				}
				OrbisLog("[games] disc set \"%s\" (%s): %zu discs -> %s", fname.c_str(), how, unique.size(), target.c_str());
				return true;
			}
			// the name is another folder's set: another name
			fname = name + " (" + std::to_string(attempt + 2) + ")";
		}
		return false;
	};
	auto lower = [](std::string k) {
		for (char& c : k)
			c = char(tolower(uint8_t(c)));
		return k;
	};

	// 1. by the names' disc tags
	{
		std::map<std::string, std::vector<SetDisc>> sets;
		std::map<std::string, std::string> base_of;
		for (size_t i = 0; i < games.size(); i++)
		{
			const GameInfo& g = games[i];
			int n = 0;
			std::string base;
			if (g.ext == ".m3u" || !DiscTag(g.file_base, &n, &base))
				continue;
			const std::string key = lower(SetFolder(g, images_in) + "\n" + base);
			sets[key].push_back({n, i});
			base_of[key] = base;
		}
		for (auto& kv : sets)
			if (kv.second.size() >= 2)
				emit(kv.second, base_of[kv.first], "named Disc 1, Disc 2...");
	}

	// 2. by the discs' headers (local discs only: one sector read per disc, remembered while the file is the same)
	{
		std::map<std::string, std::vector<SetDisc>> sets;
		std::map<std::string, std::vector<std::string>> names_of;
		std::map<std::string, std::string> title_of;
		std::map<std::string, int> count_of;
		for (size_t i = 0; i < games.size(); i++)
		{
			const GameInfo& g = games[i];
			if (used[i] || g.ext == ".m3u")
				continue;
			const HeaderInfo h = DiscHeader(g);
			if (!h.ok || h.discs < 2 || h.disc < 1 || h.product.empty())
				continue;
			const std::string key = lower(SetFolder(g, images_in) + "\n" + h.product + "\n" + h.title);
			sets[key].push_back({h.disc, i});
			names_of[key].push_back(g.file_base);
			title_of[key] = h.title;
			count_of[key] = h.discs;
		}
		for (auto& kv : sets)
		{
			std::vector<SetDisc>& discs = kv.second;
			if (discs.size() < 2)
				continue;
			// each number once (two copies of a disc, a patched and an original one: not a set we can be sure of)
			std::set<int> numbers;
			bool clean = true;
			for (const SetDisc& d : discs)
				clean = clean && numbers.insert(d.number).second && d.number <= count_of[kv.first];
			if (!clean)
			{
				OrbisLog("[games] discs with the same header (%s) but a disc number twice: left as they are",
					title_of[kv.first].c_str());
				continue;
			}
			std::string name = CommonStart(names_of[kv.first]);
			if (name.size() < 3)
				name = title_of[kv.first];
			emit(discs, name, "the discs' headers say CD-1, CD-2...");
		}
	}

	// 3. by a "_1", "-2" at the end of the names
	{
		std::map<std::string, std::vector<SetDisc>> sets;
		std::map<std::string, std::string> base_of;
		for (size_t i = 0; i < games.size(); i++)
		{
			const GameInfo& g = games[i];
			int n = 0;
			std::string base;
			if (used[i] || g.ext == ".m3u" || !NumberSuffix(g.file_base, &n, &base))
				continue;
			const std::string key = lower(SetFolder(g, images_in) + "\n" + base);
			sets[key].push_back({n, i});
			base_of[key] = base;
		}
		for (auto& kv : sets)
		{
			std::vector<SetDisc>& discs = kv.second;
			if (discs.size() < 2)
				continue;
			// 1, 2, 3... each once: "Game_1", "Game_2"; not "Save_3", "Save_7"
			std::set<int> numbers;
			for (const SetDisc& d : discs)
				numbers.insert(d.number);
			if (numbers.size() != discs.size() || *numbers.begin() != 1 || *numbers.rbegin() != int(numbers.size()))
				continue;
			emit(discs, base_of[kv.first], "named _1, _2...");
		}
	}

	if (added.empty())
		return;
	std::vector<GameInfo> kept;
	for (size_t i = 0; i < games.size(); i++)
		if (!drop[i])
			kept.push_back(std::move(games[i]));
	for (GameInfo& g : added)
		kept.push_back(std::move(g));
	games.swap(kept);
}

std::vector<GameInfo> ScanGames(const ScanProgress& progress)
{
	PrepareFolders();
	std::vector<GameInfo> games;
	CrcCache& cache = SharedCrcCache();
	cache.Begin();
	for (const std::string& root : RomRoots())
		Walk(root, 0, root.rfind("/mnt/", 0) == 0, games, cache, progress);

	size_t identified = 0;
	for (GameInfo& g : games)
	{
		if (progress)
			progress(1, identified++, games.size());
		NameGame(cache, g);
	}
	cache.Save(true);
	HidePlaylistDiscs(games);
	GroupDiscSets(games);

	// in title order, a leading "The " left out ("The King of Fighters '95" among the K games: the shelf's letter filter)
	auto sort_key = [](const std::string& t) {
		size_t i = 0;
		while (i < t.size() && (t[i] == ' ' || t[i] == '"' || t[i] == '\'' || t[i] == '~'))
			i++;
		if (t.size() >= i + 4 && strncasecmp(t.c_str() + i, "the ", 4) == 0)
			i += 4;
		return t.c_str() + i;
	};
	std::sort(games.begin(), games.end(), [&](const GameInfo& a, const GameInfo& b) {
		const int c = strcasecmp(sort_key(a.title), sort_key(b.title));
		if (c != 0)
			return c < 0;
		if (a.system != b.system)
			return a.system < b.system;
		return a.path < b.path;
	});
	OrbisLog("[games] %zu ROM(s); table of %zu names", games.size(), gamedb::Count());
	// each game once per scan is a lot of boot.log for a big library: the first 200
	constexpr size_t kLogged = 200;
	for (size_t i = 0; i < games.size() && i < kLogged; i++)
	{
		const GameInfo& g = games[i];
		OrbisLog("[games]   %s [%s] -> \"%s\"%s%s%s%s%s", g.path.c_str(), Info(g.system).id, g.nointro.c_str(),
			g.name_by_crc ? " (by CRC)" : "", g.crc_name.empty() ? "" : " (dump: ", g.crc_name.c_str(), g.crc_name.empty() ? "" : ")",
			g.header_title.empty() ? "" : (" (header: " + g.header_title + ")").c_str());
	}
	if (games.size() > kLogged)
		OrbisLog("[games]   ... and %zu more", games.size() - kLogged);
	return games;
}
bool CachedRomCrc32(const std::string& path, uint32_t* whole, uint32_t* body, bool read)
{
	CrcCache& cache = SharedCrcCache();
	cache.Ensure();
	return CachedCrc(cache, path, whole, body, read);
}

GameInfo DescribeGame(const std::string& path, const System* system)
{
	GameInfo g;
	g.path = path;
	const size_t slash = path.find_last_of('/');
	const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
	const std::string ext = ExtOf(name);
	g.file_base = name.substr(0, name.size() - ext.size());
	g.ext = ext;
	g.on_usb = path.rfind("/mnt/", 0) == 0;
	if (system)
		g.system = *system;
	else if (IsArchiveExt(ext))
	{
		if (!ArchiveSystem(path, &g.system))
			g.system = System::Saturn;
	}
	else if (!SystemForExt(ext, &g.system))
		g.system = System::Saturn;
	CrcCache& cache = SharedCrcCache();
	cache.Ensure();
	NameGame(cache, g);
	cache.Save(false); // only what this game added: the scan drops the entries of files that are gone
	return g;
}

namespace
{
// The first FILE of a .cue sheet, as a path beside it.
std::string CueFirstTrack(const std::string& cue)
{
	FILE* f = fopen(cue.c_str(), "r");
	if (!f)
		return "";
	char line[1024];
	std::string file;
	while (file.empty() && fgets(line, sizeof(line), f))
	{
		const char* p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (strncasecmp(p, "FILE", 4) != 0)
			continue;
		p += 4;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '"')
		{
			const char* q = strchr(p + 1, '"');
			if (q)
				file.assign(p + 1, q);
		}
		else
		{
			const char* e = p;
			while (*e && *e != ' ' && *e != '\t' && *e != '\r' && *e != '\n')
				e++;
			file.assign(p, e);
		}
	}
	fclose(f);
	const size_t s = file.find_last_of("/\\");
	if (s != std::string::npos)
		file = file.substr(s + 1);
	return file.empty() ? "" : cue.substr(0, cue.find_last_of('/') + 1) + file;
}

// The data track of a playlist's first disc (a .cue's first FILE, an .iso as it is), as a path beside the playlist.
std::string M3uFirstDiscTrack(const std::string& m3u)
{
	const std::vector<std::string> discs = ResolvePlaylist(m3u, nullptr, nullptr);
	if (discs.empty())
		return "";
	const std::string disc = discs[0];
	const std::string ext = ExtOf(disc);
	if (ext == ".cue")
		return CueFirstTrack(disc);
	if (ext == ".iso" || ext == ".chd")
		return disc;
	return "";
}

// A .chd's system area: the first hunk (a few CD frames of 2352 + 96 bytes; a MODE1/2048 track's frames start with its
// 2048 bytes), searched for the header's "SEGA SEGASATURN" in its first frames. libchdr is the core's.
bool ReadChdHeader(const std::string& path, romheader::Info* info)
{
	chd_file* chd = nullptr;
	if (chd_open(path.c_str(), CHD_OPEN_READ, nullptr, &chd) != CHDERR_NONE || !chd)
		return false;
	bool ok = false;
	const chd_header* h = chd_get_header(chd);
	if (h && h->hunkbytes >= 2448 && h->hunkbytes <= (1u << 20))
	{
		std::vector<uint8_t> hunk(h->hunkbytes);
		if (chd_read(chd, 0, hunk.data()) == CHDERR_NONE)
		{
			const size_t span = std::min<size_t>(hunk.size(), 2448 * 2) - 256;
			for (size_t at = 0; at <= span && !ok; at++)
				if (hunk[at] == 'S' && memcmp(&hunk[at], "SEGA SEGASATURN", 15) == 0)
					ok = romheader::Parse(&hunk[at], info);
		}
	}
	chd_close(chd);
	return ok;
}
} // namespace

bool ReadRomHeader(const std::string& path, System system, romheader::Info* info)
{
	const std::string ext = ExtOf(path);
	if (system == System::Saturn)
	{
		// a disc: its first data track (a .chd: its first hunk, through libchdr)
		const std::string track = ext == ".cue" ? CueFirstTrack(path)
			: ext == ".iso" ? path
			: ext == ".ccd" ? path.substr(0, path.size() - 4) + ".img"
			: ext == ".mds" ? path.substr(0, path.size() - 4) + ".mdf"
			: ext == ".m3u" ? M3uFirstDiscTrack(path)
			: std::string();
		if (ExtOf(track) == ".chd" || ext == ".chd")
			return ReadChdHeader(ext == ".chd" ? path : track, info);
		FILE* f = track.empty() ? nullptr : fopen(track.c_str(), "rb");
		if (!f)
			return false;
		const bool ok = romheader::ReadDisc(
			[f](uint64_t at, uint8_t* out, size_t n) { return fseek(f, long(at), SEEK_SET) == 0 && fread(out, 1, n, f) == n; }, info);
		fclose(f);
		return ok;
	}
	return false; // a Saturn game is always a disc
	if (IsArchiveExt(ext))
	{
		std::string inner;
		std::vector<uint8_t> data;
		if (!ReadFromArchive(path, &inner, &data))
			return false;
		return romheader::ReadCart(
			[&data](uint64_t at, uint8_t* out, size_t n) {
				if (at + n > data.size())
					return false;
				memcpy(out, data.data() + at, n);
				return true;
			},
			data.size(), info);
	}
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	fseek(f, 0, SEEK_END);
	const uint64_t total = uint64_t(ftell(f));
	const bool ok = romheader::ReadCart(
		[f](uint64_t at, uint8_t* out, size_t n) { return fseek(f, long(at), SEEK_SET) == 0 && fread(out, 1, n, f) == n; }, total,
		info);
	fclose(f);
	return ok;
}
} // namespace fe
