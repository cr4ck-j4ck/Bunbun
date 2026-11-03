// Bunbun — Linux platform basics: text conversion, folders, launching things.
//
// Folders (XDG):
//   ConfigDir()   $XDG_CONFIG_HOME/Bunbun   settings.ini, mascots/
//   StateDir()    $XDG_STATE_HOME/Bunbun    state.ini, bunbun.log
//   RuntimeDir()  $XDG_RUNTIME_DIR/bunbun   agent.sock, the instance lock (mode 0700)
#pragma once

#include <string>
#include <vector>

namespace bb::plat {

std::string Utf8(const std::wstring& w);
std::wstring Wide(const std::string& s);

// A path from shared code, which may join with backslashes, as a POSIX path.
std::string Native(const std::wstring& path);

std::string HomeDir();
std::string ConfigDir();
std::string StateDir();
std::string RuntimeDir();
// The folder holding the running executable (for its sibling binaries).
std::string ExeDir();
// Where the settings UI lives: $BUNBUN_DATA_DIR, next to the exe (a build
// tree), or ../share/bunbun next to it (an install, the AppImage).
std::string DataDir();

bool FileExists(const std::string& path);
bool MakeDirs(const std::string& path);
bool ReadFile(const std::string& path, std::string* out);
// Writes through a temp file and a rename, so readers never see half a file.
bool WriteFileAtomic(const std::string& path, const std::string& data);

// Starts a program detached from us (argv[0] is looked up in PATH).
bool Spawn(const std::vector<std::string>& argv);
// Opens a file, folder or https link with the desktop's default app.
bool OpenWithDefault(const std::string& target);

}  // namespace bb::plat
