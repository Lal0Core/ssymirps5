// SSYmirPS5 frontend: text with Inter and PromptFont (fe_text.h).
// SPDX-License-Identifier: MIT

#include "fe_text.h"
#include "fe_style.h"

#include "ProsperoVideo.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "third_party/stb_truetype.h"

#define YMIR_INCBIN(sym, path)                                                                                  \
	__asm__(".section .rodata\n"                                                                               \
			".balign 16\n"                                                                                     \
			".global " #sym "_begin\n" #sym "_begin:\n"                                                        \
			".incbin \"" path "\"\n"                                                                           \
			".global " #sym "_end\n" #sym "_end:\n"                                                            \
			".previous\n");                                                                                    \
	extern "C" const unsigned char sym##_begin[];                                                              \
	extern "C" const unsigned char sym##_end[];

YMIR_INCBIN(ymir_font_inter, FE_FONT_DIR "/Inter-Regular.otf")
YMIR_INCBIN(ymir_font_inter_m, FE_FONT_DIR "/Inter-Medium.otf")
YMIR_INCBIN(ymir_font_inter_sb, FE_FONT_DIR "/Inter-SemiBold.otf")
YMIR_INCBIN(ymir_font_prompt, FE_FONT_DIR "/promptfont.otf")
// Font Awesome Brands (as PS5SX2 ships it, for its author line): the GitHub mark on the shelf
YMIR_INCBIN(ymir_font_brands, FE_FONT_DIR "/fa-brands-400.otf")

namespace fe
{
namespace
{
constexpr float kIconScale = 1.45f; // button symbols next to words, as PS5SX2 draws them bigger

struct Font
{
	stbtt_fontinfo info;
	int ascent = 0, descent = 0, gap = 0;
};

struct Glyph
{
	int w = 0, h = 0, xoff = 0, yoff = 0; // bitmap placement relative to the pen on the baseline
	float advance = 0;
	std::vector<uint8_t> alpha;
	int font = 0;
};

// the font slots: 0 Inter Regular, 1 PromptFont, 2 Font Awesome Brands, 3 Inter Medium, 4 Inter SemiBold
constexpr int kFonts = 5;
int WordFont(Weight w)
{
	return w == Weight::Medium ? 3 : w == Weight::SemiBold ? 4 : 0;
}

struct Fonts
{
	Font f[kFonts];
	bool ok = false;
	std::mutex lock;
	std::unordered_map<uint64_t, std::unique_ptr<Glyph>> cache;

	Fonts()
	{
		const unsigned char* data[kFonts] = {ymir_font_inter_begin, ymir_font_prompt_begin, ymir_font_brands_begin,
			ymir_font_inter_m_begin, ymir_font_inter_sb_begin};
		ok = true;
		for (int i = 0; i < kFonts; i++)
		{
			if (!stbtt_InitFont(&f[i].info, data[i], stbtt_GetFontOffsetForIndex(data[i], 0)))
				ok = false;
			else
				stbtt_GetFontVMetrics(&f[i].info, &f[i].ascent, &f[i].descent, &f[i].gap);
		}
	}

	// The glyph for code point cp at an em of px pixels in the words' font `word` (cached; the pointer stays valid).
	const Glyph* Get(uint32_t cp, float px, int word)
	{
		const uint32_t q = uint32_t(std::lround(px * 4)); // quarter pixels
		const uint64_t key = (uint64_t(q) << 35) | (uint64_t(word) << 32) | cp;
		{
			std::lock_guard<std::mutex> l(lock);
			auto it = cache.find(key);
			if (it != cache.end())
				return it->second.get();
		}
		auto g = std::make_unique<Glyph>();
		int font = word;
		// the controller symbols (arrows and maths code points, which Inter has its own glyphs for) come from PromptFont
		const bool symbol = cp >= 0x2190 && cp <= 0x23FF;
		int index = symbol ? 0 : stbtt_FindGlyphIndex(&f[word].info, int(cp));
		if (symbol && stbtt_FindGlyphIndex(&f[1].info, int(cp)) == 0)
			index = stbtt_FindGlyphIndex(&f[word].info, int(cp));
		if (index == 0)
		{
			const int alt = stbtt_FindGlyphIndex(&f[1].info, int(cp));
			if (alt != 0)
			{
				font = 1;
				index = alt;
			}
			else if (cp >= 0xE000 && cp <= 0xF8FF) // private use: the brand marks
			{
				const int brand = stbtt_FindGlyphIndex(&f[2].info, int(cp));
				if (brand != 0)
				{
					font = 2;
					index = brand;
				}
			}
		}
		g->font = font;
		const float em = font == 1 ? px * kIconScale : px;
		const float s = stbtt_ScaleForMappingEmToPixels(&f[font].info, em);
		int adv = 0, lsb = 0;
		stbtt_GetGlyphHMetrics(&f[font].info, index, &adv, &lsb);
		g->advance = adv * s;
		int x0, y0, x1, y1;
		stbtt_GetGlyphBitmapBox(&f[font].info, index, s, s, &x0, &y0, &x1, &y1);
		g->w = std::max(0, x1 - x0);
		g->h = std::max(0, y1 - y0);
		g->xoff = x0;
		g->yoff = y0;
		if (font == 1)
			g->yoff += int(px * 0.12f); // centre the bigger symbol on the words' x-height
		if (g->w > 0 && g->h > 0)
		{
			g->alpha.resize(size_t(g->w) * g->h);
			stbtt_MakeGlyphBitmap(&f[font].info, g->alpha.data(), g->w, g->h, g->w, s, s, index);
		}
		std::lock_guard<std::mutex> l(lock);
		auto& slot = cache[key];
		if (!slot)
			slot = std::move(g);
		return slot.get();
	}

