// SSYmirPS5 frontend: the game log (fe_gamelog.h).
// SPDX-License-Identifier: MIT

#include "fe_gamelog.h"

#include "OrbisPaths.h"
#include "ProsperoCrash.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <vector>

namespace fe
{
namespace gamelog
{
namespace
{
std::mutex g_lock;
FILE* g_file = nullptr;
double g_start = 0;
int g_core_lines = 0;

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

void Write(const char* text)
{
	if (!g_file)
		return;
	fprintf(g_file, "[%8.3f] %s\n", Now() - g_start, text);
	fflush(g_file);
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(uint8_t(c)));
	return s;
}

std::string Ext(const std::string& p)
{
	const size_t dot = p.find_last_of('.');
	const size_t slash = p.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(p.substr(dot));
}

std::string DirOf(const std::string& p)
{
	const size_t slash = p.find_last_of('/');
	return slash == std::string::npos ? "." : p.substr(0, slash);
}

// "size 123456789 bytes" or "MISSING"
std::string FileState(const std::string& path)
{
	struct stat st = {};
	if (stat(path.c_str(), &st) != 0)
		return "MISSING (" + std::string(strerror(errno)) + ")";
	if (!S_ISREG(st.st_mode))
		return "not a file";
	char buf[64];
	snprintf(buf, sizeof(buf), "%lld bytes", (long long)st.st_size);
	return buf;
}

// a file in `dir` by name, ignoring case (a .cue written on Windows names its .bin in another case)
std::string FindNoCase(const std::string& dir, const std::string& name, bool* exact)
{
	*exact = true;
	struct stat st = {};
	if (stat((dir + "/" + name).c_str(), &st) == 0)
		return dir + "/" + name;
	*exact = false;
	DIR* d = opendir(dir.c_str());
	if (!d)
		return "";
	std::string found;
	const std::string want = Lower(name);
	while (dirent* e = readdir(d))
		if (Lower(e->d_name) == want)
		{
			found = dir + "/" + e->d_name;
			break;
		}
	closedir(d);
	return found;
}

std::vector<std::string> ReadLines(const std::string& path, size_t max_lines)
{
	std::vector<std::string> out;
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return out;
	char line[2048];
	while (out.size() < max_lines && fgets(line, sizeof(line), f))
	{
		line[strcspn(line, "\r\n")] = 0;
		out.push_back(line);
	}
	fclose(f);
	return out;
}

// the text between the first pair of quotes, else the second word
std::string QuotedOrWord(const std::string& line, size_t from)
{
	const size_t q1 = line.find('"', from);
	if (q1 != std::string::npos)
	{
		const size_t q2 = line.find('"', q1 + 1);
		if (q2 != std::string::npos)
			return line.substr(q1 + 1, q2 - q1 - 1);
	}
	size_t s = line.find_first_not_of(" \t", from);
	size_t e = s == std::string::npos ? s : line.find_first_of(" \t", s);
	return s == std::string::npos ? "" : line.substr(s, e == std::string::npos ? std::string::npos : e - s);
}

// The system area at the start of a data track: "SEGA SEGASATURN" at 0 (2048-byte sectors) or 16 (raw 2352).
void DescribeHeader(const std::string& data_file, const char* what)
{
	FILE* f = fopen(data_file.c_str(), "rb");
	if (!f)
	{
		Line("  header of %s: can't open it (%s)", what, strerror(errno));
		return;
	}
	uint8_t buf[2352 + 256] = {};
	const size_t n = fread(buf, 1, sizeof(buf), f);
	fclose(f);
	int at = -1;
	for (int off : {0, 16})
		if (n >= size_t(off) + 256 && memcmp(buf + off, "SEGA SEGASATURN", 15) == 0)
		{
			at = off;
			break;
		}
	if (at < 0)
	{
		char hex[3 * 32 + 1] = {};
		for (int i = 0; i < 32 && size_t(i) < n; i++)
			snprintf(hex + i * 3, 4, "%02X ", buf[i]);
		Line("  header of %s: NO Saturn system area at 0 or 16 (first bytes: %s) -- not a Saturn data track, or "
			 "an audio track listed first",
			what, hex);
		return;
	}
	auto field = [&](int off, int len) {
		std::string s(reinterpret_cast<const char*>(buf + at + off), size_t(len));
		for (char& c : s)
			if (uint8_t(c) < 0x20 || uint8_t(c) > 0x7E)
				c = '.';
		while (!s.empty() && (s.back() == ' ' || s.back() == '.'))
			s.pop_back();
		return s;
	};
	Line("  header of %s (%s sectors): maker \"%s\", product \"%s\", version \"%s\", date \"%s\", disc \"%s\", "
		 "areas \"%s\", peripherals \"%s\"",
		what, at == 0 ? "2048-byte" : "raw 2352-byte", field(0x10, 16).c_str(), field(0x20, 10).c_str(),
		field(0x2A, 6).c_str(), field(0x30, 8).c_str(), field(0x38, 8).c_str(), field(0x40, 10).c_str(),
		field(0x50, 16).c_str());
	Line("  title \"%s\"", field(0x60, 112).c_str());
}

void DescribeCue(const std::string& cue)
{
	const std::string dir = DirOf(cue);
	const std::vector<std::string> lines = ReadLines(cue, 400);
	if (lines.empty())
	{
		Line("  .cue: empty or unreadable");
		return;
	}
	int files = 0, tracks = 0, audio = 0;
	std::string first_data;
	std::string cur_file;
	for (const std::string& raw : lines)
	{
		std::string l = raw;
		l.erase(0, l.find_first_not_of(" \t"));
		const std::string up = Lower(l);
		if (up.compare(0, 5, "file ") == 0)
		{
			files++;
			const std::string name = QuotedOrWord(l, 5);
			bool exact = true;
			const std::string found = FindNoCase(dir, name, &exact);
			cur_file = found;
			Line("  .cue FILE \"%s\": %s%s", name.c_str(), found.empty() ? "MISSING" : FileState(found).c_str(),
				!found.empty() && !exact ? " (found with another upper/lower case: rename it to match the .cue)" : "");
			const std::string type = Lower(l.substr(l.find_last_of(' ') + 1));
			if (type != "binary" && type != "motorola")
				Line("    its type is \"%s\": Ymir reads BINARY files (WAVE/MP3 tracks are not supported)", type.c_str());
		}
		else if (up.compare(0, 6, "track ") == 0)
		{
			tracks++;
			const bool is_audio = up.find("audio") != std::string::npos;
			audio += is_audio;
			if (tracks <= 3 || !is_audio)
				Line("  .cue %s", l.c_str());
			if (!is_audio && first_data.empty())
				first_data = cur_file;
		}
	}
	Line("  .cue: %d file(s), %d track(s) (%d audio)", files, tracks, audio);
	if (tracks == 0)
		Line("  WARNING: the .cue lists no TRACK");
	if (!first_data.empty())
		DescribeHeader(first_data, "the first data track");
	else
		Line("  WARNING: no data track (MODE1/MODE2) in the .cue");
}
} // namespace

bool Active()
{
	std::lock_guard<std::mutex> lock(g_lock);
	return g_file != nullptr;
}

double Seconds()
{
	return g_file ? Now() - g_start : 0;
}

void Line(const char* fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	std::lock_guard<std::mutex> lock(g_lock);
	Write(buf);
}

void CoreMessage(int level, const char* text)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_file)
		return;
	static const char* const kLevel[] = {"debug", "info", "WARN", "ERROR"};
	const bool important = level >= 2;
	// everything for the first minute (and 4000 lines), then the warnings and errors (another 2000)
	if (!important && (Now() - g_start > 60.0 || g_core_lines >= 4000))
		return;
	if (g_core_lines >= 6000)
		return;
	g_core_lines++;
	char buf[2048];
	snprintf(buf, sizeof(buf), "core %s: %s", kLevel[level >= 0 && level < 4 ? level : 3], text);
	Write(buf);
	if (g_core_lines == 4000)
		Write("(core messages: only warnings and errors from here)");
}

