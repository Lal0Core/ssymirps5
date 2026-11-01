// 3.9: the Saturn cheats (fe_cheats.cpp): the libretro name of a game, the .cht reading and writing, the download
// (served here from a folder: YMIR_TEST_CHT_DIR), and the codes handed to the core.
#include "fe_cheats.h"
#include "fe_games.h"
#include "fe_tlshttp.h"
#include "libretro.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>
#include <chrono>
#include <vector>
#include <sys/stat.h>

static int g_fail = 0, g_pass = 0;
#define CHECK(c, what) do { if (c) g_pass++; else { g_fail++; printf("FAIL: %s\n", what); } } while (0)

// ---- stubs
static std::string g_root, g_product;
static std::vector<std::string> g_set; // the codes the core got
static int g_gets = 0;
std::string OrbisDir(const char* sub) { return g_root + "/" + sub; }
bool OrbisMkdirs(const std::string& p) { std::string c = "mkdir -p '" + p + "'"; return system(c.c_str()) == 0; }
void OrbisLog(const char* fmt, ...) { (void)fmt; }
RETRO_API void retro_cheat_reset(void) { g_set.clear(); }
RETRO_API void retro_cheat_set(unsigned, bool on, const char* code) { if (on) g_set.push_back(code); }
namespace crashlog { void* ArmThread() { return nullptr; } void DisarmThread(void*) {} }
namespace fe {
bool WriteFileAtomicTo(const std::string& path, const std::vector<uint8_t>& data) { std::ofstream o(path, std::ios::binary); o.write((const char*)data.data(), data.size()); return bool(o); }
bool ReadRomHeader(const std::string&, System, romheader::Info* info) { info->other = g_product; return !g_product.empty(); }
struct TlsHttp::Impl {};
TlsHttp::TlsHttp() {}
TlsHttp::~TlsHttp() {}
bool TlsHttp::Offline() const { return false; }
void TlsHttp::Term() {}
void TlsHttp::SetProgress(std::atomic<size_t>*) {}
int TlsHttp::Get(const std::string& url, std::vector<uint8_t>& out, size_t) {
	g_gets++;
	// the URL's file name, %-decoded, from the folder
	std::string f = url.substr(url.find_last_of('/') + 1), name;
	for (size_t i = 0; i < f.size(); i++)
		if (f[i] == '%' && i + 2 < f.size()) { name += char(strtol(f.substr(i + 1, 2).c_str(), nullptr, 16)); i += 2; }
		else name += f[i];
	std::ifstream in(std::string(getenv("YMIR_TEST_CHT_DIR")) + "/" + name, std::ios::binary);
	if (!in) return 404;
	std::stringstream ss; ss << in.rdbuf(); const std::string s = ss.str();
	out.assign(s.begin(), s.end());
	return 200;
}
}
using namespace fe::cheats;

static std::string HexOfTest(const std::string& c) { std::string h; for (char x : c) if (isxdigit((unsigned char)x)) h += char(toupper((unsigned char)x)); return h; }
static void WaitReady() { for (int i = 0; i < 200 && Status() == State::Looking; i++) std::this_thread::sleep_for(std::chrono::milliseconds(10)); }

