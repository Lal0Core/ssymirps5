// SSYmirPS5 frontend: the 2.7 interface's look (fe_style.h).
// SPDX-License-Identifier: MIT

#include "fe_style.h"

#include "ProsperoVideo.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace fe
{
namespace style
{
using ps5video::Rgb;

uint32_t Bg() { return Rgb(12, 13, 17); }
uint32_t Surface1() { return Rgb(24, 26, 33); }
uint32_t Surface2() { return Rgb(52, 49, 44); }
uint32_t Hi() { return Rgb(238, 240, 245); }
uint32_t Mid() { return Rgb(196, 200, 212); }
uint32_t Lo() { return Rgb(146, 151, 166); }
uint32_t Off() { return Rgb(78, 82, 94); }
uint32_t Accent() { return Rgb(203, 181, 139); }
uint32_t AccentText() { return Rgb(17, 21, 33); }
uint32_t White() { return Rgb(255, 255, 255); }
uint32_t Warn() { return Rgb(245, 178, 66); }
uint32_t WarnText() { return Rgb(250, 214, 150); }

namespace
{
// coverage a (0..255) of colour c over the surface pixel, premultiplied (the GPU lays the surface over its picture)
inline void Put(uint32_t& p, uint32_t c, uint32_t a)
{
	if (a == 0)
		return;
	const uint32_t w = a + (a >> 7), iw = 256 - w;
	const uint32_t rb = (((p & 0xff00ffu) * iw + (c & 0xff00ffu) * w) >> 8) & 0xff00ffu;
	const uint32_t g = (((p & 0x00ff00u) * iw + (c & 0x00ff00u) * w) >> 8) & 0x00ff00u;
	const uint32_t da = p >> 24;
	const uint32_t na = std::min<uint32_t>(255, a + ((da * iw) >> 8));
	p = (na << 24) | rb | g;
}

// a rounded box's signed distance (pixels), centre (cx, cy), half size (hw, hh), corner radius r
inline float BoxDist(float x, float y, float cx, float cy, float hw, float hh, float r)
{
	const float qx = std::abs(x - cx) - hw + r, qy = std::abs(y - cy) - hh + r;
	const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
	return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - r;
}
} // namespace

void RoundRect(float x, float y, float w, float h, float r, uint32_t color, int alpha)
{
	uint32_t* s = ps5video::Surface();
	if (!s || w <= 0 || h <= 0)
		return;
	const float k = float(ps5video::PixelScale());
	const int sw = ps5video::SurfaceWidth(), sh = ps5video::SurfaceHeight();
	const float px = x * k, py = y * k, pw = w * k, ph = h * k;
	const float pr = std::min(r * k, std::min(pw, ph) / 2);
	const float cx = px + pw / 2, cy = py + ph / 2;
	const int x0 = std::max(0, int(std::floor(px))), x1 = std::min(sw, int(std::ceil(px + pw)));
	const int y0 = std::max(0, int(std::floor(py))), y1 = std::min(sh, int(std::ceil(py + ph)));
	const uint32_t al = uint32_t(std::clamp(alpha, 0, 255));
	for (int yy = y0; yy < y1; yy++)
	{
		uint32_t* row = s + size_t(yy) * sw;
		const float fy = yy + 0.5f;
		// the rows away from the corners are a run of full pixels with soft ends
		const bool body = fy > py + pr + 0.5f && fy < py + ph - pr - 0.5f;
		for (int xx = x0; xx < x1; xx++)
		{
			const float fx = xx + 0.5f;
			float cov;
			if (body)
				cov = std::clamp(std::min(fx - px, px + pw - fx) + 0.5f, 0.0f, 1.0f);
			else
				cov = std::clamp(0.5f - BoxDist(fx, fy, cx, cy, pw / 2, ph / 2, pr), 0.0f, 1.0f);
			if (cov > 0)
				Put(row[xx], color, uint32_t(cov * al));
		}
	}
}

namespace
{
// any shape given by its signed distance in surface pixels (negative inside), over a box in points
template <typename D>
void Shape(float bx0, float by0, float bx1, float by1, uint32_t color, int alpha, D dist)
{
	uint32_t* s = ps5video::Surface();
	if (!s)
		return;
	const float k = float(ps5video::PixelScale());
	const int sw = ps5video::SurfaceWidth(), sh = ps5video::SurfaceHeight();
	const int x0 = std::max(0, int(std::floor(bx0 * k)) - 1), x1 = std::min(sw, int(std::ceil(bx1 * k)) + 1);
	const int y0 = std::max(0, int(std::floor(by0 * k)) - 1), y1 = std::min(sh, int(std::ceil(by1 * k)) + 1);
	const uint32_t al = uint32_t(std::clamp(alpha, 0, 255));
	for (int yy = y0; yy < y1; yy++)
	{
		uint32_t* row = s + size_t(yy) * sw;
		for (int xx = x0; xx < x1; xx++)
		{
			const float cov = std::clamp(0.5f - dist(xx + 0.5f, yy + 0.5f, k), 0.0f, 1.0f);
			if (cov > 0)
				Put(row[xx], color, uint32_t(cov * al));
		}
	}
}
} // namespace

void Picture(const uint32_t* src, int w, int h, float x, float y, float dw, float dh, float r)
{
	uint32_t* s = ps5video::Surface();
	if (!s || !src || w < 2 || h < 2 || dw <= 0 || dh <= 0)
		return;
	const float k = float(ps5video::PixelScale());
	const int sw = ps5video::SurfaceWidth(), sh = ps5video::SurfaceHeight();
	const float px = x * k, py = y * k, pw = dw * k, ph = dh * k;
	const float pr = std::min(r * k, std::min(pw, ph) / 2);
	const int x0 = std::max(0, int(std::floor(px))), x1 = std::min(sw, int(std::ceil(px + pw)));
	const int y0 = std::max(0, int(std::floor(py))), y1 = std::min(sh, int(std::ceil(py + ph)));
	const float sx = float(w) / pw, sy = float(h) / ph;
	auto lerp = [](uint32_t a, uint32_t b, uint32_t t) {
		uint32_t o = 0;
		for (int sh8 = 0; sh8 < 32; sh8 += 8)
			o |= ((((a >> sh8) & 0xff) * (256 - t) + ((b >> sh8) & 0xff) * t) >> 8) << sh8;
		return o;
	};
	for (int yy = y0; yy < y1; yy++)
	{
		uint32_t* row = s + size_t(yy) * sw;
		const float fy = std::clamp((yy + 0.5f - py) * sy - 0.5f, 0.0f, float(h - 1) - 0.001f);
		const int iy = int(fy);
		const uint32_t ty = uint32_t((fy - iy) * 256);
		const int iy1 = std::min(h - 1, iy + 1);
		for (int xx = x0; xx < x1; xx++)
		{
			const float cov = std::clamp(0.5f - BoxDist(xx + 0.5f, yy + 0.5f, px + pw / 2, py + ph / 2, pw / 2, ph / 2, pr), 0.0f, 1.0f);
			if (cov <= 0)
				continue;
			const float fx = std::clamp((xx + 0.5f - px) * sx - 0.5f, 0.0f, float(w - 1) - 0.001f);
			const int ix = int(fx), ix1 = std::min(w - 1, ix + 1);
			const uint32_t tx = uint32_t((fx - ix) * 256);
			const uint32_t* r0 = src + size_t(iy) * w;
			const uint32_t* r1 = src + size_t(iy1) * w;
			const uint32_t c = lerp(lerp(r0[ix], r0[ix1], tx), lerp(r1[ix], r1[ix1], tx), ty) | 0xff000000u;
			Put(row[xx], c, uint32_t(cov * 255));
		}
	}
}

void RoundRectOutline(float x, float y, float w, float h, float r, float thick, uint32_t color, int alpha)
{
	Shape(x - thick, y - thick, x + w + thick, y + h + thick, color, alpha, [=](float px, float py, float k) {
		const float d = BoxDist(px, py, (x + w / 2) * k, (y + h / 2) * k, w * k / 2, h * k / 2, r * k);
		return std::abs(d) - thick * k / 2;
	});
}

void Ring(float cx, float cy, float r, float thick, uint32_t color, int alpha)
{
	Shape(cx - r - thick, cy - r - thick, cx + r + thick, cy + r + thick, color, alpha, [=](float px, float py, float k) {
		const float d = std::sqrt((px - cx * k) * (px - cx * k) + (py - cy * k) * (py - cy * k)) - r * k;
		return std::abs(d) - thick * k / 2;
	});
}

void Stroke(float x0, float y0, float x1, float y1, float thick, uint32_t color, int alpha)
{
	Shape(std::min(x0, x1) - thick, std::min(y0, y1) - thick, std::max(x0, x1) + thick, std::max(y0, y1) + thick, color, alpha,
		[=](float px, float py, float k) {
			const float ax = x0 * k, ay = y0 * k, bx = x1 * k, by = y1 * k;
			const float vx = bx - ax, vy = by - ay;
			const float t = std::clamp(((px - ax) * vx + (py - ay) * vy) / std::max(1e-6f, vx * vx + vy * vy), 0.0f, 1.0f);
			const float dx = px - (ax + vx * t), dy = py - (ay + vy * t);
			return std::sqrt(dx * dx + dy * dy) - thick * k / 2;
		});
}

void DrawIcon(Icon icon, float x, float y, float s, uint32_t c)
{
	const float t = std::max(1.6f, s * 0.085f); // the line's thickness
	auto P = [&](float u, float v) { return std::pair<float, float>(x + u * s, y + v * s); };
	auto L = [&](float u0, float v0, float u1, float v1) {
		const auto a = P(u0, v0), b = P(u1, v1);
		Stroke(a.first, a.second, b.first, b.second, t, c);
	};
	switch (icon)
	{
		case Icon::Video: // a screen on its stand
			RoundRectOutline(x + s * 0.06f, y + s * 0.12f, s * 0.88f, s * 0.58f, s * 0.08f, t, c);
			L(0.5f, 0.72f, 0.5f, 0.86f);
			L(0.3f, 0.88f, 0.7f, 0.88f);
			break;
		case Icon::Audio: // a speaker and two waves
			L(0.10f, 0.38f, 0.28f, 0.38f);
			L(0.10f, 0.62f, 0.28f, 0.62f);
			L(0.10f, 0.38f, 0.10f, 0.62f);
			L(0.28f, 0.38f, 0.52f, 0.18f);
			L(0.28f, 0.62f, 0.52f, 0.82f);
			L(0.52f, 0.18f, 0.52f, 0.82f);
			L(0.68f, 0.38f, 0.72f, 0.5f);
			L(0.72f, 0.5f, 0.68f, 0.62f);
			L(0.80f, 0.26f, 0.88f, 0.5f);
			L(0.88f, 0.5f, 0.80f, 0.74f);
			break;
		case Icon::Emulation: // a chip with its pins
			RoundRectOutline(x + s * 0.24f, y + s * 0.24f, s * 0.52f, s * 0.52f, s * 0.07f, t, c);
			for (int i = 0; i < 3; i++)
			{
				const float u = 0.34f + i * 0.16f;
				L(u, 0.06f, u, 0.2f);
				L(u, 0.8f, u, 0.94f);
				L(0.06f, u, 0.2f, u);
				L(0.8f, u, 0.94f, u);
			}
			break;
		case Icon::Controls: // a pad: its body, a cross and two buttons
			RoundRectOutline(x + s * 0.04f, y + s * 0.24f, s * 0.92f, s * 0.52f, s * 0.24f, t, c);
			L(0.22f, 0.5f, 0.40f, 0.5f);
			L(0.31f, 0.41f, 0.31f, 0.59f);
			Circle(x + s * 0.66f, y + s * 0.44f, s * 0.05f, c);
			Circle(x + s * 0.78f, y + s * 0.56f, s * 0.05f, c);
			break;
		case Icon::Library: // a stack of discs' cases
			RoundRectOutline(x + s * 0.10f, y + s * 0.14f, s * 0.22f, s * 0.72f, s * 0.04f, t, c);
			RoundRectOutline(x + s * 0.40f, y + s * 0.14f, s * 0.22f, s * 0.72f, s * 0.04f, t, c);
			L(0.72f, 0.20f, 0.90f, 0.84f);
			L(0.72f, 0.20f, 0.84f, 0.16f);
			L(0.84f, 0.16f, 0.98f, 0.80f);
			break;
		case Icon::System: // a gear
		{
			const float cx = x + s / 2, cy = y + s / 2;
			for (int i = 0; i < 8; i++)
			{
				const float a = i * 3.14159265f / 4;
				Stroke(cx + std::cos(a) * s * 0.30f, cy + std::sin(a) * s * 0.30f, cx + std::cos(a) * s * 0.44f,
					cy + std::sin(a) * s * 0.44f, s * 0.13f, c);
			}
			Ring(cx, cy, s * 0.30f, t, c);
			Ring(cx, cy, s * 0.11f, t, c);
			break;
		}
	}
}

void Circle(float cx, float cy, float r, uint32_t color, int alpha)
{
	RoundRect(cx - r, cy - r, 2 * r, 2 * r, r, color, alpha);
}

void Line(float x0, float x1, float y, uint32_t color, int alpha)
{
	RoundRect(x0, y, x1 - x0, 1.0f, 0.0f, color, alpha);
}

void ClearSurface()
{
	uint32_t* s = ps5video::Surface();
	if (!s)
		return;
	const size_t n = size_t(ps5video::SurfaceWidth()) * ps5video::SurfaceHeight();
	if (ps5video::GpuActive())
		memset(s, 0, n * 4);
	else
		std::fill(s, s + n, Bg());
}

void Dim(int alpha)
{
	RoundRect(0, 0, float(ps5video::kWidth), float(ps5video::kHeight), 0, Rgb(6, 7, 10), alpha);
}

namespace
{
int NavigationSymbol(const char* symbol)
{
    if (!symbol) return 0;
    if (strcmp(symbol, icon::R3) == 0) return 1;
    if (strcmp(symbol, icon::DpadLeftRight) == 0) return 2;
    if (strcmp(symbol, icon::DpadUpDown) == 0) return 3;
    if (strcmp(symbol, icon::L3) == 0) return 4;
    if (strcmp(symbol, icon::Cross) == 0) return 5;
    if (strcmp(symbol, icon::Circle) == 0) return 6;
    if (strcmp(symbol, icon::Triangle) == 0) return 7;
    if (strcmp(symbol, icon::Square) == 0) return 8;
    if (strcmp(symbol, icon::L1) == 0) return 9;
    if (strcmp(symbol, icon::R1) == 0) return 10;
    if (strcmp(symbol, icon::L2) == 0) return 11;
    if (strcmp(symbol, icon::R2) == 0) return 12;
    if (strcmp(symbol, icon::Options) == 0) return 13;
    if (strcmp(symbol, icon::Touchpad) == 0) return 14;
    return 0;
}

// Convex five-point arm, rotated in quarter turns. All edges use the same pixel-scale antialiasing as the UI.
void PadArm(float cx, float cy, float size, int rotation, uint32_t fill)
{
    const float bx[5] = {-0.16f, 0.16f, 0.16f, 0, -0.16f};
    const float by[5] = {-0.5f, -0.5f, -0.22f, -0.07f, -0.22f};
    float vx[5], vy[5];
    for (int i = 0; i < 5; ++i)
    {
        float x = bx[i], y = by[i];
        for (int q = 0; q < rotation; ++q) { const float old = x; x = -y; y = old; }
        vx[i] = cx + x * size; vy[i] = cy + y * size;
    }
    Shape(cx-size/2, cy-size/2, cx+size/2, cy+size/2, fill, 255,
        [&](float px, float py, float k) {
            const float x=px/k, y=py/k;
            float distance=1e6f; bool inside=true;
            for (int i=0; i<5; ++i)
            {
                const int j=(i+1)%5;
                const float dx=vx[j]-vx[i], dy=vy[j]-vy[i];
                const float t=std::clamp(((x-vx[i])*dx+(y-vy[i])*dy)/(dx*dx+dy*dy),0.0f,1.0f);
                const float ex=x-vx[i]-t*dx, ey=y-vy[i]-t*dy;
                distance=std::min(distance,std::sqrt(ex*ex+ey*ey));
                if (dx*(y-vy[i])-dy*(x-vx[i]) < 0) inside=false;
            }
            return (inside?-distance:distance)*k;
        });
}

void PadArrow(float cx, float cy, float size, int rotation, uint32_t color)
{
    const float xs[3]={-0.055f,0,0.055f}, ys[3]={-0.30f,-0.36f,-0.30f};
    float x[3],y[3];
    for(int i=0;i<3;++i)
    {
        float a=xs[i],b=ys[i];
        for(int q=0;q<rotation;++q) {const float old=a;a=-b;b=old;}
        x[i]=cx+a*size;y[i]=cy+b*size;
    }
    Stroke(x[0],y[0],x[1],y[1],size*0.035f,color);
    Stroke(x[1],y[1],x[2],y[2],size*0.035f,color);
}
} // namespace

int ControllerHintWidth(const char* symbol, float size)
{
    const int kind=NavigationSymbol(symbol);
    if (!kind) return TextWidthPt(symbol,size);
    const float factor=kind==14?1.65f:kind==13?0.68f:(kind==9 || kind==10)?1.15f:1.0f;
    return int(std::ceil(size*1.2f*factor));
}

void DrawControllerHint(const char* symbol, float x, float middle_y, float size)
{
    if (size<=0) return;
    const int kind=NavigationSymbol(symbol);
    if (!kind) { TextMid(int(x),int(middle_y),symbol,size,Hi()); return; }
    const float s=size*1.2f, width=float(ControllerHintWidth(symbol,size)), cx=x+width/2, cy=middle_y;
    const uint32_t black=Rgb(7,9,13), edge=Rgb(104,119,137);
    if(kind==1 || kind==4)
    {
        Circle(cx,cy,s*0.47f,black);
        Ring(cx,cy,s*0.46f,s*0.022f,Rgb(132,145,164));
        Ring(cx,cy,s*0.40f,s*0.012f,edge);
        TextMid(int(std::lround(cx)),int(std::lround(cy)),kind==1?"R3":"L3",s*0.34f,Hi(),Weight::Medium,Align::Center);
        return;
    }
    if(kind>=5 && kind<=8)
    {
        Circle(cx,cy,s*0.47f,black);
        Ring(cx,cy,s*0.47f,s*0.015f,Rgb(54,61,73));
        if(kind==5)
        {
            const uint32_t c=Rgb(145,198,245);
            Stroke(cx-s*.19f,cy-s*.19f,cx+s*.19f,cy+s*.19f,s*.065f,c);
            Stroke(cx-s*.19f,cy+s*.19f,cx+s*.19f,cy-s*.19f,s*.065f,c);
        }
        else if(kind==6) Ring(cx,cy,s*.25f,s*.065f,Rgb(241,126,115));
        else if(kind==7)
        {
            // Optical centering: the wide base balances the narrow apex.
            const uint32_t c=Rgb(105,213,198);
            Stroke(cx,cy-s*.32f,cx+s*.27f,cy+s*.19f,s*.055f,c);
            Stroke(cx+s*.27f,cy+s*.19f,cx-s*.27f,cy+s*.19f,s*.055f,c);
            Stroke(cx-s*.27f,cy+s*.19f,cx,cy-s*.32f,s*.055f,c);
        }
        else RoundRectOutline(cx-s*.245f,cy-s*.245f,s*.49f,s*.49f,0,s*.06f,Rgb(231,151,201));
        return;
    }
    if(kind>=9 && kind<=12)
    {
        const bool trigger=kind>=11;
        const float h=s*(trigger?.94f:.80f), w=width-s*.04f, radius=s*(trigger?.22f:.16f);
        RoundRect(cx-w/2,cy-h/2,w,h,radius,black);
        RoundRectOutline(cx-w/2,cy-h/2,w,h,radius,s*.022f,edge);
        const char* label=kind==9?"L1":kind==10?"R1":kind==11?"L2":"R2";
        TextMid(int(std::lround(cx)),int(std::lround(cy)),label,s*.40f,Hi(),Weight::Medium,Align::Center);
        return;
    }
    if(kind==13)
    {
        const float w=s*.52f,h=s*.90f;
        RoundRect(cx-w/2,cy-h/2,w,h,w/2,black);
        RoundRectOutline(cx-w/2,cy-h/2,w,h,w/2,s*.022f,edge);
        for(int i=-1;i<=1;++i) Stroke(cx-s*.12f,cy+i*s*.11f,cx+s*.12f,cy+i*s*.11f,s*.035f,Hi());
        return;
    }
    if(kind==14)
    {
        const float h=s*.85f,w=width-s*.04f;
        RoundRect(cx-w/2,cy-h/2,w,h,s*.17f,black);
        RoundRectOutline(cx-w/2,cy-h/2,w,h,s*.17f,s*.022f,edge);
        for(int row=-2;row<=2;++row)
            for(int col=-4;col<=4;++col)
                Circle(cx+col*s*.14f,cy+row*s*.12f,s*.013f,Rgb(100,114,134));
        return;
    }
    for(int arm=0;arm<4;++arm)
    {
        const bool active=kind==2 ? arm%2==1 : arm%2==0;
        PadArm(cx,cy,s,arm,active?Rgb(220,229,240):Rgb(36,44,57));
        PadArrow(cx,cy,s,arm,active?Rgb(20,32,48):Rgb(113,128,149));
    }
}

void Hints(const std::vector<Hint>& items,int y)
{
    HintsInArea(items,0,ps5video::kWidth,y);
}

void HintsInArea(const std::vector<Hint>& items,int left,int width,int y)
{
	constexpr float kIcon = 30, kWord = 20;
	constexpr int kIconGap = 7, kLabelGap = 10, kGroupGap = 44;
	int total = 0;
	for (const Hint& h : items)
	{
		for (const char* ic : h.icons)
			total += ControllerHintWidth(ic, kIcon) + kIconGap;
		total += kLabelGap - kIconGap + TextWidthPt(h.label.c_str(), kWord, Weight::Medium) + kGroupGap;
	}
	total -= kGroupGap;
	// too wide (a long translation): the words a size smaller
	const float word = total > width - 80 ? 17.0f : kWord;
	if (word != kWord)
	{
		total = 0;
		for (const Hint& h : items)
		{
			for (const char* ic : h.icons)
				total += ControllerHintWidth(ic, kIcon) + kIconGap;
			total += kLabelGap - kIconGap + TextWidthPt(h.label.c_str(), word, Weight::Medium) + kGroupGap - 12;
		}
		total -= kGroupGap - 12;
	}
	int x = left + (width - total) / 2;
	for (const Hint& h : items)
	{
		for (size_t k = 0; k < h.icons.size(); k++)
		{
			DrawControllerHint(h.icons[k],float(x),float(y),kIcon);
			x += ControllerHintWidth(h.icons[k], kIcon) + (k + 1 < h.icons.size() ? kIconGap : kLabelGap);
		}
		const bool navigation=std::any_of(h.icons.begin(),h.icons.end(),[](const char* symbol) { return NavigationSymbol(symbol)!=0; });
		TextMid(x, y + 1, h.label.c_str(), word, navigation?Mid():Lo(), Weight::Medium);
		x += TextWidthPt(h.label.c_str(), word, Weight::Medium) + (word == kWord ? kGroupGap : kGroupGap - 12);
	}
}

void Toggle(float x, float y, bool on)
{
	RoundRect(x, y - 15, 55, 30, 15, on ? Accent() : Rgb(66, 70, 86)); // 3.9.7: off, a grey that shows on a selected row too
	const float knob = 23;
	const float kx = on ? x + 55 - knob - 3.5f : x + 3.5f;
	RoundRect(kx, y - knob / 2, knob, knob, knob / 2, White());
}

void Slider(float x0, float x1, float y, float value)
{
	value = std::clamp(value, 0.0f, 1.0f);
	RoundRect(x0, y - 3, x1 - x0, 6, 3, Surface2());
	RoundRect(x0, y - 3, (x1 - x0) * value, 6, 3, Accent());
	const float kx = x0 + (x1 - x0) * value;
	Circle(kx, y, 10, White());
}
// 3.9.3: a warning sign
void WarnSign(float x, float y, float size, uint32_t color)
{
	const float t = std::max(2.0f, size * 0.09f);
	const float l = x + t, r = x + size - t, top = y + t, bot = y + size * 0.92f;
	Stroke(x + size / 2, top, r, bot, t, color);
	Stroke(r, bot, l, bot, t, color);
	Stroke(l, bot, x + size / 2, top, t, color);
	Stroke(x + size / 2, y + size * 0.36f, x + size / 2, y + size * 0.62f, t, color);
	Circle(x + size / 2, y + size * 0.76f, t * 0.75f, color);
}
} // namespace style
} // namespace fe
