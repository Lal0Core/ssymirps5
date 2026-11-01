// SSYmirPS5 frontend: how long each game was played (3.11), for the case view, the Recent tab and the shelf's order.
// /data/ssymirps5/playtime.txt keeps it, a line a game ("<seconds>\t<last time played, time()>\t<path>"). Only the time
// in the game counts (not the pause menu); PlayGame adds it every minute and when the game is left.
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace fe
{
namespace playtime
{
// Adds `secs` to the game's time (and makes now its last time played); the file is written.
void Add(const std::string& path, double secs);
// The game's time in seconds (0: never played since 3.11).
long long Seconds(const std::string& path);
// When it was last played (time()), 0 if not known.
long long Last(const std::string& path);
// "3 h 20 min", "12 min", "less than 1 min" (in the player's language); "" for 0.
std::string Format(long long secs);
} // namespace playtime
} // namespace fe
