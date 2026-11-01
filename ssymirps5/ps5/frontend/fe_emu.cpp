// SSYmirPS5 frontend: the core and its libretro callbacks (fe_emu.h).
//
// Frame pacing, as in Snes9x PS5:
//   - 60 Hz games (NTSC, 59.92 Hz): Present waits for the flip, so the console's vsync paces the emulation, and
//     the resampler (44.1 -> 48 kHz) stretches the sound by up to 0.5% to keep about 60 ms queued.
//   - 50 Hz games (PAL): the audio clock paces instead (wait while more than 60 ms is queued); the flips don't
//     wait (each frame still waits, before writing, for the buffer it last flipped).
//   - In any case, if more than 120 ms is queued (vsync not blocking for some reason), wait on audio.
//   - Fast forward (hold R2): several frames per flip, no sound.
//
// SPDX-License-Identifier: MIT

#include "fe_emu.h"
#include "fe_screenshot.h"
#include "fe_cheats.h"

#include "fe_border.h"
#include "ProsperoNotify.h"
#include "fe_games.h"
#include "fe_gamelog.h"
#include "fe_i18n.h"
#include "fe_settings.h"
#include "fe_text.h"
#include "fe_style.h"
#include "fe_ffpace.h"
#include "fe_recent.h"

#include "OrbisPaths.h"
#include "ProsperoAudio.h"
#include "ProsperoCrash.h"
#include "ProsperoCrt.h"
#include "ProsperoGpu.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include "libretro.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <vector>

#include <zlib.h>

std::string FindSaturnBiosQuiet();
extern "C" int ymir_ps5_debug_state(char* buf, size_t size); // apps/ymir-libretro (the PS5 patch)
extern "C" bool ymir_ps5_cart_backup(uint8_t** data, size_t* size);
extern "C" bool ymir_ps5_format_backup(int which);
namespace emu
{
const Choice kAspects[] = {
	{"TV (4:3, the core's)", ""},
	{"Square pixels", ""},
	{"4:3", ""},
	{"16:9", ""},
};
const int kAspectCount = int(sizeof(kAspects) / sizeof(kAspects[0]));

const Choice kRegions[] = {
	{"Auto (the disc's)", "auto"},
	{"Japan", "japan"},
	{"North America", "north_america"},
	{"Europe", "europe"},
};
const int kRegionCount = int(sizeof(kRegions) / sizeof(kRegions[0]));

const Choice kCartridges[] = {
	{"Auto (RAM / ROM if the game needs it, else backup memory)", "auto"},
	{"None", "none"},
	{"1 MB DRAM expansion", "dram_8mbit"},
	{"4 MB DRAM expansion", "dram_32mbit"},
	{"ROM: King of Fighters '95", "rom_kof95"},
	{"ROM: Ultraman", "rom_ultraman"},
	{"Backup memory (4 Mbit)", "backup_4mbit"},
};
const int kCartridgeCount = int(sizeof(kCartridges) / sizeof(kCartridges[0]));

const Choice kCdSpeeds[] = {
	{"2x (the console's)", "2"},
	{"4x", "4"},
	{"8x", "8"},
};
const int kCdSpeedCount = int(sizeof(kCdSpeeds) / sizeof(kCdSpeeds[0]));

// 3.11.6: 300%, 400% and 500% are gone (more work for the console than it gains; the settings' index of one of them
// reads as 200%, the last one left: fe_settings.cpp)
const Choice kSh2Clocks[] = {
	{"25%", "25"}, {"50%", "50"}, {"75%", "75"}, {"100% (recommended)", "100"}, {"125%", "125"},
	{"150%", "150"}, {"200%", "200"},
};
const int kSh2ClockCount = int(sizeof(kSh2Clocks) / sizeof(kSh2Clocks[0]));

const Choice kAudioInterps[] = {
	{"Linear (accurate)", "linear"},
	{"Nearest neighbor", "nearest_neighbor"},
};
const int kAudioInterpCount = int(sizeof(kAudioInterps) / sizeof(kAudioInterps[0]));

const Choice kUpscalers[] = {
	{"Off", ""},
	{"AMD FSR 1.0", "FSR 1.0"}, // the shader preset's name in shaders/presets.txt
	{"SMAA + AMD FSR 1.0", "SMAA + FSR 1.0"}, // SMAA's edge antialiasing first (polygon edges), then FSR
};
const int kUpscalerCount = int(sizeof(kUpscalers) / sizeof(kUpscalers[0]));

// the core's devices (apps/ymir-libretro: RETRO_DEVICE_ANALOG and its subclasses 1 and 2)
const Choice kPadTypes[] = {
	{"Saturn Control Pad", ""},
	{"3D Control Pad (analog)", ""},
	{"Arcade Racer (wheel)", ""},
	{"Mission Stick", ""},
};
const int kPadTypeCount = int(sizeof(kPadTypes) / sizeof(kPadTypes[0]));

const PadButton kPs5Buttons[] = {
	{"Cross", SCE_PAD_BUTTON_CROSS},
	{"Circle", SCE_PAD_BUTTON_CIRCLE},
	{"Square", SCE_PAD_BUTTON_SQUARE},
	{"Triangle", SCE_PAD_BUTTON_TRIANGLE},
	{"L1", SCE_PAD_BUTTON_L1},
	{"R1", SCE_PAD_BUTTON_R1},
	{"L2", SCE_PAD_BUTTON_L2},
	{"R2", SCE_PAD_BUTTON_R2},
	{"OPTIONS", SCE_PAD_BUTTON_OPTIONS},
	{"None", 0},
};
const int kPs5ButtonCount = int(sizeof(kPs5Buttons) / sizeof(kPs5Buttons[0]));

// Ymir's RetroPad layout (apps/ymir-libretro, read_saturn_buttons): B/A/R = Saturn A/B/C, Y/X/L = X/Y/Z, L2/R2 = L/R.
// By position on the PS5 pad: Square Cross Circle = A B C, L1 Triangle R1 = X Y Z, L2 R2 = L R.
const ConsoleButton kConsoleButtons[kConsoleButtonCount] = {
	{"A", "btn_a", RETRO_DEVICE_ID_JOYPAD_B, 2},
	{"B", "btn_b", RETRO_DEVICE_ID_JOYPAD_A, 0},
	{"C", "btn_c", RETRO_DEVICE_ID_JOYPAD_R, 1},
	{"X", "btn_x", RETRO_DEVICE_ID_JOYPAD_Y, 4},
	{"Y", "btn_y", RETRO_DEVICE_ID_JOYPAD_X, 3},
	{"Z", "btn_z", RETRO_DEVICE_ID_JOYPAD_L, 5},
	{"L", "btn_l", RETRO_DEVICE_ID_JOYPAD_L2, 6},
	{"R", "btn_r", RETRO_DEVICE_ID_JOYPAD_R2, 7},
	{"Start", "btn_start", RETRO_DEVICE_ID_JOYPAD_START, 8},
};
std::string BiosFolder()
{
	static std::string cached;
	static double at = -10;
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	const double now = ts.tv_sec + ts.tv_nsec / 1e9;
	if (now - at > 2.0)
	{
		cached = FindSaturnBiosQuiet();
		at = now;
	}
	return cached;
}
int EffectiveShader()
{
	const fe::Settings& c = fe::Config();
	if (c.upscaler > 0 && c.upscaler < kUpscalerCount && ps5video::GpuActive())
	{
		static int index[8] = {-2, -2, -2, -2, -2, -2, -2, -2}; // each upscaler's shader, looked up once
		int& sh = index[c.upscaler];
		if (sh == -2)
		{
			sh = -1;
			for (int i = 1; i < ps5video::ShaderCount(); ++i)
				if (strcmp(ps5video::ShaderName(i), kUpscalers[c.upscaler].value) == 0)
					sh = i;
		}
		if (sh > 0)
			return sh;
	}
	return c.shader;
}
void Osd(const std::string& text);
} // namespace emu

namespace
{
using fe::T;

// The emulator's log lines: boot.log (when Debug logs is on) and the game log (always, while a game is open)
void ELog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void ELog(const char* fmt, ...)
{
	char buf[2048];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	OrbisLog("%s", buf);
	fe::gamelog::Line("%s", buf);
}

// printf into a std::string (the translated formats)
std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}
constexpr int kTargetQueued = ps5audio::kRate * 60 / 1000; // 60 ms of sound
constexpr int kRewindEvery = 6; // frames between rewind snapshots (a Saturn state is several MB)
constexpr size_t kRewindBytes = 384u * 1024 * 1024;

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

std::string Lower(std::string s)
{
	for (char& c : s)
		c = char(tolower(uint8_t(c)));
	return s;
}

std::string ExtOf(const std::string& p)
{
	const size_t dot = p.find_last_of('.');
	const size_t slash = p.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(p.substr(dot));
}

