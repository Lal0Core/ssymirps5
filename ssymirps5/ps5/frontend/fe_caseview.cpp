// SSYmirPS5 frontend: the game's case in 3D (fe_caseview.h).
// SPDX-License-Identifier: MIT

#include "fe_caseview.h"
#include "fe_casepanel.h"

#include "fe_backsources.h"
#include "fe_launchbox.h"
#include "fe_playtime.h"
#include "fe_recent.h"
#include "fe_tlshttp.h"

#include "fe_artmatch.h"
#include "fe_coverfetch.h"
#include "fe_http.h"
#include "fe_i18n.h"
#include "fe_romheader.h"
#include "fe_text.h"
#include "fe_style.h"
#include "fe_ui.h"

#include "OrbisPaths.h"
#include "ProsperoGpu.h"
#include "ProsperoInput.h"
#include "ProsperoSce.h"
#include "ProsperoThread.h"
#include "ProsperoVideo.h"

#include <avif/avif.h>

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <deque>
#include <map>
#include <memory>
#include <mutex>

#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_ONLY_PNG
#define STBI_NO_STDIO
#define STBI_MAX_DIMENSIONS 8192
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "third_party/stb_image.h"
#pragma GCC diagnostic pop

namespace fe
{
namespace
{
using ps5video::Rgb;
using ui::Now;
constexpr int W = ps5video::kWidth; // layout points
constexpr int H = ps5video::kHeight;
int SW = W, SH = H; // the surface's pixels (2.7: 3840x2160 with the GPU), set when the case opens
constexpr int kTexH = 900; // the faces' pictures are scaled to about this height (the case is ~780 px tall)
constexpr size_t kMaxFile = 16u << 20;
constexpr long kMissingDays = 30;
constexpr float kPi = 3.14159265358979f;

struct Img
{
	int w = 0, h = 0;
	std::vector<uint32_t> px; // row-major A8B8G8R8
	bool ok() const { return w > 0 && h > 0; }
};
using ImgPtr = std::shared_ptr<const Img>;

inline uint32_t Pack(int r, int g, int b)
{
	return Rgb(uint8_t(std::clamp(r, 0, 255)), uint8_t(std::clamp(g, 0, 255)), uint8_t(std::clamp(b, 0, 255)));
}

bool ReadAll(const std::string& path, std::vector<uint8_t>& out)
{
	FILE* f = fopen(path.c_str(), "rb");
	if (!f)
		return false;
	out.clear();
	uint8_t buf[65536];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), f)) > 0)
	{
		out.insert(out.end(), buf, buf + n);
		if (out.size() > kMaxFile)
		{
			fclose(f);
			return false;
		}
	}
	fclose(f);
	return !out.empty();
}

bool NonEmptyFile(const std::string& path)
{
	struct stat st = {};
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && st.st_size > 0;
}

std::string Lower(std::string s)
{
	for (char& c : s)
		if (c >= 'A' && c <= 'Z')
			c = char(c - 'A' + 'a');
	return s;
}

std::string ExtLower(const std::string& path)
{
	const size_t dot = path.find_last_of('.');
	const size_t slash = path.find_last_of('/');
	if (dot == std::string::npos || (slash != std::string::npos && dot < slash))
		return "";
	return Lower(path.substr(dot));
}

// Halves until about `target` tall, then a bilinear step to it exactly; alpha laid over the case's plastic.
Img Fit(const uint32_t* src, int w, int h, int target)
{
	Img cur;
	cur.w = w;
	cur.h = h;
	cur.px.assign(src, src + size_t(w) * h);
	for (uint32_t& p : cur.px)
	{
		const uint32_t a = p >> 24;
		if (a == 255)
			continue;
		auto mix = [a](uint32_t c, uint32_t bg) { return (c * a + bg * (255 - a)) / 255; };
		p = Pack(int(mix(p & 0xff, 30)), int(mix((p >> 8) & 0xff, 30)), int(mix((p >> 16) & 0xff, 34)));
	}
	while (cur.h >= target * 2 && cur.w >= 4)
	{
		Img next;
		next.w = cur.w / 2;
		next.h = cur.h / 2;
		next.px.resize(size_t(next.w) * next.h);
		for (int y = 0; y < next.h; y++)
			for (int x = 0; x < next.w; x++)
			{
				const uint32_t* r0 = &cur.px[size_t(2 * y) * cur.w + 2 * x];
				const uint32_t* r1 = r0 + cur.w;
				uint32_t out = 0xff000000u;
				for (int sh = 0; sh < 24; sh += 8)
					out |= ((((r0[0] >> sh) & 0xff) + ((r0[1] >> sh) & 0xff) + ((r1[0] >> sh) & 0xff) + ((r1[1] >> sh) & 0xff) + 2) / 4) << sh;
				next.px[size_t(y) * next.w + x] = out;
			}
		cur = std::move(next);
	}
	if (cur.h <= target)
		return cur;
	Img out;
	out.h = target;
	out.w = std::max(1, int(std::lround(double(cur.w) * target / cur.h)));
	out.px.resize(size_t(out.w) * out.h);
	const float sx = float(cur.w) / out.w, sy = float(cur.h) / out.h;
	for (int y = 0; y < out.h; y++)
	{
		const float fy = std::clamp((y + 0.5f) * sy - 0.5f, 0.0f, float(cur.h - 1));
		const int y0 = int(fy), y1 = std::min(cur.h - 1, y0 + 1);
		const uint32_t wy = uint32_t((fy - y0) * 256);
		for (int x = 0; x < out.w; x++)
		{
			const float fx = std::clamp((x + 0.5f) * sx - 0.5f, 0.0f, float(cur.w - 1));
			const int x0 = int(fx), x1 = std::min(cur.w - 1, x0 + 1);
			const uint32_t wx = uint32_t((fx - x0) * 256);
			const uint32_t a = cur.px[size_t(y0) * cur.w + x0], b = cur.px[size_t(y0) * cur.w + x1];
			const uint32_t c = cur.px[size_t(y1) * cur.w + x0], d = cur.px[size_t(y1) * cur.w + x1];
			uint32_t o = 0xff000000u;
			for (int sh = 0; sh < 24; sh += 8)
			{
				const uint32_t top = ((a >> sh) & 0xff) * (256 - wx) + ((b >> sh) & 0xff) * wx;
				const uint32_t bot = ((c >> sh) & 0xff) * (256 - wx) + ((d >> sh) & 0xff) * wx;
				o |= ((top * (256 - wy) + bot * wy) >> 16) << sh;
			}
			out.px[size_t(y) * out.w + x] = o;
		}
	}
	return out;
}

// A PNG, JPEG or AVIF file in memory -> a face picture.
bool DecodeImage(const std::vector<uint8_t>& data, const std::string& ext, Img* out)
{
	std::vector<uint32_t> rgba;
	int w = 0, h = 0;
	if (ext == ".avif" || (data.size() > 12 && memcmp(&data[4], "ftyp", 4) == 0))
	{
		if (!DecodeAvif(data.data(), data.size(), &rgba, &w, &h))
			return false;
	}
	else
	{
		int comp = 0;
		stbi_uc* px = stbi_load_from_memory(data.data(), int(data.size()), &w, &h, &comp, 4);
		if (!px)
			return false;
		rgba.assign(reinterpret_cast<uint32_t*>(px), reinterpret_cast<uint32_t*>(px) + size_t(w) * h);
		stbi_image_free(px);
	}
	if (w < 16 || h < 16)
		return false;
	*out = Fit(rgba.data(), w, h, kTexH);
	return true;
}

bool LoadImageFile(const std::string& path, Img* out)
{
	std::vector<uint8_t> data;
	return ReadAll(path, data) && DecodeImage(data, ExtLower(path), out);
}

std::string FindImage(const std::string& base)
{
	for (const char* ext : {".png", ".jpg", ".jpeg", ".PNG", ".JPG", ".avif"})
		if (NonEmptyFile(base + ext))
			return base + ext;
	return "";
}

// ---- the back covers' downloads: one thread, one product number at a time, only when a case is opened ----------
enum class BackState
{
	Waiting,
	Ok,
	NotFound, // no address had it
	Failed, // no network, or the file could not be read
};

struct BackResult
{
	BackState state = BackState::Waiting;
	ImgPtr img;
	std::string from; // "chromagi.com (downloaded)"...
	ImgPtr spine; // LaunchBox's spine of the box, when it has one
};

