// SSYmirPS5: the app's entry point (eboot.bin).
//
// Ymir's core runs as the dashboard app itself, a native PS5 title built the way PS5SX2 builds its eboot
// (ps5/coreorbis/link-vk.sh): the console gives the controller only to the title in front. SSYmirPS5.elf is
// the installer and the helper (installer/installer_main.cpp), like PS5SX2's installer and helper payloads.
//
// Like PS5SX2's main-boot.cpp it first fetches the covers the library still needs, asks to leave the sandbox
// (ProsperoJailbreak.h: /data and USB drives), then brings the PS5 layer up in order -- log, folders, video,
// audio, pads -- starts the core and hands over to the frontend: the shelf, the game, the pause menu, and back.
// It ends through the system (sceSystemServiceLoadExec("exit")), as PS5SX2 learned it must.
//
//   eboot.bin [rom]     with a ROM path, the game starts at once (no shelf; the host build's tests use it)
//
// SPDX-License-Identifier: MIT

#include "OrbisPaths.h"
#include "ProsperoAudio.h"
#include "ProsperoCrash.h"
#include "ProsperoInput.h"
#include "ProsperoJailbreak.h"
#include "ProsperoUpdate.h"
#include "ProsperoNotify.h"
#include "ProsperoSce.h"
#include "ProsperoVideo.h"

#include "fe_covers.h"
#include "fe_coverworker.h"
#include "fe_emu.h"
#include "fe_i18n.h"
#include "fe_menu.h"
#include "fe_prefetch.h"
#include "fe_playtime.h"
#include "fe_recent.h"
#include "fe_settings.h"
#include "fe_shelf.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <pthread.h>
#include <string>
#include <unistd.h>

#ifndef SSYMIRPS5_VERSION
#define SSYMIRPS5_VERSION "dev"
#endif
#ifndef YMIR_CORE_VERSION
#define YMIR_CORE_VERSION "?"
#endif

