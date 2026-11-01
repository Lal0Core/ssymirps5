// SSYmirPS5 frontend: the games played last (3.9.9), for the shelf's Recent tab. /data/ssymirps5/recent.txt keeps them, a
// line each ("<seconds since 1970>\t<path>"), the last one played first.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>
#include <vector>

namespace fe
{
namespace recent
{
constexpr int kMax = 20; // how many the tab keeps

struct Entry
{
	std::string path; // as the shelf has it (a disc set: its .m3u)
	long long when = 0; // time(): when it was last started
};

// The list, the last game played first (read from recent.txt the first time).
const std::vector<Entry>& List();
// A game started: it goes to the front (with the time now), and the list is written.
void Played(const std::string& path);
// Square on the Recent tab: the game leaves the list.
void Remove(const std::string& path);
// Days from `when` to today, by the calendar: today 0, yesterday 1.
int Days(long long when);
// When `path` was last started, 0 if it is not on the list.
long long When(const std::string& path);
// "Played today", "Played yesterday", "Played 3 days ago" (in the player's language); "" for 0.
std::string PlayedAgo(long long when);
// "today", "yesterday", "3 days ago"; "" for 0.
std::string Ago(long long when);
} // namespace recent
} // namespace fe