std::string BackDir()
{
	return OrbisDir("covers") + "/Saturn/back";
}

std::string SourcesPath()
{
	return OrbisDir("covers") + "/back-sources.txt";
}

const char* kDefaultSources =
	"# SSYmirPS5: where the back covers are downloaded from, tried in order (one address per line; # starts a comment).\n"
	"# ${code} is the disc's product number in lower case (gs-9198), ${CODE} as the disc writes it (GS-9198).\n"
	"# A back cover is downloaded only when you open a game's case (R3 on the shelf), and kept in\n"
	"# covers/Saturn/back/<code>.avif (delete it to download it again).\n"
	"# Chromagi names its back covers \"boxback\" (2.1 asked only for \"backcover\", which Chromagi doesn't use).\n"
	"https://chromagi.com/assets/ss/soft/${code}/${code}.boxback.avif\n"
	"https://chromagi.com/assets/ss/soft/${code}/${code}.backcover.avif\n";
const char* kOldChromagi = "https://chromagi.com/assets/ss/soft/${code}/${code}.backcover.avif";
const char* kBoxBack = "https://chromagi.com/assets/ss/soft/${code}/${code}.boxback.avif";

std::vector<std::string> ReadSources()
{
	std::vector<uint8_t> data;
	if (!ReadAll(SourcesPath(), data))
	{
		OrbisMkdirs(OrbisDir("covers"));
		WriteFileAtomicTo(SourcesPath(), std::vector<uint8_t>(kDefaultSources, kDefaultSources + strlen(kDefaultSources)));
		data.assign(kDefaultSources, kDefaultSources + strlen(kDefaultSources));
	}
	else
	{
		// 2.1's list: Chromagi's real name ("boxback") goes in before its line
		std::string text(data.begin(), data.end());
		const size_t at = text.find(kOldChromagi);
		if (at != std::string::npos && text.find(kBoxBack) == std::string::npos)
		{
			text.insert(at, std::string(kBoxBack) + "\n");
			data.assign(text.begin(), text.end());
			WriteFileAtomicTo(SourcesPath(), data);
			OrbisLog("[case] back-sources.txt: Chromagi's \"boxback\" address added");
		}
	}
	std::vector<std::string> out;
	std::string line;
	auto take = [&]() {
		while (!line.empty() && (line.back() == '\r' || line.back() == ' ' || line.back() == '\t'))
			line.pop_back();
		size_t s = 0;
		while (s < line.size() && (line[s] == ' ' || line[s] == '\t'))
			s++;
		line.erase(0, s);
		if (!line.empty() && line[0] != '#' && (line.rfind("https://", 0) == 0 || line.rfind("http://", 0) == 0))
			out.push_back(line);
		line.clear();
	};
	for (uint8_t c : data)
	{
		if (c == '\n')
			take();
		else
			line += char(c);
	}
	take();
	return out;
}

std::string HostOf(const std::string& url)
{
	size_t s = url.find("://");
	s = s == std::string::npos ? 0 : s + 3;
	const size_t e = url.find('/', s);
	std::string host = url.substr(s, e == std::string::npos ? std::string::npos : e - s);
	if (host.rfind("www.", 0) == 0)
		host.erase(0, 4);
	return host;
}

// What a case asks for: its disc, and the name its back cover is kept under.
struct BackQuery
{
	std::string key; // "mk-81014", or "title-virtuafighter2" for a disc without a product number
	backs::Disc disc;
};

class BackFetcher
{
public:
	BackFetcher()
	{
		m_thread = std::make_unique<ps5::BigThread>([this] { Run(); }, size_t(8) << 20);
	}

	// Starts (or finds) the disc's back cover.
	void Request(const BackQuery& q)
	{
		std::lock_guard<std::mutex> l(m_lock);
		if (m_results.count(q.key))
		{
			// a failed try (no network) is tried again when the case is opened again
			if (m_results[q.key].state != BackState::Failed)
				return;
		}
		m_results[q.key] = BackResult();
		m_queue.push_back(q);
		m_wake.notify_all();
	}

	BackResult Get(const std::string& key)
	{
		std::lock_guard<std::mutex> l(m_lock);
		auto it = m_results.find(key);
		return it == m_results.end() ? BackResult() : it->second;
	}

	// What is being done now, for the panel: LaunchBox's database's first download ("" otherwise).
	std::string Doing() { return launchbox::Progress(); }

	// The app exits: an idle thread is joined; one in a download is left to the process' end (a request can wait
	// for its timeouts).
	void Stop()
	{
		m_quit = true;
		m_wake.notify_all();
		if (!m_busy && m_thread)
			m_thread->join();
		else
			(void)m_thread.release(); // left running on purpose
	}

private:
	void Run()
	{
		for (;;)
		{
			BackQuery q;
			{
				std::unique_lock<std::mutex> l(m_lock);
				m_wake.wait(l, [this] { return m_quit || !m_queue.empty(); });
				if (m_quit)
					return;
				q = m_queue.front();
				m_queue.pop_front();
				m_busy = true;
			}
			BackResult r = Fetch(q);
			r.spine = Spine(q);
			{
				std::lock_guard<std::mutex> l(m_lock);
				m_results[q.key] = r;
				m_busy = false;
			}
		}
	}

	// The box's spine: LaunchBox's "Box - Spine" of the game (kept as <key>.spine.<ext>), turned to stand up.
	ImgPtr Spine(const BackQuery& q)
	{
		const std::string dir = BackDir();
		const std::string base = dir + "/" + q.key + ".spine";
		auto load = [](const std::vector<uint8_t>& data, const std::string& ext) -> ImgPtr {
			Img img;
			if (!DecodeImage(data, ext, &img))
				return nullptr;
			if (img.w > img.h)
			{
				// across: a quarter turn clockwise, so it reads downwards as the generated one does
				Img up;
				up.w = img.h;
				up.h = img.w;
				up.px.resize(img.px.size());
				for (int y = 0; y < up.h; y++)
					for (int x = 0; x < up.w; x++)
						up.px[size_t(y) * up.w + x] = img.px[size_t(img.h - 1 - x) * img.w + y];
				img = Fit(up.px.data(), up.w, up.h, kTexH);
			}
			return std::make_shared<Img>(std::move(img));
		};
		for (const char* ext : {".jpg", ".png"})
		{
			std::vector<uint8_t> data;
			if (NonEmptyFile(base + ext) && ReadAll(base + ext, data))
				if (ImgPtr img = load(data, ext))
					return img;
		}
		const std::string missing = base + ".missing";
		struct stat st = {};
		if (stat(missing.c_str(), &st) == 0 && time(nullptr) - st.st_mtime < kMissingDays * 86400)
			return nullptr;
		if (!launchbox::Wait(&m_quit))
			return nullptr;
		const std::vector<backs::Candidate> cands =
			backs::LaunchBoxCandidates(q.disc, launchbox::IndexPath(), launchbox::ImageBase(), "spine");
		if (!m_http)
			m_http = CoverService::MakeShelfHttp();
		bool network_failed = false;
		for (const backs::Candidate& c : cands)
		{
			std::vector<uint8_t> data;
			const int status = m_http->Get(c.url, data, kMaxFile);
			OrbisLog("[case] %s: spine GET %s (%s) -> %d, %zu bytes", q.key.c_str(), c.url.c_str(), c.label.c_str(), status, data.size());
			network_failed = network_failed || status == -1;
			if (status != 200 || data.size() < 64)
				continue;
			const std::string ext = data[0] == 0x89 ? ".png" : ".jpg";
			if (ImgPtr img = load(data, ext))
			{
				WriteFileAtomicTo(base + ext, data);
				return img;
			}
		}
		if (!network_failed)
		{
			const char* note = "no spine\n";
			WriteFileAtomicTo(missing, std::vector<uint8_t>(note, note + strlen(note)));
		}
		return nullptr;
	}

