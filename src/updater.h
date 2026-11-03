// Bunbun — one-click updates (src/updater.cpp).
#pragma once

#include <windows.h>

#include <string>

namespace bb::updater {

// True for a copy the installer put in place (unins000.exe next to bunbun.exe);
// copies run from a build folder or a zip update by downloading the new setup.
bool CanSelfUpdate();

// `bunbun.exe --update`: copies this exe to %TEMP%\Bunbun-update and starts that
// copy with --run-update, so the installer can replace the app folder. 0 = started.
int Start();

// `--run-update "<app folder>"`: the updater window and its work. Blocks until done.
int Run(HINSTANCE instance, const std::wstring& args);

}  // namespace bb::updater
