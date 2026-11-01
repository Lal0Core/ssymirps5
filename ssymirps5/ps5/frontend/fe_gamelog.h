// SSYmirPS5 frontend: the game log, /data/ssymirps5/logs/game.log (the game before it: game.prev.log).
//
// Written for every game the app starts, whatever the Debug logs setting says, so a game that doesn't start, stays
// on a black screen, has no sound or crashes can be looked into afterwards: the app and the settings, the disc (its
// files, a .cue's tracks, the system area's header), the BIOS, every message of the core while it loads, and then a
// timeline while it runs (frames, the picture black or not, the sound silent or not, the SH-2s' program counters).
// Each line is flushed at once: the file is complete up to a crash, and the crash report goes into it too.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

namespace fe
{
namespace gamelog
{
// Opens a new game.log (the last one becomes game.prev.log) with a header for this game.
void Begin(const std::string& game_path);
// A line (printf-style), with the time since Begin. Nothing while no game log is open.
void Line(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
// The core's messages: kept in full while the game loads and for its first minute, then only warnings and errors
// (a running game can print a lot).
void CoreMessage(int level, const char* text);
// Closes it, with why ("closed", "could not start: ...").
void End(const char* why);
bool Active();
double Seconds(); // since Begin

// The disc described in detail: the file, a .cue's / .m3u's / .ccd's / .mds's other files (there? how big?), the
// header of its data track. Called by Begin.
void DescribeDisc(const std::string& path);
// The BIOS folder's files (name and size; a Saturn BIOS is 512 KiB).
void DescribeBios(const std::string& dir);
} // namespace gamelog
} // namespace fe