	BackResult Fetch(const BackQuery& q)
	{
		BackResult r;
		const std::string& key = q.key;
		const std::string dir = BackDir();
		OrbisMkdirs(dir);
		// 1. downloaded before
		for (const char* ext : {".avif", ".jpg", ".png"})
		{
			const std::string file = dir + "/" + key + ext;
			if (!NonEmptyFile(file))
				continue;
			auto img = std::make_shared<Img>();
			if (LoadImageFile(file, img.get()))
			{
				r.state = BackState::Ok;
				r.img = img;
				std::vector<uint8_t> from;
				r.from = ReadAll(file + ".from", from) ? std::string(from.begin(), from.end()) : std::string(T("your picture"));
				OrbisLog("[case] %s: back cover from %s (%s)", key.c_str(), file.c_str(), r.from.c_str());
				return r;
			}
			OrbisLog("[case] %s: %s can't be read: downloading it again", key.c_str(), file.c_str());
		}
		// 2. the addresses: LaunchBox (by the game's title), Chromagi (by its number, else its title), yours
		bool network_failed = false;
		const bool have_lb = launchbox::Wait(&m_quit);
		std::vector<backs::Candidate> cands;
		if (have_lb)
			cands = backs::LaunchBoxCandidates(q.disc, launchbox::IndexPath(), launchbox::ImageBase(), "back");
		for (const backs::Candidate& c : backs::ChromagiCandidates(q.disc))
			cands.push_back(c);
		if (!q.disc.product.empty())
			for (const std::string& url : BackCoverUrls(q.disc.product, ReadSources()))
				cands.push_back({url, HostOf(url)});
		std::vector<backs::Candidate> uniq;
		for (const backs::Candidate& c : cands)
		{
			bool dup = false;
			for (const backs::Candidate& u : uniq)
				dup = dup || u.url == c.url;
			if (!dup)
				uniq.push_back(c);
		}
		std::string names;
		for (const std::string& n : backs::LookupNames(q.disc))
			names += (names.empty() ? "" : " / ") + n;
		OrbisLog("[case] %s: the game is \"%s\" (region %s), %zu address(es) to try%s", key.c_str(), names.c_str(),
			backs::PreferredRegion(q.disc).c_str(), uniq.size(), have_lb ? "" : " (no LaunchBox database)");
		// 3. not on the servers a short while ago (at the same addresses: a changed list is tried again)
		const std::string missing = dir + "/" + key + ".missing";
		std::string tried;
		for (const backs::Candidate& c : uniq)
			tried += c.url + "\n";
		struct stat st = {};
		std::vector<uint8_t> was;
		if (!network_failed && stat(missing.c_str(), &st) == 0 && time(nullptr) - st.st_mtime < kMissingDays * 86400 &&
			ReadAll(missing, was) && std::string(was.begin(), was.end()) == tried)
		{
			OrbisLog("[case] %s: no back cover online (%s, less than %ld days old)", key.c_str(), missing.c_str(), kMissingDays);
			r.state = BackState::NotFound;
			return r;
		}
		if (!m_http)
			m_http = CoverService::MakeShelfHttp();
		for (const backs::Candidate& c : uniq)
		{
			if (m_quit)
				return r;
			std::vector<uint8_t> data;
			const double t0 = Now();
			const int status = m_http->Get(c.url, data, kMaxFile);
			OrbisLog("[case] %s: GET %s (%s) -> %d, %zu bytes, %.0f ms", key.c_str(), c.url.c_str(), c.label.c_str(), status,
				data.size(), (Now() - t0) * 1000);
			if (status == -1)
			{
				network_failed = true;
				continue;
			}
			if (status != 200 || data.size() < 64)
				continue;
			auto img = std::make_shared<Img>();
			std::string ext = ExtLower(c.url);
			if (ext == ".jpeg")
				ext = ".jpg";
			if (ext != ".avif" && ext != ".jpg" && ext != ".png")
				ext = (data.size() > 12 && memcmp(&data[4], "ftyp", 4) == 0) ? ".avif" : (data[0] == 0x89 ? ".png" : ".jpg");
			if (!DecodeImage(data, ext, img.get()))
			{
				OrbisLog("[case] %s: %s is not a picture the app can read", key.c_str(), c.url.c_str());
				continue;
			}
			const std::string file = dir + "/" + key + ext;
			if (WriteFileAtomicTo(file, data))
				WriteFileAtomicTo(file + ".from", std::vector<uint8_t>(c.label.begin(), c.label.end()));
			unlink(missing.c_str());
			OrbisLog("[case] %s: back cover %dx%d saved to %s", key.c_str(), img->w, img->h, file.c_str());
			r.state = BackState::Ok;
			r.img = img;
			r.from = c.label;
			return r;
		}
		if (network_failed || uniq.empty())
		{
			r.state = uniq.empty() && !network_failed ? BackState::NotFound : BackState::Failed;
			OrbisLog("[case] %s: no back cover (%s)", key.c_str(), uniq.empty() ? "no address for this game" : "no network");
		}
		else
		{
			r.state = BackState::NotFound;
			WriteFileAtomicTo(missing, std::vector<uint8_t>(tried.begin(), tried.end()));
			OrbisLog("[case] %s: no back cover on the servers", key.c_str());
		}
		return r;
	}

	std::mutex m_lock;
	std::condition_variable m_wake;
	std::deque<BackQuery> m_queue;
	std::map<std::string, BackResult> m_results;
	std::atomic<bool> m_quit{false}, m_busy{false};
	std::unique_ptr<HttpClient> m_http;
	std::unique_ptr<ps5::BigThread> m_thread;
};

std::mutex g_fetcher_lock;
BackFetcher* g_fetcher = nullptr;

BackFetcher& Fetcher()
{
	std::lock_guard<std::mutex> l(g_fetcher_lock);
	if (!g_fetcher)
		g_fetcher = new BackFetcher();
	return *g_fetcher;
}

// ---- the pictures of the faces ---------------------------------------------------------------------------------
// The front: the cover file again at full size (the shelf keeps it at 512 px), else the shelf's copy.
Img LoadFront(const GameInfo& g, const CoverPtr& tex)
{
	Img img;
	std::vector<std::string> files;
	const std::string own = OwnCover(g);
	if (!own.empty())
		files.push_back(own);
	const std::string dir = g.path.substr(0, g.path.find_last_of('/'));
	const std::string beside = FindImage(dir + "/" + g.file_base);
	if (!beside.empty())
		files.push_back(beside);
	const std::string cache = CoverFileFor(g);
	if (!cache.empty())
		files.push_back(OrbisDir("covers") + "/" + cache);
	if (!tex || tex->real)
		for (const std::string& f : files)
			if (NonEmptyFile(f) && LoadImageFile(f, &img))
				return img;
	if (tex && tex->w[0] > 0)
	{
		const int w = tex->w[0], h = tex->h[0];
		std::vector<uint32_t> rows(size_t(w) * h);
		for (int x = 0; x < w; x++)
			for (int y = 0; y < h; y++)
				rows[size_t(y) * w + x] = tex->px[0][size_t(x) * h + y];
		img = Fit(rows.data(), w, h, kTexH);
	}
	return img;
}

// Your own back cover: "<game> (reverso)" / "(back)" beside the game or in covers/Saturn.
std::string OwnBackFile(const GameInfo& g)
{
	std::vector<std::string> names = {g.file_base};
	if (!g.title.empty() && g.title != g.file_base)
		names.push_back(g.title);
	if (!g.nointro.empty())
		names.push_back(g.nointro);
	std::vector<std::string> dirs;
	dirs.push_back(g.path.substr(0, g.path.find_last_of('/')));
	dirs.push_back(OrbisDir("covers") + "/Saturn");
	dirs.push_back(OrbisDir("covers"));
	for (const std::string& d : dirs)
		for (const std::string& n : names)
			for (const char* tag : {" (reverso)", " (back)", " (Back)", " (Reverso)", "_back", "-back"})
			{
				const std::string f = FindImage(d + "/" + n + tag);
				if (!f.empty())
					return f;
			}
	return "";
}

std::vector<std::string> Wrap(const std::string& text, int scale, int max_px, int max_lines)
{
	std::vector<std::string> lines;
	std::string cur;
	size_t i = 0;
	while (i < text.size())
	{
		size_t e = text.find(' ', i);
		if (e == std::string::npos)
			e = text.size();
		const std::string word = text.substr(i, e - i);
		i = e + 1;
		if (word.empty())
			continue;
		const std::string tryline = cur.empty() ? word : cur + " " + word;
		if (TextWidth(tryline.c_str(), scale) <= max_px || cur.empty())
			cur = tryline;
		else
		{
			lines.push_back(cur);
			cur = word;
		}
	}
	if (!cur.empty())
		lines.push_back(cur);
	if (int(lines.size()) > max_lines)
	{
		std::string rest;
		for (size_t k = size_t(max_lines - 1); k < lines.size(); k++)
			rest += (rest.empty() ? "" : " ") + lines[k];
		lines.resize(size_t(max_lines));
		lines.back() = rest;
	}
	for (std::string& l : lines)
		l = FitText(l, scale, max_px);
	return lines;
}

