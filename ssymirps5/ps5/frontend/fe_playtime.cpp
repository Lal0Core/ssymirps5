// SSYmirPS5 frontend: how long each game was played (fe_playtime.h).
// SPDX-License-Identifier: MIT

#include "fe_playtime.h"

#include "fe_i18n.h"
#include "OrbisPaths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>
#include <unordered_map>

namespace fe
{
namespace playtime
{
namespace
{
struct Entry
{
	double secs = 0;
	long long last = 0;
};

std::mutex g_mutex;
std::unordered_map<std::string, Entry> g_games;
bool g_loaded = false;

std::string FilePath()
{
	return OrbisRoot() + "/playtime.txt";
}

void Load()
{
	if (g_loaded)
		return;
	g_loaded = true;
	FILE* f = fopen(FilePath().c_str(), "r");
	if (!f)
		return;
	char line[1200];
	while (fgets(line, sizeof(line), f))
	{
		// "<seconds>\t<last>\t<path>": a line that is not is left out
		char* t1 = strchr(line, '\t');
		char* t2 = t1 ? strchr(t1 + 1, '\t') : nullptr;
		if (!t2)
			continue;
		std::string path(t2 + 1);
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
			path.pop_back();
		const double secs = atof(line);
		if (path.empty() || !(secs >= 0))
			continue;
		g_games[path] = {secs, atoll(t1 + 1)};
	}
	fclose(f);
}

// written whole to playtime.txt.part, then put in place: a cut write leaves the old file
void Save()
{
	const std::string path = FilePath(), part = path + ".part";
	FILE* f = fopen(part.c_str(), "w");
	if (!f)
		return;
	for (const auto& [game, e] : g_games)
		fprintf(f, "%.0f\t%lld\t%s\n", e.secs, e.last, game.c_str());
	const bool ok = fflush(f) == 0;
	fclose(f);
	if (ok)
		rename(part.c_str(), path.c_str());
	else
		remove(part.c_str());
}
} // namespace

void Add(const std::string& path, double secs)
{
	if (path.empty() || !(secs > 0))
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	Load();
	Entry& e = g_games[path];
	e.secs += secs;
	e.last = (long long)time(nullptr);
	Save();
}

long long Seconds(const std::string& path)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	Load();
	auto it = g_games.find(path);
	return it == g_games.end() ? 0 : (long long)it->second.secs;
}

long long Last(const std::string& path)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	Load();
	auto it = g_games.find(path);
	return it == g_games.end() ? 0 : it->second.last;
}

std::string Format(long long secs)
{
	if (secs <= 0)
		return "";
	char text[96];
	if (secs < 60)
		return T("less than 1 min");
	const long long mins = secs / 60;
	if (mins < 60)
		snprintf(text, sizeof(text), T("%lld min"), mins);
	else
		snprintf(text, sizeof(text), T("%lld h %lld min"), mins / 60, mins % 60);
	return text;
}
} // namespace playtime
} // namespace fe
