// SSYmirPS5 frontend: the 3D game shelf (fe_shelf.h).
// SPDX-License-Identifier: MIT

#include "fe_shelf.h"
#include "fe_screenshot.h"

#include "fe_borderfetch.h"
#include "fe_caseview.h"
#include "fe_launchbox.h"
#include "fe_covers.h"
#include "fe_emu.h"
#include "fe_games.h"
#include "fe_i18n.h"
#include "fe_menu.h"
#include "fe_osk.h"
#include "fe_prefetch.h"
#include "fe_playtime.h"
#include "fe_recent.h"
#include "fe_settings.h"
#include "fe_style.h"
#include "fe_tabs.h"
#include "fe_text.h"

#include "OrbisPaths.h"
#include "ProsperoInput.h"
#include "ProsperoNotify.h"
#include "ProsperoSce.h"
#include "ProsperoCrash.h"
#include "ProsperoGpu.h"
#include "ProsperoThread.h"
#include "ProsperoVideo.h"


#include <immintrin.h>

#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <ctime>
#include <functional>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#ifndef SSYMIRPS5_VERSION
#define SSYMIRPS5_VERSION "dev"
#endif

namespace fe
{
namespace
{
using ps5video::Rgb;
constexpr int W = ps5video::kWidth;
constexpr int H = ps5video::kHeight;

// ---- camera and layout (world units: the middle cover is kCoverH tall at distance kCamDist) ----------
constexpr float kFocal = 1000.0f;
constexpr float kCamDist = 1000.0f;
constexpr float kCoverH = 540.0f;
constexpr float kCamY = kCoverH * 0.42f; // eye height above the floor
constexpr float kHorizon = 455.0f; // screen row of the eye
constexpr float kSideX = 600.0f; // first side cover
constexpr float kSideStep = 165.0f; // the next ones
constexpr float kSideZ = 330.0f; // how far back the side covers stand
constexpr float kSideAngle = 1.10f; // radians (63 degrees)
constexpr float kVisible = 7.5f; // covers each side
constexpr float kReflect = 0.42f; // reflection length, of the cover's height
constexpr int kKeepRadius = 14; // textures kept around the selection

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

inline uint32_t Lerp32(uint32_t a, uint32_t b, uint32_t w) // w 0..256
{
	const uint32_t iw = 256 - w;
	const uint32_t rb = (((a & 0xff00ffu) * iw + (b & 0xff00ffu) * w) >> 8) & 0xff00ffu;
	const uint32_t g = (((a & 0x00ff00u) * iw + (b & 0x00ff00u) * w) >> 8) & 0x00ff00u;
	return 0xff000000u | rb | g;
}

inline uint32_t Scale32(uint32_t a, uint32_t s) // s 0..256
{
	const uint32_t rb = (((a & 0xff00ffu) * s) >> 8) & 0xff00ffu;
	const uint32_t g = (((a & 0x00ff00u) * s) >> 8) & 0x00ff00u;
	return 0xff000000u | rb | g;
}

inline uint32_t Add32(uint32_t a, uint32_t b) // per channel, saturating
{
	return uint32_t(_mm_cvtsi128_si32(_mm_adds_epu8(_mm_cvtsi32_si128(int(a)), _mm_cvtsi32_si128(int(b))))) | 0xff000000u;
}

// ---- a small pool: the screen is split in vertical bands, one per thread --------------------------------
class Pool
{
public:
	Pool()
	{
		unsigned n = std::thread::hardware_concurrency();
		n = std::clamp(n == 0 ? 4u : n, 2u, 6u);
		for (unsigned i = 1; i < n; i++)
			m_threads.emplace_back([this, i] { Worker(int(i)); }, 2 * 1024 * 1024);
		m_count = int(n);
	}
	~Pool()
	{
		{
			std::lock_guard<std::mutex> lock(m_lock);
			m_quit = true;
			m_gen++;
		}
		m_cv.notify_all();
		for (auto& t : m_threads)
			t.join();
	}
	// job(x0, x1) on every band, the calling thread included; returns when all are done.
	void Run(const std::function<void(int, int)>& job)
	{
		{
			std::lock_guard<std::mutex> lock(m_lock);
			m_job = &job;
			m_pending = m_count - 1;
			m_gen++;
		}
		m_cv.notify_all();
		Band(0, job);
		std::unique_lock<std::mutex> lock(m_lock);
		m_done.wait(lock, [this] { return m_pending == 0; });
		m_job = nullptr;
	}

private:
	void Band(int i, const std::function<void(int, int)>& job)
	{
		const int step = ((W / m_count) + 31) & ~31;
		const int x0 = i * step, x1 = std::min(W, x0 + step);
		if (x0 < x1)
			job(x0, x1);
	}
	void Worker(int i)
	{
		uint64_t seen = 0;
		for (;;)
		{
			const std::function<void(int, int)>* job;
			{
				std::unique_lock<std::mutex> lock(m_lock);
				m_cv.wait(lock, [&] { return m_gen != seen; });
				seen = m_gen;
				if (m_quit)
					return;
				job = m_job;
			}
			if (job)
				Band(i, *job);
			{
				std::lock_guard<std::mutex> lock(m_lock);
				if (--m_pending == 0)
					m_done.notify_one();
			}
		}
	}
	std::vector<ps5::BigThread> m_threads;
	int m_count = 1;
	std::mutex m_lock;
	std::condition_variable m_cv, m_done;
	uint64_t m_gen = 0;
	int m_pending = 0;
	bool m_quit = false;
	const std::function<void(int, int)>* m_job = nullptr;
};

// ---- the backdrop: the selected cover, blurred, darkened, with a floor and a vignette -----------------
struct Backdrop
{
	std::vector<uint32_t> px; // W*H
	const CoverTex* from = nullptr;
	uint64_t id = 0; // a new one each build: the GPU's copy is kept by it
};

void BuildBackdrop(Pool& pool, Backdrop& bd, const CoverTex* tex)
{
	bd.px.resize(size_t(W) * H);
	bd.from = tex;
	bd.id = NewCoverId();
	// 1. a 64x36 thumbnail: the cover's smallest level, scaled to fill (cropped), or a plain colour
	constexpr int TW = 64, TH = 36;
	float small[TH][TW][3];
	for (int y = 0; y < TH; y++)
		for (int x = 0; x < TW; x++)
		{
			uint32_t p = Rgb(28, 22, 60);
			if (tex)
			{
				const int l = CoverTex::kLevels - 1;
				const int tw = tex->w[l], th = tex->h[l];
				const float s = std::max(float(TW) / tw, float(TH) / th);
				const int sx = std::clamp(int((x - TW / 2.0f) / s + tw / 2.0f), 0, tw - 1);
				const int sy = std::clamp(int((y - TH / 2.0f) / s + th / 2.0f), 0, th - 1);
				p = tex->px[l][size_t(sx) * th + sy];
			}
			small[y][x][0] = float(p & 0xff);
			small[y][x][1] = float((p >> 8) & 0xff);
			small[y][x][2] = float((p >> 16) & 0xff);
		}
	// 2. blur: three box passes each way
	for (int pass = 0; pass < 3; pass++)
	{
		float tmp[TH][TW][3];
		for (int y = 0; y < TH; y++)
			for (int x = 0; x < TW; x++)
				for (int c = 0; c < 3; c++)
				{
					float s = 0;
					int n = 0;
					for (int k = -3; k <= 3; k++)
					{
						const int xx = std::clamp(x + k, 0, TW - 1);
						s += small[y][xx][c];
						n++;
					}
					tmp[y][x][c] = s / n;
				}
		for (int y = 0; y < TH; y++)
			for (int x = 0; x < TW; x++)
				for (int c = 0; c < 3; c++)
				{
					float s = 0;
					int n = 0;
					for (int k = -3; k <= 3; k++)
					{
						const int yy = std::clamp(y + k, 0, TH - 1);
						s += tmp[yy][x][c];
						n++;
					}
					small[y][x][c] = s / n;
				}
	}
	// 3. darken towards the background's near-black (2.7), so text stays readable whatever the art
	const float base[3] = {12, 13, 17};
	for (int y = 0; y < TH; y++)
		for (int x = 0; x < TW; x++)
			for (int c = 0; c < 3; c++)
				small[y][x][c] = small[y][x][c] * 0.24f + base[c] * 0.76f;
	// 4. scale up (bilinear) with a vignette and a darker floor below the covers
	const float floor_y = kHorizon + kFocal * kCamY / kCamDist;
	pool.Run([&](int x0, int x1) {
		for (int y = 0; y < H; y++)
		{
			const float fy = (y + 0.5f) * TH / H - 0.5f;
			const int y0 = std::clamp(int(std::floor(fy)), 0, TH - 1), y1 = std::min(y0 + 1, TH - 1);
			const float wy = std::clamp(fy - y0, 0.0f, 1.0f);
			const float vy = (y - H * 0.45f) / (H * 0.75f);
			// 2.7: no floor; the lower part only darker, for the title and the hints
			float floor_k = 1.0f - 0.35f * std::clamp((y - H * 0.60f) / (H * 0.40f), 0.0f, 1.0f);
			(void)floor_y;
			uint32_t* row = &bd.px[size_t(y) * W];
			for (int x = x0; x < x1; x++)
			{
				const float fx = (x + 0.5f) * TW / W - 0.5f;
				const int xa = std::clamp(int(std::floor(fx)), 0, TW - 1), xb = std::min(xa + 1, TW - 1);
				const float wx = std::clamp(fx - xa, 0.0f, 1.0f);
				const float vx = (x - W * 0.5f) / (W * 0.62f);
				const float vig = std::max(0.25f, 1.0f - 0.55f * (vx * vx + vy * vy));
				int ch[3];
				for (int c = 0; c < 3; c++)
				{
					const float top = small[y0][xa][c] * (1 - wx) + small[y0][xb][c] * wx;
					const float bot = small[y1][xa][c] * (1 - wx) + small[y1][xb][c] * wx;
					ch[c] = int((top * (1 - wy) + bot * wy) * vig * floor_k);
				}
				row[x] = Rgb(uint8_t(std::min(255, ch[0])), uint8_t(std::min(255, ch[1])), uint8_t(std::min(255, ch[2])));
			}
		}
	});
}

// ---- one cover in 3D ------------------------------------------------------------------------------------
struct Placed
{
	const CoverTex* tex = nullptr;
	float d = 0; // offset from the selection (fractional while gliding)
	float cx = 0, dist = kCamDist; // centre: x, distance from the eye
	float a = 0, b = 0; // x(t) = cx + a*t, dist(t) = dist + b*t, t in [-0.5, 0.5]
	float wh = kCoverH; // world height
	uint32_t bright = 256; // 0..256
	uint32_t alpha = 256; // fade at the ends of the shelf
	int sx0 = 0, sx1 = 0; // screen columns
};

bool Place(Placed& p, const CoverTex* tex, float d)
{
	p.tex = tex;
	p.d = d;
	const float ad = std::abs(d), sgn = d < 0 ? -1.0f : 1.0f;
	const float k = std::min(ad, 1.0f);
	const float e = k * k * (3 - 2 * k); // smoothstep
	const float x = ad <= 1 ? sgn * kSideX * e : sgn * (kSideX + (ad - 1) * kSideStep);
	const float z = kSideZ * e;
	const float ang = kSideAngle * e;
	const float aspect = float(tex->w[0]) / float(tex->h[0]);
	const float ww = kCoverH * aspect;
	p.cx = x;
	p.dist = kCamDist + z;
	p.a = ww * std::cos(ang);
	// the end towards the middle stands further back: t = +0.5 for a left cover, -0.5 for a right one
	p.b = (d < 0 ? 1.0f : -1.0f) * ww * std::sin(ang);
	p.wh = kCoverH;
	float br = 1.0f - 0.40f * e - 0.07f * std::max(0.0f, ad - 1);
	p.bright = uint32_t(std::clamp(br, 0.25f, 1.0f) * 256);
	p.alpha = uint32_t(std::clamp((kVisible - ad) / 1.0f, 0.0f, 1.0f) * 256);
	// screen extent
	float xs[2];
	for (int i = 0; i < 2; i++)
	{
		const float t = i == 0 ? -0.5f : 0.5f;
		const float dd = p.dist + p.b * t;
		if (dd < 50)
			return false;
		xs[i] = W * 0.5f + kFocal * (p.cx + p.a * t) / dd;
	}
	p.sx0 = int(std::floor(std::min(xs[0], xs[1])));
	p.sx1 = int(std::ceil(std::max(xs[0], xs[1])));
	return p.sx1 > 0 && p.sx0 < W && p.alpha > 0;
}

// Draws columns [x0, x1) of one placed cover: the cover itself and its reflection on the floor.
[[maybe_unused]] void DrawCover(const Placed& p, uint32_t* surf, int x0, int x1)
{
	const CoverTex& t = *p.tex;
	const int cx0 = std::max(x0, p.sx0), cx1 = std::min(x1, p.sx1);
	for (int x = cx0; x < cx1; x++)
	{
		const float X = (x + 0.5f - W * 0.5f) / kFocal;
		const float den = p.a - X * p.b;
		if (std::abs(den) < 1e-4f)
			continue;
		const float tt = (X * p.dist - p.cx) / den; // -0.5 .. 0.5
		if (tt < -0.5f || tt > 0.5f)
			continue;
		const float dd = p.dist + p.b * tt;
		const float top = kHorizon - kFocal * (p.wh - kCamY) / dd;
		const float bot = kHorizon + kFocal * kCamY / dd;
		const float span = bot - top;
		// mip level from the vertical scale
		int l = 0;
		const float scale = span / float(t.h[0]);
		if (scale < 0.38f)
			l = 2;
		else if (scale < 0.75f)
			l = 1;
		const int tw = t.w[l], th = t.h[l];
		const uint32_t* px = t.px[l].data();
		const float u = (tt + 0.5f) * tw - 0.5f;
		const int u0 = std::clamp(int(std::floor(u)), 0, tw - 1), u1 = std::min(u0 + 1, tw - 1);
		const uint32_t fu = uint32_t(std::clamp(u - std::floor(u), 0.0f, 1.0f) * 256);
		const uint32_t* c0 = px + size_t(u0) * th;
		const uint32_t* c1 = px + size_t(u1) * th;
		const float dv = th / span;
		// edge columns: soften the vertical edges a little
		uint32_t edge = 256;
		if (x == p.sx0 || x == p.sx1 - 1)
			edge = 140;
		const uint32_t bright = p.bright;
		const uint32_t alpha = (p.alpha * edge) >> 8;

		// fixed point: v in 16.16
		const int32_t vmax = (th - 1) << 16;
		auto sample = [&](int32_t v) -> uint32_t {
			v = std::clamp(v, 0, vmax - 1);
			const int y0 = v >> 16;
			const uint32_t fv = uint32_t(v >> 8) & 0xff;
			const uint32_t a0 = Lerp32(c0[y0], c0[y0 + 1], fv);
			const uint32_t a1 = Lerp32(c1[y0], c1[y0 + 1], fv);
			return Lerp32(a0, a1, fu);
		};
		const int32_t dv16 = int32_t(dv * 65536.0f);

		// the cover: full rows in the middle, partial coverage on the first and last row
		const int ytop = std::max(0, int(std::floor(top)));
		const int ybot = std::min(H - 1, int(std::ceil(bot)) - 1);
		int32_t v = int32_t(((ytop + 0.5f - top) * dv - 0.5f) * 65536.0f);
		uint32_t* d = surf + size_t(ytop) * W + x;
		for (int y = ytop; y <= ybot; y++, v += dv16, d += W)
		{
			uint32_t a = alpha;
			if (y == ytop || y == ybot)
			{
				float cov = 1.0f;
				if (y < top)
					cov -= top - y;
				if (y + 1 > bot)
					cov -= (y + 1) - bot;
				if (cov <= 0)
					continue;
				a = (alpha * uint32_t(cov * 256)) >> 8;
			}
			const uint32_t c = Scale32(sample(v), bright);
			*d = a >= 256 ? c : Lerp32(*d, c, a);
		}
		// the reflection, fading out, mirrored from the cover's bottom edge up
		const float rlen = span * kReflect;
		const int r0 = std::max(0, int(std::ceil(bot)));
		const int r1 = std::min(H - 1, int(bot + rlen));
		const float inv_rlen = 1.0f / rlen;
		int32_t rv = int32_t((float(th - 1) - (r0 + 0.5f - bot) * dv) * 65536.0f);
		uint32_t* rd = surf + size_t(r0) * W + x;
		for (int y = r0; y <= r1; y++, rv -= dv16, rd += W)
		{
			const float k = (y + 0.5f - bot) * inv_rlen;
			if (k >= 1)
				break;
			const uint32_t a = uint32_t(0.30f * (1 - k) * (1 - k) * alpha);
			if (a == 0)
				continue;
			*rd = Lerp32(*rd, Scale32(sample(std::max(0, rv)), bright), a);
		}
	}
}

// The glow behind the middle cover, in the cover's average colour.
[[maybe_unused]] void DrawGlow(const Placed& p, uint32_t* surf, int x0, int x1, float strength)
{
	if (strength <= 0)
		return;
	const float top = kHorizon - kFocal * (p.wh - kCamY) / p.dist;
	const float bot = kHorizon + kFocal * kCamY / p.dist;
	const float l = float(p.sx0), r = float(p.sx1);
	const float R = 70.0f;
	const uint32_t col = p.tex->average;
	const int gx0 = std::max(x0, int(l - R)), gx1 = std::min(x1, int(r + R));
	const int gy0 = std::max(0, int(top - R)), gy1 = std::min(H, int(bot + R * 0.6f));
	for (int y = gy0; y < gy1; y++)
	{
		const float dy = std::max({top - y, 0.0f, y - bot});
		const bool inside_rows = dy == 0;
		uint32_t* row = surf + size_t(y) * W;
		for (int x = gx0; x < gx1; x++)
		{
			if (inside_rows && x >= int(l) && x < int(r))
			{
				x = int(r) - 1; // the cover covers this part
				continue;
			}
			const float dx = std::max({l - x, 0.0f, x - r});
			const float dist2 = dx * dx + dy * dy;
			if (dist2 >= R * R)
				continue;
			const float k = 1 - std::sqrt(dist2) / R;
			const uint32_t a = uint32_t(k * k * strength * 0.85f * 256);
			row[x] = Add32(row[x], Scale32(col, a));
		}
	}
}

// ---- the same shelf drawn by the GPU (ps5gpu::SetScene): quads in perspective at the output's resolution --------
void FullQuad(ps5gpu::SceneQuad& q, uint64_t tex, float alpha)
{
	q.tex = tex;
	const float xs[4] = {0, float(W), 0, float(W)}, ys[4] = {0, 0, float(H), float(H)};
	for (int i = 0; i < 4; i++)
	{
		q.x[i] = xs[i];
		q.y[i] = ys[i];
		q.w[i] = 1;
	}
	q.alpha = alpha;
}

// a placed cover as quads (glow, cover, reflection), its texture sent to the GPU the first time
[[maybe_unused]] void CoverQuads(const Placed& p, std::vector<ps5gpu::SceneQuad>& out)
{
	const CoverTex& t = *p.tex;
	if (!ps5gpu::SceneTexture(t.id, t.px[0].data(), t.w[0], t.h[0], true))
		return;
	float X[2], top[2], bot[2], dd[2];
	for (int i = 0; i < 2; i++)
	{
		const float tt = i == 0 ? -0.5f : 0.5f;
		dd[i] = p.dist + p.b * tt;
		X[i] = W * 0.5f + kFocal * (p.cx + p.a * tt) / dd[i];
		top[i] = kHorizon - kFocal * (p.wh - kCamY) / dd[i];
		bot[i] = kHorizon + kFocal * kCamY / dd[i];
	}
	const float bright = p.bright / 256.0f, alpha = p.alpha / 256.0f;
	if (std::abs(p.d) < 0.5f)
	{
		ps5gpu::SceneQuad gq;
		gq.mode = ps5gpu::kQuadGlow;
		const float R = 70.0f;
		const float l = float(p.sx0), r = float(p.sx1);
		const float gt = kHorizon - kFocal * (p.wh - kCamY) / p.dist, gb = kHorizon + kFocal * kCamY / p.dist;
		const float xs[4] = {l - R, r + R, l - R, r + R}, ys[4] = {gt - R, gt - R, gb + R * 0.6f, gb + R * 0.6f};
		for (int i = 0; i < 4; i++)
		{
			gq.x[i] = xs[i];
			gq.y[i] = ys[i];
		}
		gq.rect[0] = l;
		gq.rect[1] = gt;
		gq.rect[2] = r;
		gq.rect[3] = gb;
		gq.glow_r = R;
		gq.alpha = 1.0f - std::abs(p.d) * 2.0f;
		gq.color[0] = (t.average & 0xff) / 255.0f;
		gq.color[1] = ((t.average >> 8) & 0xff) / 255.0f;
		gq.color[2] = ((t.average >> 16) & 0xff) / 255.0f;
		out.push_back(gq);
	}
	ps5gpu::SceneQuad c;
	c.tex = t.id;
	c.bright = bright;
	c.alpha = alpha;
	const float cx[4] = {X[0], X[1], X[0], X[1]}, cy[4] = {top[0], top[1], bot[0], bot[1]};
	const float cw[4] = {dd[0], dd[1], dd[0], dd[1]};
	for (int i = 0; i < 4; i++)
	{
		c.x[i] = cx[i];
		c.y[i] = cy[i];
		c.w[i] = cw[i];
	}
	// the reflection: the cover's bottom part, upside down, below it
	ps5gpu::SceneQuad r = c;
	r.mode = ps5gpu::kQuadReflection;
	r.v0 = 1.0f;
	r.v1 = 1.0f - kReflect;
	const float ry[4] = {bot[0], bot[1], bot[0] + (bot[0] - top[0]) * kReflect, bot[1] + (bot[1] - top[1]) * kReflect};
	for (int i = 0; i < 4; i++)
		r.y[i] = ry[i];
	out.push_back(r);
	out.push_back(c);
}

// ---- 2.7: the covers in a flat row (the GPU shelf): the selected one large, the others smaller and quieter --------
constexpr float kBigH = 530, kSmallH = 380, kRowY = 490, kGapBig = 40, kGapSmall = 24;

float AspectOf(const CoverTex* t)
{
	return t && t->h[0] > 0 ? float(t->w[0]) / float(t->h[0]) : 0.70f;
}

// the centres of covers lo..hi when cover c is the selected one (each cover's own width, so square Japanese art and
// tall boxes keep the same gaps)
void RowLayout(const std::vector<const CoverTex*>& tex, int lo, int c, std::vector<float>& cx)
{
	const int n = int(tex.size());
	cx.assign(size_t(n), 0.0f);
	auto width = [&](int i) { return (lo + i == c ? kBigH : kSmallH) * AspectOf(tex[size_t(i)]); };
	const int ci = c - lo;
	if (ci >= 0 && ci < n)
		cx[size_t(ci)] = W * 0.5f;
	// a centre off the list (the end of the library): as if the selected cover were there
	float right = W * 0.5f + (ci >= 0 && ci < n ? width(ci) / 2 : kBigH * 0.35f);
	for (int i = std::max(0, ci + 1); i < n; i++)
	{
		const float gap = i == ci + 1 ? kGapBig : kGapSmall;
		cx[size_t(i)] = right + gap + width(i) / 2;
		right = cx[size_t(i)] + width(i) / 2;
	}
	float left = W * 0.5f - (ci >= 0 && ci < n ? width(ci) / 2 : kBigH * 0.35f);
	for (int i = std::min(n - 1, ci - 1); i >= 0; i--)
	{
		const float gap = i == ci - 1 ? kGapBig : kGapSmall;
		cx[size_t(i)] = left - gap - width(i) / 2;
		left = cx[size_t(i)] - width(i) / 2;
	}
}

struct RowItem
{
	const CoverTex* t;
	float x0, y0, x1, y1, bright, sel;
};

// the row for a selection at `pos` (fractional while gliding), far covers first and the selected one last
std::vector<RowItem> RowItems(const std::vector<const CoverTex*>& tex, int lo, float pos)
{
	const int c0 = int(std::floor(pos)), c1 = c0 + 1;
	const float f = pos - c0;
	std::vector<float> a, b;
	RowLayout(tex, lo, c0, a);
	RowLayout(tex, lo, c1, b);
	std::vector<RowItem> items;
	for (size_t i = 0; i < tex.size(); i++)
	{
		const float d = float(lo + int(i)) - pos;
		const float sel = std::max(0.0f, 1.0f - std::abs(d));
		const float h = kSmallH + (kBigH - kSmallH) * sel;
		const float w = h * AspectOf(tex[i]);
		const float cx = a[i] + (b[i] - a[i]) * f;
		const float cy = kRowY + 18 * (1 - sel);
		if (cx + w / 2 < -40 || cx - w / 2 > W + 40)
			continue;
		const float bright = std::clamp(1.0f - 0.42f * std::min(1.0f, std::abs(d)) - 0.05f * std::max(0.0f, std::abs(d) - 1), 0.3f, 1.0f);
		items.push_back({tex[i], cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2, bright, sel});
	}
	std::sort(items.begin(), items.end(), [](const RowItem& p, const RowItem& q) { return p.sel < q.sel; });
	return items;
}

// 2.9.1: square corners, as the boxes are (2.7-2.9 rounded them)
float RowRadius(const RowItem&)
{
	return 0;
}

// the row's quads: shadows, the selected one's glow, the covers
void RowQuads(const std::vector<const CoverTex*>& tex, int lo, float pos, std::vector<ps5gpu::SceneQuad>& out)
{
	for (const RowItem& it : RowItems(tex, lo, pos))
	{
		const CoverTex& t = *it.t;
		if (!ps5gpu::SceneTexture(t.id, t.px[0].data(), t.w[0], t.h[0], true))
			continue;
		const float r = RowRadius(it);
		// its shadow
		ps5gpu::SceneQuad sq;
		sq.mode = ps5gpu::kQuadShadow;
		const float R = 26 + 14 * it.sel, dy = 12 + 6 * it.sel;
		const float sx[4] = {it.x0 - R, it.x1 + R, it.x0 - R, it.x1 + R}, sy[4] = {it.y0 - R + dy, it.y0 - R + dy, it.y1 + R + dy, it.y1 + R + dy};
		for (int i = 0; i < 4; i++)
		{
			sq.x[i] = sx[i];
			sq.y[i] = sy[i];
		}
		sq.rect[0] = it.x0;
		sq.rect[1] = it.y0 + dy;
		sq.rect[2] = it.x1;
		sq.rect[3] = it.y1 + dy;
		sq.glow_r = R;
		sq.radius = r;
		sq.alpha = 0.55f;
		out.push_back(sq);
		// the selected one's halo, in its own colour
		if (it.sel > 0.02f)
		{
			ps5gpu::SceneQuad gq;
			gq.mode = ps5gpu::kQuadGlow;
			const float G = 110;
			const float gx[4] = {it.x0 - G, it.x1 + G, it.x0 - G, it.x1 + G}, gy[4] = {it.y0 - G, it.y0 - G, it.y1 + G, it.y1 + G};
			for (int i = 0; i < 4; i++)
			{
				gq.x[i] = gx[i];
				gq.y[i] = gy[i];
			}
			gq.rect[0] = it.x0;
			gq.rect[1] = it.y0;
			gq.rect[2] = it.x1;
			gq.rect[3] = it.y1;
			gq.glow_r = G;
			gq.alpha = 0.40f * it.sel;
			gq.color[0] = (t.average & 0xff) / 255.0f;
			gq.color[1] = ((t.average >> 8) & 0xff) / 255.0f;
			gq.color[2] = ((t.average >> 16) & 0xff) / 255.0f;
			out.push_back(gq);
		}
		ps5gpu::SceneQuad q;
		q.tex = t.id;
		q.bright = it.bright;
		q.radius = r;
		const float qx[4] = {it.x0, it.x1, it.x0, it.x1}, qy[4] = {it.y0, it.y0, it.y1, it.y1};
		for (int i = 0; i < 4; i++)
		{
			q.x[i] = qx[i];
			q.y[i] = qy[i];
			q.w[i] = 1;
		}
		out.push_back(q);
	}
}

// The same row drawn by the CPU into a W x H buffer (no GPU scene): the same shadows, halo, rounded covers.
void RowCpu(uint32_t* surf, const std::vector<const CoverTex*>& tex, int lo, float pos, int x0, int x1)
{
	auto box = [](float x, float y, float l, float t, float r, float b, float rad) {
		const float cx = (l + r) / 2, cy = (t + b) / 2, hw = (r - l) / 2, hh = (b - t) / 2;
		const float qx = std::abs(x - cx) - hw + rad, qy = std::abs(y - cy) - hh + rad;
		const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
		return std::sqrt(ox * ox + oy * oy) + std::min(std::max(qx, qy), 0.0f) - rad;
	};
	for (const RowItem& it : RowItems(tex, lo, pos))
	{
		const CoverTex& t = *it.t;
		const float r = RowRadius(it);
		const float R = 26 + 14 * it.sel, dy = 12 + 6 * it.sel;
		// shadow and halo
		const float G = it.sel > 0.02f ? 110.0f : 0.0f;
		const float ext = std::max(R, G);
		const int ya = std::max(0, int(it.y0 - ext)), yb = std::min(H, int(it.y1 + ext + dy + 1));
		const int xa = std::max(x0, int(it.x0 - ext)), xb = std::min(x1, int(it.x1 + ext + 1));
		const float gr = (t.average & 0xff), gg = ((t.average >> 8) & 0xff), gb = ((t.average >> 16) & 0xff);
		for (int y = ya; y < yb; y++)
		{
			uint32_t* row = surf + size_t(y) * W;
			for (int x = xa; x < xb; x++)
			{
				uint32_t p = row[x];
				float pr = float(p & 0xff), pg = float((p >> 8) & 0xff), pb = float((p >> 16) & 0xff);
				const float ds = box(x + 0.5f, y + 0.5f, it.x0, it.y0 + dy, it.x1, it.y1 + dy, r);
				const float ks = std::clamp(1.0f - std::max(ds, 0.0f) / R, 0.0f, 1.0f);
				const float as = ks * ks * 0.55f;
				pr *= 1 - as, pg *= 1 - as, pb *= 1 - as;
				if (G > 0)
				{
					const float dg = box(x + 0.5f, y + 0.5f, it.x0, it.y0, it.x1, it.y1, 0);
					if (dg > 0 && dg < G)
					{
						const float k = 1.0f - dg / G;
						const float ag = k * k * 0.40f * it.sel * 0.85f;
						pr += gr * ag, pg += gg * ag, pb += gb * ag;
					}
				}
				row[x] = Rgb(uint8_t(std::min(255.0f, pr)), uint8_t(std::min(255.0f, pg)), uint8_t(std::min(255.0f, pb)));
			}
		}
		// the cover: the mip level nearest its size, bilinear, rounded
		const float wpx = it.x1 - it.x0, hpx = it.y1 - it.y0;
		int l = 0;
		while (l + 1 < CoverTex::kLevels && t.h[l + 1] >= hpx)
			l++;
		const int tw = t.w[l], th = t.h[l];
		const uint32_t* tp = t.px[l].data();
		const uint32_t br = uint32_t(it.bright * 256);
		const int cy0 = std::max(0, int(it.y0)), cy1 = std::min(H, int(std::ceil(it.y1)));
		const int cx0 = std::max(x0, int(it.x0)), cx1 = std::min(x1, int(std::ceil(it.x1)));
		for (int y = cy0; y < cy1; y++)
		{
			uint32_t* row = surf + size_t(y) * W;
			const float v = std::clamp((y + 0.5f - it.y0) / hpx * th - 0.5f, 0.0f, float(th - 1));
			const int iy = int(v), iy1 = std::min(th - 1, iy + 1);
			const uint32_t wy = uint32_t((v - iy) * 256);
			for (int x = cx0; x < cx1; x++)
			{
				const float cov = std::clamp(0.5f - box(x + 0.5f, y + 0.5f, it.x0, it.y0, it.x1, it.y1, r), 0.0f, 1.0f);
				if (cov <= 0)
					continue;
				const float u = std::clamp((x + 0.5f - it.x0) / wpx * tw - 0.5f, 0.0f, float(tw - 1));
				const int ix = int(u), ix1 = std::min(tw - 1, ix + 1);
				const uint32_t wx = uint32_t((u - ix) * 256);
				const uint32_t a = tp[size_t(ix) * th + iy], b = tp[size_t(ix1) * th + iy];
				const uint32_t c = tp[size_t(ix) * th + iy1], d = tp[size_t(ix1) * th + iy1];
				const uint32_t ca = uint32_t(cov * 256);
				uint32_t o = 0xff000000u;
				for (int sh = 0; sh < 24; sh += 8)
				{
					const uint32_t top = ((a >> sh) & 0xff) * (256 - wx) + ((b >> sh) & 0xff) * wx;
					const uint32_t bot = ((c >> sh) & 0xff) * (256 - wx) + ((d >> sh) & 0xff) * wx;
					const uint32_t v8 = (((top * (256 - wy) + bot * wy) >> 16) * br) >> 8;
					const uint32_t dst = (row[x] >> sh) & 0xff;
					o |= ((v8 * ca + dst * (256 - ca)) >> 8) << sh;
				}
				row[x] = o;
			}
		}
	}
}

// ---- 3.2: the selected game's file, for the line under its title ---------------------------------------------
struct FileLine
{
	std::string file; // its file name (a disc set the app grouped: its first disc's, "+N")
	int discs = 1;
};

const FileLine& FileLineOf(const GameInfo& g)
{
	static std::map<std::string, FileLine> cache; // the shelf's thread only
	auto it = cache.find(g.path);
	if (it != cache.end())
		return it->second;
	FileLine fl;
	fl.file = g.path.substr(g.path.find_last_of('/') + 1);
	fl.discs = std::max(1, g.disc_count);
	if (g.ext == ".m3u")
	{
		const std::vector<std::string> discs = ResolvePlaylist(g.path, nullptr, nullptr);
		fl.discs = std::max(fl.discs, int(discs.size()));
		// a set the app made (cache/discs, cache/playlists): the player's own first disc, not the app's playlist
		if (!discs.empty() && (g.path.find("/cache/discs/") != std::string::npos || g.path.find("/cache/playlists/") != std::string::npos))
		{
			fl.file = discs[0].substr(discs[0].find_last_of('/') + 1);
			if (discs.size() > 1)
				fl.file += "  (+" + std::to_string(discs.size() - 1) + ")";
		}
	}
	return cache.emplace(g.path, fl).first->second;
}

// ---- input: left/right with key repeat that speeds up -------------------------------------------------
struct Repeat
{
	double next = 0, since = 0;
	bool held = false;
	bool Fire(bool down, double now)
	{
		if (!down)
		{
			held = false;
			return false;
		}
		if (!held)
		{
			held = true;
			since = now;
			next = now + 0.32;
			return true;
		}
		if (now >= next)
		{
			next = now + (now - since > 1.4 ? 0.045 : 0.095);
			return true;
		}
		return false;
	}
};

void CenterText(int y, const std::string& s, int scale, uint32_t color)
{
	const std::string fit = FitText(T(s.c_str()), scale, W - 200);
	DrawText((W - TextWidth(fit.c_str(), scale)) / 2, y, fit.c_str(), scale, color);
}

bool Confirm(const char* question)
{
	std::vector<uint32_t> saved(ps5video::Surface(), ps5video::Surface() + size_t(ps5video::SurfaceWidth()) * ps5video::SurfaceHeight());
	ps5input::Poll();
	uint32_t prev = ps5input::Pad(0).buttons;
	// 2.7: a rounded panel over the shelf, darkened
	style::Dim(190);
	const int bw = 760, bh = 230, bx = (W - bw) / 2, by = (H - bh) / 2;
	style::RoundRect(float(bx), float(by), float(bw), float(bh), 26, style::Surface1());
	TextMid(W / 2, by + 80, FitTextPt(T(question), 32, bw - 100, Weight::SemiBold).c_str(), 32, style::Hi(), Weight::SemiBold, Align::Center);
	style::Hints({{{icon::Cross}, T("Yes")}, {{icon::Circle}, T("No")}}, by + 165);
	ps5video::Present(0, 0, 0, 0, true);
	for (;;)
	{
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~prev;
		prev = cur;
		if (down & SCE_PAD_BUTTON_CROSS)
			return true;
		if (down & (SCE_PAD_BUTTON_CIRCLE | SCE_PAD_BUTTON_OPTIONS))
		{
			memcpy(ps5video::Surface(), saved.data(), saved.size() * 4);
			return false;
		}
		ps5video::Present(0, 0, 1, 1, true);
	}
}

// 3.9.9: the player's answer to "Continue where you left off?", for PlayGame (ShelfTakeResume)
bool g_resume = false;

// "Continue where you left off?" over the shelf, for a game left with a state saved (emu::AutoStatePathFor): Cross
// continues, Triangle starts it over, Circle goes back without starting it (3.10.1). `when`: how long ago it was left
// ("2 days ago"), "" if not known.
enum class ResumeChoice
{
	Continue,
	StartOver,
	Back,
};
ResumeChoice AskContinue(const std::string& title, const std::string& when)
{
	std::vector<uint32_t> saved(ps5video::Surface(), ps5video::Surface() + size_t(ps5video::SurfaceWidth()) * ps5video::SurfaceHeight());
	auto answer = [&](ResumeChoice c) {
		memcpy(ps5video::Surface(), saved.data(), saved.size() * 4); // the launch animation fades the shelf, not this box
		return c;
	};
	ps5input::Poll();
	uint32_t prev = ps5input::Pad(0).buttons;
	style::Dim(190);
	const int bw = 820, bh = 270, bx = (W - bw) / 2, by = (H - bh) / 2;
	style::RoundRect(float(bx), float(by), float(bw), float(bh), 26, style::Surface1());
	TextMid(W / 2, by + 70, FitTextPt(T("Continue where you left off?"), 32, bw - 100, Weight::SemiBold).c_str(), 32,
		style::Hi(), Weight::SemiBold, Align::Center);
	const std::string sub = when.empty() ? title : title + "   \xC2\xB7   " + when;
	TextMid(W / 2, by + 125, FitTextPt(sub, 21, bw - 100, Weight::Medium).c_str(), 21, style::Lo(), Weight::Medium,
		Align::Center);
	style::Hints({{{icon::Cross}, T("Continue")}, {{icon::Triangle}, T("Start over")}, {{icon::Circle}, T("Back")}}, by + 205);
	ps5video::Present(0, 0, 0, 0, true);
	for (;;)
	{
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~prev;
		prev = cur;
		if (down & SCE_PAD_BUTTON_CROSS)
			return answer(ResumeChoice::Continue);
		if (down & SCE_PAD_BUTTON_TRIANGLE)
			return answer(ResumeChoice::StartOver);
		if (down & SCE_PAD_BUTTON_CIRCLE)
			return answer(ResumeChoice::Back);
		ps5video::Present(0, 0, 1, 1, true);
	}
}

// ---- 3.10: the launch animation: the shelf fades to black (1 s), the game's disc stands still (2 s) and then spins up
// (3 s, 3.10.2), the game starts -----
// The disc: LaunchBox's picture of it (fe_caseview.h, DiscPicture), else one drawn here: a silver CD with a rainbow
// sheen and the cover on its label. Drawn by the CPU at the surface's resolution, a band of columns per thread of the
// pool; Cross or Circle skips it. Settings > Video > Animation when opening a game turns it off.
constexpr float kFadeSecs = 1.0f, kStillSecs = 2.0f, kSpinSecs = 3.0f, kOutSecs = 0.25f;
constexpr float kSpinFrom = 20.0f, kSpinTo = 1100.0f; // degrees a second at the start and at the end
constexpr int kDiscR = 330; // the disc's radius in points
constexpr float kHole = 0.07f; // the hole's radius, of the disc's

// Degrees turned after s seconds: the speed grows from kSpinFrom to kSpinTo with the square of the time.
float DiscAngle(float s)
{
	return kSpinFrom * s + (kSpinTo - kSpinFrom) * s * s * s / (3.0f * kSpinSecs * kSpinSecs);
}

inline uint32_t Px(int r, int g, int b)
{
	return 0xff000000u | uint32_t(std::clamp(b, 0, 255)) << 16 | uint32_t(std::clamp(g, 0, 255)) << 8 |
		   uint32_t(std::clamp(r, 0, 255));
}

// The cover (column-major, CoverTex level 0) at u, v in 0..1 of its centred square.
uint32_t CoverAt(const CoverTex* c, float u, float v)
{
	const int w = c->w[0], h = c->h[0], side = std::min(w, h);
	const int x = std::clamp(int((w - side) / 2 + u * side), 0, w - 1);
	const int y = std::clamp(int((h - side) / 2 + v * side), 0, h - 1);
	return c->px[0][size_t(x) * h + y];
}

// The drawn disc, S x S, row-major.
void DrawnDisc(Pool& pool, std::vector<uint32_t>& out, int S, const CoverTex* cover)
{
	out.assign(size_t(S) * S, 0xff000000u);
	const float half = S * 0.5f;
	const bool art = cover && !cover->px[0].empty();
	// the pool's bands are of the 1920-point screen: band x0..x1 -> rows of the disc
	pool.Run([&](int x0, int x1) {
		const int r0 = int(int64_t(x0) * S / W), r1 = int(int64_t(x1) * S / W);
		for (int y = r0; y < r1; y++)
			for (int x = 0; x < S; x++)
			{
				const float u = (x + 0.5f - half) / half, v = (y + 0.5f - half) / half;
				const float r = std::sqrt(u * u + v * v);
				if (r > 1.0f)
					continue;
				// silver, lighter at the rim
				const float t = r < 0.6f ? (r - 0.2f) / 0.4f : (r - 0.6f) / 0.4f;
				float cr, cg, cb;
				if (r < 0.6f)
				{
					const float k = std::clamp(t, 0.0f, 1.0f);
					cr = 217 - 47 * k, cg = 221 - 45 * k, cb = 230 - 41 * k;
				}
				else
					cr = 170 + 59 * t, cg = 176 + 56 * t, cb = 189 + 50 * t;
				// the rainbow: two soft sweeps across the disc
				const float a = std::atan2(v, u);
				const float sheen = std::pow(std::max(0.0f, std::cos(2.0f * (a - 0.6f))), 3.0f) * 0.4f;
				const float hue = a * 0.9549f + r * 2.0f; // 0..6 round the colour wheel
				const float hr = std::clamp(std::fabs(std::fmod(hue + 6.0f, 6.0f) - 3.0f) - 1.0f, 0.0f, 1.0f);
				const float hg = std::clamp(2.0f - std::fabs(std::fmod(hue + 4.0f, 6.0f) - 2.0f), 0.0f, 1.0f);
				const float hb = std::clamp(2.0f - std::fabs(std::fmod(hue + 2.0f, 6.0f) - 2.0f), 0.0f, 1.0f);
				cr += (hr * 255 - cr) * sheen, cg += (hg * 255 - cg) * sheen, cb += (hb * 255 - cb) * sheen;
				// the label: the cover in the ring between 0.30 and 0.92
				if (art && r > 0.30f && r < 0.92f)
				{
					const uint32_t c = CoverAt(cover, (u / 0.92f + 1) * 0.5f, (v / 0.92f + 1) * 0.5f);
					cr = float(c & 0xff), cg = float((c >> 8) & 0xff), cb = float((c >> 16) & 0xff);
				}
				// the label's edges, and the clear hub round the hole
				const float px1 = 1.5f / half;
				if (std::fabs(r - 0.92f) < px1 || std::fabs(r - 0.30f) < px1)
					cr += (255 - cr) * 0.6f, cg += (255 - cg) * 0.6f, cb += (255 - cb) * 0.6f;
				else if (r < 0.23f)
					cr = cr * 0.45f + 210 * 0.55f, cg = cg * 0.45f + 215 * 0.55f, cb = cb * 0.45f + 225 * 0.55f;
				if (std::fabs(r - 0.16f) < px1)
					cr += (255 - cr) * 0.35f, cg += (255 - cg) * 0.35f, cb += (255 - cb) * 0.35f;
				out[size_t(y) * S + x] = Px(int(cr), int(cg), int(cb));
			}
	});
}

// A picture (row-major) -> the disc, S x S: its centred square, bilinear.
void PictureDisc(Pool& pool, std::vector<uint32_t>& out, int S, const std::vector<uint32_t>& px, int w, int h)
{
	out.assign(size_t(S) * S, 0xff000000u);
	const int side = std::min(w, h), ox = (w - side) / 2, oy = (h - side) / 2;
	const float step = float(side) / S;
	pool.Run([&](int x0, int x1) {
		const int r0 = int(int64_t(x0) * S / W), r1 = int(int64_t(x1) * S / W);
		for (int y = r0; y < r1; y++)
		{
			const float fy = std::clamp(oy + (y + 0.5f) * step - 0.5f, 0.0f, float(h - 1));
			const int y0 = int(fy), y1 = std::min(h - 1, y0 + 1);
			const uint32_t wy = uint32_t((fy - y0) * 256);
			for (int x = 0; x < S; x++)
			{
				const float fx = std::clamp(ox + (x + 0.5f) * step - 0.5f, 0.0f, float(w - 1));
				const int xa = int(fx), xb = std::min(w - 1, xa + 1);
				const uint32_t wx = uint32_t((fx - xa) * 256);
				const uint32_t top = Lerp32(px[size_t(y0) * w + xa], px[size_t(y0) * w + xb], wx);
				const uint32_t bot = Lerp32(px[size_t(y1) * w + xa], px[size_t(y1) * w + xb], wx);
				out[size_t(y) * S + x] = Lerp32(top, bot, wy) | 0xff000000u;
			}
		}
	});
}

// The animation. False: the player skipped it (the game starts all the same).
bool LaunchAnimation(Pool& pool, const GameInfo& game, const CoverTex* cover)
{
	const double t0 = Now();
	OrbisLog("[launchanim] %s", game.path.c_str());
	DiscPictureStart(game);
	uint32_t* surf = ps5video::Surface();
	const int k = ps5video::PixelScale(), SW = ps5video::SurfaceWidth(), SH = ps5video::SurfaceHeight();
	const size_t n = size_t(SW) * SH;
	const std::vector<uint32_t> shelf(surf, surf + n);
	ps5input::Poll();
	uint32_t prev = ps5input::Pad(0).buttons; // Cross is still down from picking the game
	auto skipped = [&] {
		ps5input::Poll();
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~prev;
		prev = cur;
		return (down & (SCE_PAD_BUTTON_CROSS | SCE_PAD_BUTTON_CIRCLE)) != 0;
	};
	auto smooth = [](float t) { return t * t * (3 - 2 * t); };
	auto black = [&] {
		ps5video::FillRect(0, 0, W, H, Rgb(0, 0, 0));
		ps5video::Present(0, 0, 0, 0, true);
	};

	// -- 1. the shelf fades to black
	for (;;)
	{
		const float t = float(Now() - t0) / kFadeSecs;
		if (t >= 1.0f)
			break;
		memcpy(surf, shelf.data(), n * 4);
		style::Dim(int(255 * smooth(t)));
		ps5video::Present(0, 0, 0, 0, true);
		if (skipped())
		{
			OrbisLog("[launchanim] skipped");
			black();
			return false;
		}
	}
	black();
	ps5gpu::ClearScene(); // the surface covers it from now on

	// -- 2. the disc and what stands behind it (made while the screen is black)
	const int S = 2 * kDiscR * k, R = kDiscR * k, hole = int(kHole * R);
	const int cx = W / 2 * k, cy = (H / 2 - 40) * k;
	std::vector<uint32_t> disc, pic;
	int pw = 0, ph = 0;
	bool done = false;
	const bool real = DiscPicture(pic, pw, ph, &done);
	if (real)
		PictureDisc(pool, disc, S, pic, pw, ph);
	else
		DrawnDisc(pool, disc, S, cover);
	OrbisLog("[launchanim] the disc: %s (%.0f ms in)", real ? "its picture" : done ? "drawn (none found)" : "drawn (still looking)",
		(Now() - t0) * 1000);
	// the backdrop: black, a glow of the cover's colour round the disc, the title and the hint under it
	const uint32_t avg = cover ? cover->average : Rgb(60, 50, 110);
	const float gr = float(avg & 0xff), gg = float((avg >> 8) & 0xff), gb = float((avg >> 16) & 0xff);
	pool.Run([&](int x0, int x1) {
		for (int y = 0; y < SH; y++)
			for (int x = x0 * k; x < x1 * k; x++)
			{
				const float d = std::hypot(float(x - cx), float(y - cy)) / R;
				const float a = 0.32f * std::clamp((2.1f - d) / 1.5f, 0.0f, 1.0f);
				surf[size_t(y) * SW + x] = Px(int(gr * a), int(gg * a), int(gb * a));
			}
	});
	TextMid(W / 2, 960, FitTextPt(game.title, 44, W - 240, Weight::SemiBold).c_str(), 44, style::Hi(), Weight::SemiBold,
		Align::Center);
	TextMid(W / 2, 1004, T("Loading..."), 24, style::Lo(), Weight::Medium, Align::Center);
	const std::string hint = std::string(T("You can turn this animation off in Settings > Video > Animation when opening a game")) +
							 "      " + icon::Cross + " " + T("Skip");
	TextMid(W / 2, 1052, FitTextPt(hint, 17, W - 120, Weight::Regular).c_str(), 17, style::Lo(), Weight::Regular, Align::Center);
	const std::vector<uint32_t> back(surf, surf + n);

	// -- 3. the disc stands still, then spins up
	const double s0 = Now();
	bool skip = false;
	for (;;)
	{
		const float s = float(Now() - s0);
		if (s >= kStillSecs + kSpinSecs || skip)
			break;
		const float in = std::min(1.0f, s / 0.3f); // it comes in: fades in, grows from 90 %
		const float scale = 0.9f + 0.1f * smooth(in);
		const float out = std::clamp((s - (kStillSecs + kSpinSecs - kOutSecs)) / kOutSecs, 0.0f, 1.0f); // the end: to black
		const float ang = DiscAngle(std::max(0.0f, s - kStillSecs)) * 3.14159265f / 180.0f; // still at first, then spinning
		const float cs = std::cos(ang) / scale, sn = std::sin(ang) / scale;
		const float half = S * 0.5f, inv_scale = 1.0f / scale;
		const uint32_t fade = uint32_t(256 * in);
		if (out > 0)
			memcpy(surf, back.data(), n * 4); // the title goes dark too
		pool.Run([&](int x0, int x1) {
			const int xa = std::max(x0 * k, cx - R - 1), xb = std::min(x1 * k, cx + R + 1);
			for (int y = std::max(0, cy - R - 1); y < std::min(SH, cy + R + 1); y++)
			{
				uint32_t* row = surf + size_t(y) * SW;
				const uint32_t* brow = back.data() + size_t(y) * SW;
				const float dy = y + 0.5f - cy;
				for (int x = xa; x < xb; x++)
				{
					const float dx = x + 0.5f - cx;
					const float rr = std::sqrt(dx * dx + dy * dy) * inv_scale; // in the disc's own pixels
					const float cover_a = std::clamp(R - rr + 0.5f, 0.0f, 1.0f) * std::clamp(rr - hole + 0.5f, 0.0f, 1.0f);
					if (cover_a <= 0)
					{
						row[x] = brow[x];
						continue;
					}
					// the disc's pixel under this one, turned back by the angle (bilinear)
					const float tx = std::clamp(cs * dx + sn * dy + half - 0.5f, 0.0f, float(S - 2));
					const float ty = std::clamp(-sn * dx + cs * dy + half - 0.5f, 0.0f, float(S - 2));
					const int ix = int(tx), iy = int(ty);
					const uint32_t wx = uint32_t((tx - ix) * 256), wy = uint32_t((ty - iy) * 256);
					const uint32_t* d0 = disc.data() + size_t(iy) * S + ix;
					uint32_t c = Lerp32(Lerp32(d0[0], d0[1], wx), Lerp32(d0[S], d0[S + 1], wx), wy);
					// a light from the top left that stays put while the disc turns
					const float l = std::max(0.0f, 1.0f - ((dx + dy) * inv_scale / (2.0f * R) + 0.5f) / 0.45f) * 0.19f;
					if (l > 0)
						c = Lerp32(c, 0xffffffffu, uint32_t(l * 256));
					const uint32_t a = uint32_t(cover_a * fade);
					row[x] = Lerp32(brow[x], c, a) | 0xff000000u;
				}
			}
		});
		if (out > 0)
			style::Dim(int(255 * out)); // over the backdrop copied whole this frame: it does not add up
		ps5video::Present(0, 0, 0, 0, true);
		skip = skipped();
	}
	black();
	OrbisLog("[launchanim] %s after %.1f s", skip ? "skipped" : "done", Now() - t0);
	return !skip;
}

// ---- state kept between visits to the shelf (the game list, its covers) -----------------------------------
struct ShelfState
{
	std::vector<GameInfo> all; // the whole library
	int family = -1; // the tab the list below shows
	std::vector<GameInfo> games; // the games on the shelf: the tab's
	CoverService covers;
	std::vector<CoverPtr> slots;
	bool started = false;
	bool downloads = true;
	std::string tab_sel[tabs::kCount]; // each storage tab's selected game, kept between visits to the shelf
};
ShelfState& S()
{
	static ShelfState* s = new ShelfState(); // never destroyed: the cover thread may outlive main's statics
	return *s;
}

std::shared_ptr<CoverTex> LoadingCard()
{
	static std::shared_ptr<CoverTex> card;
	if (card)
		return card;
	const int w = 716, h = kCoverTexH;
	auto tex = std::make_shared<CoverTex>();
	int lw = w, lh = h;
	for (int l = 0; l < CoverTex::kLevels; l++)
	{
		tex->w[l] = lw;
		tex->h[l] = lh;
		tex->px[l].resize(size_t(lw) * lh);
		for (int x = 0; x < lw; x++)
			for (int y = 0; y < lh; y++)
			{
				const bool border = x < 3 || y < 3 || x >= lw - 3 || y >= lh - 3;
				const float t = float(y) / lh;
				tex->px[l][size_t(x) * lh + y] = border ? Rgb(100, 89, 70) : Rgb(uint8_t(36 - 14 * t), uint8_t(33 - 12 * t), uint8_t(28 - 10 * t));
			}
		lw /= 2;
		lh /= 2;
	}
	tex->average = Rgb(60, 50, 110);
	card = tex;
	return card;
}

// The letter filter (2.0): 0 every game, 1 "#" (a number, a symbol, a non-Latin title), 2..27 A..Z, by the first letter of
// the shown title with a leading "The " left out ("The King of Fighters '95" is a K game).
constexpr int kLetters = 28;
int LetterOf(const GameInfo& g)
{
	const std::string& t = g.title;
	size_t i = 0;
	while (i < t.size() && (t[i] == ' ' || t[i] == '"' || t[i] == '\'' || t[i] == '~'))
		i++;
	if (t.size() >= i + 4 && strncasecmp(t.c_str() + i, "the ", 4) == 0)
		i += 4;
	while (i < t.size() && t[i] == ' ')
		i++;
	if (i >= t.size())
		return 1;
	const char c = char(toupper(uint8_t(t[i])));
	return c >= 'A' && c <= 'Z' ? 2 + (c - 'A') : 1;
}

const char* LetterLabel(int letter)
{
	static const char* const kLabels[kLetters] = {"All", "#", "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L",
		"M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z"};
	return letter >= 0 && letter < kLetters ? kLabels[letter] : "All";
}

// how many games each letter has (have[0]: all of them)
void LetterCounts(const std::vector<GameInfo>& all, int have[kLetters])
{
	for (int i = 0; i < kLetters; i++)
		have[i] = 0;
	for (const GameInfo& g : all)
	{
		have[0]++;
		have[LetterOf(g)]++;
	}
}

std::vector<GameInfo> InLetter(const std::vector<GameInfo>& all, int letter)
{
	if (letter <= 0)
		return all;
	std::vector<GameInfo> out;
	for (const GameInfo& g : all)
		if (LetterOf(g) == letter)
			out.push_back(g);
	return out;
}

std::string ShelfFmt(const char* fmt, const std::string& a)
{
	char buf[256];
	snprintf(buf, sizeof(buf), fmt, a.c_str());
	return buf;
}

// 3.9.7: the search (Square): a game is shown when every word of it is in its title or its file name, the case,
// accents and punctuation aside ("street alpha" finds "Street Fighter Alpha 2").
std::string SearchKey(const std::string& s)
{
	std::string out;
	for (size_t i = 0; i < s.size(); i++)
	{
		unsigned char c = static_cast<unsigned char>(s[i]);
		if (c == 0xC3 && i + 1 < s.size())
		{
			// the accented Latin letters (UTF-8 C3 xx) as their plain letter
			static const char* const map = "aaaaaaaceeeeiiiidnooooo/ouuuuyty"; // C3 A0..BF
			const unsigned char d = static_cast<unsigned char>(s[i + 1]);
			if (d >= 0xA0 && d <= 0xBF)
				out += map[d - 0xA0];
			else if (d >= 0x80 && d <= 0x9F)
				out += char(std::tolower(static_cast<unsigned char>(map[d - 0x80])));
			else
				out += ' ';
			i++;
			continue;
		}
		out += std::isalnum(c) ? char(std::tolower(c)) : ' ';
	}
	return out;
}

bool MatchesSearch(const GameInfo& g, const std::string& search)
{
	const std::string hay = " " + SearchKey(g.title) + " " + SearchKey(g.path.substr(g.path.find_last_of('/') + 1)) + " ";
	const std::string q = SearchKey(search);
	size_t i = 0;
	bool any = false;
	while (i < q.size())
	{
		while (i < q.size() && q[i] == ' ')
			i++;
		size_t j = i;
		while (j < q.size() && q[j] != ' ')
			j++;
		if (j > i)
		{
			any = true;
			// each word: anywhere in the title or the name, its letters together
			std::string w = q.substr(i, j - i);
			std::string squashed, hay_squashed;
			if (hay.find(w) == std::string::npos)
			{
				for (char c : hay)
					if (c != ' ')
						hay_squashed += c;
				if (hay_squashed.find(w) == std::string::npos)
					return false;
			}
		}
		i = j;
	}
	return any;
}

// The search's pill where the letter bar is: what is searched, and Circle to clear it.
void DrawSearchBar(const std::string& search, int found)
{
	constexpr float kY = 107, kH = 34;
	const std::string label = ShelfFmt(T("Search: \"%s\""), search);
	char nbuf[64];
	snprintf(nbuf, sizeof(nbuf), T("%d games"), found);
	const std::string count = found == 1 ? std::string(T("1 game")) : std::string(nbuf);
	const std::string clear = std::string(icon::Circle) + "  " + T("Clear search");
	const int lw = TextWidthPt(label.c_str(), 17, Weight::SemiBold), cw = TextWidthPt(count.c_str(), 15, Weight::Medium),
			  xw = TextWidthPt(clear.c_str(), 15, Weight::Medium);
	const int pill = lw + 40, total = pill + 18 + cw + 30 + xw;
	int x = (W - total) / 2;
	style::RoundRect(float(x), kY - kH / 2, float(pill), kH, kH / 2, style::Accent());
	TextMid(x + 20, int(kY), label.c_str(), 17, style::AccentText(), Weight::SemiBold);
	x += pill + 18;
	TextMid(x, int(kY), count.c_str(), 15, style::Lo(), Weight::Medium);
	x += cw + 30;
	TextMid(x, int(kY), clear.c_str(), 15, style::Lo(), Weight::Medium);
}

// The letter bar under the storage tabs: "All # A B ... Z", the chosen one in an accent pill, the letters without games
// faint (2.7's look).
void DrawLetterBar(const int have[kLetters], int chosen)
{
	constexpr float kSize = 16, kY = 107, kH = 30;
	constexpr int kGap = 3;
	int widths[kLetters];
	int total = 0;
	for (int i = 0; i < kLetters; i++)
	{
		const char* label = i == 0 ? T("All") : LetterLabel(i);
		widths[i] = std::max(29, TextWidthPt(label, kSize, Weight::Medium) + 18);
		total += widths[i] + (i ? kGap : 0);
	}
	int x = (W - total) / 2;
	TextMid(x - 14, int(kY), icon::DpadUpDown, 22, style::Off(), Weight::Regular, Align::Right);
	for (int i = 0; i < kLetters; i++)
	{
		const char* label = i == 0 ? T("All") : LetterLabel(i);
		if (i == chosen)
		{
			style::RoundRect(float(x), kY - kH / 2, float(widths[i]), kH, kH / 2, style::Accent());
			TextMid(x + widths[i] / 2, int(kY), label, kSize, style::AccentText(), Weight::Medium, Align::Center);
		}
		else
			TextMid(x + widths[i] / 2, int(kY), label, kSize, have[i] > 0 || i == 0 ? style::Hi() : style::Off(), Weight::Medium,
				Align::Center);
		x += widths[i] + kGap;
	}
}

void Header()
{
	Text(60, 30, "SSYmirPS5", 36, style::Hi(), Weight::SemiBold);
	const int x = 60 + TextWidthPt("SSYmirPS5", 36, Weight::SemiBold) + 12;
	// 3.9.7: the app's name in full, its version and the Ymir core's
	Text(x, 45, "Saturn for PS5", 17, style::Lo(), Weight::Medium);
	const std::string ver = std::string("v" SSYMIRPS5_VERSION "   \xC2\xB7   ") + T("Ymir core") + " " YMIR_CORE_VERSION;
	Text(x + TextWidthPt("Saturn for PS5", 17, Weight::Medium) + 12, 47, ver.c_str(), 14, style::Off());
}

void Loading(const char* text)
{
	ps5video::FillRect(0, 0, W, H, style::Bg());
	Header();
	const std::string t = FitTextPt(T(text), 26, W - 200, Weight::Medium);
	TextMid(W / 2, H / 2, t.c_str(), 26, style::Hi(), Weight::Medium, Align::Center);
	ps5video::Present(0, 0, 0, 0, true);
}

// A line already in the player's language (a formatted one), centred.
void CenterRaw(int y, const std::string& s, int scale, uint32_t color)
{
	const std::string fit = FitText(s, scale, W - 200);
	DrawText((W - TextWidth(fit.c_str(), scale)) / 2, y, fit.c_str(), scale, color);
}

// The same, with a bar under the text: fraction 0..1, or below 0 for none.
void LoadingProgress(const char* text, float fraction)
{
	ps5video::FillRect(0, 0, W, H, style::Bg());
	Header();
	const std::string t = FitTextPt(T(text), 26, W - 200, Weight::Medium);
	TextMid(W / 2, H / 2 - 20, t.c_str(), 26, style::Hi(), Weight::Medium, Align::Center);
	if (fraction >= 0)
	{
		const float bw = 640, bh = 6, bx = (W - bw) / 2, by = H / 2 + 30;
		style::RoundRect(bx, by, bw, bh, 3, style::Surface2());
		style::RoundRect(bx, by, bw * std::min(1.0f, fraction), bh, 3, style::Accent());
	}
	ps5video::Present(0, 0, 0, 0, true);
}

// ---- storage tabs (Recent, Internal, USB: fe_tabs.h) ---------
// From Snes9x PS5 (thatboialex's shelf tabs). The system tabs (Up / Down) work inside the storage tab.
// A tab's name on the screen (USB is a name; the log keeps tabs::Name in English).
const char* TabLabel(int t)
{
	return t == tabs::Usb ? tabs::Name(t) : T(tabs::Name(t));
}

// The tab bar, centred at the top: Recent, Internal, USB (only with a USB game). L1 / R1 stand at its ends.

// 2.7: the tabs as words, the chosen one bright with an accent line under it
void DrawTabs(const int counts[tabs::kCount], int active)
{
	constexpr float kSize = 20;
	constexpr int kGap = 36, kMid = 46;
	int widths[tabs::kCount] = {};
	int total = 0, shown = 0;
	for (int t : tabs::kOrder)
		if (tabs::Shown(counts, t))
			total += (widths[t] = TextWidthPt(TabLabel(t), kSize, Weight::SemiBold)) + (shown++ ? kGap : 0);
	int x = (W - total) / 2;
	if (shown > 1)
	{
		TextMid(x - 20, kMid, icon::L1, 24, style::Off(), Weight::Regular, Align::Right);
		TextMid(x + total + 20, kMid, icon::R1, 24, style::Off());
	}
	for (int t : tabs::kOrder)
	{
		if (!widths[t])
			continue;
		const bool on = t == active;
		TextMid(x, kMid, TabLabel(t), kSize, on ? style::Hi() : style::Lo(), Weight::SemiBold);
		if (on)
			style::RoundRect(float(x), kMid + 18, float(widths[t]), 3, 1.5f, style::Accent());
		x += widths[t] + kGap;
	}
}

} // namespace

void ShelfShutdown()
{
	S().covers.Stop();
	CaseViewShutdown();
	launchbox::Shutdown();
}

int DownloadBorders()
{
	const std::string pack = OrbisDir("borders");
	const std::vector<WantedCover> wanted = border::MissingBorders(pack, S().all);
	border::WriteWantedBorders(pack, wanted);
	OrbisLog("[border] user asked for the borders of the library: %zu to download", wanted.size());
	return int(wanted.size());
}

bool ShelfTakeResume()
{
	const bool r = g_resume;
	g_resume = false;
	return r;
}

std::string Shelf()
{
	Settings& cfg = Config();
	ShelfState& st = S();
	YMIR_STAGE(Shelf, "pool");
	Pool pool;
	// the GPU draws the backdrop and the covers when it can (text stays on the surface)
	struct SceneGuard
	{
		~SceneGuard() { ps5gpu::ClearScene(); }
	} scene_guard;
	const bool gpu_shelf = ps5video::GpuActive() && ps5gpu::SceneAvailable();
	OrbisLog("[shelf] drawn by the %s", gpu_shelf ? "GPU" : "CPU");

	YMIR_STAGE(Shelf, "scan games");
	Loading("Looking for games...");
	// a big library takes a while the first time (each new ROM is read once for its CRC): say how far it is,
	// redrawn at most ten times a second
	double drawn = Now();
	std::vector<GameInfo> games = ScanGames([&drawn](int stage, size_t done, size_t total) {
		const double now = Now();
		if (now - drawn < 0.1)
			return;
		drawn = now;
		char text[96];
		if (stage == 0)
			snprintf(text, sizeof(text), T("Looking for games... %zu found"), done);
		else
			snprintf(text, sizeof(text), T("Identifying games... %d%% (%zu of %zu)"), total ? int(done * 100 / total) : 0, done, total);
		LoadingProgress(text, stage == 0 ? -1.0f : total ? float(done) / float(total) : 0.f);
	});
	bool same = games.size() == st.all.size();
	for (size_t i = 0; same && i < games.size(); i++)
		same = games[i].path == st.all[i].path;
	if (!ShelfDownloads() && (!same || !st.started))
	{
		// the console: covers come from the prefetch at start (fe_prefetch.h); new ones restart the app
		YMIR_STAGE(Shelf, "wanted covers");
		CoversRestartIfNeeded(games, cfg.covers_download, false, Loading);
	}
	st.all = games;

	// -- tabs: the storage tab (L1 / R1) and, inside it, the system tab (Up / Down); st.games is what both leave
	for (const GameInfo& g : st.all)
		if (g.path == cfg.last_rom)
		{
			st.tab_sel[tabs::Of(g)] = g.path; // the game played last is where its tab opens
			st.tab_sel[tabs::Recent] = g.path;
		}
	int counts[tabs::kCount];
	int stab = cfg.last_tab;
	int letters_have[kLetters] = {};
	int letter = 0; // the letter filter shown (cfg.shelf_letter, or All when that letter has no games here)
	std::string search; // 3.9.7: the search (Square): its games only, of every letter; "" none
	int n = 0, sel = 0;
	float pos = 0;
	double sel_since = Now();
	bool tab_chosen = false; // the player pressed L1 / R1: an empty tab is then left as it is
	std::map<std::string, bool> left_saved; // the Recent tab's games with a state saved on leaving (Fill)
	// The games of the storage tab and the system tab, the selection on `keep` (else the first game). restart: the
	// cover service starts again even when the list is the same (the shelf opens on a library that changed).
	auto Fill = [&](const std::string& keep, bool restart) {
		std::vector<GameInfo> local;
		if (stab == tabs::Recent)
		{
			// 3.9.9: the games played last, the last one first, of any storage (the ones still in the library)
			left_saved.clear();
			for (const recent::Entry& e : recent::List())
				for (const GameInfo& g : st.all)
					if (g.path == e.path)
					{
						local.push_back(g);
						left_saved[g.path] = cfg.resume && access(emu::AutoStatePathFor(g.path).c_str(), F_OK) == 0;
						break;
					}
		}
		else
			for (const GameInfo& g : st.all)
				if (tabs::Of(g) == stab)
					local.push_back(g);
		if (stab == tabs::Recent)
			std::fill(letters_have, letters_have + kLetters, 0); // no letters there: the list is by time
		else
			LetterCounts(local, letters_have);
		letter = cfg.shelf_letter >= 0 && cfg.shelf_letter < kLetters && letters_have[cfg.shelf_letter] > 0 ? cfg.shelf_letter : 0;
		std::vector<GameInfo> list;
		if (stab == tabs::Recent)
			list = std::move(local);
		else if (search.empty())
			list = InLetter(local, letter);
		else
			for (const GameInfo& g : local)
				if (MatchesSearch(g, search))
					list.push_back(g);
		if (stab != tabs::Recent) // 3.11: Settings, Sort the games
			tabs::SortGames(list, cfg.shelf_sort,
				[](const std::string& p) { return std::max(recent::When(p), playtime::Last(p)); },
				[](const std::string& p) { return playtime::Seconds(p); });
		// the same games, named the same (the cover is looked up by these): the covers are left as they are
		bool same_list = list.size() == st.games.size();
		for (size_t i = 0; same_list && i < list.size(); i++)
		{
			const GameInfo &a = list[i], &b = st.games[i];
			same_list = a.path == b.path && a.system == b.system && a.nointro == b.nointro && a.title == b.title &&
						a.crc == b.crc && a.crc_name == b.crc_name && a.header_title == b.header_title &&
						a.header_japan == b.header_japan;
		}
		// the search, the Recent tab and each order: a list of their own
		const int family = (stab == tabs::Recent ? 2000 : search.empty() ? letter : 1000) + 10000 * cfg.shelf_sort;
		if (restart || !same_list || !st.started || st.downloads != cfg.covers_download || st.family != family)
		{
			st.family = family;
			st.games = std::move(list);
			st.slots.assign(st.games.size(), nullptr);
			st.downloads = cfg.covers_download;
			YMIR_STAGE(Shelf, "cover service start");
			st.covers.Start(st.games, cfg.covers_download && ShelfDownloads(), cfg.covers_download && BackgroundCovers());
			st.started = true;
		}
		n = int(st.games.size());
		sel = 0;
		for (int i = 0; i < n; i++)
			if (st.games[size_t(i)].path == keep)
				sel = i;
		pos = float(sel);
		sel_since = Now();
		if (n > 0)
			st.covers.SetFocus(sel);
	};
	// Opens storage tab `want` (or the tab that fits when it has nothing to show, see tabs::Resolve) on the game
	// `keep`, or on the one that tab had selected.
	auto OpenTab = [&](int want, const std::string& keep, bool restart) {
		tabs::Count(st.all, counts);
		for (const recent::Entry& e : recent::List())
			for (const GameInfo& g : st.all)
				if (g.path == e.path)
				{
					counts[tabs::Recent]++;
					break;
				}
		stab = tabs::Resolve(counts, want, tab_chosen);
		if (stab == tabs::Recent)
			search.clear(); // a search is of a storage tab's games
		Fill(keep.empty() ? st.tab_sel[stab] : keep, restart);
		cfg.last_tab = stab;
		OrbisLog("[shelf] tab %s: %d game(s)", tabs::Name(stab), n);
	};
	OpenTab(cfg.last_tab, "", !same);

	Backdrop bd_cur, bd_old;
	float fade = 1.0f; // 0..1: old -> cur
	YMIR_STAGE(Shelf, "build backdrop");
	BuildBackdrop(pool, bd_cur, nullptr);
	YMIR_STAGE(Shelf, "first frame");
	const CoverTex* bd_wanted = nullptr;

	Repeat rep_l, rep_r, rep_l2, rep_r2;
	ps5input::Poll();
	uint32_t prev = ps5input::Pad(0).buttons;
	double last = Now();
	bool dirty = true;
	int last_status = -1;
	// the console's and the USB drives' game folders: a new game (copied over FTP, a drive plugged in) is seen within a
	// few seconds, scanned, and its cover downloaded
	uint64_t local_sig = LocalLibrarySignature();
	double next_local = Now() + 4.0;

	for (;;)
	{
		YMIR_STAGE(Shelf, "loop");
		const double now = Now();
		const float dt = float(std::min(0.05, now - last));
		last = now;

		// -- the game folders: a game added or removed (or a drive plugged in or out) after the scan
		if (now >= next_local)
		{
			next_local = now + 4.0;
			const uint64_t sig = LocalLibrarySignature();
			if (sig != local_sig)
			{
				local_sig = sig;
				OrbisLog("[shelf] the game folders changed: scanning again");
				const std::string keep = n > 0 ? st.games[size_t(sel)].path : std::string();
				std::vector<GameInfo> fresh = ScanGames();
				if (!ShelfDownloads()) // the console: covers of games that came late are fetched by the prefetch of a restart
					CoversRestartIfNeeded(fresh, cfg.covers_download, false, Loading);
				st.all = std::move(fresh);
				OpenTab(stab, keep, false); // the covers start again only when the tab's games or their names changed
				dirty = true;
			}
		}

		// -- input
		ps5input::Poll();
		const bool touch_menu = screenshot::TakeMenuRequest();
		if (screenshot::Pending()) dirty = true;
		const uint32_t cur = ps5input::Pad(0).buttons;
		const uint32_t down = cur & ~prev;
		prev = cur;
		const int old_sel = sel;
		if (n > 0)
		{
			// the Recent tab is by time, and an order other than by name puts the letters apart: no letter jumps
			const bool letters = stab != tabs::Recent && cfg.shelf_sort == tabs::ByName;
			if (rep_l.Fire(cur & SCE_PAD_BUTTON_LEFT, now))
				sel = std::max(0, sel - 1);
			if (rep_r.Fire(cur & SCE_PAD_BUTTON_RIGHT, now))
				sel = std::min(n - 1, sel + 1);
			// 3.3: L2 / R2 jump by letter: to the first game of the letter before / after the selected one's
			auto letter_at = [&](int i) { return LetterOf(st.games[size_t(i)]); };
			if (rep_l2.Fire(cur & SCE_PAD_BUTTON_L2, now) && letters)
			{
				int i = sel;
				while (i > 0 && letter_at(i - 1) == letter_at(sel))
					i--; // the start of this letter
				if (i == sel && i > 0)
				{
					i--; // already there: the start of the letter before
					while (i > 0 && letter_at(i - 1) == letter_at(sel - 1))
						i--;
				}
				sel = i;
			}
			if (rep_r2.Fire(cur & SCE_PAD_BUTTON_R2, now) && letters)
			{
				int i = sel;
				while (i < n - 1 && letter_at(i) == letter_at(sel))
					i++;
				sel = letter_at(i) != letter_at(sel) ? i : n - 1; // the next letter's first game, else the last one
			}
		}
		if ((down & (SCE_PAD_BUTTON_UP | SCE_PAD_BUTTON_DOWN)) && letters_have[0] > 0 && search.empty())
		{
			// another letter (Down: the next one, Up: the one before), skipping the letters without games; the selection
			// stays on the same game if it is there (All)
			const std::string keep = n > 0 ? st.games[size_t(sel)].path : cfg.last_rom;
			const int dir = (down & SCE_PAD_BUTTON_DOWN) ? 1 : -1;
			int next = letter;
			for (int step = 0; step < kLetters; step++)
			{
				next = (next + dir + kLetters) % kLetters;
				if (letters_have[next] > 0)
					break;
			}
			cfg.shelf_letter = next;
			Fill(keep, false);
			OrbisLog("[shelf] letter %s: %d game(s)", LetterLabel(letter), n);
			dirty = true;
		}
		if (down & (SCE_PAD_BUTTON_L1 | SCE_PAD_BUTTON_R1))
		{
			// another tab (Recent, Internal, USB): it opens on the game it had selected
			if (n > 0)
				st.tab_sel[stab] = st.games[size_t(sel)].path;
			tab_chosen = true;
			OpenTab(tabs::Step(counts, stab, (down & SCE_PAD_BUTTON_R1) ? 1 : -1), "", false);
			dirty = true;
		}
		if (sel != old_sel)
		{
			st.covers.SetFocus(sel);
			sel_since = now;
			dirty = true;
		}
		// 3.9.9: a game left with a state saved: continue from it, or start it over. False: the player went back
		// (Circle) and the game is not started.
		auto ask_resume = [&](const GameInfo& game) {
			g_resume = false;
			if (!cfg.resume)
				return true;
			const std::string state = emu::AutoStatePathFor(game.path);
			struct stat sb;
			if (stat(state.c_str(), &sb) != 0)
				return true;
			const ResumeChoice c = AskContinue(game.title, recent::Ago((long long)sb.st_mtime));
			g_resume = c == ResumeChoice::Continue;
			OrbisLog("[shelf] %s: %s", game.path.c_str(), c == ResumeChoice::Continue ? "continue where it was left"
															: c == ResumeChoice::StartOver ? "start over" : "back");
			ps5input::Poll();
			prev = ps5input::Pad(0).buttons; // the answer's button does not count on the shelf
			return c != ResumeChoice::Back;
		};
		// 3.10: the shelf fades out and the game's disc spins (Settings > Video turns it off)
		auto launch_anim = [&](const GameInfo& game, int i) {
			if (cfg.launch_anim)
				LaunchAnimation(pool, game, st.slots[size_t(i)] ? st.slots[size_t(i)].get() : LoadingCard().get());
		};
		if ((down & SCE_PAD_BUTTON_CROSS) && n > 0 && !ask_resume(st.games[size_t(sel)]))
			dirty = true; // back to the shelf
		else if ((down & SCE_PAD_BUTTON_CROSS) && n > 0)
		{
			launch_anim(st.games[size_t(sel)], sel);
			cfg.last_rom = st.games[size_t(sel)].path;
			cfg.last_tab = stab;
			st.tab_sel[stab] = cfg.last_rom;
			cfg.Save();
			return st.games[size_t(sel)].path;
		}
		if ((down & SCE_PAD_BUTTON_R3) && n > 0)
		{
			// the game's case in 3D (front, back, spine, the header's details): X there plays it, L1 / R1 open the case of
			// the game before / after it (the shelf's selection follows)
			for (;;)
			{
				const GameInfo game = st.games[size_t(sel)];
				OrbisLog("[shelf] case view: %s", game.path.c_str());
				const CaseChoice c = CaseView(game, st.slots[size_t(sel)], sel > 0, sel + 1 < n);
				if (c == CaseChoice::Play && !ask_resume(game))
					continue; // back to the case
				if (c == CaseChoice::Play)
				{
					launch_anim(game, sel);
					cfg.last_rom = game.path;
					cfg.last_tab = stab;
					st.tab_sel[stab] = cfg.last_rom;
					cfg.Save();
					return game.path;
				}
				if (c == CaseChoice::Back)
					break;
				sel = std::clamp(sel + (c == CaseChoice::Next ? 1 : -1), 0, n - 1);
				st.covers.SetFocus(sel);
				st.covers.Collect(st.slots, kKeepRadius);
			}
			pos = float(sel);
			sel_since = Now();
			ps5input::Poll();
			prev = ps5input::Pad(0).buttons;
			last = Now();
			dirty = true;
		}
		if ((down & SCE_PAD_BUTTON_SQUARE) && stab == tabs::Recent && n > 0)
		{
			// 3.9.9: the game leaves the Recent tab (not the library); the selection goes to the next one
			const std::string gone = st.games[size_t(sel)].path;
			const std::string keep = sel + 1 < n ? st.games[size_t(sel + 1)].path : sel > 0 ? st.games[size_t(sel - 1)].path : "";
			recent::Remove(gone);
			OrbisLog("[shelf] off the Recent tab: %s", gone.c_str());
			tab_chosen = !keep.empty(); // the last one gone: the tab goes away, on to Internal
			OpenTab(keep.empty() ? int(tabs::Internal) : int(tabs::Recent), keep, false);
			dirty = true;
		}
		else if ((down & SCE_PAD_BUTTON_SQUARE) && stab != tabs::Recent && letters_have[0] > 0)
		{
			// 3.9.7: search by name: the keyboard, then the games that match (Circle clears it)
			std::string text = search;
			if (EditText(T("Search games"), text, false, 40))
			{
				while (!text.empty() && text.back() == ' ')
					text.pop_back();
				while (!text.empty() && text[0] == ' ')
					text.erase(0, 1);
				const std::string keep = n > 0 ? st.games[size_t(sel)].path : cfg.last_rom;
				search = text;
				Fill(keep, false);
				OrbisLog("[shelf] search \"%s\": %d game(s)", search.c_str(), n);
			}
			ps5input::Poll();
			prev = ps5input::Pad(0).buttons;
			dirty = true;
		}
		if ((down & SCE_PAD_BUTTON_CIRCLE) && !search.empty())
		{
			const std::string keep = n > 0 ? st.games[size_t(sel)].path : cfg.last_rom;
			search.clear();
			Fill(keep, false);
			dirty = true;
		}
		if ((down & SCE_PAD_BUTTON_TRIANGLE) || touch_menu)
		{
			const int sort_was = cfg.shelf_sort;
			SettingsMenu();
			if (cfg.shelf_sort != sort_was) // 3.11: another order: the same game stays selected
			{
				OrbisLog("[shelf] order %d", cfg.shelf_sort);
				Fill(n > 0 ? st.games[size_t(sel)].path : std::string(), false);
			}
			if (st.downloads != cfg.covers_download)
			{
				st.downloads = cfg.covers_download;
				st.covers.Start(st.games, cfg.covers_download && ShelfDownloads(), cfg.covers_download && BackgroundCovers());
				st.slots.assign(st.games.size(), nullptr);
			}
			ps5input::Poll();
			prev = ps5input::Pad(0).buttons;
			dirty = true;
		}
		if (down & SCE_PAD_BUTTON_OPTIONS)
		{
			if (Confirm("Quit SSYmirPS5?"))
			{
				cfg.last_tab = stab;
				cfg.Save();
				return "";
			}
			ps5input::Poll();
			prev = ps5input::Pad(0).buttons;
			dirty = true;
		}

		// -- covers that arrived
		const size_t before = std::count_if(st.slots.begin(), st.slots.end(), [](const CoverPtr& c) { return c != nullptr; });
		st.covers.Collect(st.slots, kKeepRadius);
		const size_t after = std::count_if(st.slots.begin(), st.slots.end(), [](const CoverPtr& c) { return c != nullptr; });
		if (after != before)
			dirty = true;

		// -- glide
		const float target = float(sel);
		if (std::abs(target - pos) > 0.0005f)
		{
			pos += (target - pos) * (1.0f - std::exp(-dt * 11.0f));
			if (std::abs(target - pos) < 0.0005f)
				pos = target;
			dirty = true;
		}

		// -- backdrop follows the selection once it rests for a moment
		const CoverTex* sel_tex = (n > 0 && st.slots[size_t(sel)]) ? st.slots[size_t(sel)].get() : nullptr;
		if (sel_tex && sel_tex != bd_cur.from && sel_tex != bd_wanted && now - sel_since > 0.12)
		{
			bd_wanted = sel_tex;
			std::swap(bd_old, bd_cur);
			BuildBackdrop(pool, bd_cur, sel_tex);
			fade = bd_old.px.empty() ? 1.0f : 0.0f;
			dirty = true;
		}
		if (fade < 1.0f)
		{
			fade = std::min(1.0f, fade + dt * 4.0f);
			dirty = true;
		}

		const int status = (st.covers.ToDownload() + st.covers.BackgroundLeft()) * 1000 + st.covers.Downloaded() +
						   (st.covers.BackgroundOffline() ? 1 << 30 : 0) + int(std::hash<std::string>()(launchbox::Progress()) & 0xffff) * 7;
		if (status != last_status)
		{
			last_status = status;
			dirty = true;
		}

		if (!dirty)
		{
			// nothing moved: flip a 1-pixel change so the loop still runs at the display's pace
			ps5video::Present(0, 0, 1, 1, true);
			continue;
		}
		dirty = false;

		// -- place the covers, far ones first
		std::vector<Placed> placed;
		if (n > 0)
		{
			const int lo = std::max(0, int(std::floor(pos - kVisible)));
			const int hi = std::min(n - 1, int(std::ceil(pos + kVisible)));
			for (int i = lo; i <= hi; i++)
			{
				const CoverTex* tex = st.slots[size_t(i)] ? st.slots[size_t(i)].get() : LoadingCard().get();
				Placed p;
				if (Place(p, tex, float(i) - pos))
					placed.push_back(p);
			}
			std::sort(placed.begin(), placed.end(), [](const Placed& a, const Placed& b) { return std::abs(a.d) > std::abs(b.d); });
		}
		uint32_t* surf = ps5video::Surface();
		const uint32_t fade_w = uint32_t(fade * 256);
		const double t_render = Now();
		if (gpu_shelf)
		{
			std::vector<ps5gpu::SceneQuad> quads;
			const bool old_too = fade_w < 256 && !bd_old.px.empty();
			if (old_too && ps5gpu::SceneTexture(bd_old.id, bd_old.px.data(), W, H, false))
			{
				quads.emplace_back();
				FullQuad(quads.back(), bd_old.id, 1.0f);
			}
			if (ps5gpu::SceneTexture(bd_cur.id, bd_cur.px.data(), W, H, false))
			{
				quads.emplace_back();
				FullQuad(quads.back(), bd_cur.id, old_too ? fade : 1.0f);
			}
			if (n > 0)
			{
				// the covers near the selection, in a flat row (2.7)
				const int lo = std::max(0, int(std::floor(pos)) - 6), hi = std::min(n - 1, int(std::ceil(pos)) + 6);
				std::vector<const CoverTex*> row;
				for (int i = lo; i <= hi; i++)
					row.push_back(st.slots[size_t(i)] ? st.slots[size_t(i)].get() : LoadingCard().get());
				RowQuads(row, lo, pos, quads);
			}
			ps5gpu::SetScene(quads);
			// what the GPU is given, once a session (the 2.0 shelf drew nothing on a PS5: this says what it had)
			static int scene_logs = 0;
			if (scene_logs < 2 && !placed.empty() && (scene_logs == 0 || now - sel_since > 1.0))
			{
				scene_logs++;
				const ps5gpu::SceneQuad* c = nullptr;
				for (const ps5gpu::SceneQuad& q : quads)
					if (q.mode == ps5gpu::kQuadImage && q.w[0] > 1.5f)
						c = &q;
				OrbisLog("[shelf] GPU scene: %zu quad(s) for %zu cover(s); a cover at x %.0f..%.0f, y %.0f..%.0f, w %.0f..%.0f",
					quads.size(), placed.size(), c ? c->x[0] : 0.f, c ? c->x[1] : 0.f, c ? c->y[0] : 0.f, c ? c->y[2] : 0.f,
					c ? c->w[0] : 0.f, c ? c->w[1] : 0.f);
			}
			// the surface: only the text over the scene (all of the 4K surface)
			const int k = ps5video::PixelScale(), sw = ps5video::SurfaceWidth();
			pool.Run([&](int x0, int x1) {
				for (int y = 0; y < H * k; y++)
					memset(surf + size_t(y) * sw + size_t(x0) * k, 0, size_t(x1 - x0) * k * 4);
			});
		}
		else
		{
		// without the GPU's scene the CPU draws the shelf at 1920x1080 (on a 4K surface: then doubled)
		uint32_t* const out_surf = surf;
		static std::vector<uint32_t> half;
		if (ps5video::PixelScale() == 2)
		{
			half.resize(size_t(W) * H);
			surf = half.data();
		}
		pool.Run([&](int x0, int x1) {
			// backdrop
			for (int y = 0; y < H; y++)
			{
				uint32_t* out = surf + size_t(y) * W;
				const uint32_t* a = bd_cur.px.data() + size_t(y) * W;
				if (fade_w >= 256 || bd_old.px.empty())
					memcpy(out + x0, a + x0, size_t(x1 - x0) * 4);
				else
				{
					const uint32_t* o = bd_old.px.data() + size_t(y) * W;
					for (int x = x0; x < x1; x++)
						out[x] = Lerp32(o[x], a[x], fade_w);
				}
			}
			// the covers in their flat row (2.7)
			if (n > 0)
			{
				const int lo = std::max(0, int(std::floor(pos)) - 6), hi = std::min(n - 1, int(std::ceil(pos)) + 6);
				std::vector<const CoverTex*> row;
				for (int i = lo; i <= hi; i++)
					row.push_back(st.slots[size_t(i)] ? st.slots[size_t(i)].get() : LoadingCard().get());
				RowCpu(surf, row, lo, pos, x0, x1);
			}
		});
		if (surf != out_surf)
		{
			const int sw = ps5video::SurfaceWidth();
			for (int y = 0; y < H; y++)
				for (int x = 0; x < W; x++)
				{
					const uint32_t c = surf[size_t(y) * W + x];
					uint32_t* o = out_surf + size_t(2 * y) * sw + 2 * x;
					o[0] = o[1] = o[sw] = o[sw + 1] = c;
				}
			surf = out_surf;
		}
		}

		{
			static double sum = 0;
			static int frames = 0;
			sum += Now() - t_render;
			if (++frames == 60)
			{
				OrbisLog("[shelf] 3D pass (%s): %.2f ms a frame (average of 60)", gpu_shelf ? "GPU quads" : "CPU",
					sum / frames * 1000.0);
				sum = 0;
				frames = 0;
			}
		}
		// -- text (2.7's look)
		Header();
		DrawTabs(counts, stab);
		if (!search.empty())
			DrawSearchBar(search, n);
		else if (letters_have[0] > 0)
			DrawLetterBar(letters_have, letter);
		if (n > 0)
		{
			char count[64];
			snprintf(count, sizeof(count), "%d / %d", sel + 1, n);
			TextMid(W - 60, 46, count, 17, style::Lo(), Weight::Medium, Align::Right);
			const GameInfo& g = st.games[size_t(sel)];
			const std::string title = FitTextPt(g.title, 48, W - 240, Weight::SemiBold);
			TextMid(W / 2, 822, title.c_str(), 48, style::Hi(), Weight::SemiBold, Align::Center);
			// 3.2: under the title, the game's file and whether it is a multi-disc game
			const FileLine& fl = FileLineOf(g);
			char discs[64];
			if (fl.discs > 1)
				snprintf(discs, sizeof(discs), T("Multi-disc (%d discs)"), fl.discs);
			else
				snprintf(discs, sizeof(discs), "%s", T("1 disc"));
			const std::string sep = "   \xC2\xB7   ";
			if (stab == tabs::Recent)
			{
				// 3.9.9: when it was played, and whether it was left with a state to continue from
				std::string sub = recent::PlayedAgo(recent::When(g.path));
				const std::string time = playtime::Format(playtime::Seconds(g.path)); // 3.11
				if (!time.empty())
					sub += sep + time;
				const auto saved = left_saved.find(g.path);
				if (saved != left_saved.end() && saved->second)
					sub += sep + T("Saved on leaving");
				TextMid(W / 2, 870, sub.c_str(), 19, style::Lo(), Weight::Medium, Align::Center);
			}
			else
			{
			const int room = W - 240 - TextWidthPt((sep + discs).c_str(), 19, Weight::Medium);
			const std::string sub = FitTextPt(fl.file, 19, room, Weight::Medium) + sep + discs;
			TextMid(W / 2, 870, sub.c_str(), 19, style::Lo(), Weight::Medium, Align::Center);
			}
		}
		else if (!search.empty())
		{
			CenterRaw(420, ShelfFmt(T("No game matches \"%s\"."), search), 5, Rgb(240, 240, 250));
			CenterRaw(500, std::string(icon::Square) + "  " + T("Search again") + "        " + icon::Circle + "  " + T("Clear search"), 3,
				style::Accent());
		}
		else
		{
			CenterText(300, "No games found.", 5, Rgb(240, 240, 250));
			CenterText(400, "Copy your Sega Saturn discs to", 3, Rgb(200, 200, 220));
			CenterText(450, "/data/ssymirps5/games   or   ssymirps5/games  on a USB drive", 4, style::Accent());
			CenterText(520, ".cue (with its .bin tracks)   .chd   .iso   .ccd   .mds   .m3u (multi-disc)", 3,
				Rgb(185, 180, 210));
			CenterText(570, "Other folders: add games=<folder> and bios=<folder> lines to /data/ssymirps5/paths.txt", 3,
				Rgb(185, 180, 210));
			CenterText(640, "The Saturn BIOS (sega_101.bin or mpr-17933.bin): /data/ssymirps5/bios or ssymirps5/bios on a USB drive.", 3,
				Rgb(200, 200, 220));

		}
		// download status
		const std::string lb_progress = launchbox::Progress();
		if (!lb_progress.empty() && cfg.covers_download)
			TextMid(W - 60, 76, lb_progress.c_str(), 15, style::Lo(), Weight::Regular, Align::Right);
		else if (st.covers.ToDownload() > 0 && !st.covers.Offline() && cfg.covers_download)
		{
			char s[96];
			snprintf(s, sizeof(s), T("Downloading covers... %d left"), st.covers.ToDownload());
			TextMid(W - 60, 76, s, 15, style::Lo(), Weight::Regular, Align::Right);
		}
		else if (st.covers.BackgroundLeft() > 0 && !st.covers.BackgroundOffline() && cfg.covers_download)
		{
			char s[96];
			snprintf(s, sizeof(s), T("Downloading covers in the background... %d left"), st.covers.BackgroundLeft());
			TextMid(W - 60, 76, s, 15, style::Lo(), Weight::Regular, Align::Right);
		}
		else if ((st.covers.Offline() || (st.covers.BackgroundLeft() > 0 && st.covers.BackgroundOffline())) &&
				 cfg.covers_download)
		{
			const char* s = T("Offline: covers next time");
			TextMid(W - 60, 76, s, 15, style::Lo(), Weight::Regular, Align::Right);
		}
		// hints (2.7: evenly spread, each icon apart)
		{
			std::vector<style::Hint> h;
			if (n > 0)
			{
				// 3.9.7: Square searches (3.3: it no longer fetched the cover); while searching, Circle clears it (no letters)
				h = {{{icon::Cross}, T("Play")}, {{icon::R3}, T("3D case")}, {{icon::DpadLeftRight}, T("Browse")}};
				const bool letters = search.empty() && stab != tabs::Recent;
				if (letters)
					h.push_back({{icon::DpadUpDown}, T("Letter")});
				h.push_back({{icon::L1, icon::R1}, T("Tabs")});
				if (letters && cfg.shelf_sort == tabs::ByName)
					h.push_back({{icon::L2, icon::R2}, T("Jump letter")});
				h.push_back({{icon::Square}, stab == tabs::Recent ? T("Remove from Recent") : T("Search")});
				if (!search.empty())
					h.push_back({{icon::Circle}, T("Clear search")});
				h.push_back({{icon::Triangle}, T("Settings")});
				h.push_back({{icon::Options}, T("Quit")});
			}
			else
			{
				h = {{{icon::L1, icon::R1}, T("Tabs")}};
				if (letters_have[0] > 0 && search.empty())
					h.push_back({{icon::DpadUpDown}, T("Letter")});
				if (!search.empty())
				{
					h.push_back({{icon::Square}, T("Search again")});
					h.push_back({{icon::Circle}, T("Clear search")});
				}
				h.push_back({{icon::Triangle}, T("Settings")});
				h.push_back({{icon::Options}, T("Quit")});
			}
			style::Hints(h);
		}

		ps5video::Present(0, 0, 0, 0, true);
	}
}
} // namespace fe