std::string FormatDate(const std::string& yyyymmdd)
{
	if (yyyymmdd.size() != 8)
		return "";
	return yyyymmdd.substr(6, 2) + "/" + yyyymmdd.substr(4, 2) + "/" + yyyymmdd.substr(0, 4);
}

struct Details
{
	std::string title, product, date, regions;
	std::string year; // from the file's name when the disc's header can't be read ("D (1996)(Acclaim)")
	int discs = 1;
};

// The generated back: the front, blurred and dark, with the details on it.
Img MakeBack(const Img& front, int w, int h, const Details& d)
{
	Img out;
	out.w = w;
	out.h = h;
	out.px.assign(size_t(w) * h, Rgb(22, 22, 30));
	if (front.ok())
	{
		// a blur: the front at 1/24, scaled up bilinearly, darkened
		const int sw = std::max(2, front.w / 24), sh = std::max(2, front.h / 24);
		std::vector<uint32_t> small(size_t(sw) * sh);
		for (int y = 0; y < sh; y++)
			for (int x = 0; x < sw; x++)
			{
				uint32_t r = 0, g = 0, b = 0, n = 0;
				for (int yy = y * front.h / sh; yy < (y + 1) * front.h / sh; yy += 2)
					for (int xx = x * front.w / sw; xx < (x + 1) * front.w / sw; xx += 2)
					{
						const uint32_t p = front.px[size_t(yy) * front.w + xx];
						r += p & 0xff;
						g += (p >> 8) & 0xff;
						b += (p >> 16) & 0xff;
						n++;
					}
				n = std::max(1u, n);
				small[size_t(y) * sw + x] = Pack(int(r / n), int(g / n), int(b / n));
			}
		for (int y = 0; y < h; y++)
		{
			const float fy = std::clamp((y + 0.5f) * sh / h - 0.5f, 0.0f, float(sh - 1));
			const int y0 = int(fy), y1 = std::min(sh - 1, y0 + 1);
			const float ty = fy - y0;
			for (int x = 0; x < w; x++)
			{
				const float fx = std::clamp((x + 0.5f) * sw / w - 0.5f, 0.0f, float(sw - 1));
				const int x0 = int(fx), x1 = std::min(sw - 1, x0 + 1);
				const float tx = fx - x0;
				float c[3];
				for (int k = 0; k < 3; k++)
				{
					auto ch = [&](int xx, int yy) { return float((small[size_t(yy) * sw + xx] >> (8 * k)) & 0xff); };
					c[k] = (ch(x0, y0) * (1 - tx) + ch(x1, y0) * tx) * (1 - ty) + (ch(x0, y1) * (1 - tx) + ch(x1, y1) * tx) * ty;
				}
				const float dark = 0.30f - 0.10f * float(y) / h;
				out.px[size_t(y) * w + x] = Pack(int(c[0] * dark + 10), int(c[1] * dark + 10), int(c[2] * dark + 14));
			}
		}
	}
	Canvas cv{out.px.data(), w, h, w};
	// the top band, as Saturn backs have
	for (int y = 0; y < h / 14; y++)
		for (int x = 0; x < w; x++)
			out.px[size_t(y) * w + x] = Rgb(12, 12, 16);
	DrawTextOn(cv, w / 20, h / 56, "SEGA SATURN", std::max(2, h / 300), Rgb(225, 225, 235), false);
	const int pad = w / 12;
	const int tscale = std::max(3, h / 170);
	int y = h / 7;
	for (const std::string& line : Wrap(d.title, tscale, w - 2 * pad, 3))
	{
		DrawTextOn(cv, pad, y, line.c_str(), tscale, Rgb(245, 245, 250), true);
		y += kGlyphH * tscale + tscale * 3;
	}
	y += h / 30;
	const int s = std::max(2, h / 260);
	auto row = [&](const char* label, const std::string& value) {
		if (value.empty())
			return;
		DrawTextOn(cv, pad, y, (std::string(T(label)) + ": " + value).c_str(), s, Rgb(205, 205, 220), true);
		y += kGlyphH * s + s * 5;
	};
	row("Product code", d.product);
	row("Release date", FormatDate(d.date));
	row("Discs", d.discs > 1 ? std::to_string(d.discs) : std::string());
	row("Region", d.regions);
	const int ns = std::max(2, h / 380);
	const std::string note = FitText(T("No back cover image: made by SSYmirPS5"), ns, w - 2 * pad);
	DrawTextOn(cv, pad, h - h / 14, note.c_str(), ns, Rgb(150, 150, 170), true);
	return out;
}

// The spine: dark plastic with the title running down it (as a western spine reads).
Img MakeSpine(int len, int thick, const std::string& title)
{
	// drawn across (len wide, thick tall), then turned a quarter clockwise
	std::vector<uint32_t> across(size_t(len) * thick);
	for (int y = 0; y < thick; y++)
	{
		const float t = float(y) / std::max(1, thick - 1);
		const int v = int(46 + 40 * (1.0f - std::abs(t - 0.45f) * 2.0f));
		for (int x = 0; x < len; x++)
			across[size_t(y) * len + x] = Pack(v, v, v + 4);
	}
	Canvas cv{across.data(), len, thick, len};
	int s = std::max(1, thick * 6 / 10 / kGlyphH);
	// a long title a size smaller (down to half), rather than cut
	for (int k = s; k > std::max(1, s / 2); k--)
	{
		const int sm = std::max(1, k * 2 / 3);
		if (thick / 2 + TextWidth("SEGA SATURN", sm) + thick + TextWidth(title.c_str(), k) <= len - thick / 2)
			break;
		s = k - 1;
	}
	const int ty = (thick - kGlyphH * s) / 2;
	DrawTextOn(cv, thick / 2, ty, "SEGA SATURN", std::max(1, s * 2 / 3), Rgb(170, 170, 185), false);
	const int left = thick / 2 + TextWidth("SEGA SATURN", std::max(1, s * 2 / 3)) + thick;
	DrawTextOn(cv, left, ty, FitText(title, s, len - left - thick / 2).c_str(), s, Rgb(240, 240, 248), false);
	Img out;
	out.w = thick;
	out.h = len;
	out.px.resize(size_t(len) * thick);
	for (int y = 0; y < len; y++)
		for (int x = 0; x < thick; x++)
			out.px[size_t(y) * thick + x] = across[size_t(thick - 1 - x) * len + y];
	return out;
}

Img Solid(uint32_t c)
{
	Img out;
	out.w = out.h = 2;
	out.px.assign(4, c);
	return out;
}