namespace
{
// The host tests: YMIR_HOST_QUIT_AFTER=seconds ends a game started from the command line.
double QuitAfter()
{
	const char* s = getenv("YMIR_HOST_QUIT_AFTER");
	return s ? atof(s) : 0.0;
}

double Now()
{
	timespec ts = {};
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec / 1e9;
}

// One game, until the player leaves it. Returns false when the player chose "Quit SSYmirPS5".
bool PlayGame(const std::string& rom, bool from_command_line)
{
	std::string error;
	if (!emu::LoadGame(rom, &error))
	{
		const std::string file = rom.substr(rom.find_last_of('/') + 1);
		ProsperoNotify(fe::T("SSYmirPS5: could not start %s"), file.c_str());
		fe::MessageBox("Could not start the game", error);
		return !from_command_line;
	}
	fe::recent::Played(rom); // 3.9.9: the shelf's Recent tab
	// 3.9.9: the player chose on the shelf to continue where they left off: the state saved on leaving is loaded after
	// the game's first frame
	bool resume = !from_command_line && fe::ShelfTakeResume();
	// 3.11: the time played: the frames' time (a gap over half a second -- the pause menu, a load -- does not count),
	// written every minute, so closing the app with the PS button loses a minute at most
	double played = 0, last_frame = Now();
	auto count_frame = [&] {
		const double now = Now(), dt = now - last_frame;
		last_frame = now;
		if (dt > 0 && dt < 0.5)
			played += dt;
		if (played >= 60)
		{
			fe::playtime::Add(rom, played);
			played = 0;
		}
	};
	auto flush_played = [&] {
		fe::playtime::Add(rom, played);
		played = 0;
	};
	// leaving the game: where the player is, for the next time
	auto leave = [&] {
		flush_played();
		if (fe::Config().resume)
			emu::SaveAutoState();
		emu::CloseGame();
	};
	const double quit_at = from_command_line && QuitAfter() > 0 ? Now() + QuitAfter() : 0;
	for (;;)
	{
		if (quit_at > 0 && Now() >= quit_at)
		{
			flush_played();
			emu::CloseGame();
			return false;
		}
		switch (emu::RunFrame())
		{
			case emu::FrameResult::Continue:
				count_frame();
				if (resume)
				{
					resume = false;
					emu::LoadAutoState();
				}
				break;
			case emu::FrameResult::Stopped:
				flush_played();
				emu::CloseGame();
				return !from_command_line;
			case emu::FrameResult::OpenMenu:
			{
				YMIR_STAGE(Menu, "pause menu");
				emu::Pause();
				const fe::PauseAction action = fe::PauseMenu();
				switch (action)
				{
					case fe::PauseAction::Resume:
						ps5video::FillRect(0, 0, ps5video::kWidth, ps5video::kHeight, ps5video::Rgb(0, 0, 0));
						ps5video::Present(0, 0, 0, 0, false);
						emu::Resume();
						last_frame = Now(); // the menu's time is not play time
						break;
					case fe::PauseAction::BackToList:
						leave();
						return true;
					case fe::PauseAction::Quit:
						leave();
						return false;
				}
				break;
			}
		}
	}
}

// Once a day the helper looks for a newer release on GitHub (ProsperoUpdate.h); when there is one, it says so in a
// notification (Settings, SYSTEM, Updates installs it). <root>/update-check.txt holds the time of the last look.
void AutoCheckUpdates()
{
	const std::string stamp = OrbisRoot() + "/update-check.txt";
	long long last = 0;
	if (FILE* f = fopen(stamp.c_str(), "r"))
	{
		if (fscanf(f, "%lld", &last) != 1)
			last = 0;
		fclose(f);
	}
	const long long now = (long long)time(nullptr);
	if (now - last < 24 * 3600 && now >= last)
		return;
	std::string reply;
	if (!jailbreak::UpdateRequest("autocheck", reply))
		return; // no helper of ours: nothing to ask
	OrbisLog("[update] daily check asked of the helper: %s", reply.c_str());
	if (FILE* f = fopen(stamp.c_str(), "w"))
	{
		fprintf(f, "%lld\n", now);
		fclose(f);
	}
}

// The end of the app: through the system, then _exit() if it never comes (PS5SX2's OrbisExitApp: on the
// console a title's exit() or return from main ends in SIGSYS).
[[noreturn]] void ExitApp(int status)
{
	OrbisLog("[boot] exit %d", status);
	ProsperoNotifyFlush();
	OrbisLogClose();
	fflush(stdout);
	fflush(stderr);
	const int rc = sceSystemServiceLoadExec("exit", nullptr);
	if (rc == 0)
		for (int i = 0; i < 100; i++)
			usleep(100 * 1000);
	_exit(status);
}

struct Args
{
	int argc;
	char** argv;
	int status;
};

int Run(int argc, char** argv)
{
	setvbuf(stdout, nullptr, _IOLBF, 0);
	crashlog::Install();
	YMIR_STAGE(Boot, "start");
	OrbisLog("[boot] SSYmirPS5 %s, Ymir core %s (built %s %s), pid %d", SSYMIRPS5_VERSION, YMIR_CORE_VERSION, __DATE__,
		__TIME__, int(getpid()));

	// Covers first, as PS5SX2 (orbis_frontend_prefetch_covers, 30 s, before its jailbreak): HTTPS works here,
	// and after the jailbreak it failed on the console (fe_prefetch.h).
	const fe::PrefetchResult prefetch = fe::PrefetchCovers(30.0);

	// out of the sandbox, like PS5SX2: before it the app sees no /data and no USB drives
	YMIR_STAGE(Boot, "jailbreak");
	std::string how;
	const bool freed = jailbreak::RequestForSelf(how);
	OrbisLog("[boot] jailbreak: %s", freed ? how.c_str() : "nobody answered");
	const bool have_data = OrbisPathsInit();
	OrbisLogOpen(); // also writes the lines above
	fe::SavePrefetched(prefetch);
	fe::RememberPrefetch(prefetch);
	// the shelf downloads the covers itself, with its own TLS (fe_tlshttp.h): libSceHttp2 fails after the jailbreak,
	// plain sockets don't. New games get their covers as soon as a scan finds them.
	fe::SetShelfDownloads(true);
	if (!have_data)
		OrbisLog("[boot] can't create %s: no access to /data", OrbisRoot().c_str());
	else
	{
		remove((OrbisRoot() + "/pid.txt").c_str()); // 3.9.6: no longer written (nothing read it)
		AutoCheckUpdates();
	}
	fe::Config().Load();
	if (have_data && freed)
	{
		// 3.11.8: our helper downloads the borders and the updates; the start (the covers' list) sent it to the ELF
		// loader already when it was not running. Without it the app downloads the borders itself, and says that the
		// updates need SSYmirPS5-v<version>.elf sent by hand
		const jailbreak::HelperState hs = jailbreak::EnsureHelper(3.0);
		OrbisLog("[boot] helper: %s", hs == jailbreak::HelperState::Running ? "running"
										: hs == jailbreak::HelperState::Started ? "started now"
										: hs == jailbreak::HelperState::NoLoader ? "none, and no ELF loader to start it"
																				: "sent, no answer");
		if (hs != jailbreak::HelperState::Running && hs != jailbreak::HelperState::Started)
		{
			fe::StartAppBorderWorker();
			ProsperoNotify(fe::T("SSYmirPS5: the helper could not be started (no ELF loader). Covers and borders still "
								 "download; for updates, send SSYmirPS5-v%s.elf with the Payload Manager."),
				SSYMIRPS5_VERSION);
		}
	}

	YMIR_STAGE(Boot, "video");
	if (!ps5video::Init())
	{
		OrbisLog("[boot] video init failed");
		ProsperoNotify("SSYmirPS5: video failed to start (see %s/logs/boot.log)", OrbisRoot().c_str());
		return 1;
	}
	// 1.0: the first start (no shader in the settings yet): CRT Easymode, the picture for 2D games; the renderer's own
	// list numbers it (the GPU's crt-easymode, the CPU's CRT Easymode style)
	if (fe::Config().shader < 0)
	{
		int pick = 0;
		for (int i = 1; i < ps5video::ShaderCount() && pick == 0; i++)
			if (strcmp(ps5video::ShaderName(i), "crt-easymode") == 0 || strcmp(ps5video::ShaderName(i), "CRT Easymode style") == 0)
				pick = i;
		fe::Config().shader = pick;
		OrbisLog("[boot] first start: shader %d (%s) by default", pick, ps5video::ShaderName(pick));
	}
	OrbisLog("[boot] splash screen hidden: %x", unsigned(sceSystemServiceHideSplashScreen()));
	YMIR_STAGE(Boot, "audio");
	if (!ps5audio::Init())
		OrbisLog("[boot] audio init failed: running without sound");
	YMIR_STAGE(Boot, "pad");
	if (!ps5input::Init())
		OrbisLog("[boot] pad init failed");

	if (!have_data)
	{
		// the jailbreak didn't happen: say so on the screen (the pad works, this is the title in front)
		char text[512];
		snprintf(text, sizeof(text),
			fe::T("Send SSYmirPS5-v%s.elf with the Payload Manager (it unlocks the app), then open SSYmirPS5 again."),
			SSYMIRPS5_VERSION);
		fe::MessageBox("SSYmirPS5 has no access to /data", text);
		ps5input::Shutdown();
		ps5audio::Shutdown();
		ps5video::Shutdown();
		return 2;
	}

	YMIR_STAGE(Boot, "emu init");
	if (!emu::InitCore())
	{
		ProsperoNotify("SSYmirPS5: the emulator failed to start");
		ps5audio::Shutdown();
		ps5video::Shutdown();
		return 1;
	}

	const bool from_command_line = argc > 1 && argv[1] && OrbisIsFile(argv[1]);
	std::string rom = from_command_line ? argv[1] : "";
	for (;;)
	{
		if (rom.empty())
			rom = fe::Shelf();
		if (rom.empty())
			break; // OPTIONS on the shelf
		const bool again = PlayGame(rom, from_command_line);
		rom.clear();
		if (!again)
			break;
	}

	OrbisLog("[boot] leaving");
	fe::ShelfShutdown();
	fe::Config().Save();
	emu::DeinitCore();
	ps5input::Shutdown();
	ps5audio::Shutdown();
	ps5video::Shutdown();
	return 0;
}

void* RunThread(void* p)
{
	Args* a = static_cast<Args*>(p);
	a->status = Run(a->argc, a->argv);
	return nullptr;
}
} // namespace

int main(int argc, char** argv)
{
	// The frontend runs on a thread with a stack of its own (8 MiB): a title's main thread gets a small one
	// (PS5SX2 runs PCSX2 on a 16 MiB worker for the same reason). The core runs on this thread too.
	Args args = {argc, argv, 1};
	pthread_attr_t attr;
	pthread_attr_init(&attr);
	pthread_attr_setstacksize(&attr, 8 * 1024 * 1024);
	pthread_t t;
	if (pthread_create(&t, &attr, RunThread, &args) == 0)
		pthread_join(t, nullptr);
	else
		args.status = Run(argc, argv);
	pthread_attr_destroy(&attr);
	ExitApp(args.status);
}
