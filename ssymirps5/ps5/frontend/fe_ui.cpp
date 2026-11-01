// SSYmirPS5 frontend: what the menu screens have in common (fe_ui.h). From Snes9x PS5 (thatboialex's network
// shares screens); fe_menu.cpp keeps its own copies of the same helpers.
// SPDX-License-Identifier: MIT

#include "fe_ui.h"

#include "fe_style.h"

#include "fe_text.h"

#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include <algorithm>
#include <ctime>

#ifndef SSYMIRPS5_VERSION
#define SSYMIRPS5_VERSION "dev"
#endif
#ifndef YMIR_CORE_VERSION
#define YMIR_CORE_VERSION "dev"
#endif

namespace fe
{
namespace ui
{
double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---- input with key repeat ---------------------------------------------------------------------------
NavReader::NavReader()
{
	ps5input::Poll();
	m_prev = ps5input::Pad(0).buttons; // buttons still held from before don't count
}

Nav NavReader::Read()
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
	n.pgup = Repeat(cur, down, SCE_PAD_BUTTON_L1, now, 4);
	n.pgdn = Repeat(cur, down, SCE_PAD_BUTTON_R1, now, 5);
	n.ok = down & SCE_PAD_BUTTON_CROSS;
	n.back = down & SCE_PAD_BUTTON_CIRCLE;
	n.options = down & SCE_PAD_BUTTON_OPTIONS;
	n.triangle = down & SCE_PAD_BUTTON_TRIANGLE;
	n.square = down & SCE_PAD_BUTTON_SQUARE;
	n.l2 = down & SCE_PAD_BUTTON_L2;
	n.r2 = down & SCE_PAD_BUTTON_R2;
	const uint32_t combo = SCE_PAD_BUTTON_L3 | SCE_PAD_BUTTON_R3; // the pause menu's buttons, as fe_menu.cpp reads them
	n.menu = (cur & combo) == combo && (down & combo);
	return n;
}

bool NavReader::Repeat(uint32_t cur, uint32_t down, uint32_t bit, double now, int i)
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

void Present()
{
	ps5video::Present(0, 0, 0, 0, true);
}

void Header(const char* subtitle)
{
	// 2.7: the background, the screen's name as its title, the version small at the top right
	ps5video::FillRect(0, 0, W, H, kBg);
	const char* ver = "Ymir " YMIR_CORE_VERSION " - PS5 " SSYMIRPS5_VERSION;
	TextMid(W - 60, 46, ver, 15, style::Off(), Weight::Regular, Align::Right);
	if (subtitle && *subtitle)
		Text(80, 80, FitTextPt(subtitle, 44, W - 400, Weight::SemiBold).c_str(), 44, style::Hi(), Weight::SemiBold);
}

void Footer(const char* help)
{
	TextMid(W / 2, 1020, FitTextPt(help, 20, W - 120, Weight::Medium).c_str(), 20, style::Lo(), Weight::Medium, Align::Center);
}

void DrawOptionBox(const char* title, const std::vector<Row>& rows, int sel, bool over_game)
{
	const int row_h = 58;
	const int bw = 1000;
	const int visible = std::min(13, int(rows.size()));
	const int start = std::min(std::max(0, sel - visible + 1), std::max(0, int(rows.size()) - visible));
	const int bh = 120 + visible * row_h + 30;
	const int bx = (W - bw) / 2;
	const int by = std::max(150, (H - bh) / 2 + 30);
	style::RoundRect(float(bx), float(by), float(bw), float(bh), 26, style::Surface1(), over_game ? 238 : 255);
	Text(bx + 44, by + 36, FitTextPt(title, 32, bw - 88, Weight::SemiBold).c_str(), 32, style::Hi(), Weight::SemiBold);
	for (int i = start; i < start + visible; i++)
	{
		const int y = by + 110 + (i - start) * row_h;
		const int mid = y + (row_h - 8) / 2;
		const bool on = int(i) == sel;
		if (on)
			style::RoundRect(float(bx + 20), float(y), float(bw - 40), float(row_h - 8), 12, style::Surface2());
		const uint32_t col = !rows[i].enabled ? style::Off() : on ? style::Hi() : style::Mid();
		const std::string label = FitTextPt(rows[i].label, 22, bw - 360, Weight::Medium);
		TextMid(bx + 44, mid, label.c_str(), 22, col, Weight::Medium);
		if (!rows[i].value.empty())
		{
			// a long (translated) value is cut before it reaches the label
			const int room = bw - 88 - TextWidthPt(label.c_str(), 22, Weight::Medium) - 40;
			const bool arrows = on && !rows[i].plain;
			const std::string value = FitTextPt(rows[i].value, 21, room - (arrows ? 60 : 0), on ? Weight::SemiBold : Weight::Medium);
			const int x1 = bx + bw - 44;
			if (arrows)
			{
				TextMid(x1, mid, "\xE2\x80\xBA", 28, style::Accent(), Weight::Medium, Align::Right);
				TextMid(x1 - 28, mid, value.c_str(), 21, style::Hi(), Weight::SemiBold, Align::Right);
				TextMid(x1 - 28 - TextWidthPt(value.c_str(), 21, Weight::SemiBold) - 16, mid, "\xE2\x80\xB9", 28, style::Accent(),
					Weight::Medium, Align::Right);
			}
			else
				TextMid(x1, mid, value.c_str(), 21, on ? style::Hi() : style::Lo(), Weight::Medium, Align::Right);
		}
	}
}
} // namespace ui
} // namespace fe