int main(int argc, char** argv)
{
	g_root = argc > 1 ? argv[1] : "/tmp/cheats-test";
	system(("rm -rf '" + g_root + "' && mkdir -p '" + g_root + "/cheats'").c_str());
	// names
	CHECK(MatchName("Virtua Fighter 2 (USA)", "") == "Virtua Fighter 2 (USA)", "Virtua Fighter 2 (USA) by file name");
	CHECK(MatchName("D (1996)(Acclaim Entertainment USA)(US)(Disc 1 of 2)", "") == "D (USA)", "a TOSEC name of D");
	CHECK(MatchName("Panzer Dragoon (USA)", "") == "Panzer Dragoon (USA)", "not the (RetroArch) variant");
	CHECK(MatchName("xyz", "MK-81014") == "Virtua Fighter 2 (USA)", "by product number MK-81014");
	CHECK(MatchName("Grandia (Japan)", "") == "", "a game without cheats: none");
	CHECK(MatchName("Sega Rally Championship (USA)", "") == "Sega Rally Championship (USA)", "Sega Rally: the Action Replay file, not the (RetroArch) one");
	CHECK(MatchName("Sega Rally Championship (1995)(Sega)(US)", "") == "Sega Rally Championship (USA)", "Sega Rally by a TOSEC name");
	CHECK(MatchName("xyz", "MK-81207") == "Sega Rally Championship (USA)", "Sega Rally by its product number");
	CHECK(MatchName("Panzer Dragoon II Zwei (USA)", "") == "Panzer Dragoon II Zwei (USA)", "Zwei: not the (RetroArch) (Rumble) one");
	{
		std::vector<Cheat> m;
		Parse("cheats = \"1\"\ncheat0_address = \"1770807\"\ncheat0_code = \"\"\ncheat0_desc = \"Time\"\ncheat0_value = \"55\"\n", m);
		CHECK(m.size() == 1 && m[0].retroarch && !m[0].usable && !m[0].on, "a RetroArch-format cheat: shown, not usable");
	}
	CHECK(UrlFor("D (USA)").find("cht/Sega%20-%20Saturn/D%20%28USA%29.cht") != std::string::npos, "the URL");
	// codes
	CHECK(Usable("160621b800a0"), "12 digits");
	CHECK(Usable("1605C942+270F"), "8+4");
	CHECK(Usable("F6000914+C305+B6002800+0000+1605C942+270F"), "a master code and a code");
	CHECK(!Usable("3605C99F+00??"), "a value to fill in");
	CHECK(!Usable("160CE42+0001"), "7 digits");
	{
		std::vector<Cheat> m;
		Parse("cheats = 2\ncheat0_desc = \"Master Code\"\ncheat0_code = \"F6000914+C305+B6002800+0000\"\ncheat1_code = \"F6000914+C305+1605C942+270F\"\n", m);
		CHECK(m.size() == 2 && m[0].hidden && !m[1].hidden, "the master code alone is hidden");
	}
	// parse and write
	const std::string t = "cheats = 2\n\ncheat0_desc = \"Inf Energy\"\ncheat0_code = \"160621b800a0\"\ncheat0_enable = false\n\ncheat1_desc = \"Pick\"\ncheat1_code = \"3605C99F+00??\"\ncheat1_enable = false\n";
	std::vector<Cheat> l;
	CHECK(Parse(t, l) && l.size() == 2 && l[0].desc == "Inf Energy" && l[0].usable && !l[1].usable, "parse");
	l[0].on = true;
	std::vector<Cheat> l2;
	CHECK(Parse(Write(t, l), l2) && l2[0].on && !l2[1].on, "write keeps what is on");
	// every libretro file parses
	int files = 0, usable = 0, total = 0;
	if (const char* d = getenv("YMIR_TEST_CHT_DIR"))
	{
		FILE* p = popen((std::string("ls '") + d + "'").c_str(), "r");
		char line[512];
		while (p && fgets(line, sizeof(line), p))
		{
			std::string n = line; n.pop_back();
			std::ifstream in(std::string(d) + "/" + n); std::stringstream ss; ss << in.rdbuf();
			std::vector<Cheat> c;
			if (Parse(ss.str(), c) && !c.empty()) files++;
			for (auto& x : c) { total++; usable += x.usable; }
		}
		if (p) pclose(p);
		printf("libretro files read: %d, cheats %d, usable %d\n", files, total, usable);
		CHECK(files >= 190, "all the libretro Saturn files read");
		// the download: D's cheats, turn one on, the core gets it, kept for the next start
		Begin(g_root + "/games/D (1996)(Acclaim Entertainment USA)(US)(Disc 1 of 2).chd");
		WaitReady();
		CHECK(Status() == State::Ready && !List().empty(), "D's cheats downloaded");
		CHECK(File().find("/cheats/Saturn/games/D (1996)(Acclaim Entertainment USA)(US)(Disc 1 of 2).cht") != std::string::npos, "the list kept in cheats/Saturn/games");
		{ struct stat st; CHECK(stat((g_root + "/cheats/Saturn/libretro/D (USA).cht").c_str(), &st) == 0, "libretro's file kept in cheats/Saturn/libretro"); }
		Set(0, true);
		Pump();
		CHECK(g_set.size() == 1 && g_set[0] == List()[0].code, "the core got the code on");
		End();
		CHECK(g_set.empty(), "closing the game resets the core's codes");
		const int gets = g_gets;
		Begin(g_root + "/games/D (1996)(Acclaim Entertainment USA)(US)(Disc 1 of 2).chd");
		WaitReady(); Pump();
		CHECK(g_gets == gets && List()[0].on && g_set.size() == 1, "next start: no download, the cheat still on");
		End();
		// 3.9.2: Almar's Guides (YMIR_ALMAR_URL: the sample in the folder)
		{
			std::ifstream in(std::string(d) + "/almar-test.md"); std::stringstream ss; ss << in.rdbuf();
			const std::vector<DbGame> db = ParseAlmar(ss.str());
			CHECK(db.size() == 4, "Almar: 4 games (the item page joins Albert Odyssey)");
			int ao = MatchDbGame(db, "Albert Odyssey - Legend of Eldean (USA)", "");
			CHECK(ao >= 0 && db[size_t(ao)].cheats.size() == 3 && db[size_t(ao)].cheats[2].first == "Unlimited Item: Herb", "Almar: the item page's cheat, named after the page");
			CHECK(ao >= 0 && db[size_t(ao)].cheats[0].second == "1607D2A8+0000+1607D2AA+0000", "Almar: two codes in a cell");
			int gr = MatchDbGame(db, "Grandia (Japan) (Disc 1)", "");
			CHECK(gr >= 0 && db[size_t(gr)].cheats[0].second == "16052A10+0098+16052A12+967F" && Usable(db[size_t(gr)].cheats[0].second), "Almar: codes split by <br>");
			CHECK(MatchDbGame(db, "Virtua Fighter 2 (USA)", "") < 0, "Almar: a game it has not");
			CHECK(MatchDbGame(db, "Astal - Something Else (USA)", "") >= 0 && MatchDbGame(db, "Astal 2 - Return (USA)", "") < 0, "Almar: by the main title, not a sequel");
		}
		Begin(g_root + "/games/Grandia (Japan) (Disc 1).chd"); WaitReady();
		CHECK(Status() == State::Ready && List().size() == 2 && File().find("/cheats/Saturn/games/Grandia (Japan) (Disc 1).cht") != std::string::npos, "a game only Almar has: its cheats");
		End();
		Begin(g_root + "/games/Sega Rally Championship (USA).chd"); WaitReady();
		{
			int shown = 0, time = 0;
			for (const Cheat& c : List()) { shown += !c.hidden; time += c.desc == "Infinite Time"; }
			CHECK(Status() == State::Ready && shown == 6 && time == 1, "Sega Rally: libretro's 5 and Almar's new one, Infinite Time once");
		}
		End();
		// 3.9.4: your own .cht files, named as GameHacking.org names its exports
		{
			std::ofstream o(g_root + "/cheats/Saturn/Metal Slug - Kakuchou Ram Doukon Okaidoku Set!! (Jpn).cht");
			o << "cheats = 2\n\ncheat0_desc = \"Hit Anywhere\"\ncheat0_code = \"16056102 0009 16055EBE 0009\"\ncheat0_enable = false\n\ncheat1_desc = \"Invincibility\"\ncheat1_code = \"16055EBE 0009\"\ncheat1_enable = false\n";
			std::ofstream o2(g_root + "/cheats/Saturn/Astal (USA).cht");
			o2 << "cheats = 2\ncheat0_desc = \"Mine\"\ncheat0_code = \"36001234 0001\"\ncheat1_desc = \"Dup\"\ncheat1_code = \"360DBB76 00FF\"\n";
		}
		Begin(g_root + "/games/Metal Slug - Super Vehicle-001 (Japan).chd"); WaitReady();
		CHECK(Status() == State::Ready && List().size() == 2 && List()[1].desc == "Invincibility", "a GameHacking.org file, its name not the game's: found by the title");
		Set(1, true); End();
		Begin(g_root + "/games/Metal Slug - Super Vehicle-001 (Japan).chd"); WaitReady();
		CHECK(Status() == State::Ready && List()[1].on, "what is on is kept");
		End();
		Begin(g_root + "/games/Metal Slug X (Japan).chd"); WaitReady();
		CHECK(Status() != State::Ready, "not for another game (Metal Slug X)");
		End();
		Begin(g_root + "/games/Astal (USA).chd"); WaitReady();
		{
			int mine = 0, dup = 0, power = 0;
			for (const Cheat& c : List()) { mine += c.desc == "Mine"; dup += c.desc == "Dup"; power += HexOfTest(c.code) == "360DBB7600FF"; }
			CHECK(Status() == State::Ready && mine == 1 && dup == 0 && power == 1, "libretro's, Almar's and yours together, a code once");
		}
		End();
		// GameHacking.org's other names: "(Euro)", the product number
		{
			std::ofstream o(g_root + "/cheats/Saturn/Battle Arena Toshinden Remix (Euro).cht");
			o << "cheats = 1\ncheat0_desc = \"Infinite Time\"\ncheat0_code = \"1605F3E4 0065\"\n";
			std::ofstream o2(g_root + "/cheats/Saturn/T-1234G.cht");
			o2 << "cheats = 1\ncheat0_desc = \"By serial\"\ncheat0_code = \"16001111 0001\"\n";
		}
		Begin(g_root + "/games/Battle Arena Toshinden Remix (Europe).chd"); WaitReady();
		{
			int t = 0; for (const Cheat& c : List()) t += c.desc == "Infinite Time";
			CHECK(Status() == State::Ready && t == 1, "\"(Euro)\" is Europe");
		}
		End();
		g_product = "T-1234G";
		Begin(g_root + "/games/Some Game (Japan).chd"); WaitReady();
		CHECK(Status() == State::Ready && List().size() == 1 && List()[0].desc == "By serial", "a file named by the product number");
		End();
		g_product.clear();
		// no network for a game: Failed
		Begin(g_root + "/games/Nope (USA).chd"); WaitReady();
		CHECK(Status() == State::None, "a game without cheats");
	}
	printf("%d passed, %d failed\n", g_pass, g_fail);
	return g_fail ? 1 : 0;
}