	float Kern(uint32_t a, uint32_t b, float px, int word)
	{
		const int ia = stbtt_FindGlyphIndex(&f[word].info, int(a)), ib = stbtt_FindGlyphIndex(&f[word].info, int(b));
		if (!ia || !ib)
			return 0;
		return stbtt_GetGlyphKernAdvance(&f[word].info, ia, ib) * stbtt_ScaleForMappingEmToPixels(&f[word].info, px);
	}
};

Fonts& F()
{
	static Fonts* fonts = new Fonts(); // never destroyed: the cover thread may draw at exit
	return *fonts;
}

uint32_t NextCodepoint(const char*& s)
{
	const uint8_t* p = reinterpret_cast<const uint8_t*>(s);
	uint32_t cp = *p++;
	int extra = 0;
	if (cp >= 0xF0)
	{
		cp &= 0x07;
		extra = 3;
	}
	else if (cp >= 0xE0)
	{
		cp &= 0x0F;
		extra = 2;
	}
	else if (cp >= 0xC0)
	{
		cp &= 0x1F;
		extra = 1;
	}
	for (int i = 0; i < extra && (*p & 0xC0) == 0x80; i++)
		cp = (cp << 6) | (*p++ & 0x3F);
	s = reinterpret_cast<const char*>(p);
	return cp;
}

inline uint32_t Blend(uint32_t dst, uint32_t color, uint32_t a) // a 0..255
{
	// weights out of 256, so red+blue fit one 32-bit multiply without carrying into each other
	const uint32_t w = a + (a >> 7);
	const uint32_t iw = 256 - w;
	const uint32_t rb = (((dst & 0xff00ffu) * iw + (color & 0xff00ffu) * w) >> 8) & 0xff00ffu;
	const uint32_t g = (((dst & 0x00ff00u) * iw + (color & 0x00ff00u) * w) >> 8) & 0x00ff00u;
	// the surface over the GPU's picture is premultiplied: text makes its pixels opaque in proportion
	const uint32_t da = dst >> 24;
	const uint32_t na = std::min<uint32_t>(255, a + ((da * iw) >> 8));
	return (na << 24) | rb | g;
}

struct Target
{
	uint32_t* px;
	int w, h, pitch;
};

// the width of a line in pixels, at an em of px
float WidthPx(const char* utf8, float px, int word)
{
	Fonts& fonts = F();
	if (!fonts.ok || !utf8)
		return 0;
	float pen = 0;
	uint32_t prev = 0;
	const char* s = utf8;
	while (*s)
	{
		const uint32_t cp = NextCodepoint(s);
		const Glyph* g = fonts.Get(cp, px, word);
		if (prev && g->font == word)
			pen += fonts.Kern(prev, cp, px, word);
		pen += g->advance;
		prev = g->font == word ? cp : 0;
	}
	return pen;
}

// (x, y): the line's top-left in pixels; px: the em in pixels
void Draw(const Target& t, float x, int y, const char* utf8, float px, uint32_t color, bool shadow, int word)
{
	Fonts& fonts = F();
	if (!fonts.ok || !utf8)
		return;
	const int baseline = y + int(std::lround(px * 0.80f));
	const int sh = std::max(1, int(px / 20)); // shadow offset
	for (int pass = shadow ? 0 : 1; pass < 2; pass++)
	{
		const uint32_t col = pass == 0 ? 0xff000000u : color;
		const uint32_t strength = pass == 0 ? 150 : 255;
		const int ox = pass == 0 ? sh : 0, oy = pass == 0 ? sh : 0;
		float pen = x;
		uint32_t prev = 0;
		const char* s = utf8;
		while (*s)
		{
			const uint32_t cp = NextCodepoint(s);
			const Glyph* g = fonts.Get(cp, px, word);
			if (prev && g->font == word)
				pen += fonts.Kern(prev, cp, px, word);
			const int gx = int(std::lround(pen)) + g->xoff + ox;
			const int gy = baseline + g->yoff + oy;
			for (int j = 0; j < g->h; j++)
			{
				const int yy = gy + j;
				if (yy < 0 || yy >= t.h)
					continue;
				uint32_t* row = t.px + size_t(yy) * t.pitch;
				const uint8_t* a = g->alpha.data() + size_t(j) * g->w;
				for (int i = 0; i < g->w; i++)
				{
					const int xx = gx + i;
					if (xx < 0 || xx >= t.w || a[i] == 0)
						continue;
					row[xx] = Blend(row[xx], col, (a[i] * strength) / 255);
				}
			}
			pen += g->advance;
			prev = g->font == word ? cp : 0;
		}
	}
}

Target SurfaceTarget()
{
	return Target{ps5video::Surface(), ps5video::SurfaceWidth(), ps5video::SurfaceHeight(), ps5video::SurfaceWidth()};
}
} // namespace

namespace
{
struct ControlSpan { const char* at; const char* symbol; };
ControlSpan NextController(const char* text)
{
    if (!text) return {nullptr,nullptr};
    const char* const symbols[]={icon::Cross,icon::Circle,icon::Triangle,icon::Square,icon::L1,icon::R1,
        icon::L2,icon::R2,icon::L3,icon::R3,icon::DpadLeftRight,icon::DpadUpDown,icon::Options,icon::Touchpad};
    for(const char* p=text;*p;++p)
        if(static_cast<unsigned char>(*p)>=0xE2)
            for(const char* symbol:symbols)
                if(strncmp(p,symbol,3)==0) return {p,symbol};
    return {nullptr,nullptr};
}
float SurfaceTextWidth(const char* text,float size,Weight weight)
{
    const float k=float(ps5video::PixelScale());
    float width=0;
    while(text && *text)
    {
        const ControlSpan next=NextController(text);
        if(!next.at) { width+=WidthPx(text,size*k,WordFont(weight))/k;break; }
        const std::string run(text,size_t(next.at-text));
        width+=WidthPx(run.c_str(),size*k,WordFont(weight))/k;
        width+=style::ControllerHintWidth(next.symbol,size);
        text=next.at+3;
    }
    return width;
}
} // namespace

int TextWidthPt(const char* utf8, float size, Weight w)
{
	return int(std::ceil(SurfaceTextWidth(utf8,size,w)));
}

void Text(int x, int y, const char* utf8, float size, uint32_t color, Weight w, Align a, bool shadow)
{
	if (!ps5video::Surface() || !utf8)
		return;
	const int k = ps5video::PixelScale();
	const float px = size * k;
    if(NextController(utf8).at)
    {
        float pen=float(x);
        if(a!=Align::Left)
        {
            const float width=SurfaceTextWidth(utf8,size,w);
            pen-=a==Align::Center?width/2:width;
        }
        const char* part=utf8;
        while(*part)
        {
            const ControlSpan next=NextController(part);
            const std::string run=next.at?std::string(part,size_t(next.at-part)):std::string(part);
            Draw(SurfaceTarget(),pen*k,y*k,run.c_str(),px,color,shadow,WordFont(w));
            pen+=WidthPx(run.c_str(),px,WordFont(w))/k;
            if(!next.at) break;
            style::DrawControllerHint(next.symbol,pen,y+size*.46f,size);
            pen+=style::ControllerHintWidth(next.symbol,size);
            part=next.at+3;
        }
        return;
    }
	float left = float(x * k);
	if (a != Align::Left)
	{
		const float wd = WidthPx(utf8, px, WordFont(w));
		left -= a == Align::Center ? wd / 2 : wd;
	}
	Draw(SurfaceTarget(), left, y * k, utf8, px, color, shadow, WordFont(w));
}

void TextMid(int x, int y, const char* utf8, float size, uint32_t color, Weight w, Align a)
{
	// the line's ink is about 0.72 em, from 0.08 em below the top: its middle 0.44 em down
	Text(x, int(std::lround(y - size * 0.46f)), utf8, size, color, w, a);
}

std::string FitTextPt(const std::string& utf8, float size, int max_pt, Weight w)
{
	if (TextWidthPt(utf8.c_str(), size, w) <= max_pt)
		return utf8;
	std::string s = utf8;
	while (!s.empty())
	{
		size_t n = s.size() - 1;
		while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80)
			n--;
		s.resize(n);
		while (!s.empty() && s.back() == ' ')
			s.pop_back();
		const std::string t = s + "\xE2\x80\xA6";
		if (TextWidthPt(t.c_str(), size, w) <= max_pt)
			return t;
	}
	return "\xE2\x80\xA6";
}