// ---- drawing: a textured quad in perspective --------------------------------------------------------------------
struct V3
{
	float x, y, z;
};
float Dot(V3 a, V3 b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 Norm(V3 a)
{
	const float l = std::sqrt(Dot(a, a));
	return l > 0 ? V3{a.x / l, a.y / l, a.z / l} : a;
}

// The face's picture over the screen quad p[0] top-left, p[1] top-right, p[2] bottom-left, p[3] bottom-right
// (a homography: exactly a plane in perspective). bright: the light on it; sheen: the plastic's reflection.
void DrawQuad(uint32_t* surf, const float px[4], const float py[4], const Img& tex, float bright, float sheen, int band0, int band1)
{
	// unit square (u, v) -> screen: corners (0,0) p0, (1,0) p1, (1,1) p3, (0,1) p2 (Heckbert)
	const double x0 = px[0], y0 = py[0], x1 = px[1], y1 = py[1], x2 = px[3], y2 = py[3], x3 = px[2], y3 = py[2];
	const double dx1 = x1 - x2, dx2 = x3 - x2, dx3 = x0 - x1 + x2 - x3;
	const double dy1 = y1 - y2, dy2 = y3 - y2, dy3 = y0 - y1 + y2 - y3;
	double a, b, c, d, e, f, g, h;
	if (std::abs(dx3) < 1e-9 && std::abs(dy3) < 1e-9)
	{
		a = x1 - x0, b = x2 - x1, c = x0, d = y1 - y0, e = y2 - y1, f = y0, g = 0, h = 0;
	}
	else
	{
		const double det = dx1 * dy2 - dx2 * dy1;
		if (std::abs(det) < 1e-12)
			return;
		g = (dx3 * dy2 - dx2 * dy3) / det;
		h = (dx1 * dy3 - dx3 * dy1) / det;
		a = x1 - x0 + g * x1, b = x3 - x0 + h * x3, c = x0;
		d = y1 - y0 + g * y1, e = y3 - y0 + h * y3, f = y0;
	}
	// its inverse (the adjugate: the scale does not matter)
	const double A = e - f * h, B = c * h - b, C = b * f - c * e;
	const double D = f * g - d, E = a - c * g, F = c * d - a * f;
	const double G = d * h - e * g, Hh = b * g - a * h, I = a * e - b * d;
	const float minx = std::min({px[0], px[1], px[2], px[3]}), maxx = std::max({px[0], px[1], px[2], px[3]});
	const float miny = std::min({py[0], py[1], py[2], py[3]}), maxy = std::max({py[0], py[1], py[2], py[3]});
	const int bx0 = std::max(0, int(std::floor(minx)) - 1), bx1 = std::min(SW - 1, int(std::ceil(maxx)) + 1);
	const int by0 = std::max(std::max(0, band0), int(std::floor(miny)) - 1), by1 = std::min(std::min(SH - 1, band1 - 1), int(std::ceil(maxy)) + 1);
	if (bx0 > bx1 || by0 > by1)
		return;
	const int tw = tex.w, th = tex.h;
	const uint32_t* tp = tex.px.data();
	const int bri = int(std::clamp(bright, 0.0f, 1.6f) * 256);
	for (int y = by0; y <= by1; y++)
	{
		const double sy = y + 0.5;
		uint32_t* row = surf + size_t(y) * SW;
		double U = A * (bx0 + 0.5) + B * sy + C;
		double V = D * (bx0 + 0.5) + E * sy + F;
		double Wt = G * (bx0 + 0.5) + Hh * sy + I;
		for (int x = bx0; x <= bx1; x++, U += A, V += D, Wt += G)
		{
			if (std::abs(Wt) < 1e-12)
				continue;
			const double iw = 1.0 / Wt;
			const float u = float(U * iw), v = float(V * iw);
			// the pixel's size in (u, v): for the anti-aliased edges
			const float dudx = std::abs(float((A - u * G) * iw)), dudy = std::abs(float((B - u * Hh) * iw));
			const float dvdx = std::abs(float((D - v * G) * iw)), dvdy = std::abs(float((E - v * Hh) * iw));
			const float pu = std::max(1e-6f, dudx + dudy), pv = std::max(1e-6f, dvdx + dvdy);
			const float cu = std::min(u, 1.0f - u) / pu + 0.5f, cv = std::min(v, 1.0f - v) / pv + 0.5f;
			if (cu <= 0.0f || cv <= 0.0f)
				continue;
			const float cover = std::min(1.0f, std::min(cu, cv));
			// bilinear
			const float fx = std::clamp(u * tw - 0.5f, 0.0f, float(tw - 1)), fy = std::clamp(v * th - 0.5f, 0.0f, float(th - 1));
			const int ix = int(fx), iy = int(fy);
			const int ix1 = std::min(tw - 1, ix + 1), iy1 = std::min(th - 1, iy + 1);
			const uint32_t wx = uint32_t((fx - ix) * 256), wy = uint32_t((fy - iy) * 256);
			const uint32_t p00 = tp[size_t(iy) * tw + ix], p10 = tp[size_t(iy) * tw + ix1];
			const uint32_t p01 = tp[size_t(iy1) * tw + ix], p11 = tp[size_t(iy1) * tw + ix1];
			// the sheen: a soft diagonal band of light across the plastic
			const float band = 1.0f - std::abs(u * 0.8f + v * 0.55f - 0.42f) * 3.2f;
			const int add = sheen > 0 && band > 0 ? int(band * band * sheen * 255) : 0;
			const uint32_t cov = uint32_t(cover * 256);
			const uint32_t dst = row[x];
			uint32_t o = 0xff000000u;
			for (int sh = 0; sh < 24; sh += 8)
			{
				const uint32_t top = ((p00 >> sh) & 0xff) * (256 - wx) + ((p10 >> sh) & 0xff) * wx;
				const uint32_t bot = ((p01 >> sh) & 0xff) * (256 - wx) + ((p11 >> sh) & 0xff) * wx;
				int s = int(((top * (256 - wy) + bot * wy) >> 16) * uint32_t(bri) >> 8) + add;
				s = std::min(255, s);
				const uint32_t dd = (dst >> sh) & 0xff;
				o |= ((uint32_t(s) * cov + dd * (256 - cov)) >> 8) << sh;
			}
			row[x] = o;
		}
	}
}

struct Face
{
	V3 c[4]; // top-left, top-right, bottom-left, bottom-right (seen from outside)
	V3 n;
	const Img* tex;
	float sheen;
};

struct Camera
{
	float yaw, pitch;
	float focal = 1500.0f, dist = 1500.0f;
	float cx, cy;
	V3 Turn(V3 p) const
	{
		const float cy_ = std::cos(yaw), sy = std::sin(yaw);
		V3 q{p.x * cy_ + p.z * sy, p.y, -p.x * sy + p.z * cy_};
		const float cp = std::cos(pitch), sp = std::sin(pitch);
		return {q.x, q.y * cp - q.z * sp, q.y * sp + q.z * cp};
	}
};

void DrawCase(uint32_t* surf, const Camera& cam, float hx, float hy, float hz, const Img& front, const Img& back,
	const Img& spine, const Img& plastic)
{
	const Face faces[6] = {
		{{{-hx, -hy, -hz}, {hx, -hy, -hz}, {-hx, hy, -hz}, {hx, hy, -hz}}, {0, 0, -1}, &front, 0.16f},
		{{{hx, -hy, hz}, {-hx, -hy, hz}, {hx, hy, hz}, {-hx, hy, hz}}, {0, 0, 1}, &back, 0.16f},
		{{{-hx, -hy, hz}, {-hx, -hy, -hz}, {-hx, hy, hz}, {-hx, hy, -hz}}, {-1, 0, 0}, &spine, 0.0f},
		{{{hx, -hy, -hz}, {hx, -hy, hz}, {hx, hy, -hz}, {hx, hy, hz}}, {1, 0, 0}, &plastic, 0.0f},
		{{{-hx, -hy, hz}, {hx, -hy, hz}, {-hx, -hy, -hz}, {hx, -hy, -hz}}, {0, -1, 0}, &plastic, 0.0f},
		{{{-hx, hy, -hz}, {hx, hy, -hz}, {-hx, hy, hz}, {hx, hy, hz}}, {0, 1, 0}, &plastic, 0.0f},
	};
	const V3 light = Norm({-0.35f, -0.45f, -1.0f}); // toward the light: up, left, the viewer's side
	for (const Face& fc : faces)
	{
		V3 p[4];
		for (int k = 0; k < 4; k++)
		{
			p[k] = cam.Turn(fc.c[k]);
			p[k].z += cam.dist;
			if (p[k].z < 50.0f)
				return; // too close (can't happen with the angles allowed)
		}
		const V3 n = cam.Turn(fc.n);
		const V3 mid{(p[0].x + p[3].x) / 2, (p[0].y + p[3].y) / 2, (p[0].z + p[3].z) / 2};
		if (Dot(n, mid) >= 0)
			continue; // facing away
		float sx[4], sy[4];
		for (int k = 0; k < 4; k++)
		{
			sx[k] = cam.cx + cam.focal * p[k].x / p[k].z;
			sy[k] = cam.cy + cam.focal * p[k].y / p[k].z;
		}
		const float lit = 0.58f + 0.50f * std::max(0.0f, Dot(n, light));
		// a face turned toward the viewer catches the sheen; one seen edge-on less so
		const float facing = std::max(0.0f, -n.z);
		// 2.7: at 4K the rows are shared out among four threads
		constexpr int kThreads = 4;
		std::vector<std::unique_ptr<ps5::BigThread>> th;
		const float lo = std::min({sy[0], sy[1], sy[2], sy[3]}), hi = std::max({sy[0], sy[1], sy[2], sy[3]});
		const int y0 = std::max(0, int(lo) - 2), y1 = std::min(SH, int(hi) + 3);
		const int step = std::max(1, (y1 - y0 + kThreads - 1) / kThreads);
		for (int t = 1; t < kThreads; t++)
		{
			const int a = y0 + t * step, b = std::min(y1, a + step);
			if (a < b)
				th.push_back(std::make_unique<ps5::BigThread>(
					[=] { DrawQuad(surf, sx, sy, *fc.tex, lit, fc.sheen * facing, a, b); }, size_t(256) << 10));
		}
		DrawQuad(surf, sx, sy, *fc.tex, lit, fc.sheen * facing, y0, std::min(y1, y0 + step));
		for (auto& t : th)
			t->join();
	}
}

// The backdrop: a dark gradient tinted by the cover, a soft shadow under the case.
void MakeBackdrop(std::vector<uint32_t>& out, uint32_t tint, float shadow_cx, float shadow_cy, float shadow_rx)
{
	// made at 1920x1080 and doubled onto a 4K surface (it is all soft gradients)
	std::vector<uint32_t> half(size_t(W) * H);
	const float tr = float(tint & 0xff), tg = float((tint >> 8) & 0xff), tb = float((tint >> 16) & 0xff);
	for (int y = 0; y < H; y++)
	{
		const float t = float(y) / H;
		for (int x = 0; x < W; x++)
		{
			const float dx = (x - W * 0.38f) / (W * 0.7f), dy = (y - H * 0.42f) / (H * 0.9f);
			const float glow = std::max(0.0f, 1.0f - std::sqrt(dx * dx + dy * dy)) * 0.30f;
			float k = 0.07f + glow * 0.7f - 0.04f * t;
			float r = 12 + tr * k, g = 13 + tg * k, b = 17 + tb * k;
			const float sdx = (x - shadow_cx) / shadow_rx, sdy = (y - shadow_cy) / (shadow_rx * 0.12f);
			const float sd = sdx * sdx + sdy * sdy;
			if (sd < 1.0f)
			{
				const float m = 1.0f - 0.7f * (1.0f - sd);
				r *= m, g *= m, b *= m;
			}
			half[size_t(y) * W + x] = Pack(int(r), int(g), int(b));
		}
	}
	out.resize(size_t(SW) * SH);
	const int k = SW / W;
	for (int y = 0; y < SH; y++)
	{
		const uint32_t* in = half.data() + size_t(y / k) * W;
		uint32_t* o = out.data() + size_t(y) * SW;
		for (int x = 0; x < SW; x++)
			o[x] = in[x / k];
	}
}

uint32_t Average(const Img& img)
{
	if (!img.ok())
		return style::Accent();
	uint64_t r = 0, g = 0, b = 0, n = 0;
	for (size_t i = 0; i < img.px.size(); i += 7)
	{
		r += img.px[i] & 0xff;
		g += (img.px[i] >> 8) & 0xff;
		b += (img.px[i] >> 16) & 0xff;
		n++;
	}
	n = std::max<uint64_t>(1, n);
	return Pack(int(r / n), int(g / n), int(b / n));
}

float Ease(float cur, float target, float rate, float dt)
{
	return cur + (target - cur) * (1.0f - std::exp(-dt * rate));
}
} // namespace

