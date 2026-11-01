// SSYmirPS5 frontend: the game's case in 3D (R3 on the shelf).
//
// The selected game's box stands alone in the middle of the screen, as a real case: the front cover, the back cover
// and a spine with the title, drawn by the CPU in true perspective (each face is mapped through its homography, so
// the picture is perspective-correct, bilinear, with anti-aliased edges and a light on the plastic). R3 (or Left /
// Right) turns it round to the back and to the front again; the left stick turns and tilts it by hand. A panel at the
// right gives what the disc's header says: product number, release date, discs, regions. X plays the game, O goes
// back to the shelf.
//
// The back cover, best first:
//   1. downloaded, only for this game, only when its case is opened, and kept in covers/Saturn/back/<product>.<ext>
//      for the next time (a game none of the addresses has leaves <product>.missing: not asked again for 30 days,
//      unless the addresses change). The game is the one its product number names (fe_backsources.h: a built-in
//      table of 2426 Saturn discs), else its names; then, in order: LaunchBox Games DB's back cover of that game (its
//      database is downloaded once), Chromagi's page of it (by the numbers Chromagi lists, else the title), and the
//      addresses in covers/back-sources.txt. A picture put in covers/Saturn/back/ as <product>.png|jpg is used as is.
//   2. your own: "<game> (reverso).png|jpg" or "<game> (back).png|jpg" beside the game or in covers/Saturn
//   3. generated: the front cover darkened, with the header's details
// AVIF is decoded by libavif with dav1d (third_party, BSD-2-Clause).
//
// SPDX-License-Identifier: MIT
#pragma once

#include "fe_covers.h"
#include "fe_games.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fe
{
// What the player chose in the case view.
enum class CaseChoice
{
	Back, // O: back to the shelf
	Play, // X
	Previous, // L1: the game before it in the list (2.9)
	Next, // R1: the one after it
};
// Shows the case of `game` (front: its shelf cover, used when the cover file can't be read again at full size).
// has_prev / has_next: L1 / R1 can go to the game before / after it.
CaseChoice CaseView(const GameInfo& game, const CoverPtr& front, bool has_prev = false, bool has_next = false);
// Stops the back-cover downloads (the app's exit).
void CaseViewShutdown();

// 3.10: the game's disc for the launch animation, best first: your own "<game> (disc).png|jpg" beside the game or in
// covers/Saturn, the one downloaded before (covers/Saturn/disc/<product>.png|jpg), LaunchBox's "Disc" of the game
// (downloaded then and kept; a game it has none of leaves <product>.missing for 30 days). DiscPictureStart looks for
// it on a thread of its own; DiscPicture gives it once found (row-major A8B8G8R8). *done: the look is over.
void DiscPictureStart(const GameInfo& game);
bool DiscPicture(std::vector<uint32_t>& px, int& w, int& h, bool* done);

// For the tests.
// The product number as a file / address name: "GS-9198" -> "gs-9198" ("" when it has nothing usable).
std::string BackCoverKey(const std::string& product);
// The addresses to try for a product number, from the address templates (${code}: lower case, ${CODE}: as written).
std::vector<std::string> BackCoverUrls(const std::string& product, const std::vector<std::string>& templates);
// An AVIF file -> rgba (row-major, A8B8G8R8 as the surface's); false if it is not one or is too big.
bool DecodeAvif(const uint8_t* data, size_t size, std::vector<uint32_t>* rgba, int* w, int* h);
// The region codes of the header ("JTUE") as names ("Japan, Asia, North America, Europe"), translated.
std::string RegionNames(const std::string& areas);
} // namespace fe
