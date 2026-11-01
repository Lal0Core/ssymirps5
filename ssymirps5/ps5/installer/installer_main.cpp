// SSYmirPS5: the payloads (SSYmirPS5.elf and SSYmirPS5-helper.elf).
//
// SSYmirPS5.elf, sent with the PS5 Payload Manager / the ELF loader (port 9021), is what PS5SX2's installer and
// helper are together:
//   1. it installs or updates the dashboard app in /data/homebrew/<TITLE_ID> (eboot.bin = Ymir's core and the frontend,
//      sce_module/libc.prx, sce_sys/param.json, icon0.png, pic0.dds, pic1.dds), built into it (install_data.cpp);
//   2. then it stays running as the helper that lets the app out of its sandbox (ProsperoJailbreak.h), until
//      the console is turned off. A second copy sent while one runs only installs.
// SSYmirPS5-helper.elf (YMIR_HELPER_ONLY) is step 2 alone: the app carries it and sends it to the ELF
// loader itself when no helper answers (after a reboot, for instance).
//
// Earlier versions used the title ID PPSA99011, which another app uses: the app is PPSA99378 now. Nothing under the old
// ID is touched (it may be another app's); when our earlier version is still there, the notification says so
// (ProsperoMigrate.h).
//
// Nothing here writes outside /data/ssymirps5 and /data/homebrew/<TITLE_ID>.
//
// SPDX-License-Identifier: MIT

#include "OrbisPaths.h"
#include "ProsperoJailbreak.h"
#include "ProsperoMigrate.h"
#include "ProsperoNotify.h"
#ifndef YMIR_HELPER_ONLY
#include "ProsperoInstall.h"
#endif

#include <cstdio>
#include <cstring>
#include <string>
#include <unistd.h>

#ifndef SSYMIRPS5_VERSION
#define SSYMIRPS5_VERSION "dev"
#endif

namespace
{
std::string g_ready_message;

void OnReady()
{
	if (!g_ready_message.empty())
		ProsperoNotify("%s", g_ready_message.c_str());
	ProsperoNotifyFlush();
}
} // namespace

int main()
{
	setvbuf(stdout, nullptr, _IOLBF, 0);
	const bool have_data = OrbisPathsInit();
#ifdef YMIR_HELPER_ONLY
	OrbisLogOpen("helper");
	OrbisLog("[helper] SSYmirPS5 helper %s (built %s %s), pid %d%s", SSYMIRPS5_VERSION, __DATE__, __TIME__,
		int(getpid()), have_data ? "" : " (no /data: no log file)");
#else
	OrbisLogOpen("installer");
	OrbisLog("[installer] SSYmirPS5 %s (built %s %s), pid %d", SSYMIRPS5_VERSION, __DATE__, __TIME__, int(getpid()));
	if (!have_data)
		OrbisLog("[installer] can't create %s", OrbisRoot().c_str());

	const char* open_hint = "Open it from the SSYmirPS5 icon on the home screen.";
	char msg[1024];
	const InstallResult installed = InstallApp();
	switch (installed)
	{
		case InstallResult::Installed:
			snprintf(msg, sizeof(msg), "SSYmirPS5 %s installed. %s", SSYMIRPS5_VERSION, open_hint);
			break;
		case InstallResult::Updated:
			snprintf(msg, sizeof(msg), "SSYmirPS5 updated to %s. %s", SSYMIRPS5_VERSION, open_hint);
			break;
		case InstallResult::UpToDate:
			snprintf(msg, sizeof(msg), "SSYmirPS5 %s is ready. %s", SSYMIRPS5_VERSION, open_hint);
			break;
		case InstallResult::NewerInstalled:
			snprintf(msg, sizeof(msg),
				"SSYmirPS5: a newer version is installed (updated from the app); this payload (%s) left it as it is. "
				"Put the newer payload in your autoload. %s",
				SSYMIRPS5_VERSION, open_hint);
			break;
		case InstallResult::Failed:
			snprintf(msg, sizeof(msg), "SSYmirPS5: could not install the app in %s (see %s/logs/installer.log)",
				AppInstallDir().c_str(), OrbisRoot().c_str());
			break;
		default:
			snprintf(msg, sizeof(msg), "SSYmirPS5: nothing to install");
			break;
	}
	if (installed != InstallResult::NewerInstalled && SyncAppMeta() > 0)
	{
		const size_t len = strlen(msg);
		snprintf(msg + len, sizeof(msg) - len, " Home screen art updated (restart the PS5 if it doesn't show).");
	}
	const std::string old_ids = migrate::FindOldInstalls();
	if (!old_ids.empty())
	{
		const size_t len = strlen(msg);
		snprintf(msg + len, sizeof(msg) - len,
			" Its title ID is now %s. The old version (%s) is left alone: to remove it, delete its folder "
			"/data/homebrew/%s (FTP), then its icon (OPTIONS, Delete). Games, saves and settings are shared.",
			YMIR_TITLE_ID, old_ids.c_str(), old_ids.c_str());
	}
	OrbisLog("[installer] %s", msg);
	g_ready_message = msg;
#endif

	// the helper: runs until the console is turned off, or returns at once when one already runs
	if (!jailbreak::ServeHelper(OnReady))
	{
#ifndef YMIR_HELPER_ONLY
		ProsperoNotify("%s", g_ready_message.c_str());
#endif
		OrbisLog("[helper] another helper is running: leaving");
	}
	ProsperoNotifyFlush();
	OrbisLogClose();
	return 0;
}
