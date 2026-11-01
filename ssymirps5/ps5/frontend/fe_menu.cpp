// SSYmirPS5 frontend: the menus.
//
// Everything is drawn on the CPU into the 1920x1080 surface (fe_text: Roboto + PromptFont) and shown with
// ps5video::Present, which waits for vsync, so the menus run at 60 Hz.
//
// SPDX-License-Identifier: MIT

#include "fe_menu.h"
#include "fe_screenshot.h"
#include "fe_cheats.h"

#include "fe_emu.h"
#include "fe_i18n.h"
#include "fe_coverworker.h"
#include "fe_settings.h"
#include "fe_shelf.h"
#include "fe_text.h"
#include "fe_style.h"

#include "OrbisPaths.h"
#include "ProsperoJailbreak.h"
#include "ProsperoUpdate.h"
#include "ProsperoCrt.h"
#include "ProsperoGpu.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"


#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <unistd.h>
#include <vector>

namespace fe
{
namespace
{
using ps5video::Rgb;

// printf into a std::string (the translated formats)
std::string Fmt(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
std::string Fmt(const char* fmt, ...)
{
	char buf[512];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);
	return buf;
}
constexpr int W = ps5video::kWidth;
constexpr int H = ps5video::kHeight;

const uint32_t kBg = Rgb(12, 13, 17);
const uint32_t kPanel = Rgb(24, 26, 33);
const uint32_t kAccent = Rgb(203, 181, 139);
const uint32_t kSel = Rgb(52, 49, 44);
const uint32_t kText = Rgb(235, 235, 245);
const uint32_t kDim = Rgb(150, 150, 175);

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---- input with key repeat ---------------------------------------------------------------------------
struct Nav
{
	bool up = false, down = false, left = false, right = false;
	bool ok = false, back = false, options = false, menu = false;
	bool l1 = false, r1 = false; // 2.7: the settings' sections
	bool square = false; // 3.9: the cheats' All off
};

class NavReader
{
public:
	NavReader()
	{
		ps5input::Poll();
		m_prev = ps5input::Pad(0).buttons; // buttons still held from before don't count
	}
	Nav Read()
	{
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~m_prev;
		m_prev = cur;
		const double now = Now();
		Nav n;
		n.up = Repeat(cur, down, SCE_PAD_BUTTON_UP, now, 0);
		n.down = Repeat(cur, down, SCE_PAD_BUTTON_DOWN, now, 1);
		n.left = Repeat(cur, down, SCE_PAD_BUTTON_LEFT, now, 2);
		n.right = Repeat(cur, down, SCE_PAD_BUTTON_RIGHT, now, 3);
		n.ok = down & SCE_PAD_BUTTON_CROSS;
		n.back = down & SCE_PAD_BUTTON_CIRCLE;
		n.options = down & SCE_PAD_BUTTON_OPTIONS;
		n.l1 = down & SCE_PAD_BUTTON_L1;
		n.r1 = down & SCE_PAD_BUTTON_R1;
		n.square = down & SCE_PAD_BUTTON_SQUARE;
		n.menu = screenshot::TakeMenuRequest();
		return n;
	}

private:
	bool Repeat(uint32_t cur, uint32_t down, uint32_t bit, double now, int i)
	{
		if (down & bit)
		{
			m_next[i] = now + 0.40;
			return true;
		}
		if ((cur & bit) && now >= m_next[i])
		{
			m_next[i] = now + 0.07;
			return true;
		}
		return false;
	}
	uint32_t m_prev = 0;
	double m_next[4] = {};
};

void Present()
{
	ps5video::Present(0, 0, 0, 0, true);
}

void Header(const char* subtitle)
{
	// 2.7: the background, the version small at the top right (each screen writes its own title)
	ps5video::FillRect(0, 0, W, H, style::Bg());
	const char* ver = "SSYmirPS5 " SSYMIRPS5_VERSION " - Ymir " YMIR_CORE_VERSION;
	TextMid(W - 60, 46, ver, 15, style::Off(), Weight::Regular, Align::Right);
	(void)subtitle;
}

[[maybe_unused]] void Footer(const std::string& help)
{
	TextMid(W / 2, 1020, FitTextPt(help, 20, W - 120, Weight::Medium).c_str(), 20, style::Lo(), Weight::Medium, Align::Center);
}

// A box of label/value rows (pause menu, settings); a row with header=true is a section title.
struct Row
{
	std::string label;
	std::string value; // "" = an action
	bool enabled = true;
	bool header = false;
	bool raw = false; // the label is a name (a cheat's), shown as it is, not translated
	std::string help; // 3.6: what it is for (English, translated when drawn)
	bool slider = false; // 3.11.3: a 0-100% value drawn as a slider (the others, 150% or 25%, as words)
};

// "VIDEO" -> "Video", "VÍDEO" -> "Vídeo" (the categories are written in capitals; Latin letters and their accents)
std::string TitleCase(const std::string& s)
{
	std::string out;
	bool first = true;
	for (size_t i = 0; i < s.size(); i++)
	{
		const unsigned char c = (unsigned char)s[i];
		if (c == 0xC3 && i + 1 < s.size())
		{
			unsigned char d = (unsigned char)s[i + 1];
			if (!first && d >= 0x80 && d <= 0x9E && d != 0x97)
				d = (unsigned char)(d + 0x20);
			out += char(c);
			out += char(d);
			i++;
			first = false;
			continue;
		}
		if (c == ' ' || c == '(')
		{
			out += char(c);
			first = c == ' ' ? false : first;
			continue;
		}
		out += char(!first && c >= 'A' && c <= 'Z' ? c + 32 : c);
		first = false;
	}
	return out;
}

style::Icon IconFor(const std::string& header)
{
	if (header.rfind("VIDEO", 0) == 0)
		return style::Icon::Video;
	if (header.rfind("AUDIO", 0) == 0)
		return style::Icon::Audio;
	if (header.rfind("EMULATION", 0) == 0)
		return style::Icon::Emulation;
	if (header.rfind("CONTROLS", 0) == 0)
		return style::Icon::Controls;
	if (header.rfind("LIBRARY", 0) == 0)
		return style::Icon::Library;
	return style::Icon::System;
}

// The value of a row drawn on the right of its line (ending at x1): a switch for On/Off, a slider for a percentage,
// else the words (with < > around them on the selected row).
void DrawValue(const Row& row, int x1, int mid, bool selected)
{
	if (row.value == "\x01On" || row.value == "\x01Off")
	{
		style::Toggle(float(x1 - 55), float(mid), row.value == "\x01On");
		return;
	}
	const std::string v = T(row.value.c_str());
	// 3.11.3: only the rows that are a share of a whole (volume, FSR sharpness): before, any value up to 200% got a
	// slider, so 150% / 200% of fast forward and of the SH-2 clock drew one past its end, without the < > of the others
	int pct = -1;
	if (row.slider && v.size() >= 2 && v.back() == '%')
	{
		char* end = nullptr;
		const long n = strtol(v.c_str(), &end, 10);
		if (end && *end == '%' && n >= 0 && n <= 100)
			pct = int(n);
	}
	const uint32_t vc = !row.enabled ? style::Off() : selected ? style::Hi() : style::Lo();
	if (pct >= 0 && row.enabled)
	{
		const int tw = TextWidthPt(v.c_str(), 21, Weight::Medium);
		TextMid(x1, mid, v.c_str(), 21, vc, Weight::Medium, Align::Right);
		style::Slider(float(x1 - tw - 24 - 210), float(x1 - tw - 24), float(mid), pct / 100.0f);
		return;
	}
	const std::string fit = FitTextPt(v, 21, 560, selected ? Weight::SemiBold : Weight::Medium);
	if (selected && row.enabled)
	{
		TextMid(x1, mid, "\xE2\x80\xBA", 28, style::Accent(), Weight::Medium, Align::Right);
		const int vx = x1 - 28;
		TextMid(vx, mid, fit.c_str(), 21, vc, Weight::SemiBold, Align::Right);
		TextMid(vx - TextWidthPt(fit.c_str(), 21, Weight::SemiBold) - 16, mid, "\xE2\x80\xB9", 28, style::Accent(), Weight::Medium,
			Align::Right);
	}
	else
		TextMid(x1, mid, fit.c_str(), 21, vc, Weight::Medium, Align::Right);
}

// Settings (rows with section titles): the sections on the left with their icons, the selected one's rows on the
// right. Otherwise (the pause menu) one panel of rows in the middle.
// warn / warn2: 3.9.3, a caution over the rows of the panel in the middle (the cheats), two lines; "" none
void DrawOptionBox(const std::string& title, const std::vector<Row>& rows, int sel, bool over_game, const std::string& warn = "",
	const std::string& warn2 = "", const std::vector<std::string>& foot = {})
{
	bool sections = false;
	for (const Row& r : rows)
		sections = sections || r.header;
	if (sections)
	{
		Text(80, 80, FitTextPt(T(title.c_str()), 44, 820, Weight::SemiBold).c_str(), 44, style::Hi(), Weight::SemiBold);
		// the section the selected row is in, and its rows
		int sec = -1;
		for (int i = 0; i <= sel && i < int(rows.size()); i++)
			if (rows[size_t(i)].header)
				sec = i;
		std::vector<int> heads;
		for (int i = 0; i < int(rows.size()); i++)
			if (rows[size_t(i)].header)
				heads.push_back(i);
		int y = 180;
		for (int hdr : heads)
		{
			const bool on = hdr == sec;
			std::string name = T(rows[size_t(hdr)].label.c_str());
			const size_t paren = name.find(" (");
			if (paren != std::string::npos)
				name = name.substr(0, paren);
			name = TitleCase(name);
			if (on)
			{
				style::RoundRect(60, float(y), 390, 58, 13, style::Surface2());
				style::RoundRect(60, float(y + 15), 4, 28, 2, style::Accent());
			}
			style::DrawIcon(IconFor(rows[size_t(hdr)].label), 90, float(y + 15), 28, on ? style::Hi() : style::Lo());
			TextMid(136, y + 29, name.c_str(), 22, on ? style::Hi() : style::Lo(), on ? Weight::SemiBold : Weight::Medium);
			y += 68;
		}
		// the rows of this section (the last one also ends with Back)
		int first = sec + 1, last = int(rows.size()) - 1;
		for (int i = first; i < int(rows.size()); i++)
			if (rows[size_t(i)].header)
			{
				last = i - 1;
				break;
			}
		const int x0 = 540, x1 = W - 100, row_h = 75, top_y = 150;
		const int visible = std::max(1, (905 - top_y) / row_h); // 3.6: under it, the selected setting's description
		int top = first;
		const int count = last - first + 1;
		if (count > visible)
			top = std::clamp(sel - visible / 2, first, last - visible + 1);
		// a section title's words in brackets: under the list's top ("Saturn button -> PS5 button")
		{
			const std::string full = T(rows[size_t(std::max(0, sec))].label.c_str());
			const size_t paren = full.find(" (");
			if (paren != std::string::npos && full.back() == ')')
				TextMid(x0 + 30, top_y - 30, full.substr(paren + 2, full.size() - paren - 3).c_str(), 17, style::Lo());
		}
		for (int i = top; i <= last && i < top + visible; i++)
		{
			const Row& row = rows[size_t(i)];
			const int ry = top_y + (i - top) * row_h;
			const bool on = i == sel;
			if (on)
				style::RoundRect(float(x0), float(ry), float(x1 - x0), float(row_h - 9), 13, style::Surface2());
			const int mid = ry + (row_h - 9) / 2;
			const uint32_t lc = !row.enabled ? style::Off() : on ? style::Hi() : style::Mid();
			const std::string label = FitTextPt(row.raw ? row.label : T(row.label.c_str()), 23, (x1 - x0) / 2, Weight::Medium);
			TextMid(x0 + 30, mid, label.c_str(), 23, lc, Weight::Medium);
			if (!row.value.empty())
				DrawValue(row, x1 - 30, mid, on);
		}
		if (count > visible)
		{
			const float track = float(visible * row_h);
			const float thumb = std::max(40.0f, track * visible / count);
			const float ty = top_y + (track - thumb) * (top - first) / std::max(1, count - visible);
			style::RoundRect(float(x1 + 24), ty, 5, thumb, 2.5f, style::Surface2());
		}
		// 3.6: what the selected setting is for, in at most two lines under the list
		if (sel >= 0 && sel < int(rows.size()) && !rows[size_t(sel)].help.empty())
		{
			const std::string help = T(rows[size_t(sel)].help.c_str());
			const int width = x1 - x0 - 60;
			std::vector<std::string> lines(1);
			size_t i = 0;
			while (i < help.size())
			{
				const size_t sp = help.find(' ', i);
				const std::string word = help.substr(i, sp == std::string::npos ? std::string::npos : sp - i);
				const std::string tryline = lines.back().empty() ? word : lines.back() + " " + word;
				if (!lines.back().empty() && TextWidthPt(tryline.c_str(), 19, Weight::Regular) > width)
					lines.push_back(word);
				else
					lines.back() = tryline;
				i = sp == std::string::npos ? help.size() : sp + 1;
			}
			if (lines.size() > 2)
			{
				lines.resize(2);
				lines[1] = FitTextPt(lines[1] + " ...", 19, width, Weight::Regular);
			}
			style::Line(float(x0 + 30), float(x1 - 30), 925, style::Hi(), 28);
			for (size_t k = 0; k < lines.size(); k++)
				TextMid(x0 + 30, 958 + int(k) * 28, lines[k].c_str(), 19, style::Lo(), Weight::Regular);
		}
		return;
	}
	// one panel in the middle (the pause menu, a list)
	const int row_h = 58;
	// 3.9.4: a box of help under the rows (the cheats: how to add your own), its first line a title
	const int foot_h = foot.empty() ? 0 : 24 + 34 + int(foot.size() - 1) * 30 + 20;
	const int room = (H - 60 - 150 - (warn.empty() ? 0 : 86) - foot_h) / row_h;
	const int visible = std::max(1, std::min({int(rows.size()), 14, room}));
	const int bw = 900;
	const int warn_h = warn.empty() ? 0 : 86; // the caution's box and the gap under it
	const int bh = 120 + warn_h + visible * row_h + 30 + foot_h;
	const int bx = (W - bw) / 2;
	const int by = std::max(20, (H - bh) / 2 - 20);
	const int rows_y = by + 110 + warn_h;
	int top = 0;
	if (int(rows.size()) > visible)
		top = std::max(0, std::min(int(rows.size()) - visible, sel - visible / 2));
	style::RoundRect(float(bx), float(by), float(bw), float(bh), 26, style::Surface1(), over_game ? 238 : 255);
	Text(bx + 44, by + 38, FitTextPt(T(title.c_str()), 34, bw - 88, Weight::SemiBold).c_str(), 34, style::Hi(), Weight::SemiBold);
	if (!warn.empty())
	{
		const float wy = float(by + 100);
		style::RoundRect(float(bx + 20), wy, float(bw - 40), 72, 12, style::Warn(), 34);
		style::RoundRectOutline(float(bx + 20), wy, float(bw - 40), 72, 12, 1.0f, style::Warn(), 110);
		style::WarnSign(float(bx + 40), wy + 20, 30, style::Warn());
		TextMid(bx + 86, int(wy) + 24, FitTextPt(warn, 21, bw - 130, Weight::SemiBold).c_str(), 21, style::WarnText(), Weight::SemiBold);
		if (!warn2.empty())
			TextMid(bx + 86, int(wy) + 51, FitTextPt(warn2, 19, bw - 130, Weight::Regular).c_str(), 19, style::Mid(), Weight::Regular);
	}
	for (int i = 0; i < visible; i++)
	{
		const Row& row = rows[size_t(top + i)];
		const int y = rows_y + i * row_h;
		const int mid = y + (row_h - 8) / 2;
		const bool on = top + i == sel;
		if (on)
			style::RoundRect(float(bx + 20), float(y), float(bw - 40), float(row_h - 8), 12, style::Surface2());
		const uint32_t col = !row.enabled ? style::Off() : on ? style::Hi() : style::Mid();
		const std::string label = FitTextPt(row.raw ? row.label : T(row.label.c_str()), 22, bw * 3 / 5, Weight::Medium);
		TextMid(bx + 44, mid, label.c_str(), 22, col, Weight::Medium);
		if (!row.value.empty())
			DrawValue(row, bx + bw - 44, mid, on);
	}
	if (!foot.empty())
	{
		const float fy = float(rows_y + visible * row_h + 14);
		style::RoundRect(float(bx + 20), fy, float(bw - 40), float(foot_h - 20), 12, style::Surface2());
		TextMid(bx + 44, int(fy) + 26, FitTextPt(foot[0], 20, bw - 88, Weight::SemiBold).c_str(), 20, style::Hi(), Weight::SemiBold);
		for (size_t k = 1; k < foot.size(); k++)
			TextMid(bx + 44, int(fy) + 26 + 34 + int(k - 1) * 30, FitTextPt(foot[k], 19, bw - 88, Weight::Regular).c_str(), 19,
				style::Mid(), Weight::Regular);
	}
	if (int(rows.size()) > visible)
	{
		const float track = float(visible * row_h);
		const float thumb = std::max(30.0f, track * visible / int(rows.size()));
		const float ty = rows_y + (track - thumb) * top / std::max(1, int(rows.size()) - visible);
		style::RoundRect(float(bx + bw - 14), ty, 5, thumb, 2.5f, style::Surface2());
	}
}

// The first row of the next (dir 1) or the previous (dir -1) section, round the ends: R1 / L1 in Settings.
int StepSection(int sel, int dir, const std::vector<Row>& rows)
{
	std::vector<int> heads;
	for (int i = 0; i < int(rows.size()); i++)
		if (rows[size_t(i)].header)
			heads.push_back(i);
	if (heads.empty())
		return sel;
	int cur = 0;
	for (size_t k = 0; k < heads.size(); k++)
		if (heads[k] <= sel)
			cur = int(k);
	const int next = (cur + dir + int(heads.size())) % int(heads.size());
	const int row = heads[size_t(next)] + 1;
	return row < int(rows.size()) ? row : sel;
}

int Step(int sel, int dir, const std::vector<Row>& rows)
{
	const int n = int(rows.size());
	for (int i = 0; i < n; i++)
	{
		sel = (sel + dir + n) % n;
		if (!rows[size_t(sel)].header)
			return sel;
	}
	return sel;
}

// an on/off setting's value: drawn as a switch (DrawValue), never as words
const char* OnOff(bool b)
{
	return b ? "\x01On" : "\x01Off";
}

// ---- settings ----------------------------------------------------------------------------------------
enum SettingRow
{
	H_VIDEO,
	S_RENDERER, // Vulkan (GPU) or CPU: after a restart
	S_UPSCALER, // AMD FSR 1.0 (GPU)
	S_FSRSHARP,
	S_SHADER, // when upscaling is off
	S_SHADERPARAMS, // GPU only: the shader's parameters, a screen of their own
	S_SCALE,
	S_ASPECT,
	S_BORDER, // the art around the picture (fe_border.h)
	S_SMOOTH,
	S_SCANLINES,
	S_DEINTERLACE,
	S_MESHES,
	S_FPS,
	S_SCREENSHOTS,
	S_LAUNCHANIM,
	H_AUDIO,
	S_AUDIO,
	S_VOLUME,
	S_INTERP,
	S_SCSP,
	H_EMU,
	S_FF,
	S_REWIND,
	S_RESUME,
	S_REGION,
	S_CART,
	S_CARTSHARED,
	S_CDSPEED,
	S_SH2CLOCK,
	S_SH2CACHE,
	S_CDBLLE,
	H_CONTROLS,
	S_PADTYPE,
	S_BTN_FIRST,
	S_BTN_LAST = S_BTN_FIRST + emu::kConsoleButtonCount - 1,
	S_BTN_DEFAULT,
	H_LIBRARY,
	S_COVERS,
	S_SORT, // 3.11: the shelf's order
	S_BORDERS_ALL, // Download all borders (an action)
	H_SYSTEM,
	S_BIOS, // where the Saturn BIOS was found (information)
	S_LANGUAGE,
	S_PERFLOG,
	S_DEBUGLOGS,
	S_UPDATES,
	S_COUNT
};

// 3.11.5: 300% and up are gone: the console can't run a Saturn game that fast, so they all ran as Unlimited
const int kFfSteps[] = {150, 200, 0};
constexpr int kFfCount = int(sizeof(kFfSteps) / sizeof(kFfSteps[0]));

// ---- updates (ProsperoUpdate.h): the helper checks, downloads and installs; this screen shows it ----------------
struct UpdateView
{
	update::Status st;
	bool helper = false;
	double polled = -1;
};
UpdateView g_upd;

void PollUpdate(bool force)
{
	if (!force && Now() - g_upd.polled < 0.5)
		return;
	g_upd.polled = Now();
	std::string reply;
	g_upd.helper = jailbreak::UpdateRequest("status", reply) && update::Decode(reply, g_upd.st);
}

std::string UpdateValue()
{
	char buf[96];
	if (!g_upd.helper)
		return "Needs the helper";
	const update::Status& st = g_upd.st;
	switch (st.phase)
	{
		case update::Phase::Checking: return "Checking...";
		case update::Phase::UpToDate: return "Up to date";
		case update::Phase::Available:
			snprintf(buf, sizeof(buf), T("Install %s (%.1f MB)"), st.latest.c_str(), st.size / 1048576.0);
			return buf;
		case update::Phase::Downloading:
			snprintf(buf, sizeof(buf), T("Downloading %d%%"), st.total ? int(st.done * 100 / st.total) : 0);
			return buf;
		case update::Phase::Installing:
			snprintf(buf, sizeof(buf), T("Installing %d/%d"), int(st.done), int(st.total));
			return buf;
		case update::Phase::Done: return "Installed: restart";
		case update::Phase::Failed: return "Failed: try again";
		default: return "Check now";
	}
}

// 3.11.8: what the last try to start the helper found (Settings, Updates, X without a helper), for the line below
std::string g_helper_note;

void UpdateAction()
{
	std::string reply;
	if (!g_upd.helper)
	{
		// no helper: start it now through the ELF loader (built into the app), as the start does
		const jailbreak::HelperState hs = jailbreak::EnsureHelper(5.0);
		g_helper_note = hs == jailbreak::HelperState::NoLoader
			? Fmt(T("No ELF loader to start the helper: send SSYmirPS5-v%s.elf with the Payload Manager."), SSYMIRPS5_VERSION)
			: hs == jailbreak::HelperState::NoAnswer ? std::string(T("The helper was sent but did not answer: try again in a moment."))
													 : std::string();
		PollUpdate(true);
		return;
	}
	switch (g_upd.st.phase)
	{
		case update::Phase::Available: jailbreak::UpdateRequest("install", reply); break;
		case update::Phase::Checking:
		case update::Phase::Downloading:
		case update::Phase::Installing: break;
		case update::Phase::Done:
		{
			// the new build, opened in this one's place
			const char* path = "/data/homebrew/" YMIR_TITLE_ID "/eboot.bin";
			OrbisLog("[update] restarting %s", path);
			Config().Save();
			OrbisLogClose();
			const int rc = sceSystemServiceLoadExec(path, nullptr);
			if (rc == 0)
				for (int i = 0; i < 100; i++)
					usleep(100 * 1000);
			OrbisLogOpen("boot-after-update");
			OrbisLog("[update] sceSystemServiceLoadExec(%s) -> %x: close the app and open it again", path, unsigned(rc));
			break;
		}
		default: jailbreak::UpdateRequest("check", reply); break;
	}
	PollUpdate(true);
}

// Settings::border: nothing, the game's own art from /data/ssymirps5/borders (else a default, else the built-in frame), or the
// default frame only.
const char* BorderName(int b)
{
	switch (b)
	{
		case 1: return "Auto";
		case 2: return "Default frame";
		default: return "Off";
	}
}

// The Download all borders row: what the helper is doing (borders/progress.txt), or what the last press asked for.
struct BorderAsk
{
	double at = -1000;
	int queued = 0;
} g_border_ask;

std::string BorderDownloadValue()
{
	const CoverProgress p = ReadBorderProgress();
	const double age = Now() - g_border_ask.at;
	// 3.11.7: no helper (the payload not sent since the console started): it says so, instead of "Waiting for the
	// helper" for two minutes and then nothing. 3.11.8: once the app downloads the borders itself, its progress shows
	if (!g_upd.helper && !AppBorderWorkerRunning())
		return age < 6.0 && g_border_ask.queued > 0 ? Fmt(T("%d queued"), g_border_ask.queued) : std::string("Needs the helper");
	if (p.valid && p.left > 0)
		return Fmt(T("%d left"), int(p.left));
	// 3.11.9: a helper that answers but does not take the list up (an old one, still running since the console
	// started): after 15 s the app downloads the borders itself
	const bool working = p.valid && (p.left > 0 || p.fetched > 0);
	if (g_border_ask.queued > 0 && !working && age > 15.0 && age < 120.0 && !AppBorderWorkerRunning())
	{
		OrbisLog("[borders] the helper has not taken the list up in %.0f s: the app downloads them", age);
		StartAppBorderWorker();
	}
	if (age < 6.0)
		return g_border_ask.queued > 0 ? Fmt(T("%d queued"), g_border_ask.queued) : std::string("Nothing to fetch");
	if (g_border_ask.queued > 0 && !p.valid && age < 120.0)
		return "Waiting for the helper";
	if (p.valid && p.fetched > 0)
		return Fmt(T("%d fetched"), int(p.fetched));
	return "";
}

// 3.6: what each setting is for, in a few words (under the list, for the selected one; translated)
const char* SettingHelp(int s)
{
	if (s >= S_BTN_FIRST && s <= S_BTN_LAST)
		return "The PS5 button that works as this Saturn button.";
	switch (s)
	{
		case S_RENDERER: return "Who draws the picture: the PS5's graphics chip (faster, allows shaders) or its processor. Applies after a restart.";
		case S_UPSCALER: return "Enlarges the picture to your TV's resolution with AMD FSR: sharper than simply stretching it.";
		case S_FSRSHARP: return "How much FSR sharpens the edges. Too much can look harsh.";
		case S_SHADER: return "Filters that change the look of the picture, such as imitating an old tube TV.";
		case S_SHADERPARAMS: return "Fine-tunes the chosen shader: brightness, curvature, strength...";
		case S_SCALE: return "How the picture fills the screen: fitted, at an exact multiple (sharpest) or stretched.";
		case S_ASPECT: return "The shape of the picture. 4:3 is the Saturn's own; 16:9 stretches it to widescreen.";
		case S_BORDER: return "Fills the black bars at the sides with artwork of the game.";
		case S_SMOOTH: return "Softens the pixels. Off: sharper, square pixels.";
		case S_SCANLINES: return "Adds thin horizontal lines, like an old TV.";
		case S_DEINTERLACE: return "Removes the flicker and the lines in high-resolution games, such as Virtua Fighter 2.";
		case S_MESHES: return "Shows as real transparency what the Saturn drew as a dot pattern (shadows, glass, smoke).";
		case S_FPS: return "Shows the frames per second in a corner: 60 means full speed.";
		case S_SCREENSHOTS: return "Tap touchpad for the menu; hold over 1 second to capture. Release when notified. PNG: screenshots/.";
		case S_LAUNCHANIM: return "Before a game starts, the screen fades to black and the game's disc spins for a few seconds.";
		case S_AUDIO: return "Turns the game's sound on or off.";
		case S_VOLUME: return "The game's volume.";
		case S_INTERP: return "How sounds are smoothed. Linear is faithful to the Saturn; nearest neighbor sounds rougher.";
		case S_SCSP: return "Runs the sound chip on another core: faster. Turn it off if the sound crackles.";
		case S_FF: return "How fast the game runs while you hold R3.";
		case S_REWIND: return "Hold L3 to take the game back a few seconds.";
		case S_RESUME: return "Leaving a game saves where you are; the next time, it asks whether to continue from there.";
		case S_REGION: return "The country of the emulated console. Auto uses the disc's; change it if a game won't start.";
		case S_CART: return "The cartridge in the Saturn's slot: memory for saves, or the extra RAM some games need.";
		case S_CARTSHARED: return "Whether all games save on one backup cartridge, or each game has its own.";
		case S_CDSPEED: return "How fast the CD drive reads. Faster: shorter loading; some games may fail.";
		case S_SH2CLOCK: return "The speed of the Saturn's processors. 100% is the original; more can remove slowdowns, but breaks some games.";
		case S_SH2CACHE: return "Emulates the processors' cache: more accurate (a few games need it), but slower.";
		case S_CDBLLE: return "Emulates the CD drive with its original program (the bios/cdb file): more accurate, slower.";
		case S_PADTYPE: return "The Saturn controller your PS5 pad acts as: standard, analog 3D, racing wheel or Mission Stick.";
		case S_BTN_DEFAULT: return "Puts every button back where it was at first.";
		case S_COVERS: return "Downloads the covers of your games from the internet.";
		case S_SORT: return "The order of the games on the shelf. The year is the one on the disc; the Recent tab is always by time.";
		case S_BORDERS_ALL: return "Downloads every border now, to use them without a connection.";
		case S_BIOS: return "Where the Saturn BIOS was found. Games need it to run.";
		case S_LANGUAGE: return "The language of the app.";
		case S_PERFLOG: return "Records how fast the game runs, to report speed problems.";
		case S_DEBUGLOGS: return "Saves detailed logs to find bugs. Leave it off if everything works.";
		case S_UPDATES: return "Checks for and installs new versions of the app.";
		default: return "";
	}
}

Row SettingRowFor(int s)
{
	const Settings& c = Config();
	char buf[48];
	switch (s)
	{
		case H_VIDEO: return {"VIDEO", "", true, true};
		case S_RENDERER:
		{
			// takes effect at the next start; the value says so while it differs from the one running
			const char* want = c.renderer ? "Vulkan (GPU)" : "CPU";
			if (bool(c.renderer) != ps5video::GpuActive())
				return {"Renderer", Fmt(T("%s (after a restart)"), T(want))};
			return {"Renderer", want};
		}
		case S_UPSCALER:
			if (!ps5video::GpuActive())
				return {"Upscaling", "(needs Vulkan)", false};
			return {"Upscaling", emu::kUpscalers[c.upscaler % emu::kUpscalerCount].name};
		case S_FSRSHARP:
		{
			const bool on = ps5video::GpuActive() && c.upscaler > 0;
			snprintf(buf, sizeof(buf), "%d%%", 100 - c.fsr_sharpness * 5);
			Row r{"FSR sharpness", on ? std::string(buf) : std::string("(FSR off)"), on};
			r.slider = true;
			return r;
		}
		case S_SHADER:
		{
			const bool fsr = ps5video::GpuActive() && c.upscaler > 0;
			return {ps5video::GpuActive() ? "Shader (GPU)" : "Shader",
				fsr ? std::string("(FSR on)") : std::string(ps5video::ShaderName(std::min(c.shader, ps5video::ShaderCount() - 1))),
				!fsr};
		}
		case S_SHADERPARAMS:
		{
			const int sh = emu::EffectiveShader();
			const int n = sh > 0 ? ps5gpu::ShaderParamCount(sh) : 0;
			if (n == 0)
				return {"Shader parameters", sh > 0 ? "None" : "(shader Off)", false};
			return {"Shader parameters", Fmt(T("%d to adjust"), n)};
		}
		case S_SCALE: return {"Screen size", ps5video::ScaleName(ps5video::Scale(c.scale))};
		case S_ASPECT: return {"Aspect ratio", emu::kAspects[c.aspect % emu::kAspectCount].name};
		case S_BORDER: return {"Border", BorderName(c.border)};
		case S_SMOOTH:
		{
			const int sh = emu::EffectiveShader();
			return {"Smooth picture", sh ? "(shader)" : OnOff(c.smooth), sh == 0};
		}
		case S_SCANLINES:
		{
			const int sh = emu::EffectiveShader();
			return {"Scanlines (CRT effect)", sh ? "(shader)" : OnOff(c.scanlines), sh == 0};
		}
		case S_DEINTERLACE: return {"Deinterlace (high-res modes)", OnOff(c.deinterlace)};
		case S_MESHES: return {"Transparent meshes", OnOff(c.transparent_meshes)};
		case S_FPS: return {"Show FPS", OnOff(c.show_fps)};
		case S_SCREENSHOTS: return {"Screenshots at any time", OnOff(c.screenshots)};
		case S_LAUNCHANIM: return {"Animation when opening a game", OnOff(c.launch_anim)};
		case H_AUDIO: return {"AUDIO", "", true, true};
		case S_AUDIO: return {"Sound", OnOff(c.audio)};
		case S_VOLUME:
		{
			snprintf(buf, sizeof(buf), "%d%%", c.volume);
			Row r{"Volume", buf};
			r.slider = true;
			return r;
		}
		case S_INTERP: return {"Sample interpolation", emu::kAudioInterps[c.audio_interp % emu::kAudioInterpCount].name};
		case S_SCSP: return {"Sound chip on its own thread", OnOff(c.threaded_scsp)};
		case H_EMU: return {"EMULATION", "", true, true};
		case S_FF:
			if (c.ff_speed == 0)
				return {"Fast forward speed (hold R3)", "Unlimited"};
			snprintf(buf, sizeof(buf), "%d%%", c.ff_speed);
			return {"Fast forward speed (hold R3)", buf};
		case S_REWIND: return {"Rewind (hold L3)", OnOff(c.rewind)};
		case S_RESUME: return {"Continue where you left off", OnOff(c.resume)};
		case S_REGION: return {"Console region", emu::kRegions[c.region % emu::kRegionCount].name};
		case S_CART: return {"Cartridge", emu::kCartridges[c.cartridge % emu::kCartridgeCount].name};
		case S_CARTSHARED: return {"Backup cartridge", c.cart_shared ? "Shared by all games" : "One per game"};
		case S_CDSPEED: return {"CD read speed", emu::kCdSpeeds[c.cd_speed % emu::kCdSpeedCount].name};
		case S_SH2CLOCK: return {"SH-2 clock", emu::kSh2Clocks[c.sh2_clock % emu::kSh2ClockCount].name};
		case S_SH2CACHE: return {"SH-2 cache emulation", OnOff(c.sh2_cache)};
		case S_CDBLLE: return {"CD block LLE (needs bios/cdb)", OnOff(c.cdblock_lle)};
		case H_CONTROLS: return {"CONTROLS (Saturn button  ->  PS5 button)", "", true, true};
		case S_PADTYPE: return {"Player 1 controller", emu::kPadTypes[c.pad_type % emu::kPadTypeCount].name};
		case S_BTN_DEFAULT: return {"Default button layout", ""};
		case H_LIBRARY: return {"LIBRARY", "", true, true};
		case S_COVERS: return {"Download covers", OnOff(c.covers_download)};
		case S_SORT:
		{
			static const char* const kSorts[] = {"Name", "Recently played", "Most played", "Year"};
			return {"Sort the games", kSorts[std::clamp(c.shelf_sort, 0, 3)]};
		}
		case S_BORDERS_ALL: return {"Download all borders", BorderDownloadValue()};
		case H_SYSTEM: return {"SYSTEM", "", true, true};
		case S_BIOS:
		{
			const std::string b = emu::BiosFolder();
			return {"Saturn BIOS", b.empty() ? std::string("Not found") : b};
		}
		case S_LANGUAGE: return {"Language", LanguageName(c.language)};
		case S_PERFLOG: return {"Performance log", OnOff(c.perf_log)};
		case S_DEBUGLOGS: return {"Debug logs", OnOff(c.debug_logs)};
		case S_UPDATES: return {Fmt(T("Updates (this is %s)"), SSYMIRPS5_VERSION), UpdateValue()};
		default:
			if (s >= S_BTN_FIRST && s <= S_BTN_LAST)
			{
				const int i = s - S_BTN_FIRST;
				const int b = c.buttons[i];
				return {Fmt(T("Button %s"), T(emu::kConsoleButtons[i].name)),
					emu::kPs5Buttons[b >= 0 && b < emu::kPs5ButtonCount ? b : emu::kPs5ButtonCount - 1].name};
			}
			return {"", ""};
	}
}

int Cycle(int v, int dir, int n)
{
	return ((v + dir) % n + n) % n;
}

void ChangeSetting(int s, int dir)
{
	Settings& c = Config();
	switch (s)
	{
		case S_RENDERER: c.renderer = c.renderer ? 0 : 1; break;
		case S_UPSCALER: c.upscaler = Cycle(c.upscaler, dir, emu::kUpscalerCount); break;
		case S_FSRSHARP: c.fsr_sharpness = std::max(0, std::min(20, c.fsr_sharpness - dir)); break;
		case S_SHADER: c.shader = Cycle(std::min(c.shader, ps5video::ShaderCount() - 1), dir, ps5video::ShaderCount()); break;
		case S_SCALE: c.scale = Cycle(c.scale, dir, int(ps5video::Scale::Count)); break;
		case S_ASPECT: c.aspect = Cycle(c.aspect, dir, emu::kAspectCount); break;
		case S_BORDER: c.border = Cycle(c.border, dir, 3); break;
		case S_SMOOTH: c.smooth = !c.smooth; break;
		case S_SCANLINES: c.scanlines = !c.scanlines; break;
		case S_DEINTERLACE: c.deinterlace = !c.deinterlace; break;
		case S_MESHES: c.transparent_meshes = !c.transparent_meshes; break;
		case S_FPS: c.show_fps = !c.show_fps; break;
		case S_SCREENSHOTS: c.screenshots = !c.screenshots; break;
		case S_LAUNCHANIM: c.launch_anim = !c.launch_anim; break;
		case S_AUDIO: c.audio = !c.audio; break;
		case S_VOLUME: c.volume = std::max(0, std::min(100, c.volume + dir * 10)); break;
		case S_INTERP: c.audio_interp = Cycle(c.audio_interp, dir, emu::kAudioInterpCount); break;
		case S_SCSP: c.threaded_scsp = !c.threaded_scsp; break;
		case S_FF:
		{
			int i = 0;
			while (i < kFfCount - 1 && kFfSteps[i] != c.ff_speed)
				i++;
			c.ff_speed = kFfSteps[Cycle(i, dir, kFfCount)];
			break;
		}
		case S_REWIND: c.rewind = !c.rewind; break;
		case S_RESUME: c.resume = !c.resume; break;
		case S_REGION: c.region = Cycle(c.region, dir, emu::kRegionCount); break;
		case S_CART: c.cartridge = Cycle(c.cartridge, dir, emu::kCartridgeCount); break;
		case S_CARTSHARED: c.cart_shared = !c.cart_shared; break;
		case S_CDSPEED: c.cd_speed = Cycle(c.cd_speed, dir, emu::kCdSpeedCount); break;
		case S_SH2CLOCK: c.sh2_clock = Cycle(c.sh2_clock, dir, emu::kSh2ClockCount); break;
		case S_SH2CACHE: c.sh2_cache = !c.sh2_cache; break;
		case S_CDBLLE: c.cdblock_lle = !c.cdblock_lle; break;
		case S_COVERS: c.covers_download = !c.covers_download; break;
		case S_SORT: c.shelf_sort = Cycle(c.shelf_sort, dir, 4); break;
		case S_LANGUAGE: c.language = Cycle(c.language, dir, int(Language::Count)); break;
		case S_PERFLOG: c.perf_log = !c.perf_log; break;
		case S_DEBUGLOGS:
			c.debug_logs = !c.debug_logs;
			OrbisLogSetEnabled(c.debug_logs); // at once: the line saying so is the last (or first) one written
			break;
		case S_PADTYPE: c.pad_type = Cycle(c.pad_type, dir, emu::kPadTypeCount); break;
		case S_BTN_DEFAULT: c.DefaultButtons(); break;
		default:
			if (s >= S_BTN_FIRST && s <= S_BTN_LAST)
			{
				int& b = c.buttons[s - S_BTN_FIRST];
				b = Cycle(b, dir, emu::kPs5ButtonCount);
			}
			break;
	}
	emu::ApplySettings();
}

// The rows shown, and which setting each one is (`ids`; S_COUNT is Back): Shader parameters only with the GPU.
std::vector<Row> SettingRows(std::vector<int>& ids)
{
	std::vector<Row> rows;
	ids.clear();
	for (int s = 0; s < S_COUNT; s++)
	{
		if ((s == S_SHADERPARAMS || s == S_UPSCALER || s == S_FSRSHARP) && !ps5video::GpuActive())
			continue;
		rows.push_back(SettingRowFor(s));
		rows.back().help = SettingHelp(s);
		ids.push_back(s);
	}
	rows.push_back({"Back", ""});
	ids.push_back(S_COUNT);
	return rows;
}

void Background(bool over_game)
{
	if (over_game)
	{
		// the paused game, darkened, under the box
		ps5video::FillRect(0, 0, W, H, Rgb(0, 0, 0));
		emu::RedrawLastFrame();
		style::Dim(200);
	}
	else
		Header("");
}

void ShaderParamsScreen(bool over_game);

void SettingsScreen(bool over_game)
{
	NavReader nav;
	std::vector<int> ids;
	int sel = 1; // Renderer (row 0 is the VIDEO title)
	for (;;)
	{
		PollUpdate(false);
		const std::vector<Row> rows = SettingRows(ids);
		const int id = ids[size_t(std::min(sel, int(ids.size()) - 1))];
		Background(over_game);
		DrawOptionBox(GameHasOwnSettings() ? Fmt(T("Settings: %s"), emu::GameName().c_str()) : std::string("Settings"), rows, sel, over_game);
		if (id == S_UPDATES && g_upd.st.phase == update::Phase::Failed && !g_upd.st.error.empty())
			DrawText(80, H - 130, FitText(g_upd.st.error, 2, W - 160).c_str(), 2, kAccent);
		else if (id == S_UPDATES && !g_upd.helper)
			DrawText(80, H - 130, FitText(!g_helper_note.empty() ? g_helper_note
				: std::string(T("Updates are downloaded by the helper: press X to start it.")), 2, W - 160).c_str(), 2, kDim);
		if (!over_game)
			style::Hints({{{icon::DpadUpDown}, T("Choose")}, {{icon::DpadLeftRight}, T("Adjust")}, {{icon::L1, icon::R1}, T("Category")},
				{{icon::Cross}, T("Change")}, {{icon::Circle}, T("Back")}});
		Present();
		const Nav n = nav.Read();
		if (n.up)
			sel = Step(sel, -1, rows);
		if (n.down)
			sel = Step(sel, 1, rows);
		if (n.r1)
			sel = StepSection(sel, 1, rows);
		if (n.l1)
			sel = StepSection(sel, -1, rows);
		if (id == S_UPDATES)
		{
			if (n.ok)
				UpdateAction();
		}
		else if (id == S_BORDERS_ALL)
		{
			if (n.ok)
			{
				g_border_ask.queued = DownloadBorders();
				g_border_ask.at = Now();
				if (!g_upd.helper)
					StartAppBorderWorker(); // 3.11.8: no helper: the app downloads them itself
			}
		}
		else if (id == S_SHADERPARAMS)
		{
			if (n.ok && rows[size_t(sel)].enabled)
			{
				ShaderParamsScreen(over_game);
				nav = NavReader();
			}
		}
		else if (id < S_COUNT && (n.left || n.right || n.ok))
			ChangeSetting(id, n.left ? -1 : 1);
		if (n.back || n.options || n.menu || (n.ok && id == S_COUNT))
		{
			Config().Save();
			return;
		}
	}
}

// Settings -> Shader parameters (GPU): the picked shader's #pragma parameter values, changed live with Left/Right
// (Cross puts one back to its default; the last row puts them all back). Kept in shader-params.ini on leaving.
std::string ParamValue(const ps5gpu::ParamInfo& p)
{
	char buf[32];
	const bool whole = p.step >= 1.0f && std::floor(p.value) == p.value;
	snprintf(buf, sizeof(buf), whole ? "%.0f" : (p.step >= 0.1f ? "%.1f" : (p.step >= 0.01f ? "%.2f" : "%.3f")), double(p.value));
	return buf;
}

void ShaderParamsScreen(bool over_game)
{
	const int shader = emu::EffectiveShader();
	NavReader nav;
	int sel = 0;
	for (;;)
	{
		const int n = ps5gpu::ShaderParamCount(shader);
		std::vector<Row> rows;
		std::vector<ps5gpu::ParamInfo> info(static_cast<size_t>(std::max(0, n)));
		for (int i = 0; i < n; i++)
		{
			ps5gpu::ShaderParam(shader, i, &info[size_t(i)]);
			const ps5gpu::ParamInfo& p = info[size_t(i)];
			// a parameter that is only a title in the shader (min == max) is shown as one
			if (p.min == p.max)
				rows.push_back({p.desc && *p.desc ? p.desc : p.name, "", true, true});
			else
				rows.push_back({p.desc && *p.desc ? p.desc : p.name, ParamValue(p) + (p.value != p.def ? " *" : "")});
		}
		rows.push_back({"Put them all back to the shader's own", ""});
		rows.push_back({"Back", ""});
		if (rows[size_t(sel)].header)
			sel = Step(sel, 1, rows);
		Background(over_game);
		DrawOptionBox(Fmt(T("Shader parameters: %s"), ps5video::ShaderName(shader)), rows, sel, over_game);
		if (sel < n)
			DrawText(80, H - 130, FitText(Fmt(T("%s  (default %s, from %s to %s)"), info[size_t(sel)].name,
				ParamValue({nullptr, nullptr, 0, 0, 0, info[size_t(sel)].step, info[size_t(sel)].def}).c_str(),
				ParamValue({nullptr, nullptr, 0, 0, 0, info[size_t(sel)].step, info[size_t(sel)].min}).c_str(),
				ParamValue({nullptr, nullptr, 0, 0, 0, info[size_t(sel)].step, info[size_t(sel)].max}).c_str()), 2, W - 160).c_str(), 2, kDim);
		if (!over_game)
			style::Hints({{{icon::DpadLeftRight}, T("Adjust")}, {{icon::Cross}, T("Default")}, {{icon::Circle}, T("Back")}});
		Present();
		const Nav nv = nav.Read();
		if (nv.up)
			sel = Step(sel, -1, rows);
		if (nv.down)
			sel = Step(sel, 1, rows);
		if (sel < n)
		{
			const ps5gpu::ParamInfo& p = info[size_t(sel)];
			const float step = p.step > 0 ? p.step : (p.max - p.min) / 100.0f;
			if (nv.left || nv.right)
			{
				float v = p.value + (nv.left ? -step : step);
				v = std::round((v - p.min) / step) * step + p.min; // on the shader's own steps
				v = std::max(p.min, std::min(p.max, v));
				ps5gpu::SetShaderParam(shader, p.name, v);
			}
			else if (nv.ok)
				ps5gpu::SetShaderParam(shader, p.name, p.def);
		}
		else if (nv.ok && sel == n)
			ps5gpu::ResetShaderParams(shader);
		if (nv.back || nv.options || nv.menu || (nv.ok && sel == n + 1))
		{
			if (!ps5gpu::SaveShaderParams())
				emu::Osd("Could not save the shader parameters");
			return;
		}
	}
}

// Word wrap into lines that fit max_px at `scale`.
std::vector<std::string> Wrap(const std::string& text, int scale, int max_px)
{
	std::vector<std::string> lines;
	std::string cur;
	size_t pos = 0;
	while (pos <= text.size())
	{
		size_t sp = text.find(' ', pos);
		if (sp == std::string::npos)
			sp = text.size();
		const std::string word = text.substr(pos, sp - pos);
		const std::string tryline = cur.empty() ? word : cur + " " + word;
		if (!cur.empty() && TextWidth(tryline.c_str(), scale) > max_px)
		{
			lines.push_back(cur);
			cur = word;
		}
		else
			cur = tryline;
		pos = sp + 1;
	}
	if (!cur.empty())
		lines.push_back(cur);
	return lines;
}
} // namespace

void SettingsMenu()
{
	SettingsScreen(false);
}

// 3.9: the game's cheats: X turns the selected one on or off, Square all off. Kept in its .cht (on the next time too).
void CheatsScreen()
{
	NavReader nav;
	int sel = 0;
	for (;;)
	{
		const std::vector<cheats::Cheat> all = cheats::List();
		std::vector<int> index; // the rows' cheats (the master code is not shown)
		for (size_t i = 0; i < all.size(); i++)
			if (!all[i].hidden)
				index.push_back(int(i));
		std::vector<cheats::Cheat> list;
		for (int i : index)
			list.push_back(all[size_t(i)]);
		std::vector<Row> rows;
		int on = 0;
		for (const cheats::Cheat& c : list)
		{
			Row r;
			r.label = c.desc;
			r.raw = true;
			r.value = c.usable ? OnOff(c.on) : T(c.retroarch ? "(format not supported)" : "(needs a value)");
			r.enabled = c.usable;
			rows.push_back(r);
			on += c.on;
		}
		const bool none = rows.empty();
		if (none)
			rows.push_back({cheats::Status() == cheats::State::Failed ? "Could not download them" : "None for this game", "", false});
		sel = std::max(0, std::min(sel, int(rows.size()) - 1));
		Background(true);
		// 3.9.4: how to add your own, in three steps
		const std::string dir = OrbisDir("cheats") + "/Saturn";
		const std::vector<std::string> foot = {T("Add your own cheats:"),
			T("1. Download the game's .cht file (GameHacking.org: \"libretro (.cht)\")."),
			T("    You can add more cheats to that file by hand."),
			Fmt(T("2. Copy it over FTP to %s"), dir.c_str()), T("3. Start the game again: the cheats are added to the list.")};
		DrawOptionBox(Fmt(T("Cheats (%d on)"), on), rows, sel, true, none ? "" : T("Some cheats can make the game fail."),
			none ? "" : T("If the game freezes or looks wrong, turn that cheat off."), foot);
		if (none)
			style::Hints({{{icon::Circle}, T("Back")}});
		else
			style::Hints({{{icon::Cross}, T("On / off")}, {{icon::Square}, T("All off")}, {{icon::Circle}, T("Back")}});
		Present();
		const Nav n = nav.Read();
		if (n.up)
			sel = (sel + int(rows.size()) - 1) % int(rows.size());
		if (n.down)
			sel = (sel + 1) % int(rows.size());
		if (n.back || n.options || n.menu)
			return;
		if (!none && (n.ok || n.left || n.right) && list[size_t(sel)].usable)
			cheats::Set(index[size_t(sel)], !list[size_t(sel)].on);
		if (n.square)
			cheats::Set(-1, false);
	}
}


PauseAction PauseMenu()
{
	Settings& cfg = Config();
	NavReader nav;
	enum Item
	{
		I_RESUME,
		I_DISC,
		I_SAVE,
		I_LOAD,
		I_UNDO,
		I_SLOT,
		I_SETTINGS,
		I_OWN,
		I_CHEATS, // 3.9
		I_RESET,
		I_POWER,
		I_FORMAT_INT,
		I_FORMAT_CART,
		I_LIST,
		I_QUIT,
		I_COUNT
	};
	int sel = I_RESUME;
	int confirm = -1; // an initialize row pressed once: the second press does it
	std::string toast;
	double toast_until = 0;
	// 3.11: the picture of the slot's state, read again when the slot or the row changes, or after an action
	int thumb_key = -1;
	std::vector<uint32_t> thumb;
	int thumb_w = 0, thumb_h = 0;
	long long thumb_when = 0;
	bool thumb_used = false;

	for (;;)
	{
		char buf[64];
		std::vector<Row> rows(I_COUNT);
		rows[I_RESUME] = {"Resume", ""};
		{
			const int discs = emu::DiscCount();
			if (discs > 1)
			{
				snprintf(buf, sizeof(buf), T("%d of %d"), emu::DiscIndex() + 1, discs);
				rows[I_DISC] = {"Disc in the console", buf};
			}
			else
				rows[I_DISC] = {"Disc in the console", "1 of 1", false};
		}
		snprintf(buf, sizeof(buf), T("Save state (slot %d)"), cfg.state_slot);
		rows[I_SAVE] = {buf, ""};
		snprintf(buf, sizeof(buf), T("Load state (slot %d)"), cfg.state_slot);
		rows[I_LOAD] = {buf, "", emu::StateExists(cfg.state_slot)};
		snprintf(buf, sizeof(buf), T(emu::StateExists(cfg.state_slot) ? "%d (used)" : "%d (empty)"), cfg.state_slot);
		rows[I_SLOT] = {"State slot", buf};
		snprintf(buf, sizeof(buf), T("Load the previous state (slot %d)"), cfg.state_slot);
		rows[I_UNDO] = {buf, "", emu::PreviousStateExists(cfg.state_slot)};
		rows[I_SETTINGS] = {GameHasOwnSettings() ? "Settings (this game's)" : "Settings", ""};
		rows[I_OWN] = {"Settings for this game", GameHasOwnSettings() ? "Its own" : "The global ones"};
		{
			const cheats::State cs = cheats::Status();
			int on = 0, all = 0;
			for (const cheats::Cheat& c : cheats::List())
				if (!c.hidden)
				{
					all++;
					on += c.on;
				}
			if (cs == cheats::State::Ready && all > 0)
				rows[I_CHEATS] = {"Cheats", Fmt(T("%d of %d on"), on, all)};
			else if (cs == cheats::State::Looking)
				rows[I_CHEATS] = {"Cheats", "Looking for cheats...", false};
			else if (cs == cheats::State::Failed)
				rows[I_CHEATS] = {"Cheats", "Could not download them"};
			else
				rows[I_CHEATS] = {"Cheats", "None for this game"};
		}
		rows[I_RESET] = {"Reset", ""};
		rows[I_POWER] = {"Power cycle", ""};
		rows[I_FORMAT_INT] = {"Initialize the internal memory", confirm == I_FORMAT_INT ? "Press X again" : ""};
		rows[I_FORMAT_CART] = {"Initialize the backup cartridge",
			emu::HasBackupCart() ? (confirm == I_FORMAT_CART ? "Press X again" : "") : "(none in the slot)",
			emu::HasBackupCart()};
		rows[I_LIST] = {"Back to the game list", ""};
		rows[I_QUIT] = {"Quit SSYmirPS5", ""};

		Background(true);
		const std::string sys = emu::SystemName();
		DrawOptionBox(emu::GameName(), rows, sel, true);
		if (!sys.empty())
		{
			const int by = std::max(20, (H - (120 + std::min(int(I_COUNT), 14) * 58 + 30)) / 2 - 20);
			TextMid(W / 2 + 450 - 44, by + 58, sys.c_str(), 17, style::Lo(), Weight::Medium, Align::Right);
		}
		if (sel == I_SAVE || sel == I_LOAD || sel == I_UNDO || sel == I_SLOT)
		{
			// 3.11: right of the box, the game as it was when the slot's state was saved, and when
			const bool previous = sel == I_UNDO;
			const int key = cfg.state_slot * 2 + (previous ? 1 : 0);
			if (key != thumb_key)
			{
				thumb_key = key;
				thumb_when = 0;
				thumb_used = emu::StateThumb(cfg.state_slot, previous, thumb, thumb_w, thumb_h, &thumb_when);
			}
			constexpr int tx = 1450, ty = 260, tw = 420, th = 315;
			if (thumb_used && !thumb.empty())
			{
				style::RoundRect(tx - 3, ty - 3, tw + 6, th + 6, 18, style::White(), 40);
				style::Picture(thumb.data(), thumb_w, thumb_h, tx, ty, tw, th, 16);
			}
			else
			{
				style::RoundRect(tx, ty, tw, th, 16, style::Surface2(), 235);
				const std::string empty = FitTextPt(T(thumb_used ? "No picture (saved before 3.11)" : "Empty slot"), 21, tw - 40);
				TextMid(tx + tw / 2, ty + th / 2, empty.c_str(), 21, style::Lo(), Weight::Medium, Align::Center);
			}
			snprintf(buf, sizeof(buf), T(previous ? "Previous state, slot %d" : "Slot %d"), cfg.state_slot);
			TextMid(tx, ty + th + 36, FitTextPt(buf, 24, tw, Weight::SemiBold).c_str(), 24, style::Hi(), Weight::SemiBold);
			if (thumb_used)
				TextMid(tx, ty + th + 72, emu::StateWhen(thumb_when, false).c_str(), 19, style::Lo(), Weight::Medium);
		}
		if (!toast.empty() && Now() < toast_until)
		{
			const char* tt = T(toast.c_str());
			const int tw = TextWidthPt(tt, 21, Weight::Medium);
			style::RoundRect(float((W - tw) / 2 - 32), float(H - 120), float(tw + 64), 56, 28, style::Surface2());
			TextMid(W / 2, H - 92, tt, 21, style::Hi(), Weight::Medium, Align::Center);
		}
		Present();

		const Nav n = nav.Read();
		if (n.ok || n.left || n.right)
			thumb_key = -1; // a save, a load, another slot: the picture is read again
		if (n.up)
			sel = Step(sel, -1, rows);
		if (n.down)
			sel = Step(sel, 1, rows);
		if (n.back || n.options || n.menu)
			return PauseAction::Resume;

		if (confirm >= 0 && sel != confirm)
			confirm = -1;
		if ((sel == I_FORMAT_INT || sel == I_FORMAT_CART) && n.ok)
		{
			const int which = sel == I_FORMAT_INT ? 0 : 1;
			if (which == 1 && !emu::HasBackupCart())
			{
				toast = "No backup cartridge in the slot (Settings, Cartridge).";
				toast_until = Now() + 2.5;
			}
			else if (confirm != sel)
			{
				confirm = sel;
				toast = which == 0 ? "This erases every save of this game in the internal memory. Press X again to do it."
								   : "This erases every save on the backup cartridge. Press X again to do it.";
				toast_until = Now() + 3.0;
			}
			else
			{
				confirm = -1;
				if (emu::FormatBackup(which))
				{
					emu::Osd(which == 0 ? "Internal memory initialized: the game starts again"
										: "Backup cartridge initialized: the game starts again");
					return PauseAction::Resume;
				}
				toast = "Could not initialize it.";
				toast_until = Now() + 2.5;
			}
		}
		else if (sel == I_DISC && (n.left || n.right || n.ok))
		{
			const int discs = emu::DiscCount();
			if (discs > 1)
			{
				const int want = n.left ? (emu::DiscIndex() + discs - 1) % discs : (emu::DiscIndex() + 1) % discs;
				char t[64];
				snprintf(t, sizeof(t), T("Disc %d of %d inserted"), want + 1, discs);
				toast = emu::ChangeDisc(want) ? std::string(t) : std::string("Could not change the disc");
			}
			else
				toast = "This game has one disc (several discs: name them \"(Disc 1)\", \"(Disc 2)\"... or use an .m3u).";
			toast_until = Now() + 2.5;
		}
		else if (sel == I_SLOT && (n.left || n.right || n.ok))
		{
			cfg.state_slot = n.left ? (cfg.state_slot + 8) % 10 + 1 : cfg.state_slot % 10 + 1;
			cfg.Save();
		}
		else if (sel == I_OWN && (n.left || n.right || n.ok))
		{
			const bool own = !GameHasOwnSettings();
			if (SetGameOwnSettings(own))
			{
				emu::ApplySettings();
				toast = own ? "This game now keeps its own settings." : "This game uses the global settings again.";
			}
			else
				toast = "Could not save this game's settings.";
			toast_until = Now() + 2.5;
		}
		else if (n.ok)
		{
			switch (sel)
			{
				case I_RESUME: return PauseAction::Resume;
				case I_SAVE:
					toast = emu::SaveState(cfg.state_slot) ? "State saved." : "Could not save the state.";
					toast_until = Now() + 2.0;
					cfg.Save();
					break;
				case I_LOAD:
					if (emu::LoadState(cfg.state_slot))
						return PauseAction::Resume;
					toast = emu::StateExists(cfg.state_slot) ? "This state can't be loaded." : "This slot is empty.";
					toast_until = Now() + 2.0;
					break;
				case I_UNDO:
					if (emu::LoadPreviousState(cfg.state_slot))
						return PauseAction::Resume;
					toast = "This slot has no previous state.";
					toast_until = Now() + 2.0;
					break;
				case I_SETTINGS:
					SettingsScreen(true);
					nav = NavReader();
					break;
				case I_CHEATS:
					if (rows[I_CHEATS].enabled)
					{
						CheatsScreen();
						nav = NavReader();
					}
					break;
				case I_RESET:
					emu::Reset();
					return PauseAction::Resume;
				case I_POWER:
					emu::PowerCycle();
					return PauseAction::Resume;
				case I_LIST: return PauseAction::BackToList;
				case I_QUIT: return PauseAction::Quit;
				default: break;
			}
		}
	}
}

void MessageBox(const std::string& title, const std::string& text)
{
	NavReader nav;
	const int bw = 1400;
	const std::vector<std::string> lines = Wrap(T(text.c_str()), 3, bw - 80);
	const int bh = 220 + int(lines.size()) * 44;
	for (;;)
	{
		Header("");
		const int bx = (W - bw) / 2, by = (H - bh) / 2;
		style::RoundRect(float(bx), float(by), float(bw), float(bh), 26, style::Surface1());
		Text(bx + 44, by + 36, FitTextPt(T(title.c_str()), 34, bw - 88, Weight::SemiBold).c_str(), 34, style::Hi(), Weight::SemiBold);
		int y = by + 120;
		for (const std::string& l : lines)
		{
			Text(bx + 44, y, l.c_str(), 24, style::Mid());
			y += 44;
		}
		style::Hints({{{icon::Cross}, T("OK")}}, by + bh - 50);
		Present();
		const Nav n = nav.Read();
		if (n.ok || n.back || n.options)
			return;
	}
}
} // namespace fe
