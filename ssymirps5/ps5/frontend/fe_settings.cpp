// SSYmirPS5 frontend: settings file.
// SPDX-License-Identifier: MIT

#include "fe_settings.h"

#include "OrbisPaths.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

namespace fe
{
namespace
{
std::string IniPath()
{
	return OrbisRoot() + "/ssymirps5.ini";
}

int Clamp(int v, int lo, int hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}

const char* const kButtonKeys[Settings::kButtons] = {"btn_a", "btn_b", "btn_c", "btn_x", "btn_y", "btn_z", "btn_l",
	"btn_r", "btn_start"};
constexpr int kPs5ButtonChoices = 10; // emu::kPs5ButtonCount
constexpr int kShaderCount = 64; // the most of either renderer (ps5video::ShaderCount() at use)
} // namespace

constexpr int Settings::kDefaultButtons[Settings::kButtons];

void Settings::DefaultButtons()
{
	for (int i = 0; i < kButtons; i++)
		buttons[i] = kDefaultButtons[i];
}

Settings& Config()
{
	static Settings s;
	return s;
}

namespace
{
// the running game's file, and the global values of the keys it overrides
std::string g_game_ini;
bool g_game_active = false; // a game is running (BeginGame was called)
bool g_game_own = false; // ...and it has its own settings file
Settings g_global;

// The settings a game can have of its own: the picture, the sound, the core's emulation, the controls.
void CopyGameKeys(Settings& to, const Settings& from)
{
	to.upscaler = from.upscaler;
	to.fsr_sharpness = from.fsr_sharpness;
	to.scale = from.scale;
	to.shader = from.shader;
	to.aspect = from.aspect;
	to.border = from.border;
	to.smooth = from.smooth;
	to.scanlines = from.scanlines;
	to.deinterlace = from.deinterlace;
	to.transparent_meshes = from.transparent_meshes;
	to.volume = from.volume;
	to.audio_interp = from.audio_interp;
	to.threaded_scsp = from.threaded_scsp;
	to.region = from.region;
	to.cartridge = from.cartridge;
	to.cd_speed = from.cd_speed;
	to.sh2_clock = from.sh2_clock;
	to.sh2_cache = from.sh2_cache;
	to.cdblock_lle = from.cdblock_lle;
	to.pad_type = from.pad_type;
	for (int i = 0; i < Settings::kButtons; i++)
		to.buttons[i] = from.buttons[i];
}
} // namespace

void Settings::Load()
{
	// off by default (2.0); the host tests turn the default on (orbis_paths.cpp reads the same variable)
	if (const char* d = getenv("YMIR_LOGS_DEFAULT"))
		debug_logs = atoi(d) != 0;
	if (LoadFrom(IniPath()))
		OrbisLog("[settings] loaded %s", IniPath().c_str());
}

bool Settings::LoadFrom(const std::string& path)
{
	FILE* f = fopen(path.c_str(), "r");
	if (!f)
		return false;
	char line[1024];
	while (fgets(line, sizeof(line), f))
	{
		line[strcspn(line, "\r\n")] = 0;
		char* eq = strchr(line, '=');
		if (!eq || line[0] == '#' || line[0] == ';')
			continue;
		*eq = 0;
		const std::string key = line;
		const char* val = eq + 1;
		const int n = atoi(val);
		if (key == "scale")
			scale = Clamp(n, 0, 2);
		else if (key == "shader")
			shader = Clamp(n, 0, kShaderCount - 1);
		else if (key == "upscaler")
			upscaler = Clamp(n, 0, 2);
		else if (key == "fsr_sharpness")
			fsr_sharpness = Clamp(n, 0, 20);
		else if (key == "aspect")
			aspect = Clamp(n, 0, 3);
		else if (key == "border")
			border = Clamp(n, 0, 2);
		else if (key == "border_download")
			border_download = n != 0;
		else if (key == "smooth")
			smooth = n != 0;
		else if (key == "scanlines")
			scanlines = n != 0;
		else if (key == "deinterlace")
			deinterlace = n != 0;
		else if (key == "transparent_meshes")
			transparent_meshes = n != 0;
		else if (key == "show_fps")
			show_fps = n != 0;
		else if (key == "screenshots")
			screenshots = n != 0;
		else if (key == "launch_anim")
			launch_anim = n != 0;
		else if (key == "audio")
			audio = n != 0;
		else if (key == "volume")
			volume = Clamp(n, 0, 100);
		else if (key == "audio_interp")
			audio_interp = Clamp(n, 0, 1);
		else if (key == "threaded_scsp")
			threaded_scsp = n != 0;
		else if (key == "ff_speed")
			ff_speed = n == 0 || n > 200 ? 0 : Clamp(n, 150, 200); // 3.11.5: 300% and up ran at the console's most anyway: unlimited
		else if (key == "rewind")
			rewind = n != 0;
		else if (key == "resume")
			resume = n != 0;
		else if (key == "region")
			region = Clamp(n, 0, 3);
		else if (key == "cartridge")
			cartridge = Clamp(n, 0, 6);
		else if (key == "cart_shared")
			cart_shared = n != 0;
		else if (key == "cd_speed")
			cd_speed = Clamp(n, 0, 2);
		else if (key == "sh2_clock")
			sh2_clock = Clamp(n, 0, 6); // 3.11.6: 7-9 (300-500%, gone) read as 6, 200%: not round to 25%
		else if (key == "sh2_cache")
			sh2_cache = n != 0;
		else if (key == "cdblock_lle")
			cdblock_lle = n != 0;
		else if (key == "pad_type")
			pad_type = Clamp(n, 0, 3);
		else if (key.compare(0, 4, "btn_") == 0)
		{
			for (int i = 0; i < kButtons; i++)
				if (key == kButtonKeys[i])
					buttons[i] = Clamp(n, 0, kPs5ButtonChoices - 1);
		}
		else if (key == "state_slot")
			state_slot = Clamp(n, 1, 10);
		else if (key == "covers_download")
			covers_download = n != 0;
		else if (key == "language")
			language = Clamp(n, 0, 4);
		else if (key == "renderer")
			renderer = Clamp(n, 0, 1);
		else if (key == "perf_log")
			perf_log = n != 0;
		else if (key == "debug_logs")
			debug_logs = n != 0;
		else if (key == "shelf_family")
			shelf_family = Clamp(n, 0, 64);
		else if (key == "shelf_letter")
			shelf_letter = Clamp(n, 0, 27);
		else if (key == "shelf_sort")
			shelf_sort = Clamp(n, 0, 3);
		else if (key == "last_tab")
			last_tab = Clamp(n, 0, 3);
		else if (key == "last_rom")
			last_rom = val;
	}
	fclose(f);
	return true;
}

void Settings::Save() const
{
	if (g_game_active && g_game_own)
	{
		// the game's keys to its own file; the global file keeps the global values of those keys
		SaveTo(g_game_ini, true);
		Settings global = *this;
		CopyGameKeys(global, g_global);
		global.SaveTo(IniPath(), false);
		return;
	}
	SaveTo(IniPath(), false);
}

bool Settings::SaveTo(const std::string& path, bool game_keys_only) const
{
	const std::string tmp = path + ".tmp";
	FILE* f = fopen(tmp.c_str(), "w");
	if (!f)
	{
		OrbisLog("[settings] can't write %s", tmp.c_str());
		return false;
	}
	fprintf(f, game_keys_only ? "# SSYmirPS5: this game's own settings\n" : "# SSYmirPS5\n");
	fprintf(f, "upscaler=%d\n", upscaler);
	fprintf(f, "fsr_sharpness=%d\n", fsr_sharpness);
	fprintf(f, "scale=%d\n", scale);
	fprintf(f, "shader=%d\n", shader);
	fprintf(f, "aspect=%d\n", aspect);
	fprintf(f, "border=%d\n", border);
	fprintf(f, "smooth=%d\n", smooth ? 1 : 0);
	fprintf(f, "scanlines=%d\n", scanlines ? 1 : 0);
	fprintf(f, "deinterlace=%d\n", deinterlace ? 1 : 0);
	fprintf(f, "transparent_meshes=%d\n", transparent_meshes ? 1 : 0);
	if (!game_keys_only)
	{
		fprintf(f, "show_fps=%d\n", show_fps ? 1 : 0);
		fprintf(f, "screenshots=%d\n", screenshots ? 1 : 0);
		fprintf(f, "launch_anim=%d\n", launch_anim ? 1 : 0);
		fprintf(f, "audio=%d\n", audio ? 1 : 0);
	}
	fprintf(f, "volume=%d\n", volume);
	fprintf(f, "audio_interp=%d\n", audio_interp);
	fprintf(f, "threaded_scsp=%d\n", threaded_scsp ? 1 : 0);
	if (!game_keys_only)
	{
		fprintf(f, "ff_speed=%d\n", ff_speed);
		fprintf(f, "rewind=%d\n", rewind ? 1 : 0);
		fprintf(f, "resume=%d\n", resume ? 1 : 0);
	}
	fprintf(f, "region=%d\n", region);
	fprintf(f, "cartridge=%d\n", cartridge);
	if (!game_keys_only)
		fprintf(f, "cart_shared=%d\n", cart_shared ? 1 : 0);
	fprintf(f, "cd_speed=%d\n", cd_speed);
	fprintf(f, "sh2_clock=%d\n", sh2_clock);
	fprintf(f, "sh2_cache=%d\n", sh2_cache ? 1 : 0);
	fprintf(f, "cdblock_lle=%d\n", cdblock_lle ? 1 : 0);
	fprintf(f, "pad_type=%d\n", pad_type);
	for (int i = 0; i < kButtons; i++)
		fprintf(f, "%s=%d\n", kButtonKeys[i], buttons[i]);
	if (!game_keys_only)
	{
		fprintf(f, "state_slot=%d\n", state_slot);
		fprintf(f, "covers_download=%d\n", covers_download ? 1 : 0);
		fprintf(f, "border_download=%d\n", border_download ? 1 : 0);
		fprintf(f, "language=%d\n", language);
		fprintf(f, "renderer=%d\n", renderer);
		fprintf(f, "perf_log=%d\n", perf_log ? 1 : 0);
		fprintf(f, "debug_logs=%d\n", debug_logs ? 1 : 0);
		fprintf(f, "shelf_family=%d\n", shelf_family);
		fprintf(f, "shelf_letter=%d\n", shelf_letter);
		fprintf(f, "shelf_sort=%d\n", shelf_sort);
		fprintf(f, "last_tab=%d\n", last_tab);
		fprintf(f, "last_rom=%s\n", last_rom.c_str());
	}
	bool ok = ferror(f) == 0;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
	{
		remove(tmp.c_str());
		OrbisLog("[settings] can't save %s (disk full?)", path.c_str());
		return false;
	}
	return true;
}

void BeginGame(const std::string& game_ini)
{
	EndGame();
	g_game_ini = game_ini;
	g_game_active = true;
	g_global = Config();
	Settings own = Config();
	g_game_own = own.LoadFrom(game_ini);
	if (g_game_own)
	{
		CopyGameKeys(Config(), own);
		OrbisLog("[settings] this game's own settings: %s", game_ini.c_str());
	}
}

void EndGame()
{
	if (!g_game_active)
		return;
	if (g_game_own)
	{
		CopyGameKeys(Config(), g_global);
		OrbisLog("[settings] back to the global settings");
	}
	g_game_active = g_game_own = false;
	g_game_ini.clear();
}

bool GameHasOwnSettings()
{
	return g_game_active && g_game_own;
}

bool SetGameOwnSettings(bool own)
{
	if (!g_game_active || own == g_game_own)
		return g_game_active;
	if (own)
	{
		const size_t slash = g_game_ini.find_last_of('/');
		if (slash != std::string::npos)
			OrbisMkdirs(g_game_ini.substr(0, slash));
		g_global = Config(); // what the global settings are now
		if (!Config().SaveTo(g_game_ini, true))
			return false;
		g_game_own = true;
		OrbisLog("[settings] the game keeps its own settings: %s", g_game_ini.c_str());
	}
	else
	{
		remove(g_game_ini.c_str());
		CopyGameKeys(Config(), g_global);
		g_game_own = false;
		OrbisLog("[settings] the game uses the global settings again");
	}
	Config().Save();
	return true;
}
} // namespace fe
