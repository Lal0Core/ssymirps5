// SSYmirPS5 frontend: LaunchBox Games DB, the first place box art is downloaded from (fe_launchbox.h).
// SPDX-License-Identifier: MIT

#include "fe_launchbox.h"

#include "fe_covers.h"
#include "fe_coverfetch.h"
#include "fe_i18n.h"
#include "fe_romheader.h"
#include "fe_tlshttp.h"
#include "fe_ui.h"

#include "OrbisPaths.h"
#include "ProsperoThread.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace fe
{
namespace launchbox
{
namespace
{
constexpr size_t kMaxDatabase = size_t(1) << 30;
std::mutex g_lock;
std::atomic<int> g_state{int(State::Idle)};
std::atomic<int> g_phase{0}; // 1 downloading, 2 reading
std::atomic<size_t> g_bytes{0};
std::atomic<int> g_generation{0};
std::atomic<bool> g_quit{false};
ps5::BigThread* g_thread = nullptr;

std::string Url()
{
	const char* e = getenv("YMIR_LAUNCHBOX_URL");
	return e && *e ? e : "https://gamesdb.launchbox-app.com/Metadata.zip";
}

void Run()
{
	const std::string index = IndexPath();
	OrbisMkdirs(index.substr(0, index.find_last_of('/')));
	const std::string failed = index + ".failed";
	struct stat st = {};
	if (stat(failed.c_str(), &st) == 0 && time(nullptr) - st.st_mtime < 86400)
	{
		OrbisLog("[launchbox] the database failed less than a day ago: not tried now");
		g_state = int(State::Failed);
		return;
	}
	g_phase = 1;
	g_bytes = 0;
	std::vector<uint8_t> zip;
	const double t0 = ui::Now();
	int status;
	{
		TlsHttp http;
		http.SetProgress(&g_bytes);
		status = http.Get(Url(), zip, kMaxDatabase);
	}
	if (status == -1 && !g_quit)
	{
		std::unique_ptr<HttpClient> other = CoverService::MakeShelfHttp();
		status = other->Get(Url(), zip, kMaxDatabase);
	}
	OrbisLog("[launchbox] GET %s -> %d, %zu MB, %.0f s", Url().c_str(), status, zip.size() >> 20, ui::Now() - t0);
	if (status != 200 || zip.size() < 22)
	{
		g_phase = 0;
		if (status != -1)
		{
			const char* note = "the LaunchBox database could not be downloaded\n";
			WriteFileAtomicTo(failed, std::vector<uint8_t>(note, note + strlen(note)));
		}
		g_state = int(State::Failed);
		return;
	}
	g_phase = 2;
	const std::string tmp = OrbisDir("cache") + "/launchbox-Metadata.zip";
	OrbisMkdirs(OrbisDir("cache"));
	const bool saved = WriteFileAtomicTo(tmp, zip);
	std::vector<uint8_t>().swap(zip);
	const int n = saved ? backs::BuildLaunchBoxIndex(tmp, index) : -1;
	unlink(tmp.c_str());
	g_phase = 0;
	if (n < 0)
	{
		const char* note = "the LaunchBox database could not be read\n";
		WriteFileAtomicTo(failed, std::vector<uint8_t>(note, note + strlen(note)));
		g_state = int(State::Failed);
		return;
	}
	unlink(failed.c_str());
	g_generation++;
	g_state = int(State::Ready);
}
} // namespace

std::string IndexPath()
{
	return OrbisDir("covers") + "/Saturn/launchbox-backs.tsv";
}

std::string ImageBase()
{
	const char* e = getenv("YMIR_LAUNCHBOX_IMAGES");
	return e && *e ? e : "https://images.launchbox-app.com/";
}

void Start()
{
	std::lock_guard<std::mutex> l(g_lock);
	if (g_state != int(State::Idle))
		return;
	if (backs::LaunchBoxIndexValid(IndexPath()))
	{
		g_state = int(State::Ready);
		g_generation++;
		return;
	}
	g_state = int(State::Working);
	g_thread = new ps5::BigThread([] { Run(); }, size_t(4) << 20);
}

State GetState()
{
	return State(g_state.load());
}

std::string Progress()
{
	char buf[200];
	if (g_phase == 1)
	{
		snprintf(buf, sizeof(buf), T("Downloading the LaunchBox database (first time only): %zu MB"), g_bytes.load() >> 20);
		return buf;
	}
	if (g_phase == 2)
		return T("Reading the LaunchBox database...");
	return "";
}

int Generation()
{
	return g_generation;
}

bool Wait(const std::atomic<bool>* quit)
{
	Start();
	while (GetState() == State::Working && !(quit && *quit))
		usleep(100 * 1000);
	return GetState() == State::Ready;
}

void Shutdown()
{
	std::lock_guard<std::mutex> l(g_lock);
	g_quit = true;
	if (g_thread && GetState() != State::Working)
	{
		g_thread->join();
		delete g_thread;
	}
	g_thread = nullptr; // a download in progress is left to the process' end
}

backs::Disc DiscOf(const GameInfo& g)
{
	static std::mutex lock;
	static std::unordered_map<std::string, backs::Disc> cache;
	struct stat st = {};
	const bool local = stat(g.path.c_str(), &st) == 0;
	const std::string key = g.path + "\t" + std::to_string((long long)st.st_size) + "\t" + std::to_string((long long)st.st_mtime);
	{
		std::lock_guard<std::mutex> l(lock);
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;
	}
	backs::Disc d;
	romheader::Info info;
	if (local && ReadRomHeader(g.path, g.system, &info))
	{
		d.product = info.other;
		d.areas = info.areas;
	}
	for (const std::string& n : {g.nointro, g.title, g.file_base, info.title})
		if (!n.empty())
			d.names.push_back(n);
	std::lock_guard<std::mutex> l(lock);
	cache[key] = d;
	return d;
}

std::string Urls(const GameInfo& g, const char* kind)
{
	if (GetState() != State::Ready)
		return "";
	static std::mutex lock;
	static std::unordered_map<std::string, std::string> cache;
	static int cache_gen = -1;
	const std::string key = std::string(kind) + "|" + g.path + "|" + g.nointro + "|" + g.file_base;
	{
		std::lock_guard<std::mutex> l(lock);
		if (cache_gen != Generation())
		{
			cache.clear();
			cache_gen = Generation();
		}
		auto it = cache.find(key);
		if (it != cache.end())
			return it->second;
	}
	std::string out;
	for (const backs::Candidate& c : backs::LaunchBoxCandidates(DiscOf(g), IndexPath(), ImageBase(), kind))
		out += (out.empty() ? "" : "\t") + c.url;
	if (!out.empty())
		OrbisLog("[launchbox] %s (%s): %s", g.file_base.c_str(), kind, out.c_str());
	std::lock_guard<std::mutex> l(lock);
	cache[key] = out;
	return out;
}
} // namespace launchbox
} // namespace fe