void Begin(const std::string& game_path)
{
	{
		std::lock_guard<std::mutex> lock(g_lock);
		if (g_file)
		{
			Write("== (a new game starts)");
			fclose(g_file);
			g_file = nullptr;
		}
		const std::string dir = OrbisDir("logs");
		OrbisMkdirs(dir);
		const std::string path = dir + "/game.log";
		rename(path.c_str(), (dir + "/game.prev.log").c_str());
		g_file = fopen(path.c_str(), "w");
		g_start = Now();
		g_core_lines = 0;
		if (!g_file)
			return;
		crashlog::SetExtraFd(fileno(g_file));
		char when[64] = "?";
		const time_t t = time(nullptr);
		struct tm tmv = {};
		if (localtime_r(&t, &tmv))
			strftime(when, sizeof(when), "%Y-%m-%d %H:%M:%S", &tmv);
		char head[1024];
		snprintf(head, sizeof(head), "== SSYmirPS5 %s (core Ymir %s, built %s %s), title %s, started %s", SSYMIRPS5_VERSION,
			YMIR_CORE_VERSION, __DATE__, __TIME__, YMIR_TITLE_ID, when);
		Write(head);
		snprintf(head, sizeof(head), "== game: %s", game_path.c_str());
		Write(head);
	}
	DescribeDisc(game_path);
}