bool FileExists(const std::string& p)
{
	struct stat st = {};
	return stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool ReadFile(const std::string& path, std::vector<uint8_t>& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.clear();
	uint8_t buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
		out.insert(out.end(), buf, buf + n);
	fclose(f);
	return true;
}

// data -> path.part, flushed to the disk (fsync) and closed without error, read back and compared, then renamed
// over path: the old file or the whole new one, never a cut or wrong one (a full disk or an I/O error shows up at
// fflush / fsync / fclose; a write the storage got wrong, in the comparison).
bool WriteFileAtomic(const std::string& path, const void* data, size_t size)
{
	const std::string tmp = path + ".part";
	FILE* f = fopen(tmp.c_str(), "wb");
	if (!f)
		return false;
	bool ok = fwrite(data, 1, size, f) == size;
	ok = fflush(f) == 0 && ok;
	ok = fsync(fileno(f)) == 0 && ok;
	ok = fclose(f) == 0 && ok;
	if (ok)
	{
		std::vector<uint8_t> back;
		ok = ReadFile(tmp, back) && back.size() == size && (size == 0 || memcmp(back.data(), data, size) == 0);
		if (!ok)
			ELog("[emu] %s: what was written doesn't read back the same: not used", tmp.c_str());
	}
	if (!ok || rename(tmp.c_str(), path.c_str()) != 0)
	{
		remove(tmp.c_str());
		return false;
	}
	return true;
}

uint64_t Fnv(const uint8_t* p, size_t n)
{
	uint64_t h = 1469598103934665603ull;
	for (size_t i = 0; i < n; i++)
		h = (h ^ p[i]) * 1099511628211ull;
	return h;
}

struct State
{
	bool inited = false;
	bool loaded = false;
	std::string path, name, dir, ext; // the game; ext without the dot
	std::string load_path; // what the core opens: the path, or a .m3u's copy with its discs' full paths
	fe::System system = fe::System::Saturn;
	std::vector<uint8_t> rom; // a ROM read from a .zip or .7z
	retro_game_info_ext info_ext = {};
	std::string sys_dir, save_dir;

	// the core's options, as libretro variables
	std::map<std::string, std::string> opts;
	bool opts_changed = false;

	// video
	unsigned pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
	std::vector<uint32_t> frame;
	int fw = 0, fh = 0;
	int av_w = 0, av_h = 0; // the core's picture size when the game was loaded (before the first frame gives fw, fh)
	bool new_frame = false;
	// 3.4: the deinterlacing on the GPU (the core draws one field a frame, as the Saturn does); the field the last
	// frame drew (0: its even lines, 1: its odd ones)
	bool gpu_deint = false;
	int field_parity = 0;
	double fps = 60.0, core_aspect = 4.0 / 3.0;
	double sample_rate = 44100.0;

	// sound: the core's samples of this frame, and the resampler's state
	std::vector<int16_t> in;
	bool mute = false; // fast forward, rewind: drop the sound
	double frac = 0;
	int16_t hist[3][2] = {}; // the three frames before this call's first one (the cubic filter reads 4)
	// the rate control and the ring, measured between two log lines
	double adj_sum = 0, adj_min = 2, adj_max = 0;
	uint64_t adj_n = 0;
	int queued_min = 1 << 30, queued_max = 0;
	std::vector<int16_t> out;
	std::vector<int16_t> silence; // the zeros that refill a dry ring
	int pads_applied = -1; // the pad type / adapter last given to the core

	// pads
	ps5input::PadState pads[ps5input::kMaxPads];
	uint32_t prev_p1 = 0;
	// the core's disc control (games of several discs: an .m3u)
	retro_disk_control_ext_callback disk = {};
	bool have_disk = false;
	// the game log's timeline (DiagReport)
	struct Diag
	{
		double next = 0;      // when the next timeline line is due
		int step = 0;         // which of kDiagTimes
		double last_picture = 0; // the last time a frame had something other than black
		double black_warned = 0;
		uint64_t frames = 0, video_calls = 0, audio_frames = 0;
		int peak = 0;         // the loudest sample since the last line
		double last_sound = 0;
		uint32_t last_pc = 0;
		double pc_same_since = 0;
		double core_ms = 0;
		uint64_t core_n = 0;
	} diag; // the touchpad, since it went down, was used as a modifier (not a tap)
	bool wait_release = true;

	// fast forward, rewind
	bool turbo = false, rewinding = false;
	std::deque<std::vector<uint8_t>> rewind;
	size_t rewind_bytes = 0;
	int rewind_tick = 0;

	// saves
	uint64_t sram_hash = 0;
	size_t sram_size = 0;
	size_t cart_size = 0; // the backup cartridge in the slot (0: none)
	uint64_t cart_hash = 0;
	double next_sram_check = 0;

	// messages and the FPS counter
	std::string osd;
	double osd_until = 0;
	double ff_credit = 0; // fast forward's frames owed, carried between passes (fe_ffpace.h)
	double ff_last = 0; // 3.11.4: the last pass's time
	// 3.11.4: the speed fast forward really runs at, for the corner while R3 is held and for the log
	double ff_since = 0, ff_started = 0, ff_shown = 0;
	long ff_frames = 0, ff_total = 0;
	// 3.11: the slot's picture while touchpad + Left / Right says which slot is chosen
	std::vector<uint32_t> slot_thumb;
	int slot_thumb_w = 0, slot_thumb_h = 0;
	double slot_thumb_until = 0;
	bool overlay_was_drawn = false;
	int fps_frames = 0;
	double fps_since = 0, fps_shown = 0;
	uint64_t frames = 0;
	double next_stats = 0;
	int stats_reports = 0;
	// the shader's cost, measured every 2 s; a warning once per game when it can't keep up
	double next_shader_check = 0;
	double shader_ms_sum = 0;
	int shader_ms_n = 0;
	bool shader_warned = false;
	// saves: the backup copy of the battery save is made once per game, before its first write
	bool sram_backed_up = false;
	bool sram_backup_warned = false;
	double last_shader_ms = 0;
};
State g;

// ---- libretro callbacks -------------------------------------------------------------------------------------
void CoreLog(enum retro_log_level level, const char* fmt, ...)
{
	char buf[1024];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	size_t n = strlen(buf);
	while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
		buf[--n] = 0;
	static const char* const kLevel[] = {"debug", "info", "warn", "error"};
	fe::gamelog::CoreMessage(int(level), buf);
	if (level == RETRO_LOG_DEBUG)
		return;
	OrbisLog("[core] %s%s%s", level == RETRO_LOG_INFO ? "" : kLevel[level < 4 ? level : 3],
		level == RETRO_LOG_INFO ? "" : ": ", buf);
}

bool Environment(unsigned cmd, void* data)
{
	switch (cmd)
	{
		case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
			*static_cast<const char**>(data) = g.sys_dir.c_str();
			return true;
		case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
			*static_cast<const char**>(data) = g.save_dir.c_str();
			return true;
		case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
		{
			const unsigned f = *static_cast<const unsigned*>(data);
			if (f != RETRO_PIXEL_FORMAT_RGB565 && f != RETRO_PIXEL_FORMAT_0RGB1555 && f != RETRO_PIXEL_FORMAT_XRGB8888)
				return false;
			g.pixel_format = f;
			return true;
		}
		case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
			static_cast<retro_log_callback*>(data)->log = CoreLog;
			return true;
		case RETRO_ENVIRONMENT_GET_VARIABLE:
		{
			retro_variable* v = static_cast<retro_variable*>(data);
			auto it = v->key ? g.opts.find(v->key) : g.opts.end();
			if (it == g.opts.end())
			{
				v->value = nullptr;
				return false;
			}
			v->value = it->second.c_str();
			return true;
		}
		case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
			*static_cast<bool*>(data) = g.opts_changed;
			g.opts_changed = false;
			return true;
		case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
			*static_cast<unsigned*>(data) = 0; // the core then uses SET_VARIABLES (accepted below)
			return true;
		case RETRO_ENVIRONMENT_GET_CAN_DUPE:
			*static_cast<bool*>(data) = true;
			return true;
		case RETRO_ENVIRONMENT_GET_INPUT_BITMASKS:
			return true;
		case RETRO_ENVIRONMENT_GET_GAME_INFO_EXT:
			if (!g.loaded && g.path.empty())
				return false;
			*static_cast<const retro_game_info_ext**>(data) = &g.info_ext;
			return true;
		case RETRO_ENVIRONMENT_SET_GEOMETRY:
		{
			const retro_game_geometry* geo = static_cast<const retro_game_geometry*>(data);
			if (geo->aspect_ratio > 0.1f)
				g.core_aspect = geo->aspect_ratio;
			return true;
		}
		case RETRO_ENVIRONMENT_SET_SYSTEM_AV_INFO:
		{
			const retro_system_av_info* av = static_cast<const retro_system_av_info*>(data);
			if (av->geometry.aspect_ratio > 0.1f)
				g.core_aspect = av->geometry.aspect_ratio;
			if (av->timing.fps > 1)
				g.fps = av->timing.fps;
			if (av->timing.sample_rate > 1000)
				g.sample_rate = av->timing.sample_rate;
			ELog("[emu] av info: %.3f fps, %.0f Hz", g.fps, g.sample_rate);
			return true;
		}
		case RETRO_ENVIRONMENT_SET_MESSAGE:
		{
			const retro_message* m = static_cast<const retro_message*>(data);
			if (m && m->msg)
				emu::Osd(m->msg);
			return true;
		}
		case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE:
			if (!data)
				return false;
			g.disk = *static_cast<const retro_disk_control_ext_callback*>(data);
			g.have_disk = true;
			return true;
		case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE:
			if (!data)
				return false;
			{
				const retro_disk_control_callback* cb = static_cast<const retro_disk_control_callback*>(data);
				g.disk = retro_disk_control_ext_callback{};
				g.disk.set_eject_state = cb->set_eject_state;
				g.disk.get_eject_state = cb->get_eject_state;
				g.disk.get_image_index = cb->get_image_index;
				g.disk.set_image_index = cb->set_image_index;
				g.disk.get_num_images = cb->get_num_images;
				g.have_disk = true;
			}
			return true;
		case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
			*static_cast<unsigned*>(data) = 1;
			return true;
		// accepted and ignored
		case RETRO_ENVIRONMENT_SET_VARIABLES:
		case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
		case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
		case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
		case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
		case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
		case RETRO_ENVIRONMENT_SET_SUBSYSTEM_INFO:
		case RETRO_ENVIRONMENT_SET_CONTENT_INFO_OVERRIDE:
		case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
		case RETRO_ENVIRONMENT_SET_MINIMUM_AUDIO_LATENCY:
		case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
			return true;
		default:
			return false;
	}
}

inline uint32_t Expand565(uint16_t c)
{
	const uint32_t r = (c >> 11) & 31, gg = (c >> 5) & 63, b = c & 31;
	return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((gg << 2) | (gg >> 4)) << 8) | ((b << 3) | (b >> 2));
}

inline uint32_t Expand555(uint16_t c)
{
	const uint32_t r = (c >> 10) & 31, gg = (c >> 5) & 31, b = c & 31;
	return 0xff000000u | (((r << 3) | (r >> 2)) << 16) | (((gg << 3) | (gg >> 2)) << 8) | ((b << 3) | (b >> 2));
}

void Video(const void* data, unsigned w, unsigned h, size_t pitch)
{
	if (!data || w == 0 || h == 0)
		return; // a duplicated frame: keep the last one
	// 3.4: an interlaced picture (448 lines or more: the high-resolution modes) with the GPU deinterlacing it: which
	// field is the new one, from the lines that changed since the last frame
	const bool interlaced = g.gpu_deint && h >= 400 && int(w) == g.fw && int(h) == g.fh && !g.frame.empty();
	unsigned changed[2] = {0, 0};
	g.fw = int(w);
	g.fh = int(h);
	g.frame.resize(size_t(w) * h);
	const uint8_t* src = static_cast<const uint8_t*>(data);
	for (unsigned y = 0; y < h; y++)
	{
		uint32_t* out = g.frame.data() + size_t(y) * w;
		if (g.pixel_format == RETRO_PIXEL_FORMAT_XRGB8888)
		{
			const uint32_t* in = reinterpret_cast<const uint32_t*>(src + y * pitch);
			if (interlaced)
			{
				uint32_t diff = 0;
				for (unsigned x = 0; x < w; x++)
				{
					const uint32_t v = in[x] | 0xff000000u;
					diff |= v ^ out[x];
					out[x] = v;
				}
				changed[y & 1] += diff != 0;
			}
			else
				for (unsigned x = 0; x < w; x++)
					out[x] = in[x] | 0xff000000u;
		}
		else
		{
			const uint16_t* in = reinterpret_cast<const uint16_t*>(src + y * pitch);
			if (g.pixel_format == RETRO_PIXEL_FORMAT_RGB565)
				for (unsigned x = 0; x < w; x++)
					out[x] = Expand565(in[x]);
			else
				for (unsigned x = 0; x < w; x++)
					out[x] = Expand555(in[x]);
		}
	}
	if (interlaced)
	{
		// the field that changed more is the one drawn now; a still picture: the fields take turns, as they do
		if (changed[0] > changed[1] + 2)
			g.field_parity = 0;
		else if (changed[1] > changed[0] + 2)
			g.field_parity = 1;
		else
			g.field_parity ^= 1;
	}
	ps5gpu::SetDeinterlace(g.gpu_deint && h >= 400, g.field_parity);
	g.new_frame = true;
	g.diag.video_calls++;
}

void AudioSample(int16_t l, int16_t r)
{
	if (g.mute)
		return;
	g.in.push_back(l);
	g.in.push_back(r);
}

size_t AudioBatch(const int16_t* data, size_t frames)
{
	g.diag.audio_frames += frames;
	for (size_t i = 0; i < frames * 2; i += 8)
	{
		const int v = data[i] < 0 ? -int(data[i]) : int(data[i]);
		if (v > g.diag.peak)
			g.diag.peak = v;
	}
	if (!g.mute)
		g.in.insert(g.in.end(), data, data + frames * 2);
	return frames;
}

void InputPoll()
{
	ps5input::Poll();
	for (int p = 0; p < ps5input::kMaxPads; p++)
		g.pads[p] = ps5input::Snapshot(p);
}

// The RetroPad Ymir reads, from the player's button layout (Settings, CONTROLS; by position by default:
// Square/Cross/Circle = A/B/C, L1/Triangle/R1 = X/Y/Z, L2/R2 = L/R, OPTIONS = Start).
// stick_dpad: the left stick also moves the D-pad (the digital Saturn pad); the analog devices read it as a stick.
uint16_t RetroPadBits(const ps5input::PadState& p, bool dpad_off, bool stick_dpad)
{
	if (!p.connected)
		return 0;
	const fe::Settings& cfg = fe::Config();
	uint16_t bits = 0;
	for (int i = 0; i < emu::kConsoleButtonCount; i++)
	{
		const int b = cfg.buttons[i];
		const unsigned bit = b >= 0 && b < emu::kPs5ButtonCount ? emu::kPs5Buttons[b].bit : 0;
		if (bit && (p.raw_buttons & bit))
			bits |= uint16_t(1u << emu::kConsoleButtons[i].retro_id);
	}
	struct Dir
	{
		unsigned id;
		uint32_t bit;
	};
	static const Dir kDirs[] = {
		{RETRO_DEVICE_ID_JOYPAD_UP, SCE_PAD_BUTTON_UP},
		{RETRO_DEVICE_ID_JOYPAD_DOWN, SCE_PAD_BUTTON_DOWN},
		{RETRO_DEVICE_ID_JOYPAD_LEFT, SCE_PAD_BUTTON_LEFT},
		{RETRO_DEVICE_ID_JOYPAD_RIGHT, SCE_PAD_BUTTON_RIGHT},
	};
	const uint32_t dirs = stick_dpad ? p.buttons : p.raw_buttons;
	if (!dpad_off)
		for (const Dir& d : kDirs)
			if (dirs & d.bit)
				bits |= uint16_t(1u << d.id);
	return bits;
}

