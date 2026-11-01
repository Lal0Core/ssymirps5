// The fast forward's pace (frontend/fe_ffpace.h): how many of the game's frames each pass runs, by the clock.
// The loop of fe_emu.cpp is simulated: a pass runs its frames (each costs the console `cost` seconds), shows the
// picture (no wait), and sleeps a millisecond when it had nothing to run.
// SPDX-License-Identifier: MIT

#include "fe_ffpace.h"

#include <cmath>
#include <cstdio>

using namespace fe;

static int g_fail = 0;
#define CHECK(c)                                                          \
	do                                                                    \
	{                                                                     \
		if (!(c))                                                         \
		{                                                                 \
			printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c);           \
			g_fail++;                                                     \
		}                                                                 \
	} while (0)

struct Run
{
	double speed; // frames run / the frames of normal speed
	double longest_gap; // the longest time between two pictures, seconds
};

// a minute of fast forward at ff% of a fps game, each frame costing `cost` seconds
static Run Simulate(int ff, double fps, double cost)
{
	double t = 0, credit = 0, last = 0, last_picture = 0, gap = 0;
	long frames = 0;
	while (t < 60.0)
	{
		const int runs = FastForwardRuns(ff, fps, t - last, credit);
		last = t;
		if (runs == 0)
		{
			t += 0.001; // ahead of time: the loop sleeps
			continue;
		}
		t += runs * cost;
		frames += runs;
		gap = std::max(gap, t - last_picture);
		last_picture = t;
	}
	return {frames / (fps * t), gap};
}

static bool Near(double a, double b, double tol = 0.02)
{
	return std::fabs(a - b) <= tol * b;
}

int main()
{
	// a console that keeps up (2 ms a frame: 8x at most): every speed of the menu is the one asked for
	for (int ff : {150, 200, 300, 400, 500})
		for (double fps : {59.94, 50.0})
		{
			const Run r = Simulate(ff, fps, 0.002);
			if (!Near(r.speed, ff / 100.0))
				printf("  fast console: %d%% at %.2f fps runs at %.3fx\n", ff, fps, r.speed);
			CHECK(Near(r.speed, ff / 100.0));
			CHECK(r.longest_gap < 0.05); // a picture about every refresh
		}
	CHECK(!Near(Simulate(150, 60, 0.002).speed, Simulate(200, 60, 0.002).speed));

	// a console that can't (7 ms a frame: 2.38x at most at 60 fps): the speeds it reaches are the ones asked for; the
	// higher ones all run at its most, none slower (3.11.3 waited for a refresh after each batch: 500% ran at ~2x)
	{
		const double most = 1.0 / (0.007 * 59.94);
		CHECK(Near(Simulate(150, 59.94, 0.007).speed, 1.5));
		CHECK(Near(Simulate(200, 59.94, 0.007).speed, 2.0));
		for (int ff : {300, 400, 500, 1000})
		{
			const Run r = Simulate(ff, 59.94, 0.007);
			if (!Near(r.speed, most))
				printf("  slow console: %d%% runs at %.3fx (its most: %.3fx)\n", ff, r.speed, most);
			CHECK(Near(r.speed, most));
			CHECK(r.longest_gap < 0.1); // still a picture every few refreshes
		}
	}

	// unlimited: a fixed number of frames between pictures
	{
		double credit = 0;
		CHECK(FastForwardRuns(0, 60.0, 0.016, credit) == kFfUnlimitedRuns);
	}
	// a long stall (a menu, a load) is not caught up all at once
	{
		double credit = 0;
		CHECK(FastForwardRuns(500, 60.0, 5.0, credit) <= FastForwardBatch(500, 60.0));
		CHECK(credit >= 0 && credit <= 1);
	}
	if (g_fail)
		return 1;
	printf("ffpace: all checks passed\n");
	return 0;
}
