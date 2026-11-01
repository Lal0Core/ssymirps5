// SSYmirPS5: the /data/ssymirps5 folder layout and the boot log (include-orbis/OrbisPaths.h).
//
// stat(), not access(): PS5SX2 found (vk-285-46) that a sandboxed process can get EPERM from access() on
// files that exist, while stat() answers on both sides of a jailbreak. The same rule is kept here.
//
// SPDX-License-Identifier: MIT

#include "OrbisPaths.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <mutex>
#include <vector>

namespace
{
std::mutex s_log_mutex;
FILE* s_log = nullptr;
bool s_enabled = true; // the "Debug logs" setting
std::string s_name;    // the log OrbisLogOpen opened (or would have), to open again when logs are turned on

// The "Debug logs" setting, read here (not through fe::Settings: the installer and the helper have none): off unless
// ssymirps5.ini says debug_logs=1. The host tests turn the default on with YMIR_LOGS_DEFAULT=1 (they read boot.log).
bool IniSaysLogsOff()
{
	const char* def = getenv("YMIR_LOGS_DEFAULT");
	bool off = !(def && atoi(def) != 0);
	FILE* f = fopen((OrbisRoot() + "/ssymirps5.ini").c_str(), "r");
	if (!f)
		return off;
	char line[512];
	while (fgets(line, sizeof(line), f))
		if (strncmp(line, "debug_logs=", 11) == 0)
			off = atoi(line + 11) == 0;
	fclose(f);
	return off;
}

// 3.9.6: games/ is the games folder (roms/ is not used any more)
const char* const kSubdirs[] = {"games", "logs", "covers", "saves", "states", "bios", "cache", "cheats"};
} // namespace

const std::string& OrbisRoot()
{
	static const std::string root = [] {
		const char* env = getenv("SSYMIRPS5_ROOT");
		return std::string(env && *env ? env : ORBIS_ROOT_DEFAULT);
	}();
	return root;
}

std::string OrbisDir(const char* sub)
{
	return OrbisRoot() + "/" + sub;
}

bool OrbisIsDir(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool OrbisIsFile(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool OrbisMkdirs(const std::string& path)
{
	if (path.empty())
		return false;
	std::string cur;
	size_t pos = 0;
	while (pos != std::string::npos)
	{
		pos = path.find('/', pos + 1);
		cur = path.substr(0, pos);
		if (cur.empty() || OrbisIsDir(cur))
			continue;
		if (mkdir(cur.c_str(), 0777) != 0 && errno != EEXIST)
			return false;
	}
	return OrbisIsDir(path);
}

bool OrbisPathsInit()
{
	if (!OrbisMkdirs(OrbisRoot()))
		return false;
	for (const char* sub : kSubdirs)
		OrbisMkdirs(OrbisDir(sub));
	return true;
}

// <root>/paths.txt: folders of the user's own, one per line, "games=<folder>" or "bios=<folder>" (# starts a comment).
// Written with an explanation the first time the app starts; read again at every scan.
static const char kPathsTemplate[] =
	"# SSYmirPS5: more folders for your games and your BIOS, one per line.\n"
	"# The app always looks in these (on the console and on every USB drive, N = 0..7):\n"
	"#   /data/ssymirps5/games   /data/ssymirps5/bios\n"
	"#   /mnt/usbN/ssymirps5/games   /mnt/usbN/ssymirps5/bios\n"
	"# Add others below (remove the # at the start of a line):\n"
	"#games=/mnt/usb0/Saturn\n"
	"#games=/data/juegos/saturn\n"
	"#bios=/mnt/usb0/BIOS\n";

// 3.9.7: the comment lines older versions wrote, naming roms/ (no longer used): made the current ones, the rest of
// the file (your own lines) as it is
static void UpdatePathsComments(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return;
	std::string text;
	char buf[4096];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0 && text.size() < (1u << 20))
		text.append(buf, n);
	fclose(f);
	const struct
	{
		const char* old_line;
		const char* new_line;
	} kLines[] = {
		{"#   /data/ssymirps5/roms   /data/ssymirps5/games   /data/ssymirps5/bios", "#   /data/ssymirps5/games   /data/ssymirps5/bios"},
		{"#   /mnt/usbN/ssymirps5/roms   /mnt/usbN/ssymirps5/games   /mnt/usbN/ssymirps5/bios",
			"#   /mnt/usbN/ssymirps5/games   /mnt/usbN/ssymirps5/bios"},
	};
	bool changed = false;
	for (const auto& l : kLines)
	{
		const size_t at = text.find(l.old_line);
		if (at != std::string::npos)
		{
			text.replace(at, strlen(l.old_line), l.new_line);
			changed = true;
		}
	}
	if (!changed)
		return;
	const std::string tmp = path + ".tmp";
	if (FILE* o = fopen(tmp.c_str(), "wb"))
	{
		const bool ok = fwrite(text.data(), 1, text.size(), o) == text.size();
		if (fclose(o) == 0 && ok)
			rename(tmp.c_str(), path.c_str());
		else
			remove(tmp.c_str());
	}
}

static std::vector<std::string> UserPaths(const char* kind)
{
	std::vector<std::string> out;
	const std::string path = OrbisRoot() + "/paths.txt";
	{
		static bool updated = false;
		if (!updated)
		{
			updated = true;
			UpdatePathsComments(path);
		}
	}
	FILE* f = fopen(path.c_str(), "r");
	if (!f)
	{
		if ((f = fopen(path.c_str(), "w")) != nullptr)
		{
			fputs(kPathsTemplate, f);
			fclose(f);
		}
		return out;
	}
	const size_t klen = strlen(kind);
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		line[strcspn(line, "\r\n")] = 0;
		char* p = line;
		while (*p == ' ' || *p == '\t')
			p++;
		if (*p == '#' || strncmp(p, kind, klen) != 0 || p[klen] != '=')
			continue;
		std::string dir = p + klen + 1;
		while (!dir.empty() && (dir.back() == ' ' || dir.back() == '\t' || dir.back() == '/'))
			dir.pop_back();
		while (!dir.empty() && (dir.front() == ' ' || dir.front() == '\t'))
			dir.erase(dir.begin());
		if (!dir.empty() && dir[0] == '/')
			out.push_back(dir);
	}
	fclose(f);
	return out;
}