void End(const char* why)
{
	std::lock_guard<std::mutex> lock(g_lock);
	if (!g_file)
		return;
	char buf[512];
	snprintf(buf, sizeof(buf), "== end: %s", why ? why : "");
	Write(buf);
	crashlog::SetExtraFd(-1);
	fclose(g_file);
	g_file = nullptr;
}

void DescribeDisc(const std::string& path)
{
	const std::string ext = Ext(path);
	Line("disc: %s, %s", ext.empty() ? "(no extension)" : ext.c_str(), FileState(path).c_str());
	if (path.size() >= 255)
		Line("  WARNING: the path is %zu characters long (254 at most)", path.size());
	const std::string dir = DirOf(path);
	if (ext == ".cue")
		DescribeCue(path);
	else if (ext == ".m3u")
	{
		const std::vector<std::string> lines = ReadLines(path, 50);
		int discs = 0;
		for (std::string l : lines)
		{
			l.erase(0, l.find_first_not_of(" \t"));
			if (l.empty() || l[0] == '#')
				continue;
			for (char& c : l)
				if (c == '\\')
					c = '/';
			discs++;
			bool exact = true;
			std::string found;
			if (l[0] == '/')
			{
				struct stat st = {};
				if (stat(l.c_str(), &st) == 0)
					found = l;
			}
			else
			{
				// a disc in a sub-folder: the folder as it is, the file by name ignoring case
				const size_t sl = l.find_last_of('/');
				found = sl == std::string::npos ? FindNoCase(dir, l, &exact)
												: FindNoCase(dir + "/" + l.substr(0, sl), l.substr(sl + 1), &exact);
			}
			Line("  .m3u disc %d \"%s\": %s", discs, l.c_str(), found.empty() ? "not at that path (looked for in the sub-folders: see \"playlist disc\" below)" : FileState(found).c_str());
			if (discs == 1 && !found.empty())
			{
				if (Ext(found) == ".cue")
					DescribeCue(found);
				else
					DescribeHeader(found, "disc 1");
			}
		}
		if (discs == 0)
			Line("  WARNING: the .m3u lists no disc");
	}
	else if (ext == ".ccd" || ext == ".mds")
	{
		const std::string base = path.substr(0, path.size() - 4);
		const char* const others[] = {ext == ".ccd" ? ".img" : ".mdf", ".sub"};
		for (const char* o : others)
		{
			bool exact = true;
			const std::string name = base.substr(base.find_last_of('/') + 1) + o;
			const std::string found = FindNoCase(dir, name, &exact);
			Line("  %s: %s", name.c_str(), found.empty() ? (strcmp(o, ".sub") == 0 ? "not there (optional)" : "MISSING")
														   : FileState(found).c_str());
			if (strcmp(o, ".sub") != 0 && !found.empty())
				DescribeHeader(found, o);
		}
	}
	else if (ext == ".iso" || ext == ".bin" || ext == ".img")
		DescribeHeader(path, "the image");
	else if (ext == ".chd")
		Line("  .chd: compressed (its header is read by the core; see the core's lines below)");
	else
		Line("  WARNING: %s is not a disc format SSYmirPS5 lists", ext.c_str());
}

void DescribeBios(const std::string& dir)
{
	if (dir.empty())
		return;
	for (const char* sub : {"", "/Saturn", "/cdb"})
	{
		const std::string d = dir + sub;
		DIR* dd = opendir(d.c_str());
		if (!dd)
			continue;
		std::vector<std::string> names;
		while (dirent* e = readdir(dd))
			if (e->d_name[0] != '.')
				names.push_back(e->d_name);
		closedir(dd);
		std::sort(names.begin(), names.end());
		for (const std::string& n : names)
		{
			struct stat st = {};
			if (stat((d + "/" + n).c_str(), &st) != 0 || !S_ISREG(st.st_mode))
				continue;
			const bool bios_size = st.st_size == 512 * 1024;
			Line("  bios file %s/%s: %lld bytes%s", d.c_str(), n.c_str(), (long long)st.st_size,
				strcmp(sub, "/cdb") == 0 || bios_size ? "" : " (a Saturn BIOS is 524288 bytes)");
		}
	}
}
} // namespace gamelog
} // namespace fe
