// SSYmirPS5 frontend: LaunchBox Games DB, the first place box art is downloaded from (fe_backsources.h).
//
// Its public database (gamesdb.launchbox-app.com/Metadata.zip) is downloaded once, in the background, as soon as the
// shelf starts (or a case is opened), read for the Sega Saturn games' box art -- front, back, spine, by region -- and
// deleted: what is kept is covers/Saturn/launchbox-backs.tsv. From then on every cover the shelf downloads is asked
// of LaunchBox first (libretro-thumbnails and the other databases after it), and so are the case view's back and spine.
// A game is the one its disc's product number names (the built-in table), else its names, its region first.
//
// SPDX-License-Identifier: MIT
#pragma once

#include "fe_backsources.h"
#include "fe_games.h"

#include <atomic>
#include <string>

namespace fe
{
namespace launchbox
{
enum class State
{
	Idle, // not started
	Working, // downloading or reading the database
	Ready, // the index is there
	Failed, // no network (tried again next time the app starts) or not a database (not before a day)
};

// Starts the database's download in the background, once (nothing when the index is there already).
void Start();
State GetState();
// The download's progress for the screen ("Downloading the LaunchBox database (first time only): 45 MB"), "" when
// nothing is being done.
std::string Progress();
// Changes each time the index becomes ready: a cover list made before it must be made again.
int Generation();
// Waits while the database is downloaded and read; false when there is no index after all. quit: stops waiting.
bool Wait(const std::atomic<bool>* quit);
// Stops the thread at the app's exit (left running in a download).
void Shutdown();

std::string IndexPath(); // covers/Saturn/launchbox-backs.tsv
std::string ImageBase(); // https://images.launchbox-app.com/ (YMIR_LAUNCHBOX_IMAGES in the tests)

// What the game's disc says, for the lookups (its header is read once per file).
backs::Disc DiscOf(const GameInfo& g);
// The game's LaunchBox pictures of a kind ("front"...), tab-separated addresses, best first; "" when none (or no index
// yet). Remembered per game until the index changes.
std::string Urls(const GameInfo& g, const char* kind);
} // namespace launchbox
} // namespace fe
