#pragma once

// Loads a Saturn disc from a real optical drive exposed by RetroArch's
// physical CD-ROM VFS (the "cdrom://" scheme). See docs/saturn-real-disc-feasibility.md.

#include <ymir/media/disc.hpp>
#include <ymir/media/loader/loader_result.hpp>

#include <string>

struct retro_vfs_interface;

namespace ymir_libretro {

// Returns true if the given content path refers to a physical CD-ROM drive
// presented by RetroArch (e.g. "cdrom://drive1.cue" or "cdrom://D:/drive.cue").
bool IsCDROMPath(const std::string &path);

// Builds a Disc backed by RetroArch's physical CD-ROM VFS from a synthesized cue
// path (as handed to retro_load_game when the user picks a real disc).
// Requires the frontend VFS interface (>= v3). On failure the Disc is invalidated
// and false is returned; diagnostics are reported through cbMsg.
bool LoadCDROMDisc(const retro_vfs_interface *vfs, const std::string &cuePath, ymir::media::Disc &disc,
                   ymir::media::CbLoaderMessage cbMsg);

} // namespace ymir_libretro