std::string BackCoverKey(const std::string& product)
{
	std::string out;
	for (char c : product)
	{
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')
			out += c;
		else if (c == ' ' && !out.empty() && out.back() != '-')
			continue;
	}
	while (!out.empty() && (out.back() == '-' || out.back() == '.'))
		out.pop_back();
	out = Lower(out);
	bool alnum = false;
	for (char c : out)
		alnum = alnum || (c >= '0' && c <= '9');
	return alnum && out.size() >= 3 && out.find("..") == std::string::npos ? out : std::string();
}

std::vector<std::string> BackCoverUrls(const std::string& product, const std::vector<std::string>& templates)
{
	std::vector<std::string> codes;
	const std::string key = BackCoverKey(product);
	if (key.empty())
		return {};
	codes.push_back(key);
	// "mk-81009-50" (a European release's suffix) -> "mk-81009" too
	const size_t first = key.find('-');
	const size_t last = key.find_last_of('-');
	if (first != std::string::npos && last != first && last + 1 < key.size())
		codes.push_back(key.substr(0, last));
	std::vector<std::string> out;
	for (const std::string& code : codes)
	{
		std::string upper = code;
		for (char& c : upper)
			if (c >= 'a' && c <= 'z')
				c = char(c - 'a' + 'A');
		for (std::string url : templates)
		{
			for (size_t p; (p = url.find("${code}")) != std::string::npos;)
				url.replace(p, 7, code);
			for (size_t p; (p = url.find("${CODE}")) != std::string::npos;)
				url.replace(p, 7, upper);
			if (std::find(out.begin(), out.end(), url) == out.end())
				out.push_back(url);
		}
	}
	return out;
}

bool DecodeAvif(const uint8_t* data, size_t size, std::vector<uint32_t>* rgba, int* w, int* h)
{
	avifDecoder* dec = avifDecoderCreate();
	if (!dec)
		return false;
	dec->maxThreads = 1;
	dec->imageDimensionLimit = 8192;
	dec->imageSizeLimit = 8192 * 8192;
	dec->strictFlags = AVIF_STRICT_DISABLED;
	dec->ignoreExif = AVIF_TRUE;
	dec->ignoreXMP = AVIF_TRUE;
	avifImage* img = avifImageCreateEmpty();
	bool ok = false;
	if (img)
	{
		const avifResult r = avifDecoderReadMemory(dec, img, data, size);
		if (r == AVIF_RESULT_OK && img->width > 0 && img->height > 0)
		{
			avifRGBImage rgb;
			avifRGBImageSetDefaults(&rgb, img);
			rgb.format = AVIF_RGB_FORMAT_RGBA;
			rgb.depth = 8;
			rgba->assign(size_t(img->width) * img->height, 0xff000000u);
			rgb.pixels = reinterpret_cast<uint8_t*>(rgba->data());
			rgb.rowBytes = img->width * 4;
			ok = avifImageYUVToRGB(img, &rgb) == AVIF_RESULT_OK;
			*w = int(img->width);
			*h = int(img->height);
		}
		else
			OrbisLog("[case] AVIF: %s", avifResultToString(r));
		avifImageDestroy(img);
	}
	avifDecoderDestroy(dec);
	return ok;
}

std::string RegionNames(const std::string& areas)
{
	std::string out;
	for (char c : areas)
	{
		const char* name = c == 'J' ? "Japan"
			: c == 'T' ? "Asia"
			: c == 'U' ? "North America"
			: c == 'B' ? "Brazil"
			: c == 'K' ? "Korea"
			: c == 'A' ? "Asia (PAL)"
			: c == 'E' ? "Europe"
			: c == 'L' ? "Latin America"
			: nullptr;
		if (!name)
			continue;
		const std::string t = T(name);
		if (out.find(t) != std::string::npos)
			continue;
		out += (out.empty() ? "" : ", ") + t;
	}
	return out;
}

namespace
{
// ---- 3.10: the game's disc, for the launch animation (fe_shelf.cpp): one thread, the game just picked ----------
class DiscFetcher
{
public:
	DiscFetcher() { m_thread = std::make_unique<ps5::BigThread>([this] { Run(); }, size_t(8) << 20); }

	void Request(const GameInfo& g)
	{
		std::lock_guard<std::mutex> l(m_lock);
		m_want = g;
		m_have_want = true;
		m_result.reset();
		m_done = false;
		m_wake.notify_all();
	}
	// The picture of the game asked last, once found; null while looking or when there is none.
	ImgPtr Get(bool* done)
	{
		std::lock_guard<std::mutex> l(m_lock);
		if (done)
			*done = m_done;
		return m_result;
	}
	void Stop()
	{
		m_quit = true;
		m_wake.notify_all();
		if (!m_busy && m_thread)
			m_thread->join();
		else
			(void)m_thread.release(); // left running on purpose (a download can wait for its timeouts)
	}

private:
	void Run()
	{
		for (;;)
		{
			GameInfo g;
			{
				std::unique_lock<std::mutex> l(m_lock);
				m_wake.wait(l, [this] { return m_quit || m_have_want; });
				if (m_quit)
					return;
				g = m_want;
				m_have_want = false;
				m_busy = true;
			}
			ImgPtr img = Find(g);
			{
				std::lock_guard<std::mutex> l(m_lock);
				m_busy = false;
				if (!m_have_want) // not asked for another game meanwhile
				{
					m_result = img;
					m_done = true;
				}
			}
		}
	}

	static ImgPtr Load(const std::string& file)
	{
		auto img = std::make_shared<Img>();
		return !file.empty() && LoadImageFile(file, img.get()) ? img : nullptr;
	}