void TextOn(const Canvas& canvas, int x, int y, const char* utf8, float size_px, uint32_t color, Weight w, Align a)
{
	float left = float(x);
	if (a != Align::Left)
	{
		const float wd = WidthPx(utf8, size_px, WordFont(w));
		left -= a == Align::Center ? wd / 2 : wd;
	}
	Draw(Target{canvas.px, canvas.w, canvas.h, canvas.pitch}, left, y, utf8, size_px, color, false, WordFont(w));
}

int TextWidth(const char* utf8, int scale)
{
	return TextWidthPt(utf8, float(scale * kGlyphH));
}

void DrawText(int x, int y, const char* utf8, int scale, uint32_t color, bool outline)
{
	Text(x, y, utf8, float(scale * kGlyphH), color, Weight::Regular, Align::Left, outline);
}

void DrawTextOn(const Canvas& canvas, int x, int y, const char* utf8, int scale, uint32_t color, bool outline)
{
	Draw(Target{canvas.px, canvas.w, canvas.h, canvas.pitch}, float(x), y, utf8, float(scale * kGlyphH), color, outline, 0);
}

std::string FitText(const std::string& utf8, int scale, int max_px)
{
	if (TextWidth(utf8.c_str(), scale) <= max_px)
		return utf8;
	std::string s = utf8;
	while (!s.empty())
	{
		size_t n = s.size() - 1;
		while (n > 0 && (uint8_t(s[n]) & 0xC0) == 0x80)
			n--;
		s.resize(n);
		const std::string t = s + "\xE2\x80\xA6"; // the ellipsis character, as PS5SX2
		if (TextWidth(t.c_str(), scale) <= max_px)
			return t;
	}
	return "\xE2\x80\xA6";
}
} // namespace fe

