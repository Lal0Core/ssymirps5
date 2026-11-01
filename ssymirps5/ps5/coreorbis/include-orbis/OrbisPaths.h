// SSYmirPS5: the /data/ssymirps5 folder layout and the boot log (orbis-shims/orbis_paths.cpp).
//
//   /data/ssymirps5/roms, games Saturn discs (.cue .chd .iso .ccd .mds .m3u); sub-folders are fine
//   /data/ssymirps5/bios         the Saturn BIOS (sega_101.bin, mpr-17933.bin...); cdb/ for the CD block ROM
//   /data/ssymirps5/paths.txt    more folders of your own: games=<folder>, bios=<folder>
//   /data/ssymirps5/saves        the backup RAM (<game>.srm)
//   /data/ssymirps5/states       save states, <game>.state1 .. <game>.state10
//   /data/ssymirps5/covers       box art (downloaded, or your own <ROM file name>.png), crc-cache.txt, wanted.txt
//   /data/ssymirps5/logs         boot.log (this run) and boot.prev.log (the run before); installer.log, helper.log
//   /data/ssymirps5/ssymirps5.ini  the frontend's settings
//
// USB drives are searched too: /mnt/usbN/ssymirps5/{roms,games,bios} (N = 0..7) and /mnt/extN/ssymirps5/...
//
// SPDX-License-Identifier: MIT

#pragma once

#include <string>
#include <vector>

#ifndef ORBIS_ROOT_DEFAULT
#define ORBIS_ROOT_DEFAULT "/data/ssymirps5"
#endif

// Root folder (ORBIS_ROOT_DEFAULT; the host tests point it elsewhere with SSYMIRPS5_ROOT).
const std::string& OrbisRoot();
// <root>/<sub>, created at boot by OrbisPathsInit.
std::string OrbisDir(const char* sub);
// Creates the folder tree. Returns false when the root itself can't be created (no /data access).
bool OrbisPathsInit();
// Folders that hold games and exist right now: roms/ and games/ in /data/ssymirps5 and in ssymirps5/ on every USB drive,
// then the games= folders of <root>/paths.txt (written with examples the first time).
std::vector<std::string> OrbisRomRoots();
// Folders that may hold the BIOS and exist right now: bios/ in /data/ssymirps5 and in ssymirps5/ on every USB drive, then
// the bios= folders of <root>/paths.txt.
std::vector<std::string> OrbisBiosDirs();

bool OrbisIsDir(const std::string& path);
bool OrbisIsFile(const std::string& path);
bool OrbisMkdirs(const std::string& path);

// <root>/logs/<name>.log (the run before kept as <name>.prev.log): boot.log for the emulator, installer.log
// for the installer/helper payload. Every line also goes to stdout (once the log is open). Lines logged before the file is open
// (before the jailbreak shows /data to the app) are kept and written first.
void OrbisLogOpen(const char* name = "boot");
void OrbisLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void OrbisLogClose();
// Debug logs on or off: the "Debug logs" setting (debug_logs= in ssymirps5.ini, on unless it says 0).
// OrbisLogOpen reads it, so the app, the installer and the helper all follow it; while off, OrbisLog writes
// nothing (no file, no stdout) and the log files of earlier runs are left as they are.
void OrbisLogSetEnabled(bool on);
// Reads the setting again and applies it (the helper, which keeps running, calls this for each request).
void OrbisLogRefresh();
bool OrbisLogEnabled();
// The open log's file descriptor, for the crash handler's signal-safe write(); -1 before it is open.
int OrbisLogFd();
