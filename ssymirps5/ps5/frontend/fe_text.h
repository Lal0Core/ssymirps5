// SSYmirPS5 frontend: text -- Inter (Regular, Medium, SemiBold; the 2.7 interface's typeface) for words and PromptFont
// for the controller's button symbols (frontend/assets/fonts) -- rasterized with stb_truetype (anti-aliased, glyphs
// cached per size) on the CPU.
//
// Positions and sizes are in layout points (1920x1080); on a 4K surface (ps5video::PixelScale() 2) the glyphs are made at
// twice the size, so text is sharp on a 4K TV. (x, y) is the top-left of the line.
// The old unit, 'scale': an em of 10*scale points (a line about that tall).
// A character Inter doesn't have is taken from PromptFont, drawn a bit larger, so button symbols can be written
// inline: "Jugar " + icon::Cross ...
//
// SPDX-License-Identifier: MIT
#pragma once

#include <cstdint>
#include <string>

namespace fe
{
constexpr int kGlyphH = 10; // a line is kGlyphH * scale points

enum class Weight
{
	Regular,
	Medium,
	SemiBold,
};
enum class Align
{
	Left,
	Center,
	Right,
};

// size: the em in points (a line of text is about that tall).
int TextWidthPt(const char* utf8, float size, Weight w = Weight::Regular);
void Text(int x, int y, const char* utf8, float size, uint32_t color, Weight w = Weight::Regular, Align a = Align::Left,
	bool shadow = false);
// The same with its vertical middle on y.
void TextMid(int x, int y, const char* utf8, float size, uint32_t color, Weight w = Weight::Regular, Align a = Align::Left);
std::string FitTextPt(const std::string& utf8, float size, int max_pt, Weight w = Weight::Regular);

int TextWidth(const char* utf8, int scale);
// outline: a soft dark shadow under the text, for text over pictures.
void DrawText(int x, int y, const char* utf8, int scale, uint32_t color, bool outline = true);
// The same into any 32-bit pixel buffer (cover placeholders, from the cover thread), in its own pixels.
struct Canvas
{
	uint32_t* px;
	int w, h, pitch;
};
void DrawTextOn(const Canvas& canvas, int x, int y, const char* utf8, int scale, uint32_t color, bool outline = true);
void TextOn(const Canvas& canvas, int x, int y, const char* utf8, float size_px, uint32_t color, Weight w = Weight::Regular,
	Align a = Align::Left);
// Cuts the string so it fits max_px (adds "...").
std::string FitText(const std::string& utf8, int scale, int max_px);

// PromptFont's controller symbols (the same code points as PS5SX2's fe_text.h).
namespace icon
{
constexpr const char* Cross = "\xE2\x87\xA3";
constexpr const char* Circle = "\xE2\x87\xA2";
constexpr const char* Triangle = "\xE2\x87\xA1";
constexpr const char* Square = "\xE2\x87\xA0";
constexpr const char* L1 = "\xE2\x86\xB0";
constexpr const char* R1 = "\xE2\x86\xB1";
constexpr const char* L2 = "\xE2\x86\xB2";
constexpr const char* R2 = "\xE2\x86\xB3";
constexpr const char* L3 = "\xE2\x86\xBA";
constexpr const char* R3 = "\xE2\x86\xBB";
constexpr const char* DpadLeftRight = "\xE2\x86\xA2";
constexpr const char* DpadUpDown = "\xE2\x86\xA3";
constexpr const char* Options = "\xE2\x88\x88";
constexpr const char* Touchpad = "\xEE\x84\x80"; // private UI token, drawn geometrically
constexpr const char* GitHub = "\xEF\x82\x9B"; // U+F09B, Font Awesome Brands
} // namespace icon
} // namespace fe

namespace fe {
// Monotonic ping-pong scroll, with reading pauses. Drawing is clipped to its own rectangle.
double MarqueeOffset(double text_width, double view_width, double elapsed);
void TextMarquee(int x,int y,int width,int height,const std::string& text,float size,uint32_t color,
    double elapsed,Weight weight=Weight::Regular);
}
