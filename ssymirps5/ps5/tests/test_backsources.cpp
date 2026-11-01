// SSYmirPS5 host test: the back covers' game lookup (fe_backsources.cpp): product numbers, Chromagi's pages, LaunchBox.
// SPDX-License-Identifier: MIT
#include "fe_backsources.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

void OrbisLog(const char* fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fputc('\n', stderr);
}
namespace fe
{
bool WriteFileAtomicTo(const std::string& path, const std::vector<uint8_t>& data)
{
	FILE* f = fopen(path.c_str(), "wb");
	if (!f)
		return false;
	fwrite(data.data(), 1, data.size(), f);
	return fclose(f) == 0;
}
} // namespace fe

using namespace fe::backs;
static int failures = 0;
static void Expect(bool ok, const char* what)
{
	printf("%s: %s\n", ok ? "ok" : "FAIL", what);
	failures += !ok;
}
static std::string First(const std::vector<Candidate>& c)
{
	return c.empty() ? std::string() : c[0].url;
}

int main(int argc, char** argv)
{
	const std::string dir = argc > 1 ? argv[1] : "/tmp";
	std::string t, r;
	Expect(SerialTitle("MK-81014", &t, &r) && t.find("Virtua Fighter 2") == 0, "MK-81014 is Virtua Fighter 2");
	Expect(SerialTitle("mk 81014", nullptr, nullptr), "the number is found however it is written");
	Expect(SerialTitle("T-32501G", &t, nullptr) && t.find("Advanced V.G.") == 0, "T-32501G is Advanced V.G.");
	Expect(!SerialTitle("ZZ-0000", nullptr, nullptr), "an unknown number is no game");

	Disc vf2{"MK-81014", "UTE", {"Some Renamed File"}};
	Expect(LookupNames(vf2).size() == 1 && LookupNames(vf2)[0] == "Virtua Fighter 2", "a known number: only its title counts, not the file's name");
	Expect(First(ChromagiCandidates(vf2)).find("/fvir16ssc/fvir16ssc.boxback.avif") != std::string::npos,
		"MK-81014 -> Chromagi's FVIR16SSC page (MK81014-50)");
	Disc jp{"GS-9079", "J", {}};
	Expect(First(ChromagiCandidates(jp)).find("/gs-9079/") != std::string::npos, "GS-9079 -> its own page");
	Disc vf1{"GS-9001", "J", {}};
	Expect(First(ChromagiCandidates(vf1)).find("/gs-9001/") != std::string::npos, "Virtua Fighter is not Virtua Fighter 2");
	Disc unknown{"T-32501G", "J", {}};
	Expect(ChromagiCandidates(unknown).empty(), "Advanced V.G.: Chromagi has no page, so none is tried");
	Disc chd{"", "", {"Virtua Fighter 2 (Europe)"}};
	Expect(PreferredRegion(chd) == "Europe" && LookupNames(chd)[0] == "Virtua Fighter 2", "a .chd: named by its file");

	// LaunchBox: a Metadata.xml in pieces
	const char* xml =
		"<LaunchBox><Game><Name>Virtua Fighter 2</Name><DatabaseID>200</DatabaseID><Platform>Sony Playstation 3</Platform></Game>"
		"<Game><Name>Virtua Fighter 2</Name><DatabaseID>100</DatabaseID><Platform>Sega Saturn</Platform><GameControllerSupport /></Game>"
		"<Game><Name>Virtua Fighter</Name><DatabaseID>101</DatabaseID><Platform>Sega Saturn</Platform></Game>"
		"<Game><Name>Panzer Dragoon II Zwei</Name><DatabaseID>102</DatabaseID><Platform>Sega Saturn</Platform></Game>"
		"<GameAlternateName><AlternateName>Dragon &amp; Zwei</AlternateName><DatabaseID>102</DatabaseID><Region>Japan</Region></GameAlternateName>"
		"<GameImage><DatabaseID>200</DatabaseID><FileName>ps3.jpg</FileName><Type>Box - Back</Type><Region>North America</Region></GameImage>"
		"<GameImage><DatabaseID>100</DatabaseID><FileName>vf2-jp.jpg</FileName><Type>Box - Back</Type><Region>Japan</Region></GameImage>"
		"<GameImage><DatabaseID>100</DatabaseID><FileName>vf2-us.jpg</FileName><Type>Box - Back</Type><Region>North America</Region></GameImage>"
		"<GameImage><DatabaseID>100</DatabaseID><FileName>vf2-front.jpg</FileName><Type>Box - Front</Type><Region>Europe</Region></GameImage>"
		"<GameImage><DatabaseID>102</DatabaseID><FileName>pd2-jp.jpg</FileName><Type>Box - Back</Type><Region>Japan</Region></GameImage>"
		"<GameImage><DatabaseID>100</DatabaseID><FileName>vf2-disc.png</FileName><Type>Disc</Type><Region>North America</Region></GameImage>"
		"<GameImage><DatabaseID>100</DatabaseID><FileName>vf2-fan.png</FileName><Type>Fanart - Background</Type><Region>North America</Region></GameImage>"
		"</LaunchBox>";
	LaunchBoxParser p;
	for (size_t i = 0, n = strlen(xml); i < n; i += 37)
		p.Feed(xml + i, std::min<size_t>(37, n - i));
	const std::string index = p.Finish();
	Expect(p.games() == 3, "three Saturn games (the PS3 one left out)");
	Expect(index.find("ps3.jpg") == std::string::npos && index.find("vf2-front.jpg\tfront") != std::string::npos, "only Saturn box art, fronts too");
	const std::string path = dir + "/lb-index.tsv";
	fe::WriteFileAtomicTo(path, std::vector<uint8_t>(index.begin(), index.end()));
	const std::string base = "https://images.launchbox-app.com/";
	Disc us{"MK-81014", "U", {}};
	auto lb = LaunchBoxCandidates(us, path, base, "back");
	Expect(First(lb) == base + "vf2-us.jpg" && lb.size() == 2, "Virtua Fighter 2 (USA disc): the US back, then the Japanese one");
	Expect(First(LaunchBoxCandidates(jp, path, base, "back")) == base + "vf2-jp.jpg", "Virtua Fighter 2 (Japanese disc): the Japanese back");
	Expect(LaunchBoxCandidates(vf1, path, base, "back").empty(), "Virtua Fighter (no back in LaunchBox): not Virtua Fighter 2's");
	Expect(LaunchBoxCandidates(unknown, path, base, "back").empty(), "Advanced V.G.: nothing");
	Disc pd2{"", "", {"Panzer Dragoon II Zwei (Japan)"}};
	Expect(First(LaunchBoxCandidates(pd2, path, base, "back")) == base + "pd2-jp.jpg", "found by its file name when it has no number");
	Expect(First(LaunchBoxCandidates(us, path, base, "front")) == base + "vf2-front.jpg", "the front cover, from the same list");
	Expect(First(LaunchBoxCandidates(us, path, base, "disc")) == base + "vf2-disc.png", "3.10: the disc, for the launch animation");
	Expect(LaunchBoxCandidates(us, path, base, "disc").size() == 1, "3.10: only the disc (not the other fan art)");
	Expect(LaunchBoxCandidates(pd2, path, base, "disc").empty(), "3.10: a game without a disc: none");
	Expect(LaunchBoxIndexValid(path), "the index is of this format");
	printf("%s\n", failures ? "FAILED" : "all passed");
	return failures ? 1 : 0;
}