	ImgPtr Find(const GameInfo& g)
	{
		// 1. yours: "<game> (disc).png|jpg" beside the game or in covers/Saturn
		const std::string beside = g.path.substr(0, g.path.find_last_of('/') + 1) + g.file_base + " (disc)";
		for (const std::string& base : {beside, OrbisDir("covers") + "/Saturn/" + g.file_base + " (disc)"})
			if (ImgPtr img = Load(FindImage(base)))
			{
				OrbisLog("[launchanim] %s: your disc picture %s", g.file_base.c_str(), base.c_str());
				return img;
			}
		// 2. downloaded before: covers/Saturn/disc/<product number, else the file's name>
		const backs::Disc disc = launchbox::DiscOf(g);
		std::string key = BackCoverKey(disc.product);
		if (key.empty())
			for (char c : g.file_base)
				key += (isalnum(uint8_t(c)) || c == '-' || c == ' ') ? char(tolower(uint8_t(c))) : '_';
		const std::string dir = OrbisDir("covers") + "/Saturn/disc";
		OrbisMkdirs(dir);
		for (const char* ext : {".png", ".jpg"})
			if (ImgPtr img = Load(NonEmptyFile(dir + "/" + key + ext) ? dir + "/" + key + ext : std::string()))
			{
				OrbisLog("[launchanim] %s: disc picture %s/%s%s", g.file_base.c_str(), dir.c_str(), key.c_str(), ext);
				return img;
			}
		// 3. LaunchBox's "Disc" of the game (only when its index is there: the animation does not wait for the database)
		const std::string missing = dir + "/" + key + ".missing";
		struct stat st = {};
		if (stat(missing.c_str(), &st) == 0 && time(nullptr) - st.st_mtime < kMissingDays * 86400)
		{
			OrbisLog("[launchanim] %s: no disc picture online (%s)", g.file_base.c_str(), missing.c_str());
			return nullptr;
		}
		if (launchbox::GetState() != launchbox::State::Ready)
		{
			OrbisLog("[launchanim] %s: no LaunchBox database yet: the disc is drawn", g.file_base.c_str());
			return nullptr;
		}
		const std::vector<backs::Candidate> cands =
			backs::LaunchBoxCandidates(disc, launchbox::IndexPath(), launchbox::ImageBase(), "disc");
		if (!m_http)
			m_http = CoverService::MakeShelfHttp();
		bool network_failed = false;
		for (const backs::Candidate& c : cands)
		{
			if (m_quit || m_have_want)
				return nullptr;
			std::vector<uint8_t> data;
			const double t0 = Now();
			const int status = m_http->Get(c.url, data, kMaxFile);
			OrbisLog("[launchanim] %s: GET %s -> %d, %zu bytes, %.0f ms", g.file_base.c_str(), c.url.c_str(), status,
				data.size(), (Now() - t0) * 1000);
			if (status == -1)
			{
				network_failed = true;
				continue;
			}
			if (status != 200 || data.size() < 64)
				continue;
			const std::string ext = data[0] == 0x89 ? ".png" : ".jpg";
			auto img = std::make_shared<Img>();
			if (!DecodeImage(data, ext, img.get()))
				continue;
			WriteFileAtomicTo(dir + "/" + key + ext, data);
			OrbisLog("[launchanim] %s: disc %dx%d saved to %s/%s%s", g.file_base.c_str(), img->w, img->h, dir.c_str(),
				key.c_str(), ext.c_str());
			return img;
		}
		if (!network_failed)
		{
			const char* note = cands.empty() ? "no disc for this game\n" : "no disc on the servers\n";
			WriteFileAtomicTo(missing, std::vector<uint8_t>(note, note + strlen(note)));
		}
		OrbisLog("[launchanim] %s: no disc picture (%s): the disc is drawn", g.file_base.c_str(),
			network_failed ? "no network" : cands.empty() ? "LaunchBox has none" : "none could be read");
		return nullptr;
	}

	std::mutex m_lock;
	std::condition_variable m_wake;
	GameInfo m_want;
	bool m_have_want = false, m_done = false;
	ImgPtr m_result;
	std::atomic<bool> m_quit{false}, m_busy{false};
	std::unique_ptr<HttpClient> m_http;
	std::unique_ptr<ps5::BigThread> m_thread;
};

DiscFetcher* g_discs = nullptr;

DiscFetcher& Discs()
{
	std::lock_guard<std::mutex> l(g_fetcher_lock);
	if (!g_discs)
		g_discs = new DiscFetcher();
	return *g_discs;
}
} // namespace

void DiscPictureStart(const GameInfo& game)
{
	Discs().Request(game);
}

bool DiscPicture(std::vector<uint32_t>& px, int& w, int& h, bool* done)
{
	ImgPtr img = Discs().Get(done);
	if (!img || !img->ok())
		return false;
	px = img->px;
	w = img->w;
	h = img->h;
	return true;
}

void CaseViewShutdown()
{
	std::lock_guard<std::mutex> l(g_fetcher_lock);
	if (g_fetcher)
		g_fetcher->Stop();
	if (g_discs)
		g_discs->Stop();
}

