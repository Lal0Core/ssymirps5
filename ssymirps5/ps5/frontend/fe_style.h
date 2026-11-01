// SSYmirPS5 frontend: the 2.7 interface's look -- its colours, and the shapes every screen draws with.
//
// Minimal and dark: a near-black background tinted by the selected game's art, one accent colour (matte champagne) only on what
// is selected or on, Inter for the words, rounded shapes with anti-aliased edges. Everything is placed in layout points
// (1920x1080) and drawn at the surface's resolution (4K with the GPU: ps5video::PixelScale()), so edges stay sharp.
//
// SPDX-License-Identifier: MIT
#pragma once

#include "fe_text.h"

#include <cstdint>
#include <string>
#include <vector>

namespace fe
{
namespace style
{
// colours as the surface writes them (ps5video::Rgb)
uint32_t Bg(); // the background: near-black
uint32_t Surface1(); // a panel
uint32_t Surface2(); // a selected row, a button
uint32_t Hi(); // titles, selected text
uint32_t Mid(); // values, labels
uint32_t Lo(); // hints, secondary
uint32_t Off(); // disabled (a letter without games)
uint32_t Accent(); // matte champagne
uint32_t AccentText(); // dark text on the filled accent
uint32_t Warn(); // 3.9.3: a caution (amber)
uint32_t WarnText(); // its text, lighter
uint32_t White();

// Shapes, in points, anti-aliased at the surface's resolution. alpha 0..255 (premultiplied onto the surface).
void RoundRect(float x, float y, float w, float h, float r, uint32_t color, int alpha = 255);
void Circle(float cx, float cy, float r, uint32_t color, int alpha = 255);
// 3.11: a picture (row-major, the surface's format) drawn into a rectangle of points, bilinear, its corners rounded
void Picture(const uint32_t* px, int w, int h, float x, float y, float dw, float dh, float r);
// a horizontal hairline, 1 point thick
void Line(float x0, float x1, float y, uint32_t color, int alpha = 40);
// The whole surface transparent (over the GPU's scene) or the background colour (without the GPU).
void ClearSurface();
// The surface darkened behind a dialog (premultiplied: what is under it shows through less).
void Dim(int alpha = 170);

// Controller hints along the bottom: each item's icons then its word, the items spread evenly around the middle.
struct Hint
{
	std::vector<const char*> icons;
	std::string label;
};
void Hints(const std::vector<Hint>& items, int y = 1020);
// Same icon/text sizes in a reserved horizontal area, e.g. below the 3D case.
void HintsInArea(const std::vector<Hint>& items,int left,int width,int y=1020);
// Controller symbols use shared geometric drawings and measured optical boxes; unrelated glyphs keep their fonts.
int ControllerHintWidth(const char* symbol, float size);
void DrawControllerHint(const char* symbol, float x, float middle_y, float size);


// Outlines and strokes (line icons): a rounded box's outline, a circle's, a segment with round ends.
void RoundRectOutline(float x, float y, float w, float h, float r, float thick, uint32_t color, int alpha = 255);
void Ring(float cx, float cy, float r, float thick, uint32_t color, int alpha = 255);
void Stroke(float x0, float y0, float x1, float y1, float thick, uint32_t color, int alpha = 255);

// 3.9.3: a warning sign (a triangle with "!"), size x size at (x, y)
void WarnSign(float x, float y, float size, uint32_t color);

// The settings' category icons, line drawings in a size x size square at (x, y).
enum class Icon
{
	Video,
	Audio,
	Emulation,
	Controls,
	Library,
	System,
};
void DrawIcon(Icon icon, float x, float y, float size, uint32_t color);

// An on/off switch, its left edge at x, its middle at y.
void Toggle(float x, float y, bool on);
// A slider from x0 to x1, value 0..1.
void Slider(float x0, float x1, float y, float value);
} // namespace style
} // namespace fe
