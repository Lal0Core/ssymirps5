// SSYmirPS5 frontend: the games played last (fe_recent.h).
// SPDX-License-Identifier: MIT

#include "fe_recent.h"

#include "fe_i18n.h"
#include "OrbisPaths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <mutex>

namespace fe
{
namespace recent
{
namespace
{
std::mutex g_mutex;
std::vector<Entry> g_list;
bool g_loaded = false;

std::string FilePath()
{
	return OrbisRoot() + "/recent.txt";
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
	while (fgets(line, sizeof(line), f) && int(g_list.size()) < kMax)
	{
		char* tab = strchr(line, '\t');
		if (!tab)
			continue;
		std::string path(tab + 1);
		while (!path.empty() && (path.back() == '\n' || path.back() == '\r'))
			path.pop_back();
		if (!path.empty())
			g_list.push_back({path, atoll(line)});
	}
	fclose(f);
}

// written whole to recent.txt.part, then put in place: a cut write leaves the old list
void Save()
{
	const std::string path = FilePath(), part = path + ".part";
	FILE* f = fopen(part.c_str(), "w");
	if (!f)
		return;
	for (const Entry& e : g_list)
		fprintf(f, "%lld\t%s\n", e.when, e.path.c_str());
	const bool ok = fflush(f) == 0;
	fclose(f);
	if (ok)
		rename(part.c_str(), path.c_str());
	else
		remove(part.c_str());
}

void EraseLocked(const std::string& path)
{
	for (size_t i = 0; i < g_list.size();)
		if (g_list[i].path == path)
			g_list.erase(g_list.begin() + long(i));
		else
			i++;
}

// days between two times, by the calendar (today 0, yesterday 1)
int DaysAgo(long long when)
{
	const time_t now = time(nullptr), then = time_t(when);
	struct tm a = {}, b = {};
	localtime_r(&now, &a);
	localtime_r(&then, &b);
	a.tm_hour = b.tm_hour = 12; // noon: a day of 23 or 25 hours still counts as one
	a.tm_min = b.tm_min = a.tm_sec = b.tm_sec = 0;
	a.tm_isdst = b.tm_isdst = -1;
	const double d = difftime(mktime(&a), mktime(&b)) / 86400.0;
	return d < 0.5 ? 0 : int(d + 0.5);
}
} // namespace

int Days(long long when)
{
	return DaysAgo(when);
}

const std::vector<Entry>& List()
{
	std::lock_guard<std::mutex> lock(g_mutex);
	Load();
	return g_list;
}

void Played(const std::string& path)
{
	if (path.empty())
		return;
	std::lock_guard<std::mutex> lock(g_mutex);
	Load();
	EraseLocked(path);
	g_list.insert(g_list.begin(), Entry{path, (long long)time(nullptr)});
	if (int(g_list.size()) > kMax)
		g_list.resize(size_t(kMax));
	Save();
}

void Remove(const std::string& path)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	Load();
	EraseLocked(path);
	Save();
}

long long When(const std::string& path)
{
	std::lock_guard<std::mutex> lock(g_mutex);
	Load();
	for (const Entry& e : g_list)
		if (e.path == path)
			return e.when;
	return 0;
}

std::string PlayedAgo(long long when)
{
	if (when <= 0)
		return "";
	const int d = DaysAgo(when);
	if (d == 0)
		return T("Played today");
	if (d == 1)
		return T("Played yesterday");
	char text[96];
	snprintf(text, sizeof(text), T("Played %d days ago"), d);
	return text;
}

std::string Ago(long long when)
{
	if (when <= 0)
		return "";
	const int d = DaysAgo(when);
	if (d == 0)
		return T("today");
	if (d == 1)
		return T("yesterday");
	char text[96];
	snprintf(text, sizeof(text), T("%d days ago"), d);
	return text;
}
} // namespace recent
} // namespace fe