// a stick axis (0..255, 128 centred) as libretro's -32768..32767, with a small dead zone
int16_t Axis(uint8_t v)
{
	const int c = int(v) - 128;
	if (c > -10 && c < 10)
		return 0;
	return int16_t(std::max(-32768, std::min(32767, c * 258)));
}

int16_t InputState(unsigned port, unsigned device, unsigned index, unsigned id)
{
	if (port >= unsigned(ps5input::kMaxPads))
		return 0;
	// The touchpad is the hot-key modifier for player 1: the D-pad stays with the hot keys while it is held.
	// While the buttons that closed a menu (Cross, Circle) or opened it (the touchpad) are still held,
	// player 1's pad is not passed on: Resume with Cross must not press B in the game.
	if (port == 0 && g.wait_release)
		return 0;
	const ps5input::PadState& p = g.pads[port];
	const bool analog_dev = port == 0 && fe::Config().pad_type % emu::kPadTypeCount != 0;
	if ((device & RETRO_DEVICE_MASK) == RETRO_DEVICE_ANALOG)
	{
		if (!p.connected || id > RETRO_DEVICE_ID_ANALOG_Y)
			return 0;
		if (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
			return Axis(id == RETRO_DEVICE_ID_ANALOG_X ? p.lx : p.ly);
		if (index == RETRO_DEVICE_INDEX_ANALOG_RIGHT)
			return Axis(id == RETRO_DEVICE_ID_ANALOG_X ? p.rx : p.ry);
		return 0;
	}
	if ((device & RETRO_DEVICE_MASK) != RETRO_DEVICE_JOYPAD || index != 0)
		return 0;
	const bool mod = port == 0 && (p.raw_buttons & SCE_PAD_BUTTON_TOUCH_PAD) != 0;
	const uint16_t bits = RetroPadBits(p, mod, !analog_dev);
	if (id == RETRO_DEVICE_ID_JOYPAD_MASK)
		return int16_t(bits);
	return id < 16 ? int16_t((bits >> id) & 1) : 0;
}

// ---- sound: the core's 44.1 kHz -> 48 kHz, the rate steered by the ring's fill ---------------------------------
// ---- the performance log (Settings, SYSTEM, Performance log): logs/perf-<system>-<game>.csv, a row a second -----
// What a real game does to the frame budget, for checking FMV, CD audio and long sessions on the console:
// the core's time per frame (average and worst), the presentation's (the shader on the CPU, or the hand-over to
// the GPU), the GPU shader's own time, the frames that came late, the sound queued and the underruns, the rate
// control, the CD drive (CD audio playing, data being read) and the PCM chip, and the drift between the sound
// the core made and the frames it ran (it should stay flat over hours: a growing one is sound running ahead of,
// or behind, the picture).
struct Perf
{
	FILE* f = nullptr;
	double start = 0, next = 0, last_frame = 0;
	uint64_t frames = 0, rows = 0;
	double core_sum = 0, core_max = 0, present_sum = 0, frame_max = 0;
	int late = 0, cdda = 0, cdread = 0, pcm = 0;
	uint64_t underruns0 = 0;
	// over the whole session
	double audio_frames = 0, emu_frames = 0;
	double total_core = 0, worst_core = 0;
	uint64_t total_frames = 0, total_late = 0;
	double drift_min = 1e9, drift_max = -1e9;
};
Perf g_perf;

double PerfDrift()
{
	return g.sample_rate > 0 && g.fps > 0 ? (g_perf.audio_frames / g.sample_rate - g_perf.emu_frames / g.fps) * 1000.0 : 0;
}

void PerfStart()
{
	g_perf = Perf();
	if (!fe::Config().perf_log)
		return;
	std::string name = fe::Info(g.system).folder + std::string("-") + g.name;
	for (char& c : name)
		if (c == '/' || c == '\\' || c == ':' || uint8_t(c) < 32)
			c = '_';
	const std::string path = OrbisDir("logs") + "/perf-" + name + ".csv";
	g_perf.f = fopen(path.c_str(), "w");
	if (!g_perf.f)
	{
		OrbisLog("[perf] can't write %s", path.c_str());
		return;
	}
	fprintf(g_perf.f, "# SSYmirPS5 %s performance log: %s (%s), %.3f fps, renderer %s, shader %s\n",
		SSYMIRPS5_VERSION, g.name.c_str(), emu::SystemName().c_str(), g.fps, ps5video::GpuActive() ? "GPU" : "CPU",
		ps5video::ShaderName(emu::EffectiveShader()));
	fprintf(g_perf.f, "time_s,frames,fps,core_ms_avg,core_ms_max,present_ms_avg,frame_ms_max,late_frames,shader_ms,"
					  "audio_queued_ms,underruns,rate,cd_audio_pct,cd_read_pct,pcm_pct,av_drift_ms\n");
	fflush(g_perf.f);
	g_perf.start = g_perf.next = g_perf.last_frame = Now();
	g_perf.next += 1.0;
	g_perf.underruns0 = ps5audio::Underruns();
	OrbisLog("[perf] logging to %s", path.c_str());
}

void PerfFrame(double core_ms, double present_ms, double shader_ms)
{
	if (!g_perf.f)
		return;
	const double now = Now();
	const double frame_ms = (now - g_perf.last_frame) * 1000.0;
	g_perf.last_frame = now;
	g_perf.frames++;
	g_perf.core_sum += core_ms;
	g_perf.core_max = std::max(g_perf.core_max, core_ms);
	g_perf.present_sum += present_ms;
	g_perf.frame_max = std::max(g_perf.frame_max, frame_ms);
	if (!g.turbo && !g.rewinding && frame_ms > 1500.0 / std::max(1.0, g.fps))
		g_perf.late++;
	g_perf.total_core += core_ms;
	g_perf.worst_core = std::max(g_perf.worst_core, core_ms);
	g_perf.total_frames++;
	if (now < g_perf.next)
		return;
	const double span = now - (g_perf.next - 1.0);
	const double n = double(std::max<uint64_t>(1, g_perf.frames));
	const uint64_t under = ps5audio::Underruns();
	const double drift = PerfDrift();
	g_perf.drift_min = std::min(g_perf.drift_min, drift);
	g_perf.drift_max = std::max(g_perf.drift_max, drift);
	g_perf.total_late += uint64_t(g_perf.late);
	fprintf(g_perf.f, "%.1f,%llu,%.2f,%.3f,%.3f,%.3f,%.2f,%d,%.3f,%.1f,%llu,%.4f,%.0f,%.0f,%.0f,%.2f\n",
		now - g_perf.start, (unsigned long long)g_perf.frames, g_perf.frames / std::max(1e-3, span), g_perf.core_sum / n,
		g_perf.core_max, g_perf.present_sum / n, g_perf.frame_max, g_perf.late, shader_ms,
		ps5audio::Queued() * 1000.0 / ps5audio::kRate, (unsigned long long)(under - g_perf.underruns0),
		g.adj_n ? g.adj_sum / double(g.adj_n) : 1.0, g_perf.cdda * 100.0 / n, g_perf.cdread * 100.0 / n,
		g_perf.pcm * 100.0 / n, drift);
	fflush(g_perf.f);
	g_perf.rows++;
	g_perf.underruns0 = under;
	g_perf.frames = 0;
	g_perf.core_sum = g_perf.core_max = g_perf.present_sum = g_perf.frame_max = 0;
	g_perf.late = g_perf.cdda = g_perf.cdread = g_perf.pcm = 0;
	g_perf.next = now + 1.0;
}

extern "C" void fe_chdcache_take(unsigned long long* hits, unsigned long long* misses);

void PerfStop()
{
	if (!g_perf.f)
		return;
	unsigned long long hits = 0, misses = 0;
	fe_chdcache_take(&hits, &misses);
	if (hits + misses)
		OrbisLog("[perf] CHD: %llu hunk(s) read, %llu decompressed ahead on the worker, %llu on the emulation thread",
			hits + misses, hits, misses);
	const double secs = Now() - g_perf.start;
	OrbisLog("[perf] %.0f s, %llu frames: core %.2f ms a frame on average, %.2f ms at worst; %llu late frames; "
			 "sound/picture drift %.1f to %.1f ms",
		secs, (unsigned long long)g_perf.total_frames,
		g_perf.total_frames ? g_perf.total_core / double(g_perf.total_frames) : 0.0, g_perf.worst_core,
		(unsigned long long)g_perf.total_late, g_perf.rows ? g_perf.drift_min : 0.0, g_perf.rows ? g_perf.drift_max : 0.0);
	fclose(g_perf.f);
	g_perf.f = nullptr;
}

void FlushAudio()
{
	const size_t frames = g.in.size() / 2;
	if (frames == 0)
		return;
	if (!g.turbo && !g.rewinding)
		g_perf.audio_frames += double(frames); // what the core made, for the sound/picture drift
	const fe::Settings& cfg = fe::Config();
	if (!cfg.audio || g.mute || !ps5audio::Available())
	{
		g.in.clear();
		return;
	}
	int queued = ps5audio::Queued();
	if (queued < kTargetQueued / 4)
	{
		// the ring ran (nearly) dry: a game just started, or came back from a menu, or the frame was late. Fill
		// it with silence up to the target at once; the rate control alone (0.5%) would take ~10 s to get there,
		// and play crackling meanwhile.
		g.silence.assign(size_t(kTargetQueued - queued) * 2, 0);
		ps5audio::Push(g.silence.data(), kTargetQueued - queued);
		queued = kTargetQueued;
	}
	double adj = double(kTargetQueued - queued) / kTargetQueued;
	adj = 1.0 + std::max(-1.0, std::min(1.0, adj)) * 0.005;
	const double step = g.sample_rate / (ps5audio::kRate * adj); // input frames per output frame
	g.adj_sum += adj;
	g.adj_n++;
	g.adj_min = std::min(g.adj_min, adj);
	g.adj_max = std::max(g.adj_max, adj);
	g.queued_min = std::min(g.queued_min, queued);
	g.queued_max = std::max(g.queued_max, queued);
	const double vol = cfg.volume / 100.0;
	g.out.clear();
	// 4-point, 3rd-order Hermite interpolation (Catmull-Rom) between the frames around each output frame: flat to
	// well above 10 kHz and with little aliasing, where linear interpolation dulled the treble and an 8-bit
	// fraction added noise. The frames before this call's first one are kept in g.hist.
	auto at = [&](long k, int c) -> float {
		return k < 0 ? float(g.hist[3 + k][c]) : float(g.in[size_t(k) * 2 + c]);
	};
	double pos = g.frac; // the output frame lies between input frames (pos - 2) and (pos - 1): see below
	while (pos < double(frames))
	{
		const long i = long(pos); // between frames i - 2 and i - 1 (both already here), with i, i - 3 around
		const float t = float(pos - double(i));
		for (int c = 0; c < 2; c++)
		{
			const float y0 = at(i - 3, c), y1 = at(i - 2, c), y2 = at(i - 1, c), y3 = at(i, c);
			const float c1 = 0.5f * (y2 - y0);
			const float c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
			const float c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
			const float v = float((((c3 * t + c2) * t + c1) * t + y1) * vol);
			g.out.push_back(int16_t(std::max(-32768.0f, std::min(32767.0f, std::nearbyint(v)))));
		}
		pos += step;
	}
	g.frac = pos - double(frames);
	// the last three frames, for the next call (read before g.hist is written: a short call reads g.hist too)
	int16_t last[3][2];
	for (int k = 0; k < 3; k++)
		for (int c = 0; c < 2; c++)
			last[k][c] = int16_t(at(long(frames) - 3 + k, c));
	memcpy(g.hist, last, sizeof(last));
	g.in.clear();
	ps5audio::Push(g.out.data(), int(g.out.size() / 2));
}

// ---- saves ------------------------------------------------------------------------------------------------
// Saves are kept in the system's folder (saves/Saturn/<game>.srm, states/Saturn/<game>.state3...).
std::string SaveDir(const char* kind)
{
	return OrbisDir(kind) + "/" + fe::Info(g.system).folder;
}

std::string SramPath()
{
	return SaveDir("saves") + "/" + g.name + ".srm";
}

// The game's own settings, when it has them: games/<system>/<game>.ini
std::string GameIniPath()
{
	// game-settings/ (1.5): games/ holds games now (OrbisRomRoots); a file of 1.0-1.4 there is moved over
	const std::string now = OrbisDir("game-settings") + "/" + fe::Info(g.system).folder + "/" + g.name + ".ini";
	const std::string before = OrbisDir("games") + "/" + fe::Info(g.system).folder + "/" + g.name + ".ini";
	if (!FileExists(now) && FileExists(before))
	{
		OrbisMkdirs(now.substr(0, now.find_last_of('/')));
		ELog("[emu] this game's settings moved: %s -> %s (%s)", before.c_str(), now.c_str(),
			rename(before.c_str(), now.c_str()) == 0 ? "ok" : strerror(errno));
	}
	return now;
}

std::string StatePath(int slot)
{
	return SaveDir("states") + "/" + g.name + ".state" + std::to_string(slot);
}

// Saves made by 1.0's first builds sit directly in saves/ and states/: the first game of that name to start takes
// them into its system's folder (as before, when that game was the one using them).
void MigrateSaves()
{
	OrbisMkdirs(SaveDir("saves"));
	OrbisMkdirs(SaveDir("states"));
	auto move = [](const std::string& from, const std::string& to) {
		if (FileExists(from) && !FileExists(to))
		{
			const bool ok = rename(from.c_str(), to.c_str()) == 0;
			ELog("[emu] moved %s -> %s (%s)", from.c_str(), to.c_str(), ok ? "ok" : "failed");
		}
	};
	move(OrbisDir("saves") + "/" + g.name + ".srm", SramPath());
	for (int slot = 1; slot <= 10; slot++)
		move(OrbisDir("states") + "/" + g.name + ".state" + std::to_string(slot), StatePath(slot));
}

void LoadSram()
{
	uint8_t* mem = static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
	// before the first retro_run the core reports the full size (0x10000); once running, only up to the last byte
	// that isn't 0xFF -- 0 for a save the game erased, which would then never be written. Keep the full size.
	const size_t size = retro_get_memory_size(RETRO_MEMORY_SAVE_RAM);
	g.sram_size = mem ? size : 0;
	g.sram_hash = mem && size ? Fnv(mem, size) : 0;
	if (!mem || !size)
		return;
	std::vector<uint8_t> data;
	// a disc set the shelf grouped (1.5): its saves were the first disc's until then
	if (!FileExists(SramPath()) && g.ext == "m3u")
	{
		std::vector<uint8_t> list;
		if (ReadFile(g.path, list))
		{
			std::string text(list.begin(), list.end());
			size_t p = 0;
			while (p < text.size() && text[p] == '#')
				p = text.find('\n', p) == std::string::npos ? text.size() : text.find('\n', p) + 1;
			std::string first = text.substr(p, text.find('\n', p) == std::string::npos ? std::string::npos : text.find('\n', p) - p);
			first = first.substr(first.find_last_of('/') + 1);
			first = first.substr(0, first.find_last_of('.'));
			const std::string old = SaveDir("saves") + "/" + first + ".srm";
			if (!first.empty() && FileExists(old))
			{
				std::vector<uint8_t> d;
				if (ReadFile(old, d) && WriteFileAtomic(SramPath(), d.data(), d.size()))
					ELog("[emu] the first disc's save %s is this disc set's save now", old.c_str());
			}
		}
	}
	if (ReadFile(SramPath(), data) && !data.empty())
	{
		memcpy(mem, data.data(), std::min(size, data.size()));
		g.sram_hash = Fnv(mem, size);
		ELog("[emu] battery save loaded: %s (%zu bytes)", SramPath().c_str(), data.size());
	}
}

// Before a save file is written for the first time while a game runs, the file as it was when the game started is
// kept beside it as <file>.bak (replacing the last one): a game that wrecks its own save, or a bad write, can be
// undone from there.
bool CopyFile(const std::string& from, const std::string& to)
{
	std::vector<uint8_t> data;
	return ReadFile(from, data) && WriteFileAtomic(to, data.data(), data.size());
}

// *done is set only once the copy is really there (or there was nothing to copy): a failed copy is tried again at
// the next write, and said on the screen once. Returns whether the file may be written over: false while the copy
// of it couldn't be made (the caller then writes beside it, never over it).
bool BackupOnce(const std::string& path, bool* done)
{
	if (*done)
		return true;
	if (!FileExists(path))
	{
		*done = true;
		return true;
	}
	const bool ok = CopyFile(path, path + ".bak");
	ELog("[emu] kept the save as it was: %s.bak (%s)", path.c_str(), ok ? "ok" : "failed");
	if (ok)
		*done = true;
	else if (!g.sram_backup_warned)
	{
		g.sram_backup_warned = true;
		emu::Osd("Could not back up the save: the new one is kept beside it (.srm.new)");
	}
	return ok;
}

// Writes the battery save when it changed (or when `force`).
void SaveSram(bool force)
{
	uint8_t* mem = static_cast<uint8_t*>(retro_get_memory_data(RETRO_MEMORY_SAVE_RAM));
	const size_t size = g.sram_size;
	if (!mem || !size)
		return;
	const uint64_t h = Fnv(mem, size);
	if (h == g.sram_hash && !force)
		return;
	if (h == g.sram_hash && !FileExists(SramPath()))
	{
		// nothing written by the game yet: don't create a file of zeros
		bool blank = true;
		for (size_t i = 0; i < size && blank; i++)
			blank = mem[i] == 0x00 || mem[i] == 0xff;
		if (blank)
			return;
	}
	// over the file only once its backup is there; otherwise beside it, the file left as it was
	const std::string target = BackupOnce(SramPath(), &g.sram_backed_up) ? SramPath() : SramPath() + ".new";
	const bool ok = WriteFileAtomic(target, mem, size);
	if (ok)
		g.sram_hash = h; // a failed write is tried again 3 s later and when the game closes
	ELog("[emu] battery save -> %s (%s)", target.c_str(), ok ? "ok" : "failed");
}

// ---- the backup memory cartridge (4 Mbit, "Power Memory"): its contents in saves/, per game or shared -------------
std::string CartPath()
{
	return fe::Config().cart_shared ? SaveDir("saves") + "/Shared backup cartridge.bcr"
									 : SaveDir("saves") + "/" + g.name + ".bcr";
}

void LoadCart()
{
	g.cart_size = 0;
	g.cart_hash = 0;
	uint8_t* mem = nullptr;
	size_t size = 0;
	if (!ymir_ps5_cart_backup(&mem, &size))
		return;
	g.cart_size = size;
	std::vector<uint8_t> data;
	if (ReadFile(CartPath(), data) && data.size() == size)
	{
		memcpy(mem, data.data(), size);
		ELog("[emu] backup cartridge loaded: %s (%zu bytes)", CartPath().c_str(), size);
	}
	else
		ELog("[emu] backup cartridge: a new one (%s, %zu bytes, formatted)%s", CartPath().c_str(), size,
			data.empty() ? "" : " -- the file there has another size: not used");
	g.cart_hash = Fnv(mem, size);
}

void SaveCart(bool force)
{
	uint8_t* mem = nullptr;
	size_t size = 0;
	if (!g.cart_size || !ymir_ps5_cart_backup(&mem, &size) || size != g.cart_size)
		return;
	const uint64_t h = Fnv(mem, size);
	if (h == g.cart_hash && !force)
		return;
	if (h == g.cart_hash && !force && !FileExists(CartPath()))
		return; // only formatted, nothing saved on it yet
	const bool ok = WriteFileAtomic(CartPath(), mem, size);
	if (ok)
		g.cart_hash = h;
	ELog("[emu] backup cartridge -> %s (%s)", CartPath().c_str(), ok ? "ok" : "failed");
}

// The Saturn BIOS, found before loading a disc: the first folder of OrbisBiosDirs() (the app's bios/ on the console and
// on every USB drive, then paths.txt's bios= folders) with one of these names, in it or in its Saturn/ sub-folder;
// a folder on the game's own drive first. Ymir also takes any file there whose hash it knows, and the region setting
// picks among several. "" when there is none.
std::string FindSaturnBios(const std::string& game, bool log = true)
{
	static const char* const kNames[] = {"sega_101.bin", "mpr-17933.bin", "saturn_bios.bin", "sega1003.bin",
		"sega_100.bin", "mpr-18100.bin", "Sega Saturn BIOS (EUR).bin", "Sega Saturn BIOS (JPN).bin",
		"Sega Saturn BIOS (USA).bin"};
	std::vector<std::string> dirs = OrbisBiosDirs();
	// the game's drive: /mnt/usb0/... prefers /mnt/usb0/...
	const size_t drive_end = game.rfind("/mnt/", 0) == 0 ? game.find('/', 5) : std::string::npos;
	if (drive_end != std::string::npos)
	{
		const std::string drive = game.substr(0, drive_end + 1);
		std::stable_partition(dirs.begin(), dirs.end(), [&](const std::string& d) { return d.rfind(drive, 0) == 0; });
	}
	for (const std::string& d : dirs)
		for (const char* sub : {"", "/Saturn"})
			for (const char* n : kNames)
				if (FileExists(d + sub + "/" + n))
				{
					if (log)
						ELog("[emu] Saturn BIOS: %s%s/%s", d.c_str(), sub, n);
					return d;
				}
	return "";
}

} // namespace
std::string FindSaturnBiosQuiet()
{
	return FindSaturnBios("", false);
}
namespace
{
void ShowOverlays(ps5video::Rect* damage)
{
	const fe::Settings& cfg = fe::Config();
	const double now = Now();
	bool drawn = false;
	if (cfg.show_fps)
	{
		char buf[32];
		snprintf(buf, sizeof(buf), "%.1f fps", g.fps_shown);
		const int w = fe::TextWidth(buf, 3);
		ps5video::FillRect(ps5video::kWidth - w - 60, 20, w + 40, 50, ps5video::Rgb(0, 0, 0));
		fe::DrawText(ps5video::kWidth - w - 40, 28, buf, 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (g.turbo && g.ff_shown > 0)
	{
		// 3.11.4: the speed fast forward really runs at, top right (under the FPS when they are shown)
		char buf[48];
		snprintf(buf, sizeof(buf), ">> %.1fx", g.ff_shown);
		const int w = fe::TextWidth(buf, 3), y = cfg.show_fps ? 80 : 20;
		ps5video::FillRect(ps5video::kWidth - w - 60, y, w + 40, 50, ps5video::Rgb(0, 0, 0));
		fe::DrawText(ps5video::kWidth - w - 40, y + 8, buf, 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (!g.slot_thumb.empty() && now < g.slot_thumb_until)
	{
		// 3.11: the chosen slot's picture, top left
		fe::style::RoundRect(36, 36, 264, 200, 12, ps5video::Rgb(255, 255, 255), 70);
		fe::style::Picture(g.slot_thumb.data(), g.slot_thumb_w, g.slot_thumb_h, 40, 40, 256, 192, 10);
		drawn = true;
	}
	if (!g.osd.empty() && now < g.osd_until)
	{
		const std::string text = fe::FitText(g.osd, 3, ps5video::kWidth - 160);
		const int w = fe::TextWidth(text.c_str(), 3);
		ps5video::DarkenRect(40, ps5video::kHeight - 100, w + 40, 60);
		fe::DrawText(60, ps5video::kHeight - 90, text.c_str(), 3, ps5video::Rgb(255, 255, 255));
		drawn = true;
	}
	if (drawn || g.overlay_was_drawn)
		*damage = ps5video::Rect{0, 0, ps5video::kWidth, ps5video::kHeight};
	if (!drawn && g.overlay_was_drawn)
		ps5video::InvalidateFrame(); // clear what the overlay left outside the picture
	g.overlay_was_drawn = drawn;
}

double FrameAspect()
{
	const fe::Settings& cfg = fe::Config();
	switch (cfg.aspect)
	{
		case 1: return g.fh > 0 ? double(g.fw) / (g.fh > 300 ? g.fh / 2 : g.fh) : 4.0 / 3.0;
		case 2: return 4.0 / 3.0;
		case 3: return 16.0 / 9.0;
		default: return g.core_aspect;
	}
}

// The picture the border's window is fitted to (fe_border.h): FrameAspect's ratio and the line grid DrawLast uses, from the
// last frame, or before the first one from the size the core gave at load.
fe::border::Shape BorderShape()
{
	const fe::Settings& cfg = fe::Config();
	fe::border::Shape shape;
	shape.valid = true;
	const int w = g.fw > 0 ? g.fw : g.av_w, h = g.fh > 0 ? g.fh : g.av_h;
	const int lines = h > 300 ? h / 2 : h;
	shape.base_h = lines > 0 ? lines : 224;
	switch (cfg.aspect)
	{
		case 1: shape.aspect = w > 0 && lines > 0 ? double(w) / lines : 4.0 / 3.0; break;
		case 2: shape.aspect = 4.0 / 3.0; break;
		case 3: shape.aspect = 16.0 / 9.0; break;
		default: shape.aspect = g.core_aspect; break;
	}
	shape.scale = ps5video::Scale(cfg.scale);
	return shape;
}

ps5video::Rect DrawLast()
{
	if (g.frame.empty())
		return ps5video::Rect{0, 0, 0, 0};
	const fe::Settings& cfg = fe::Config();
	const int base_h = g.fh > 300 ? g.fh / 2 : g.fh; // interlaced (double field) pictures: the 224-line grid
	return ps5video::DrawFrame(g.frame.data(), g.fw, g.fh, base_h, FrameAspect(), ps5video::Scale(cfg.scale), cfg.smooth,
		cfg.scanlines, emu::EffectiveShader());
}

void RewindPush()
{
	const size_t size = retro_serialize_size();
	if (size == 0)
		return;
	std::vector<uint8_t> s(size);
	if (!retro_serialize(s.data(), size))
		return;
	g.rewind_bytes += size;
	g.rewind.push_back(std::move(s));
	while (g.rewind_bytes > kRewindBytes && !g.rewind.empty())
	{
		g.rewind_bytes -= g.rewind.front().size();
		g.rewind.pop_front();
	}
}

void RewindClear()
{
	g.rewind.clear();
	g.rewind_bytes = 0;
	g.rewind_tick = 0;
}

// ---- the game log (fe_gamelog.h): the settings at the start, then a timeline while the game runs ----------------

void LogSettings()
{
	using namespace emu;
	const fe::Settings& c = fe::Config();
	fe::gamelog::Line("settings: region %s, cartridge %s, CD %s, SH-2 clock %s, SH-2 cache %s, CD block LLE %s, "
		"deinterlace %s, transparent meshes %s, sound %s, interpolation %s, threaded SCSP %s, pad %s, rewind %s",
		kRegions[c.region % kRegionCount].value, kCartridges[c.cartridge % kCartridgeCount].value,
		kCdSpeeds[c.cd_speed % kCdSpeedCount].value, kSh2Clocks[c.sh2_clock % kSh2ClockCount].value,
		c.sh2_cache ? "on" : "off", c.cdblock_lle ? "on" : "off", c.deinterlace ? "on" : "off",
		c.transparent_meshes ? "on" : "off", c.audio ? "on" : "off", kAudioInterps[c.audio_interp % kAudioInterpCount].value,
		c.threaded_scsp ? "on" : "off", kPadTypes[c.pad_type % kPadTypeCount].name, c.rewind ? "on" : "off");
	fe::gamelog::Line("video: renderer %s (wanted %s), upscaler %s, shader %d, scale %d, aspect %d, border %d; this "
		"game's own settings: %s",
		ps5video::GpuActive() ? "Vulkan" : "CPU", c.renderer ? "Vulkan" : "CPU",
		kUpscalers[c.upscaler % kUpscalerCount].name, c.shader, c.scale, c.aspect, c.border,
		fe::GameHasOwnSettings() ? "yes" : "no");
}

using Diag = State::Diag;

Diag DiagState()
{
	Diag d;
	d.next = Now() + 1.0;
	d.last_picture = d.last_sound = d.pc_same_since = Now();
	return d;
}

// what share of the last frame is not black, and its mean brightness (0..255), from every 7th pixel
void PictureStats(double* lit, double* mean)
{
	*lit = 0;
	*mean = 0;
	if (g.frame.empty())
		return;
	uint64_t n = 0, on = 0, sum = 0;
	for (size_t i = 0; i < g.frame.size(); i += 7)
	{
		const uint32_t p = g.frame[i];
		const int y = int(((p >> 16) & 255) * 3 + ((p >> 8) & 255) * 6 + (p & 255)) / 10;
		sum += uint64_t(y);
		on += y > 12;
		n++;
	}
	*lit = n ? double(on) / double(n) : 0;
	*mean = n ? double(sum) / double(n) : 0;
}

// Each frame: the black-picture and stuck-CPU watch; at 1, 2, 3, 5, 10, 20, 30, 45 and 60 s, then every minute, a
// line. final: the last line, when the game closes.
void DiagReport(bool final)
{
	if (!fe::gamelog::Active())
		return;
	Diag& d = g.diag;
	const double now = Now();
	d.frames++;
	if ((d.frames % 30) == 0 || final)
	{
		double lit = 0, mean = 0;
		PictureStats(&lit, &mean);
		if (lit > 0.002)
			d.last_picture = now;
		else if (now - d.last_picture > 8.0 && now - d.black_warned > 30.0)
		{
			d.black_warned = now;
			char st[256];
			ymir_ps5_debug_state(st, sizeof(st));
			fe::gamelog::Line("WARNING: the picture has been black for %.0f s (%llu frames from the core, %dx%d): %s",
				now - d.last_picture, (unsigned long long)d.video_calls, g.fw, g.fh, st);
		}
		char st[256];
		ymir_ps5_debug_state(st, sizeof(st));
		unsigned pc = 0;
		if (const char* p = strstr(st, "master SH-2 PC "))
			pc = unsigned(strtoul(p + 15, nullptr, 16));
		if (pc != d.last_pc)
		{
			d.last_pc = pc;
			d.pc_same_since = now;
		}
	}
	if (d.peak > 64)
		d.last_sound = now;
	if (!final && now < d.next)
		return;
	static const double kDiagTimes[] = {1, 2, 3, 5, 10, 20, 30, 45, 60};
	const int steps = int(sizeof(kDiagTimes) / sizeof(kDiagTimes[0]));
	const double since = fe::gamelog::Seconds();
	double lit = 0, mean = 0;
	PictureStats(&lit, &mean);
	char st[256];
	ymir_ps5_debug_state(st, sizeof(st));
	fe::gamelog::Line("%s%llu frames run, %llu pictures from the core (last %dx%d, %.0f%% not black, brightness %.0f), "
		"%.1f fps, core %.1f ms/frame, sound %llu samples (peak %d%s), %s%s",
		final ? "at the end: " : "", (unsigned long long)d.frames, (unsigned long long)d.video_calls, g.fw, g.fh,
		lit * 100.0, mean, g.fps_shown, d.core_n ? d.core_ms / double(d.core_n) : 0.0,
		(unsigned long long)d.audio_frames, d.peak, now - d.last_sound > 10.0 ? ", SILENT for over 10 s" : "", st,
		now - d.pc_same_since > 10.0 ? " -- the master SH-2 has been at the same address for over 10 s (stuck?)" : "");
	d.peak = 0;
	d.core_ms = 0;
	d.core_n = 0;
	while (d.step < steps && kDiagTimes[d.step] <= since + 0.25)
		d.step++;
	d.next = now + (d.step < steps ? kDiagTimes[d.step] - since : 60.0);
	if (d.next < now + 0.5)
		d.next = now + 0.5;
}
} // namespace

namespace emu
{
bool InitCore()
{
	if (g.inited)
		return true;
	YMIR_STAGE(Emu, "init");
	g.sys_dir = OrbisDir("bios");
	g.save_dir = OrbisDir("saves");
	ApplySettings();
	retro_set_environment(Environment);
	retro_set_video_refresh(Video);
	retro_set_audio_sample(AudioSample);
	retro_set_audio_sample_batch(AudioBatch);
	retro_set_input_poll(InputPoll);
	retro_set_input_state(InputState);
	retro_init();
	retro_system_info info = {};
	retro_get_system_info(&info);
	ELog("[emu] %s %s ready (libretro API %u; BIOS folder %s)", info.library_name, info.library_version,
		retro_api_version(), g.sys_dir.c_str());
	g.inited = true;
	return true;
}

void DeinitCore()
{
	if (!g.inited)
		return;
	CloseGame();
	retro_deinit();
	g.inited = false;
}

// The pads plugged into the console's two ports (apps/ymir-libretro's devices): the pad type from the settings in
// port 1, a Saturn pad in port 2 (a second PS5 pad plays it).
void ApplyControllers()
{
	const fe::Settings& s = fe::Config();
	const int pad = s.pad_type % kPadTypeCount;
	if (pad == g.pads_applied)
		return;
	g.pads_applied = pad;
	static const unsigned kDevices[] = {RETRO_DEVICE_JOYPAD, RETRO_DEVICE_ANALOG,
		RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 1), RETRO_DEVICE_SUBCLASS(RETRO_DEVICE_ANALOG, 2)};
	retro_set_controller_port_device(0, kDevices[pad]);
	retro_set_controller_port_device(1, RETRO_DEVICE_JOYPAD);
	ELog("[emu] controllers: %s", kPadTypes[pad].name);
}

void ApplySettings()
{
	const fe::Settings& s = fe::Config();
	auto set = [](const char* key, const std::string& value) {
		auto it = g.opts.find(key);
		if (it == g.opts.end() || it->second != value)
		{
			g.opts[key] = value;
			g.opts_changed = true;
		}
	};
	set("ymir_region", kRegions[s.region % kRegionCount].value);
	set("ymir_cartridge", kCartridges[s.cartridge % kCartridgeCount].value);
	set("ymir_cd_speed", kCdSpeeds[s.cd_speed % kCdSpeedCount].value);
	// Auto: the games that need no RAM or ROM cartridge get the backup memory one
	set("ymir_ps5_auto_backup_cart", "enabled");
	set("ymir_sh2_clock", kSh2Clocks[s.sh2_clock % kSh2ClockCount].value);
	set("ymir_sh2_cache", s.sh2_cache ? "enabled" : "disabled");
	set("ymir_cdblock_lle", s.cdblock_lle ? "enabled" : "disabled");
	set("ymir_rtc_mode", "virtual");
	// 3.4: with the GPU the deinterlacing is its own (cheap): the core draws one field a frame as the Saturn does,
	// instead of both (its deinterlacer: twice the drawing, too slow for Virtua Fighter 2 and the like)
	g.gpu_deint = s.deinterlace && ps5video::GpuActive();
	set("ymir_deinterlace", s.deinterlace && !g.gpu_deint ? "enabled" : "disabled");
	set("ymir_transparent_meshes", s.transparent_meshes ? "enabled" : "disabled");
	set("ymir_threaded_vdp1", "enabled");
	set("ymir_threaded_vdp2", "enabled");
	set("ymir_threaded_deinterlacer", "enabled");
	set("ymir_audio_interpolation", kAudioInterps[s.audio_interp % kAudioInterpCount].value);
	set("ymir_threaded_scsp", s.threaded_scsp ? "enabled" : "disabled");
	set("ymir_audio_step_granularity", "0");
	// FSR 1.0's sharpening (RCAS, in stops: 0 the sharpest), from the settings' 0..20 (tenths)
	const int fsr = EffectiveShader();
	if (s.upscaler > 0 && fsr > 0)
		ps5gpu::SetShaderParam(fsr, "FSR_SHARPENING", float(s.fsr_sharpness) / 10.0f);
	if (!s.rewind)
		RewindClear();
	if (g.loaded)
	{
		ApplyControllers();
		fe::border::Reselect(BorderShape()); // the Border setting, or a picture setting the window follows, changed
	}
}

bool LoadGame(const std::string& path, std::string* error)
{
	if (!g.inited)
		return false;
	YMIR_STAGE(Emu, "load game");
	CloseGame();
	fe::gamelog::Begin(path);
	const std::string file = path.substr(path.find_last_of('/') + 1);
	if (path.size() >= 255)
	{
		// the core keeps the game's path and the paths it builds from it in 256-byte buffers
		ELog("[emu] could not start %s: the path is %zu characters long", path.c_str(), path.size());
		if (error)
			*error = Fmt(T("The path of %s is too long (over 254 characters): move it to a shorter folder."), file.c_str());
		fe::gamelog::End("could not start: the path is too long");
		return false;
	}
	std::string ext = ExtOf(path);
	g.path = path;
	g.dir = path.substr(0, path.find_last_of('/'));
	g.name = file.substr(0, file.size() - ext.size());
	g.rom.clear();
	g.info_ext = retro_game_info_ext();

	std::string inner; // (no archives: a Saturn game is a disc image)
	if (!fe::SystemForExt(ext, &g.system))
		g.system = fe::System::Saturn;
	g.ext = ext.empty() ? "" : ext.substr(1);

	{
		const std::string bios_dir = FindSaturnBios(path);
		for (const std::string& d : OrbisBiosDirs())
			fe::gamelog::Line("bios folder %s%s", d.c_str(), d == bios_dir ? "  <- used" : "");
		fe::gamelog::DescribeBios(bios_dir);
		if (bios_dir.empty())
		{
			fe::gamelog::Line("ERROR: no Saturn BIOS (sega_101.bin, mpr-17933.bin...) in any BIOS folder");
			fe::gamelog::End("could not start: no BIOS");
			ELog("[emu] missing firmware: Saturn BIOS (checked before loading)");
			ELog("[emu] could not start %s: missing the Saturn BIOS", path.c_str());
			if (error)
				*error = Fmt(T("Saturn games need the console's BIOS (sega_101.bin or mpr-17933.bin). Copy it to "
							   "%s/, to ssymirps5/bios on a USB drive, or to a bios= folder of %s/paths.txt, and try again."),
					OrbisDir("bios").c_str(), OrbisRoot().c_str());
			g.path.clear();
			return false;
		}
		g.sys_dir = bios_dir; // the core reads its system folder at each retro_load_game
	}
	// a .m3u whose lines don't say where the discs are ("Game_1.cue" while it is in "Game/Disc 1/"): the core gets a copy
	// with the full paths found (cache/playlists/, the same file name: the saves keep the game's name)
	g.load_path = g.path;
	if (g.ext == "m3u")
	{
		std::vector<std::string> missing;
		bool changed = false;
		const std::vector<std::string> discs = fe::ResolvePlaylist(g.path, &missing, &changed);
		for (size_t i = 0; i < discs.size(); i++)
			fe::gamelog::Line("playlist disc %zu: %s%s", i + 1, discs[i].c_str(), FileExists(discs[i]) ? "" : "  (NOT FOUND)");
		for (const std::string& m : missing)
			ELog("[emu] the playlist names \"%s\", which is not beside it nor in its sub-folders", m.c_str());
		if (changed && !discs.empty())
		{
			const std::string dir = OrbisDir("cache") + "/playlists";
			OrbisMkdirs(dir);
			std::string text = "# SSYmirPS5: " + g.path + " with the discs' full paths\n";
			for (const std::string& d : discs)
				text += d + "\n";
			const std::string copy = dir + "/" + file;
			if (WriteFileAtomic(copy, text.data(), text.size()))
			{
				g.load_path = copy;
				ELog("[emu] the playlist's discs are in its sub-folders: the core reads %s", copy.c_str());
			}
		}
	}
	g.info_ext.full_path = g.rom.empty() ? g.load_path.c_str() : nullptr;
	g.info_ext.archive_path = g.rom.empty() ? nullptr : g.path.c_str();
	g.info_ext.dir = g.dir.c_str();
	g.info_ext.name = g.name.c_str();
	g.info_ext.ext = g.ext.c_str();
	g.info_ext.data = g.rom.empty() ? nullptr : g.rom.data();
	g.info_ext.size = g.rom.size();
	g.info_ext.file_in_archive = !g.rom.empty();
	g.info_ext.persistent_data = false;

	retro_game_info gi = {};
	gi.path = g.load_path.c_str();
	gi.data = g.rom.empty() ? nullptr : g.rom.data();
	gi.size = g.rom.size();

	ELog("[emu] loading %s%s%s", path.c_str(), inner.empty() ? "" : " -> ", inner.c_str());
	fe::BeginGame(GameIniPath());
	ApplySettings();
	LogSettings();
	g.opts_changed = true;
	g.frame.clear();
	g.fw = g.fh = 0;
	g.in.clear();
	g.mute = false;
	const double t_load = Now();
	fe::gamelog::Line("retro_load_game...");
	const bool loaded_ok = retro_load_game(&gi);
	fe::gamelog::Line("retro_load_game -> %s (%.0f ms)", loaded_ok ? "ok" : "FAILED", (Now() - t_load) * 1000.0);
	if (!loaded_ok)
	{
		fe::gamelog::End("could not start: the core refused the disc (see the core's lines above)");
		ELog("[emu] could not start %s", path.c_str());
		if (error)
		{
			*error = Fmt(T("Ymir could not start %s: the disc image is incomplete or not a Saturn disc (a .cue needs its "
						   ".bin tracks beside it), or the BIOS in %s is not a Saturn BIOS."),
				file.c_str(), g.sys_dir.c_str());
		}
		g.path.clear();
		g.rom.clear();
		fe::EndGame();
		ApplySettings();
		return false;
	}
	g.pads_applied = -1;
	ApplyControllers();
	retro_system_av_info av = {};
	retro_get_system_av_info(&av);
	g.fps = av.timing.fps > 1 ? av.timing.fps : 60.0;
	g.sample_rate = av.timing.sample_rate > 1000 ? av.timing.sample_rate : 44100.0;
	g.core_aspect = av.geometry.aspect_ratio > 0.1f ? av.geometry.aspect_ratio : 4.0 / 3.0;
	g.av_w = int(av.geometry.base_width);
	g.av_h = int(av.geometry.base_height);
	g.loaded = true;
	g.wait_release = true;
	g.prev_p1 = 0;
	g.turbo = g.rewinding = false;
	g.frac = 0;
	memset(g.hist, 0, sizeof(g.hist));
	g.adj_sum = 0;
	g.adj_n = 0;
	g.adj_min = 2;
	g.adj_max = 0;
	g.queued_min = 1 << 30;
	g.queued_max = 0;
	g.next_shader_check = Now() + 2.0;
	g.shader_ms_sum = 0;
	g.shader_ms_n = 0;
	g.shader_warned = false;
	g.sram_backed_up = false;
	g.sram_backup_warned = false;
	g.frames = 0;
	g.fps_frames = 0;
	g.fps_since = Now();
	g.next_stats = Now() + 1.0;
	g.stats_reports = 0;
	g.next_sram_check = Now() + 3.0;
	g.overlay_was_drawn = false;
	RewindClear();
	ps5video::InvalidateFrame();
	MigrateSaves();
	LoadSram();
	LoadCart();
	PerfStart();
	// the border: the game described once (its system as found here: a zip is not opened again), its picture
	// read from /data, before the game's first frame (a new picture shape later only refits it)
	fe::border::Select(path, BorderShape(), &g.system);
	{
		char st[256];
		ymir_ps5_debug_state(st, sizeof(st));
		fe::gamelog::Line("started: %s, %ux%u base, aspect %.4f, renderer %s, picture shader %s; %s", g.name.c_str(),
			av.geometry.base_width, av.geometry.base_height, g.core_aspect, ps5video::GpuActive() ? "Vulkan" : "CPU",
			ps5video::ShaderName(EffectiveShader()), st);
	}
	g.diag = DiagState();
	// 3.8: no controls message at the start (the pause menu has them)
	fe::cheats::Begin(g.path); // 3.9: its cheats (found or downloaded in the background)
	ELog("[emu] running \"%s\" (%s), %.3f fps, %.0f Hz, aspect %.4f, state size %zu", g.name.c_str(),
		SystemName().c_str(), g.fps, g.sample_rate, g.core_aspect, retro_serialize_size());
	return true;
}

void CloseGame()
{
	if (!g.loaded)
		return;
	YMIR_STAGE(Emu, "close game");
	fe::cheats::End();
	DiagReport(true);
	PerfStop();
	SaveSram(false);
	SaveCart(false);
	retro_unload_game();
	fe::gamelog::End("closed");
	g.loaded = false;
	g.frame.clear();
	g.rom.clear();
	RewindClear();
	fe::border::Release();
	fe::EndGame();
	ApplySettings();
	ELog("[emu] game closed");
}

bool GameLoaded()
{
	return g.loaded;
}

std::string GameName()
{
	return g.name;
}

std::string SystemName()
{
	return g.loaded || !g.path.empty() ? fe::Info(g.system).name : "";
}

void Pause() {}

void Resume()
{
	ApplySettings();
	g.wait_release = true;
	ps5video::InvalidateFrame();
	g.new_frame = true;
}

void Osd(const std::string& text)
{
	fe::gamelog::Line("message on screen: %s", text.c_str());
	g.osd = T(text.c_str());
	g.osd_until = Now() + 2.5;
}

// A state file is the core's state followed by a 16-byte trailer: "GPXS", the trailer's version (1), the state's
// size and its CRC-32 (zlib's), little-endian. A load checks all four, so a state cut short, padded, or changed
// anywhere inside is refused, not only one of the wrong size. States saved before 1.9 have no trailer: they are
// still loaded when their size is exactly the core's (as 1.8 did), and said to be unchecked in the log.
constexpr uint32_t kStateMagic = 0x53585047; // "GPXS"
constexpr size_t kStateTrailer = 16;

void AddTrailer(std::vector<uint8_t>& data)
{
	const uint32_t size = uint32_t(data.size());
	const uint32_t crc = uint32_t(crc32(0L, data.data(), uInt(data.size())));
	const uint32_t t[4] = {kStateMagic, 1, size, crc};
	for (uint32_t v : t)
		for (int i = 0; i < 4; i++)
			data.push_back(uint8_t(v >> (8 * i)));
}

uint32_t Le32(const uint8_t* p)
{
	return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

enum class StateCheck
{
	Good, // the trailer and its CRC match
	Legacy, // no trailer, the core's size: a state of 1.8 or older
	Damaged, // a trailer that doesn't match, or no trailer and the wrong size
};

// Checks `data` as a state file for a core whose states are `want` bytes; on success it is cut to the state alone.
StateCheck CheckState(std::vector<uint8_t>& data, size_t want, std::string& why)
{
	if (data.size() == want + kStateTrailer)
	{
		const uint8_t* t = data.data() + want;
		if (Le32(t) != kStateMagic || Le32(t + 4) != 1 || Le32(t + 8) != want)
		{
			why = "its trailer is not a SSYmirPS5 one";
			return StateCheck::Damaged;
		}
		const uint32_t crc = uint32_t(crc32(0L, data.data(), uInt(want)));
		if (crc != Le32(t + 12))
		{
			char buf[96];
			snprintf(buf, sizeof(buf), "CRC-32 %08x, the file says %08x", crc, Le32(t + 12));
			why = buf;
			return StateCheck::Damaged;
		}
		data.resize(want);
		return StateCheck::Good;
	}
	if (data.size() == want)
	{
		// a trailer-less file of the core's size: an older state, unless it ends like a trailer of another size
		return StateCheck::Legacy;
	}
	char buf[96];
	snprintf(buf, sizeof(buf), "%zu bytes, the core's states are %zu (+%zu)", data.size(), want, kStateTrailer);
	why = buf;
	return StateCheck::Damaged;
}

// rename(), with the host tests' fault injection: YMIR_HOST_FAIL_RENAME=<item>[,<item>...] makes every rename whose
// source or target contains an item's text fail (EIO), to check the save paths against a storage that refuses a
// step ("from=<text>" / "to=<text>" look at one side only).
int Rename(const std::string& from, const std::string& to)
{
#ifndef __PROSPERO__
	if (const char* fail = getenv("YMIR_HOST_FAIL_RENAME"))
	{
		std::string list = fail;
		size_t pos = 0;
		while (pos <= list.size())
		{
			size_t end = list.find(',', pos);
			if (end == std::string::npos)
				end = list.size();
			std::string item = list.substr(pos, end - pos);
			// "from=<text>" matches the source only, "to=<text>" the target only, "<text>" either
			bool src = true, dst = true;
			if (item.compare(0, 5, "from=") == 0)
			{
				item = item.substr(5);
				dst = false;
			}
			else if (item.compare(0, 3, "to=") == 0)
			{
				item = item.substr(3);
				src = false;
			}
			if (!item.empty() && ((src && from.find(item) != std::string::npos) || (dst && to.find(item) != std::string::npos)))
			{
				errno = EIO;
				return -1;
			}
			pos = end + 1;
		}
	}
#endif
	return rename(from.c_str(), to.c_str());
}

// A state written over another one keeps the other as <state>.prev: the new state is written in full first
// (<state>.part, flushed and read back by WriteFileAtomic), then the old one becomes .prev, then the new one takes
// its name. If the old one can't be kept, nothing is replaced. If the new one can't take its name, the old one is
// put back; if even that fails, it is still whole in <state>.prev (the pause menu's "Load the previous state"), and
// the log and the screen say so -- no step loses the state the slot had.
bool WriteState(const std::string& path, std::vector<uint8_t> data)
{
	AddTrailer(data);
	const std::string part = path + ".part";
	const std::string prev = path + ".prev";
	if (!WriteFileAtomic(part, data.data(), data.size())) // flushed and read back
		return false;
	const bool had_old = FileExists(path);
	if (had_old && Rename(path, prev) != 0)
	{
		ELog("[emu] could not keep the previous state of %s (errno %d): the slot is left as it was", path.c_str(),
			errno);
		remove(part.c_str());
		return false;
	}
	if (Rename(part, path) != 0)
	{
		const int err = errno;
		remove(part.c_str());
		if (!had_old)
		{
			ELog("[emu] could not put the new state in place (errno %d): the slot stays empty", err);
			return false;
		}
		if (Rename(prev, path) == 0)
		{
			ELog("[emu] could not put the new state in place (errno %d): the old one is back, the slot is left as it was",
				err);
			return false;
		}
		ELog("[emu] could not put the new state in place (errno %d), nor the old one back (errno %d): the slot's "
				 "state is whole in %s",
			err, errno, prev.c_str());
		emu::Osd("The slot's state is kept as its previous state: load it from the pause menu");
		return false;
	}
	return true;
}

bool LoadStateFile(const std::string& path, const char* what, int slot)
{
	std::vector<uint8_t> data;
	if (!ReadFile(path, data) || data.empty())
	{
		Osd(Fmt(T(strcmp(what, "The state") ? "The previous state of slot %d is empty" : "The state of slot %d is empty"), slot));
		return false;
	}
	// a damaged state, or one from another core version, is refused: the game goes on as it was
	const size_t want = retro_serialize_size();
	std::string why;
	const StateCheck check = CheckState(data, want, why);
	if (check == StateCheck::Damaged)
	{
		ELog("[emu] load state %d (%s): %s: refused", slot, path.c_str(), why.c_str());
		Osd("This state is damaged or from another version: not loaded");
		return false;
	}
	// the game as it is now, to go back to if the core refuses the state half-way
	std::vector<uint8_t> before(want);
	const bool have_before = retro_serialize(before.data(), want);
	bool ok = retro_unserialize(data.data(), data.size());
	if (!ok && have_before)
		retro_unserialize(before.data(), want);
	ELog("[emu] load state %d%s: %s%s", slot, strstr(path.c_str(), ".prev") ? " (previous)" : "", ok ? "ok" : "failed",
		check == StateCheck::Legacy ? " (an older state without a checksum)" : " (checksum ok)");
	if (ok)
		Osd(Fmt(T(strcmp(what, "The state") ? "The previous state of slot %d loaded" : "The state of slot %d loaded"), slot));
	else
		Osd("Could not load the state");
	if (ok)
		RewindClear();
	return ok;
}

// 3.11: a state's picture, <state>.thumb: "YTH1", its width and height (little-endian), then its pixels (the surface's
// format, row-major): the game's last frame at 320x240 (4:3, as the TV showed it), each pixel the mean of 2x2 samples.
constexpr int kThumbW = 320, kThumbH = 240;

bool WriteThumb(const std::string& path)
{
	if (g.frame.empty() || g.fw < 2 || g.fh < 2)
		return false;
	std::vector<uint8_t> out(12 + size_t(kThumbW) * kThumbH * 4);
	memcpy(out.data(), "YTH1", 4);
	const uint32_t dims[2] = {kThumbW, kThumbH};
	for (int d = 0; d < 2; d++)
		for (int i = 0; i < 4; i++)
			out[4 + d * 4 + i] = uint8_t(dims[d] >> (8 * i));
	uint32_t* px = reinterpret_cast<uint32_t*>(out.data() + 12);
	for (int y = 0; y < kThumbH; y++)
		for (int x = 0; x < kThumbW; x++)
		{
			uint32_t sum[3] = {0, 0, 0};
			for (int s = 0; s < 4; s++)
			{
				const int sx = std::min(g.fw - 1, int((x + 0.25f + 0.5f * (s & 1)) * g.fw / kThumbW));
				const int sy = std::min(g.fh - 1, int((y + 0.25f + 0.5f * (s >> 1)) * g.fh / kThumbH));
				const uint32_t c = g.frame[size_t(sy) * g.fw + sx]; // 0xffRRGGBB
				sum[0] += (c >> 16) & 0xff, sum[1] += (c >> 8) & 0xff, sum[2] += c & 0xff;
			}
			px[size_t(y) * kThumbW + x] = ps5video::Rgb(uint8_t(sum[0] / 4), uint8_t(sum[1] / 4), uint8_t(sum[2] / 4));
		}
	return WriteFileAtomic(path, out.data(), out.size());
}

bool SaveState(int slot)
{
	if (!g.loaded)
		return false;
	const size_t size = retro_serialize_size();
	std::vector<uint8_t> data(size);
	const std::string path = StatePath(slot);
	const bool had_old = FileExists(path);
	bool ok = size > 0 && retro_serialize(data.data(), size) && WriteState(path, std::move(data));
	if (ok)
	{
		// the picture goes with its state: the old one with the state that became .prev
		if (had_old && FileExists(path + ".thumb"))
			Rename(path + ".thumb", path + ".prev.thumb");
		else if (had_old)
			remove((path + ".prev.thumb").c_str());
		if (!WriteThumb(path + ".thumb"))
			remove((path + ".thumb").c_str());
	}
	ELog("[emu] save state %d: %s (%zu bytes)", slot, ok ? "ok" : "failed", size);
	if (ok)
		Osd(Fmt(T("State saved to slot %d"), slot));
	else
		Osd("Could not save the state");
	return ok;
}

bool LoadState(int slot)
{
	return g.loaded && LoadStateFile(StatePath(slot), "The state", slot);
}

bool LoadPreviousState(int slot)
{
	return g.loaded && LoadStateFile(StatePath(slot) + ".prev", "The previous state", slot);
}

std::string AutoStatePathFor(const std::string& rom)
{
	// the name LoadGame gives the game (the file without its extension), in its system's states folder
	const std::string file = rom.substr(rom.find_last_of('/') + 1);
	const std::string ext = ExtOf(rom);
	fe::System system = fe::System::Saturn;
	if (!fe::SystemForExt(ext, &system))
		system = fe::System::Saturn;
	return OrbisDir("states") + "/" + fe::Info(system).folder + "/" + file.substr(0, file.size() - ext.size()) + ".auto";
}

bool SaveAutoState()
{
	if (!g.loaded)
		return false;
	const size_t size = retro_serialize_size();
	std::vector<uint8_t> data(size);
	OrbisMkdirs(SaveDir("states"));
	const bool ok = size > 0 && retro_serialize(data.data(), size) && WriteState(AutoStatePathFor(g.path), std::move(data));
	ELog("[emu] auto state on leaving: %s (%zu bytes)", ok ? "ok" : "failed", size);
	return ok;
}

bool LoadAutoState()
{
	if (!g.loaded)
		return false;
	const std::string path = AutoStatePathFor(g.path);
	std::vector<uint8_t> data;
	const size_t want = retro_serialize_size();
	std::string why;
	bool ok = ReadFile(path, data) && !data.empty();
	if (!ok)
		why = "no file";
	else if (CheckState(data, want, why) == StateCheck::Damaged)
		ok = false;
	else
	{
		ok = retro_unserialize(data.data(), data.size());
		if (!ok)
			why = "the core refused it";
	}
	ELog("[emu] auto state: %s%s", ok ? "loaded" : "not loaded: ", ok ? "" : why.c_str());
	if (ok)
	{
		RewindClear();
		Osd("Continuing where you left off");
	}
	else
		Osd("The game saved on leaving could not be loaded: starting from the beginning");
	return ok;
}

bool StateThumb(int slot, bool previous, std::vector<uint32_t>& px, int& w, int& h, long long* when)
{
	px.clear();
	w = h = 0;
	if (!g.loaded)
		return false;
	const std::string state = StatePath(slot) + (previous ? ".prev" : "");
	struct stat st = {};
	if (stat(state.c_str(), &st) != 0)
		return false;
	if (when)
		*when = (long long)st.st_mtime;
	std::vector<uint8_t> data;
	if (!ReadFile(state + ".thumb", data) || data.size() < 12 || memcmp(data.data(), "YTH1", 4) != 0)
		return true;
	const int tw = int(Le32(&data[4])), th = int(Le32(&data[8]));
	if (tw < 2 || th < 2 || tw > 1024 || th > 1024 || data.size() != 12 + size_t(tw) * th * 4)
		return true;
	px.resize(size_t(tw) * th);
	memcpy(px.data(), data.data() + 12, px.size() * 4);
	w = tw;
	h = th;
	return true;
}

std::string StateWhen(long long when, bool short_form)
{
	if (when <= 0)
		return "";
	const time_t t = time_t(when);
	struct tm tm = {};
	localtime_r(&t, &tm);
	char hm[16];
	snprintf(hm, sizeof(hm), "%02d:%02d", tm.tm_hour, tm.tm_min);
	const int days = fe::recent::Days(when);
	if (short_form)
		return days <= 1 ? std::string(T(days == 0 ? "today" : "yesterday")) + " " + hm : fe::recent::Ago(when);
	if (days <= 1)
		return Fmt(T(days == 0 ? "Saved today at %s" : "Saved yesterday at %s"), hm);
	return Fmt(T("Saved %d days ago"), days);
}

bool StateExists(int slot)
{
	return g.loaded && FileExists(StatePath(slot));
}

bool PreviousStateExists(int slot)
{
	return g.loaded && FileExists(StatePath(slot) + ".prev");
}

void Reset()
{
	if (!g.loaded)
		return;
	retro_reset();
	RewindClear();
	ELog("[emu] reset");
}

bool FormatBackup(int which)
{
	if (!g.loaded)
		return false;
	if (which == 1 && !g.cart_size)
		return false;
	const bool ok = ymir_ps5_format_backup(which);
	ELog("[emu] %s formatted: %s", which == 0 ? "internal backup memory" : "backup cartridge", ok ? "ok" : "FAILED");
	if (!ok)
		return false;
	if (which == 0)
		SaveSram(true);
	else
		SaveCart(true);
	retro_reset(); // the game looks at the memory when it starts
	RewindClear();
	return true;
}

bool HasBackupCart()
{
	return g.loaded && g.cart_size > 0;
}

int DiscCount()
{
	return g.loaded && g.have_disk && g.disk.get_num_images ? int(g.disk.get_num_images()) : 0;
}

int DiscIndex()
{
	return g.loaded && g.have_disk && g.disk.get_image_index ? int(g.disk.get_image_index()) : 0;
}

bool ChangeDisc(int index)
{
	const int n = DiscCount();
	if (n < 2 || index < 0 || index >= n || !g.disk.set_eject_state || !g.disk.set_image_index)
		return false;
	if (index == DiscIndex())
		return true;
	// the console's way: open the lid, swap the disc, close the lid (the game sees the tray open and closed)
	bool ok = g.disk.set_eject_state(true);
	ok = ok && g.disk.set_image_index(unsigned(index));
	ok = g.disk.set_eject_state(false) && ok;
	ELog("[emu] disc %d of %d: %s", index + 1, n, ok ? "inserted" : "FAILED");
	Osd(ok ? Fmt(T("Disc %d of %d inserted"), index + 1, n) : std::string("Could not change the disc"));
	RewindClear();
	return ok;
}

void PowerCycle()
{
	if (!g.loaded)
		return;
	const std::string path = g.path;
	CloseGame();
	std::string error;
	if (!LoadGame(path, &error))
	{
		ELog("[emu] power cycle: %s", error.c_str());
		ProsperoNotify(T("SSYmirPS5: the game could not be started again. %s"), error.c_str());
	}
}

void RedrawLastFrame()
{
	ps5video::InvalidateFrame(); // what is around the picture is drawn again too (black, or the border)
	DrawLast();
}

FrameResult RunFrame()
{
	if (!g.loaded)
		return FrameResult::Stopped;
	fe::Settings& cfg = fe::Config();

	// -- hot keys (player 1)
	ps5input::Poll();
	const uint32_t raw = ps5input::Pad(0).raw_buttons;
	constexpr uint32_t kTouch = SCE_PAD_BUTTON_TOUCH_PAD;
	if (g.wait_release)
	{
		g.wait_release = (raw & (SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3 | SCE_PAD_BUTTON_CROSS | SCE_PAD_BUTTON_CIRCLE |
							 kTouch)) != 0;
		g.prev_p1 = raw;
	}
	const uint32_t pressed = raw & ~g.prev_p1;
	g.prev_p1 = raw;
	// Touchpad release: up to 1 second opens the menu; holding over 1 second captures immediately.
	if (screenshot::TakeMenuRequest()) return FrameResult::OpenMenu;

	const bool mod = (raw & kTouch) != 0;
	const bool l3 = (raw & SCE_PAD_BUTTON_L3) != 0 && !(raw & SCE_PAD_BUTTON_R3);
	const bool r3 = (raw & SCE_PAD_BUTTON_R3) != 0 && !(raw & SCE_PAD_BUTTON_L3);
	if (mod && (pressed & (SCE_PAD_BUTTON_UP | SCE_PAD_BUTTON_DOWN | SCE_PAD_BUTTON_LEFT | SCE_PAD_BUTTON_RIGHT)))
	{
		if (pressed & SCE_PAD_BUTTON_UP)
			SaveState(cfg.state_slot);
		else if (pressed & SCE_PAD_BUTTON_DOWN)
			LoadState(cfg.state_slot);
		else
		{
			cfg.state_slot = (pressed & SCE_PAD_BUTTON_RIGHT) ? cfg.state_slot % 10 + 1 : (cfg.state_slot + 8) % 10 + 1;
			long long when = 0;
			const bool used = StateThumb(cfg.state_slot, false, g.slot_thumb, g.slot_thumb_w, g.slot_thumb_h, &when);
			std::string text = Fmt(T(used ? "State slot %d (used)" : "State slot %d (empty)"), cfg.state_slot);
			if (used)
				text += "   \xC2\xB7   " + StateWhen(when, true);
			Osd(text);
			g.slot_thumb_until = g.slot_thumb.empty() ? 0 : Now() + 2.5;
			ELog("[emu] state slot %d", cfg.state_slot);
			cfg.Save();
		}
	}
	const bool want_rewind = cfg.rewind && l3;
	if (want_rewind != g.rewinding)
	{
		g.rewinding = want_rewind;
		ELog("[emu] rewind %s (%zu snapshots, %zu MB)", want_rewind ? "on" : "off", g.rewind.size(),
			g.rewind_bytes >> 20);
	}
	const bool want_turbo = r3;
	if (want_turbo != g.turbo)
	{
		const double t = Now();
		if (!want_turbo && g.ff_started > 0 && t > g.ff_started)
			ELog("[emu] fast forward off: %.1f s at %.2fx (asked %d%%)", t - g.ff_started,
				g.ff_total / (std::max(1.0, g.fps) * (t - g.ff_started)), cfg.ff_speed);
		else if (want_turbo)
			ELog("[emu] fast forward on (%d%%)", cfg.ff_speed);
		g.turbo = want_turbo;
		g.ff_credit = 0;
		g.ff_last = g.ff_since = g.ff_started = t;
		g.ff_frames = g.ff_total = 0;
		g.ff_shown = 0;
	}

	// -- emulate
	YMIR_STAGE(Emu, "retro_run");
	double core_ms = 0, present_ms = 0;
	if (g.rewinding)
	{
		if (!g.rewind.empty())
		{
			const std::vector<uint8_t>& s = g.rewind.back();
			retro_unserialize(s.data(), s.size());
			g.rewind_bytes -= s.size();
			g.rewind.pop_back();
		}
		g.mute = true;
		fe::cheats::Pump();
		retro_run();
		g.mute = false;
		g.in.clear();
	}
	else
	{
		// fast forward at the setting's speed, by the clock (fe_ffpace.h); 0: ahead of time, nothing to run yet
		int runs = 1;
		if (g.turbo)
		{
			const double t = Now();
			runs = fe::FastForwardRuns(cfg.ff_speed, g.fps, t - g.ff_last, g.ff_credit);
			g.ff_last = t;
			g.ff_frames += runs;
			g.ff_total += runs;
			if (t - g.ff_since >= 0.5)
			{
				g.ff_shown = g.ff_frames / (std::max(1.0, g.fps) * (t - g.ff_since));
				g.ff_frames = 0;
				g.ff_since = t;
			}
			if (runs == 0)
				usleep(1000);
		}
		for (int i = 0; i < runs; i++)
		{
			g.mute = g.turbo;
			const double t0 = Now();
			fe::cheats::Pump();
			retro_run();
			core_ms += (Now() - t0) * 1000.0;
			if (!g.turbo)
				g_perf.emu_frames += 1.0;
			g.frames++;
			g.fps_frames++;
			if (cfg.rewind && ++g.rewind_tick >= kRewindEvery)
			{
				g.rewind_tick = 0;
				RewindPush();
			}
		}
		g.mute = false;
	}
	FlushAudio();
	g.diag.core_ms += core_ms;
	g.diag.core_n++;
	DiagReport(false);

	// -- picture
	const double now = Now();
	if (now - g.fps_since >= 1.0)
	{
		g.fps_shown = g.fps_frames / (now - g.fps_since);
		g.fps_frames = 0;
		g.fps_since = now;
	}
	const bool pal = g.fps < 55.0;
	if (g.new_frame || (!g.osd.empty() && now < g.osd_until + 0.1) || cfg.show_fps)
	{
		g.new_frame = false;
		const double t_pic = Now();
		fe::border::Reselect(BorderShape()); // a few comparisons: a new picture shape (rare) refits the border chosen at load
		ps5video::Rect r = DrawLast();
		ShowOverlays(&r);
		present_ms = (Now() - t_pic) * 1000.0; // the picture's preparation (a CPU shader included), not the vsync wait
		// 60 Hz games wait for the flip: the display paces them (fast forward is paced by the clock: no wait)
		ps5video::Present(r.x, r.y, r.w, r.h, !pal && !g.turbo);
	}

	// -- pacing on the sound: 50 Hz games, or a ring filling up
	if (!g.turbo && !g.rewinding)
	{
		if (cfg.audio && ps5audio::Available())
		{
			// 50 Hz: the sound is the clock; 60 Hz: vsync is, and this only catches a display that doesn't block
			const int limit = pal ? kTargetQueued : kTargetQueued * 2;
			const double give_up = Now() + 0.1;
			while (ps5audio::Queued() > limit && Now() < give_up)
				usleep(1000);
		}
		else if (pal)
		{
			static double next = 0;
			if (next < now - 0.1)
				next = now;
			next += 1.0 / g.fps;
			if (next > Now())
				usleep(useconds_t((next - Now()) * 1e6));
		}
	}


	// -- battery save, every 3 s when it changed; the log, now and then
	if (now >= g.next_sram_check)
	{
		g.next_sram_check = now + 3.0;
		SaveSram(false);
	SaveCart(false);
	}
	// a GPU shader being compiled (the first time only: then it comes from the pipeline cache): said on screen
	if (ps5video::ShaderBuilding())
	{
		g.osd = Fmt(T("Preparing the shader %s..."), ps5video::ShaderName(emu::EffectiveShader()));
		g.osd_until = now + 0.5;
		g.next_shader_check = now + 2.0; // the plain picture's time is not the shader's
		ps5video::TakeShaderMs();
	}
	if (now >= g.next_shader_check)
	{
		// the shader's time per frame, every 2 s; when the game also runs slow, say once which setting helps
		g.next_shader_check = now + 2.0;
		const double ms = ps5video::TakeShaderMs();
		g.last_shader_ms = ms;
		if (ms > 0)
		{
			g.shader_ms_sum += ms;
			g.shader_ms_n++;
		}
		const double budget = 1000.0 / std::max(50.0, g.fps);
		if (!g.shader_warned && !g.turbo && !g.rewinding && emu::EffectiveShader() != 0 && ms > budget * 0.6 &&
			g.fps_shown > 0 && g.fps_shown < g.fps * 0.95)
		{
			g.shader_warned = true;
			ELog("[emu] the shader %s takes %.1f ms a frame and the game runs at %.1f fps: suggested a lighter one",
				ps5video::ShaderName(emu::EffectiveShader()), ms, g.fps_shown);
			Osd(Fmt(T("%s is too heavy here: choose a lighter shader (Settings, Shader)"), ps5video::ShaderName(emu::EffectiveShader())));
		}
	}
	if (now >= g.next_stats)
	{
		g.stats_reports++;
		g.next_stats = now + (g.stats_reports < 3 ? 5.0 : 60.0);
		const double shader_ms = g.shader_ms_n ? g.shader_ms_sum / g.shader_ms_n : 0.0;
		g.shader_ms_sum = 0;
		g.shader_ms_n = 0;
		const double rate = g.adj_n ? g.adj_sum / double(g.adj_n) : 1.0;
		ELog("[emu] frame %llu, %.1f fps, audio queued %d (%.0f ms; %.0f-%.0f ms), rate x%.4f (%.4f-%.4f), "
				 "underruns %llu, shader %.1f ms",
			(unsigned long long)g.frames, g.fps_shown, ps5audio::Queued(), ps5audio::Queued() * 1000.0 / ps5audio::kRate,
			g.adj_n ? g.queued_min * 1000.0 / ps5audio::kRate : 0.0, g.adj_n ? g.queued_max * 1000.0 / ps5audio::kRate : 0.0,
			rate, g.adj_n ? g.adj_min : 1.0, g.adj_n ? g.adj_max : 1.0, (unsigned long long)ps5audio::Underruns(), shader_ms);
		g.adj_sum = 0;
		g.adj_n = 0;
		g.adj_min = 2;
		g.adj_max = 0;
		g.queued_min = 1 << 30;
		g.queued_max = 0;
	}
	PerfFrame(core_ms, present_ms, g.last_shader_ms);
	return FrameResult::Continue;
}
} // namespace emu