// the app's folder on the console and on every drive: <base>/<sub> for /data/ssymirps5, /mnt/usbN/ssymirps5, /mnt/extN/ssymirps5
static void StandardDirs(const char* sub, std::vector<std::string>& out)
{
	out.push_back(OrbisDir(sub));
	char buf[64];
	for (int i = 0; i < 8; i++)
	{
		snprintf(buf, sizeof(buf), "/mnt/usb%d/ssymirps5/%s", i, sub);
		out.push_back(buf);
	}
	for (int i = 0; i < 2; i++)
	{
		snprintf(buf, sizeof(buf), "/mnt/ext%d/ssymirps5/%s", i, sub);
		out.push_back(buf);
	}
}

// The folders that exist, without repeats or a folder inside another one of the list (its games would show twice).
static std::vector<std::string> Existing(const std::vector<std::string>& dirs)
{
	std::vector<std::string> out;
	for (const std::string& d : dirs)
	{
		if (!OrbisIsDir(d))
			continue;
		bool covered = false;
		for (const std::string& o : out)
			if (d == o || (d.size() > o.size() && d.compare(0, o.size(), o) == 0 && d[o.size()] == '/'))
				covered = true;
		if (covered)
			continue;
		// a folder that holds one already listed replaces it
		for (size_t i = 0; i < out.size();)
			if (out[i].size() > d.size() && out[i].compare(0, d.size(), d) == 0 && out[i][d.size()] == '/')
				out.erase(out.begin() + long(i));
			else
				i++;
		out.push_back(d);
	}
	return out;
}

std::vector<std::string> OrbisRomRoots()
{
	std::vector<std::string> dirs;
	StandardDirs("games", dirs);
	for (const std::string& d : UserPaths("games"))
		dirs.push_back(d);
	return Existing(dirs);
}

std::vector<std::string> OrbisBiosDirs()
{
	std::vector<std::string> dirs;
	StandardDirs("bios", dirs);
	for (const std::string& d : UserPaths("bios"))
		dirs.push_back(d);
	std::vector<std::string> out;
	for (const std::string& d : dirs)
		if (OrbisIsDir(d) && std::find(out.begin(), out.end(), d) == out.end())
			out.push_back(d);
	return out;
}

