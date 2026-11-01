// SSYmirPS5 frontend: the shelf's tabs -- Recent, Internal and USB -- and which game stands on which. The shelf keeps one game list; a tab is the indices of its games (docs: ADR-004 in .context).
// SPDX-License-Identifier: MIT
#pragma once

#include "fe_games.h"

#include <algorithm>
#include <functional>
#include <string>
#include <vector>

namespace fe
{
namespace tabs
{
enum Tab
{
	Internal = 0, // the console's drive (/data/ssymirps5/games and the like)
	Usb = 1,      // /mnt/... ; shown only while a game is on a USB drive
	Recent = 2,   // the games played last (fe_recent.h), of any storage; first in the bar, shown once there are any
	kCount = 3,
};

// the order of the bar (and of L1 / R1)
constexpr int kOrder[kCount] = {Recent, Internal, Usb};

inline const char* Name(int t)
{
	return t == Usb ? "USB" : t == Recent ? "Recent" : "Internal";
}

// The storage tab of a game (never Recent: a game played lately is on its storage tab too).
inline int Of(const GameInfo& g)
{
	return g.on_usb ? Usb : Internal;
}

inline void Count(const std::vector<GameInfo>& games, int counts[kCount])
{
	for (int t = 0; t < kCount; t++)
		counts[t] = 0;
	for (const GameInfo& g : games)
		counts[Of(g)]++;
}

inline bool Shown(const int counts[kCount], int t)
{
	// USB and Recent only with games on them
	return t == Internal || ((t == Usb || t == Recent) && counts[t] > 0);
}

// The tab to show when `want` was asked for. `keep_empty`: stay on `want` even with no games (the player chose it).
// Otherwise an empty or hidden tab hands over to the first shown tab that has games. With no games anywhere: `want`,
// or Internal when that is hidden.
inline int Resolve(const int counts[kCount], int want, bool keep_empty)
{
	if (want < 0 || want >= kCount)
		want = Internal;
	const bool shown = Shown(counts, want);
	if (shown && (counts[want] > 0 || keep_empty))
		return want;
	for (int t = 0; t < kCount; t++)
		if (counts[t] > 0 && Shown(counts, t))
			return t;
	return shown ? want : int(Internal);
}

// The next shown tab in a direction (+1 / -1) in the bar's order, round and round.
inline int Step(const int counts[kCount], int from, int dir)
{
	int at = 0;
	for (int i = 0; i < kCount; i++)
		if (kOrder[i] == from)
			at = i;
	for (int i = 0; i < kCount; i++)
	{
		at = (at + dir + kCount) % kCount;
		if (Shown(counts, kOrder[at]))
			return kOrder[at];
	}
	return from;
}

// The shelf list in tab order (Internal, USB), each tab by title as ScanGames sorted it: a tab's games are then
// side by side, so the cover service's window around the selection (fe_covers.h) covers the tab's neighbours.
inline void OrderByTab(std::vector<GameInfo>& games)
{
	std::stable_sort(games.begin(), games.end(), [](const GameInfo& a, const GameInfo& b) { return Of(a) < Of(b); });
}

// 3.11: the shelf's order (Settings, Sort the games): 0 by name (as the scan sorted them: left as it is), 1 the last
// played first, 2 the most played first, 3 by year (the oldest first). Games without a value (never played, a disc
// with no year) go after the rest, by name as before. last(path) / played(path): when it was last played, for how long.
enum Sort
{
	ByName = 0,
	ByLastPlayed = 1,
	ByMostPlayed = 2,
	ByYear = 3,
};
inline void SortGames(std::vector<GameInfo>& games, int sort, const std::function<long long(const std::string&)>& last,
	const std::function<long long(const std::string&)>& played)
{
	if (sort == ByName)
		return;
	std::vector<std::pair<long long, size_t>> keys(games.size());
	for (size_t i = 0; i < games.size(); i++)
		keys[i] = {sort == ByLastPlayed ? last(games[i].path) : sort == ByMostPlayed ? played(games[i].path)
														   : (long long)games[i].year,
			i};
	std::stable_sort(keys.begin(), keys.end(), [sort](const auto& a, const auto& b) {
		if ((a.first > 0) != (b.first > 0))
			return a.first > 0; // the ones with a value first
		if (a.first == b.first)
			return false; // the order they had (by name)
		return sort == ByYear ? a.first < b.first : a.first > b.first;
	});
	std::vector<GameInfo> out;
	out.reserve(games.size());
	for (const auto& k : keys)
		out.push_back(std::move(games[k.second]));
	games = std::move(out);
}

// Indices into `games` of the games on tab t, in list order.
inline std::vector<int> View(const std::vector<GameInfo>& games, int t)
{
	std::vector<int> v;
	for (size_t i = 0; i < games.size(); i++)
		if (Of(games[i]) == t)
			v.push_back(int(i));
	return v;
}
} // namespace tabs
} // namespace fe
