// Bunbun — known folders for the Linux build (see windows.h in this folder).
//
//   FOLDERID_Profile         $HOME
//   FOLDERID_RoamingAppData  $XDG_CONFIG_HOME (~/.config)
//   FOLDERID_LocalAppData    $XDG_STATE_HOME (~/.local/state)
#pragma once

#include "windows.h"

enum KNOWNFOLDERID { FOLDERID_Profile, FOLDERID_RoamingAppData, FOLDERID_LocalAppData };
using REFKNOWNFOLDERID = KNOWNFOLDERID;
#define KF_FLAG_CREATE 0x8000

// *path is malloc'd; free it with CoTaskMemFree, as on Windows.
HRESULT SHGetKnownFolderPath(KNOWNFOLDERID id, DWORD flags, HANDLE token, PWSTR* path);
void CoTaskMemFree(void* p);
int SHCreateDirectoryExW(void* hwnd, PCWSTR path, void* security);
