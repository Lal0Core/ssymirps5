// SSYmirPS5 frontend: fast forward's pace. While R3 is held the game runs at Settings > Emulation > Fast forward speed.
// 3.11.4: by the clock, not by the display: each pass runs the game's frames the time since the last pass asks for at
// that speed (the fraction carried over), at most a refresh's worth plus one, and the picture is shown without
// waiting for the flip. When the console can't keep up, the game runs as fast as it can, with no time lost waiting
// for a refresh (3.11.3 waited for one after each batch, so 500% ran slower than 300%).
// Before 3.11.3 nothing paced fast forward (every speed ran as fast as the CPU could) and 150% ran as 200%.
// SPDX-License-Identifier: MIT
#pragma once

#include <algorithm>
#include <cmath>

namespace fe
{
constexpr int kFfUnlimitedRuns = 8; // "Unlimited": this many frames between pictures
constexpr double kDisplayHz = 60.0;

// The most frames a pass runs at this speed: a refresh's worth, plus one (so a slow console still shows a picture
// about every refresh instead of running many frames between pictures).
inline int FastForwardBatch(int ff_speed, double game_fps)
{
	return int(std::ceil(ff_speed / 100.0 * std::max(1.0, game_fps) / kDisplayHz)) + 1;
}

// The game's frames to run now. ff_speed: the setting's percentage (0: unlimited); game_fps: the game's rate (59.94,
// 50); elapsed: seconds since the last pass; credit: the frames owed, carried between passes (0 when fast forward
// starts). 0: ahead of time, nothing to run yet.
inline int FastForwardRuns(int ff_speed, double game_fps, double elapsed, double& credit)
{
	if (ff_speed <= 0)
		return kFfUnlimitedRuns;
	credit += ff_speed / 100.0 * std::max(1.0, game_fps) * std::clamp(elapsed, 0.0, 0.1);
	const int runs = std::clamp(int(credit), 0, FastForwardBatch(ff_speed, game_fps));
	credit = std::clamp(credit - runs, 0.0, 1.0); // frames the console could not run are not caught up later
	return runs;
}
} // namespace fe