CaseChoice CaseView(const GameInfo& g, const CoverPtr& front_tex, bool has_prev, bool has_next)
{
	ps5gpu::ClearScene();
	uint32_t* surf = ps5video::Surface();
	SW = ps5video::SurfaceWidth();
	SH = ps5video::SurfaceHeight();
	const float kpx = float(ps5video::PixelScale());
	ps5video::FillRect(0, 0, W, H, style::Bg());
	TextMid(W / 2, H / 2, T("Loading..."), 24, style::Lo(), Weight::Medium, Align::Center);
	ps5video::Present(0, 0, 0, 0, true);

	// what the disc's header says
	romheader::Info info;
	const bool have_header = ReadRomHeader(g.path, g.system, &info);
	Details d;
	d.title = !g.title.empty() ? g.title : (have_header ? info.title : g.file_base);
	if (have_header)
	{
		d.product = info.other;
		d.date = info.date;
		d.regions = RegionNames(info.areas);
	}
	d.discs = g.disc_count > 0 ? g.disc_count : (have_header && info.discs > 0 ? info.discs : 1);
	OrbisLog("[case] %s: header %s, product \"%s\", date %s, areas \"%s\", %d disc(s)", g.path.c_str(), have_header ? "read" : "not read",
		d.product.c_str(), d.date.c_str(), have_header ? info.areas.c_str() : "", d.discs);

	// the back: downloaded first (priority), then yours, then a generated one. The game is the one the product number
	// says (fe_backsources.h), else the one its names say.
	BackQuery q;
	q.disc.product = d.product;
	q.disc.areas = have_header ? info.areas : std::string();
	for (const std::string& n : {g.nointro, g.title, g.file_base, have_header ? info.title : std::string()})
		if (!n.empty())
			q.disc.names.push_back(n);
	std::string serial_title, serial_region;
	std::string official; // the game the product number is (shown, so a wrong number is seen)
	if (backs::SerialTitle(d.product, &serial_title, &serial_region))
	{
		official = artmatch::Parse(serial_title).title;
		if (official.empty())
			official = serial_title;
	}
	{
		// the title: the game the product number names, else the name without its tags ("D (1996)(Acclaim)(US)" -> "D")
		const std::string plain = artmatch::Parse(d.title).title;
		if (!official.empty())
			d.title = official;
		else if (!plain.empty())
			d.title = plain;
		// no header: what the file's name says (a year in a tag, the region)
		const std::string name = g.file_base + " " + g.nointro;
		for (size_t p = name.find('('); p != std::string::npos; p = name.find('(', p + 1))
			if (p + 5 < name.size() && (name.compare(p + 1, 2, "19") == 0 || name.compare(p + 1, 2, "20") == 0) &&
				isdigit((unsigned char)name[p + 3]) && isdigit((unsigned char)name[p + 4]) && !isdigit((unsigned char)name[p + 5]))
			{
				d.year = name.substr(p + 1, 4);
				break;
			}
		if (d.regions.empty())
		{
			std::string r = artmatch::Parse(name).region;
			if (r.empty())
			{
				const std::string l = artmatch::Lower(name);
				r = l.find("(us)") != std::string::npos || l.find("(u)") != std::string::npos ? "USA"
					: l.find("(jp)") != std::string::npos || l.find("(j)") != std::string::npos ? "Japan"
					: l.find("(eu)") != std::string::npos || l.find("(e)") != std::string::npos ? "Europe" : "";
			}
			d.regions = r == "USA" ? T("North America") : r == "Japan" ? T("Japan") : r == "Europe" ? T("Europe") : r == "World" ? "World" : "";
		}
	}
	if (!official.empty())
		OrbisLog("[case] %s is \"%s\" (%s) in the product number table", d.product.c_str(), serial_title.c_str(), serial_region.c_str());
	q.key = BackCoverKey(d.product);
	if (q.key.empty())
	{
		const std::vector<std::string> names = backs::LookupNames(q.disc);
		const std::string t = names.empty() ? std::string() : artmatch::TitleKey(names[0]);
		q.key = t.empty() ? std::string() : "title-" + t.substr(0, 60);
	}
	const std::string key = q.key;
	if (!key.empty())
		Fetcher().Request(q);

	Img front = LoadFront(g, front_tex);
	const float aspect = front.ok() ? std::clamp(float(front.w) / front.h, 0.62f, 1.30f) : 0.88f;
	const float hy = 372.0f, hx = hy * aspect, hz = std::max(22.0f, hx * 0.10f);
	if (!front.ok())
		front = Solid(Rgb(50, 46, 80));
	Img own_back;
	const std::string own_file = OwnBackFile(g);
	if (!own_file.empty() && !LoadImageFile(own_file, &own_back))
		OrbisLog("[case] your back cover %s can't be read", own_file.c_str());
	const int bw = std::max(64, int(kTexH * aspect));
	const Img made_back = MakeBack(front, bw, kTexH, d);
	const Img made_spine = MakeSpine(kTexH, std::max(24, int(kTexH * hz / hy)), d.title);
	const Img plastic = Solid(Rgb(70, 70, 80));

	Camera cam;
	// the camera in the surface's pixels (the case is placed in points: x 660, y 500)
	cam.cx = 660.0f * kpx;
	cam.cy = 500.0f * kpx;
	cam.focal *= kpx;
	std::vector<uint32_t> backdrop;
	MakeBackdrop(backdrop, Average(front), 660.0f, 500.0f + hy + 52.0f, hx * 1.25f);

	// the panel's fixed lines
	const int panel_x = 1150, panel_w = W - panel_x - 100;


	// the game's file, as it is on the disk: a disc set the app grouped (its playlist in cache/) shows its first disc's
	// file and how many more there are
	std::string file_name = g.path.substr(g.path.find_last_of('/') + 1);
	if (g.path.find("/cache/discs/") != std::string::npos || g.path.find("/cache/playlists/") != std::string::npos)
	{
		const std::vector<std::string> discs = ResolvePlaylist(g.path, nullptr, nullptr);
		if (!discs.empty())
		{
			file_name = discs[0].substr(discs[0].find_last_of('/') + 1);
			if (discs.size() > 1)
				file_name += "  (+" + std::to_string(discs.size() - 1) + ")";
		}
	}

	ps5input::Poll();
	uint32_t prev = ps5input::Pad(0).raw_buttons | ps5input::Pad(0).buttons;
	float base = -0.9f, base_target = 0.0f; // the turn: 0 the front, pi the back (it starts turning in)
	float man_yaw = 0, man_pitch = 0, rest = 0;
	double last = Now();
	const double start = last;
	bool showing_back = false;
	BackResult back_res;
	ImgPtr back_img, spine_img;
	std::string back_from;
	for (;;)
	{
		const double now = Now();
		const float dt = float(std::min(0.05, now - last));
		last = now;
		ps5input::Poll();
		const ps5input::PadState& pad = ps5input::Pad(0);
		const uint32_t cur = pad.raw_buttons;
		const uint32_t down = cur & ~prev;
		prev = cur;
		if (down & SCE_PAD_BUTTON_CROSS)
		{
			ps5input::Poll();
			return CaseChoice::Play;
		}
		if (down & (SCE_PAD_BUTTON_CIRCLE | SCE_PAD_BUTTON_OPTIONS))
			return CaseChoice::Back;
		if ((down & SCE_PAD_BUTTON_L1) && has_prev)
			return CaseChoice::Previous;
		if ((down & SCE_PAD_BUTTON_R1) && has_next)
			return CaseChoice::Next;
		if (down & (SCE_PAD_BUTTON_R3 | SCE_PAD_BUTTON_LEFT | SCE_PAD_BUTTON_RIGHT | SCE_PAD_BUTTON_SQUARE))
		{
			showing_back = !showing_back;
			const float dir = (down & SCE_PAD_BUTTON_LEFT) ? -1.0f : 1.0f;
			base_target += dir * kPi;
		}
		// the left stick turns and tilts it by hand (it springs back when let go)
		auto axis = [](uint8_t v) {
			const float a = (float(v) - 128.0f) / 127.0f;
			return std::abs(a) < 0.12f ? 0.0f : std::clamp(a, -1.0f, 1.0f);
		};
		const float sx = axis(pad.lx), sy = axis(pad.ly);
		man_yaw = Ease(man_yaw, sx * 1.25f, 8.0f, dt);
		man_pitch = Ease(man_pitch, -sy * 0.55f, 8.0f, dt);
		// at rest the case stands a little turned, so its spine and its depth show (as a box on a table)
		rest = Ease(rest, showing_back ? 0.42f : -0.42f, 4.0f, dt);
		base = Ease(base, base_target, 6.5f, dt);
		const float idle = float(std::sin((now - start) * 0.9)) * 0.06f * (1.0f - std::min(1.0f, std::abs(sx) + std::abs(sy)));
		cam.yaw = base + rest + man_yaw + idle;
		cam.pitch = man_pitch - 0.05f;

		// the back cover's state
		if (!key.empty() && back_res.state == BackState::Waiting)
		{
			back_res = Fetcher().Get(key);
			if (back_res.state == BackState::Ok)
			{
				back_img = back_res.img;
				back_from = back_res.from;
			}
			if (back_res.state != BackState::Waiting)
				spine_img = back_res.spine;
		}
		const Img& spine = spine_img ? *spine_img : made_spine;
		const Img* back = back_img ? back_img.get() : own_back.ok() ? &own_back : &made_back;
		// only the first download of LaunchBox's database is told (it takes minutes); a picture's source is not
		const std::string back_note = !key.empty() && back_res.state == BackState::Waiting ? Fetcher().Doing() : std::string();

		// -- draw
		memcpy(surf, backdrop.data(), backdrop.size() * 4);
		DrawCase(surf, cam, hx, hy, hz, front, *back, spine, plastic);

        // The table ends above a fixed, single action area; every value scrolls only inside its column.
        std::string sub=d.date.size()==8?d.date.substr(0,4):d.year;
        if(d.discs>1) { char count[48];snprintf(count,sizeof(count),T("%d discs"),d.discs);sub+=(sub.empty()?"":"   ")+std::string(count); }
        std::vector<std::pair<std::string,std::string>> rows={{"Product code",d.product}};
        if(!official.empty() && official!=d.title) rows.push_back({"Game",official});
        rows.push_back({"Release date",FormatDate(d.date)});
        rows.push_back({"Discs",std::to_string(d.discs)});
        rows.push_back({"Region",d.regions});
        if(const long long secs=playtime::Seconds(g.path);secs>0) {
            rows.push_back({"Time played",playtime::Format(secs)});
            rows.push_back({"Last played",recent::Ago(std::max(recent::When(g.path),playtime::Last(g.path)))});
        }
        rows.push_back({"File",file_name});
        DrawCasePanel(panel_x,panel_w,d.title,sub,rows,showing_back?T("Front side"):T("Back side"),back_note,now-start);
		// which side shows: two dots under the case
		for (int i = 0; i < 2; i++)
		{
			const bool on = (i == 1) == showing_back;
			style::RoundRect(660.0f - 22 + i * 26, 968, on ? 20.0f : 10.0f, 6, 3, on ? style::Accent() : style::Off());
		}
        // Rotation/navigation help stays under the 3D case, outside the right-hand action area.
        std::vector<style::Hint> navigation={{{icon::DpadLeftRight},T("Turn")},{{icon::L3},T("Rotate")}};
        if(has_prev || has_next) navigation.push_back({{icon::L1,icon::R1},T("Game")});
        const int case_center=int(std::lround(cam.cx/kpx));
        style::HintsInArea(navigation,case_center-475,950,1020);

		ps5video::Present(0, 0, 0, 0, true);
	}
}
} // namespace fe
