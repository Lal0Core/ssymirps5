// The shelf's tab rules (frontend/fe_tabs.h): which game stands on which tab, which tab opens, how L1 / R1 step.
// SPDX-License-Identifier: MIT

#include "fe_tabs.h"

#include <cstdio>
#include <string>

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

static GameInfo Game(const char* path, bool usb)
{
	GameInfo g;
	g.path = path;
	g.on_usb = usb;
	return g;
}

int main()
{
	using namespace tabs;

	// membership: USB over the console's drive
	CHECK(Of(Game("/data/a.cue", false)) == Internal);
	CHECK(Of(Game("/mnt/usb0/a.cue", true)) == Usb);

	std::vector<GameInfo> games = {Game("/mnt/usb0/n.cue", true), Game("/data/b.cue", false), Game("/mnt/usb0/c.cue", true),
								   Game("/data/a.cue", false)};
	int counts[kCount];
	Count(games, counts);
	CHECK(counts[Internal] == 2 && counts[Usb] == 2 && counts[Recent] == 0);

	// the shelf list: each tab's games side by side, the order inside a tab kept
	OrderByTab(games);
	CHECK(games[0].path == "/data/b.cue" && games[1].path == "/data/a.cue");
	CHECK(games[2].path == "/mnt/usb0/n.cue" && games[3].path == "/mnt/usb0/c.cue");
	const std::vector<int> usb = View(games, Usb);
	CHECK(usb.size() == 2 && usb[0] == 2 && usb[1] == 3);
	CHECK(View(games, Internal).size() == 2);

	// USB is hidden while no game is on a USB drive; Internal is always there
	const int none[kCount] = {0, 0, 0};
	CHECK(Shown(none, Internal) && !Shown(none, Usb) && !Shown(none, Recent));
	CHECK(Shown(counts, Usb));

	// the tab that opens: (counts, asked for, the player chose it)
	const int local_only[kCount] = {3, 0, 0};
	const int usb_only[kCount] = {0, 2, 0};
	CHECK(Resolve(local_only, Internal, false) == Internal);
	CHECK(Resolve(usb_only, Usb, false) == Usb);
	CHECK(Resolve(usb_only, Internal, false) == Usb);   // an empty tab hands over to one with games
	CHECK(Resolve(usb_only, Internal, true) == Internal); // ... unless the player chose it
	CHECK(Resolve(local_only, Usb, true) == Internal);  // the USB tab is gone with its last game, even if chosen
	CHECK(Resolve(none, Internal, false) == Internal);  // nothing anywhere: where it was asked for
	CHECK(Resolve(none, Usb, false) == Internal);
	CHECK(Resolve(none, 7, false) == Internal);         // a damaged setting
	CHECK(Resolve(none, -1, false) == Internal);

	// L1 / R1 go round the shown tabs
	CHECK(Step(none, Internal, +1) == Internal); // only Internal shown: it stays
	CHECK(Step(none, Usb, +1) == Internal);      // from a hidden tab: to the next shown one
	CHECK(Step(none, Internal, -1) == Internal);
	CHECK(Step(counts, Internal, +1) == Usb);
	CHECK(Step(counts, Usb, +1) == Internal);
	CHECK(Step(counts, Internal, -1) == Usb);

	CHECK(std::string(Name(Internal)) == "Internal" && std::string(Name(Usb)) == "USB");

	// Recent, first in the bar, only once a game was played; no game's storage tab
	int played[kCount] = {3, 2, 4};
	CHECK(!Shown(counts, Recent) && Shown(played, Recent));
	CHECK(Step(played, Internal, -1) == Recent);
	CHECK(Step(played, Recent, +1) == Internal);
	CHECK(Step(played, Usb, +1) == Recent);
	CHECK(Step(played, Recent, -1) == Usb);
	CHECK(Resolve(played, Recent, false) == Recent);
	CHECK(Resolve(counts, Recent, false) == Internal); // none played: the tab is not there
	for (const GameInfo& g : {Game("/data/a.cue", false), Game("/mnt/usb0/b.cue", true)})
		CHECK(Of(g) != Recent);
	CHECK(std::string(Name(Recent)) == "Recent");

	// 3.11: the shelf's order; the ones without a value after the rest, in the order they had (by name)
	{
		auto make = [](const char* path, int year) {
			GameInfo g = Game(path, false);
			g.year = year;
			return g;
		};
		const std::vector<GameInfo> by_name = {make("a", 1996), make("b", 0), make("c", 1994), make("d", 1996), make("e", 0)};
		auto order = [](const std::vector<GameInfo>& v) {
			std::string o;
			for (const GameInfo& g : v)
				o += g.path;
			return o;
		};
		auto last = [](const std::string& p) -> long long { return p == "c" ? 300 : p == "e" ? 200 : p == "a" ? 100 : 0; };
		auto played = [](const std::string& p) -> long long { return p == "d" ? 50 : p == "b" ? 900 : 0; };
		std::vector<GameInfo> v = by_name;
		SortGames(v, ByName, last, played);
		CHECK(order(v) == "abcde");
		v = by_name;
		SortGames(v, ByYear, last, played);
		CHECK(order(v) == "cadbe"); // 1994, then 1996 (a before d, by name), then the two without a year
		v = by_name;
		SortGames(v, ByLastPlayed, last, played);
		CHECK(order(v) == "ceabd");
		v = by_name;
		SortGames(v, ByMostPlayed, last, played);
		CHECK(order(v) == "bdace");
	}

	if (g_fail)
		return 1;
	printf("tabs: all checks passed\n");
	return 0;
}