namespace fe {
double MarqueeOffset(double text_width,double view_width,double elapsed)
{
    const double distance=std::max(0.0,text_width-view_width);
    if(distance<=0 || elapsed<=2) return 0;
    const double travel=distance/34.0, pause=2, cycle=2*travel+2*pause;
    const double phase=std::fmod(std::max(0.0,elapsed),cycle);
    if(phase<pause) return 0;
    if(phase<pause+travel) return (phase-pause)*34;
    if(phase<2*pause+travel) return distance;
    if(phase<2*pause+2*travel) return distance-(phase-2*pause-travel)*34;
    return 0;
}
void TextMarquee(int x,int y,int width,int height,const std::string& text,float size,uint32_t color,
    double elapsed,Weight weight)
{
    if(width<=0 || height<=0 || !ps5video::Surface()) return;
    const int k=ps5video::PixelScale(), sw=ps5video::SurfaceWidth(), sh=ps5video::SurfaceHeight();
    const int left=std::max(0,x*k), top=std::max(0,y*k);
    const int right=std::min(sw,(x+width)*k), bottom=std::min(sh,(y+height)*k);
    if(left>=right || top>=bottom) return;
    const int w=right-left,h=bottom-top;
    // Copy the real background, then render into a bounded canvas. Nothing can leak into labels or actions.
    thread_local std::vector<uint32_t> scratch;
    scratch.resize(size_t(w)*h);
    uint32_t* surface=ps5video::Surface();
    for(int row=0;row<h;++row) std::copy_n(surface+size_t(top+row)*sw+left,w,scratch.data()+size_t(row)*w);
    const int offset=int(std::lround(MarqueeOffset(TextWidthPt(text.c_str(),size,weight),width,elapsed)*k));
    TextOn({scratch.data(),w,h,w},x*k-left-offset,y*k-top,text.c_str(),size*k,color,weight);
    for(int row=0;row<h;++row) std::copy_n(scratch.data()+size_t(row)*w,w,surface+size_t(top+row)*sw+left);
}
}