// Lines logged before the log file is open (the native app logs its jailbreak before /data is visible).
static std::vector<std::string>& EarlyLines()
{
	static std::vector<std::string>* lines = new std::vector<std::string>();
	return *lines;
}

void OrbisLogOpen(const char* name)
{
	std::lock_guard<std::mutex> lock(s_log_mutex);
	if (s_log)
		return;
	s_name = name;
	if (IniSaysLogsOff())
	{
		// off: nothing written, and the files of earlier runs are not rotated away
		s_enabled = false;
		EarlyLines().clear();
		return;
	}
	s_enabled = true;
	const std::string dir = OrbisDir("logs");
	if (!OrbisIsDir(dir))
		return;
	const std::string cur = dir + "/" + name + ".log";
	const std::string prev = dir + "/" + name + ".prev.log";
	rename(cur.c_str(), prev.c_str()); // keep the run before, like PS5SX2's logs
	s_log = fopen(cur.c_str(), "w");
	if (s_log)
	{
		for (const std::string& line : EarlyLines())
		{
			fputs(line.c_str(), s_log);
			printf("[ssymirps5 %s", line.c_str() + 1); // held back until the setting could be read
		}
		fflush(stdout);
		fflush(s_log);
		EarlyLines().clear();
	}
}

int OrbisLogFd()
{
	// the open boot.log's fd, for the crash handler's signal-safe write(); -1 before it is open
	return s_log ? fileno(s_log) : -1;
}

void OrbisLog(const char* fmt, ...)
{
	char line[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);

	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	const double t = ts.tv_sec + ts.tv_nsec / 1e9;

	std::lock_guard<std::mutex> lock(s_log_mutex);
	if (!s_enabled)
		return;
	if (s_log || !s_name.empty())
	{
		printf("[ssymirps5 %10.3f] %s\n", t, line);
		fflush(stdout);
	}
	if (s_log)
	{
		fprintf(s_log, "[%10.3f] %s\n", t, line);
		fflush(s_log); // a crash must not lose the lines before it
	}
	else if (EarlyLines().size() < 4000)
	{
		char stamped[1100];
		snprintf(stamped, sizeof(stamped), "[%10.3f] %s\n", t, line);
		EarlyLines().push_back(stamped);
	}
}

void OrbisLogSetEnabled(bool on)
{
	std::lock_guard<std::mutex> lock(s_log_mutex);
	if (on == s_enabled)
		return;
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	const double t = ts.tv_sec + ts.tv_nsec / 1e9;
	if (!on)
	{
		printf("[ssymirps5 %10.3f] [log] debug logs turned off\n", t);
		fflush(stdout);
		if (s_log)
		{
			fprintf(s_log, "[%10.3f] [log] debug logs turned off\n", t);
			fclose(s_log);
			s_log = nullptr;
		}
		EarlyLines().clear();
		s_enabled = false;
		return;
	}
	s_enabled = true;
	// the same file again, added to (this run's lines before the switch stay in it)
	const std::string dir = OrbisDir("logs");
	if (!s_name.empty() && OrbisIsDir(dir))
		s_log = fopen((dir + "/" + s_name + ".log").c_str(), "a");
	printf("[ssymirps5 %10.3f] [log] debug logs turned on\n", t);
	fflush(stdout);
	if (s_log)
	{
		fprintf(s_log, "[%10.3f] [log] debug logs turned on\n", t);
		fflush(s_log);
	}
}

void OrbisLogRefresh()
{
	bool open;
	{
		std::lock_guard<std::mutex> lock(s_log_mutex);
		open = !s_name.empty();
	}
	if (open)
		OrbisLogSetEnabled(!IniSaysLogsOff());
}

bool OrbisLogEnabled()
{
	std::lock_guard<std::mutex> lock(s_log_mutex);
	return s_enabled;
}

void OrbisLogClose()
{
	std::lock_guard<std::mutex> lock(s_log_mutex);
	if (s_log)
	{
		fclose(s_log);
		s_log = nullptr;
	}
}
