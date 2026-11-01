// SSYmirPS5 frontend: the game shelf -- a 3D cover flow of the library, like PS5SX2's.
//
// The selected game's box art stands in the middle, lit by a glow in its own colour, over a blurred
// copy of the same art; the rest of the library stands at an angle on both sides, in perspective, and every
// cover is mirrored on a glossy floor. Moving the selection glides the whole shelf.
//
// PS5SX2 draws its shelf with Vulkan. SSYmirPS5 is a payload with no GPU driver, so the shelf is rendered
// by the CPU -- true perspective, not a fake: a cover turned about its vertical axis has a constant depth
// down every screen column, so each column is one perspective-correct texture column drawn as a vertical
// span (bilinear, three mip levels, anti-aliased top and bottom edges), spread over several cores.
//
// SPDX-License-Identifier: MIT
#pragma once

#include <string>

namespace fe
{
// Returns the game to play, or "" when the player quits.
std::string Shelf();
// 3.9.9: whether the player chose, when the game Shelf() returned was picked, to continue where they left off (the
// state saved on leaving it, emu::LoadAutoState). Asking clears it.
bool ShelfTakeResume();
// Stops the cover downloads before the payload exits.
void ShelfShutdown();
// Settings, Download all borders: borders/wanted.txt for the helper, with The Bezel Project's border of every game of the
// library that has none yet (fe_borderfetch.h). Returns how many.
int DownloadBorders();
} // namespace fe
