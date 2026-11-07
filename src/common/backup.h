// Bunbun — backups of your settings and data (Settings → Backup & restore).
//
// A backup is an ordinary .zip (stored, not compressed, so it needs no library)
// holding Bunbun's files by name, plus a small manifest. Names are the same on
// every platform ("water.txt", "mascots/<pack>/…"); each platform decides which
// of its folders a name belongs in (Windows: Roaming or Local AppData; Linux:
// ~/.config or ~/.local/state), so a backup made on Windows restores on Linux
// and the other way round. Models, caches, pictures and logs are not included.
//
// Pure: no file system access here. Shared with Linux and the unit tests.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace bb::backup {

struct File {
    std::string name;  // "settings.ini", "mascots/Bunny/idle.png" (forward slashes)
    std::string data;
};

constexpr const char* kManifest = "bunbun-backup.txt";
constexpr size_t kMaxBytes = 512u * 1024u * 1024u;  // refuse anything larger (a pack folder of videos, say)

// The single files a backup keeps. Folders: mascots/ (skin packs) and ask/ (Ask Bun's prompt).
constexpr const char* kFiles[] = {"settings.ini", "state.ini", "water.txt", "screentime.tsv", "note.txt",
                                  "todos.tsv",    "timers.txt", "shelf.txt", "work.txt"};
constexpr const char* kFolders[] = {"mascots/", "ask/"};

// Only plain relative names of the files above: no "..", no drive or root, no backslashes.
inline bool IsBackupName(const std::string& name) {
    if (name.empty() || name.size() > 240 || name[0] == '/' || name.find('\\') != std::string::npos ||
        name.find(':') != std::string::npos || name.find("..") != std::string::npos ||
        name.find("//") != std::string::npos || name.back() == '/')
        return false;
    for (const char* f : kFiles)
        if (name == f) return true;
    for (const char* d : kFolders)
        if (name.rfind(d, 0) == 0 && name.size() > std::strlen(d)) return true;
    return false;
}

inline uint32_t Crc32(const std::string& s) {
    static uint32_t table[256];
    static bool ready = false;
    if (!ready) {
        for (uint32_t i = 0; i < 256; ++i) {
            uint32_t c = i;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            table[i] = c;
        }
        ready = true;
    }
    uint32_t c = 0xFFFFFFFFu;
    for (unsigned char ch : s) c = table[(c ^ ch) & 0xFF] ^ (c >> 8);
    return c ^ 0xFFFFFFFFu;
}

namespace detail {
inline void Put16(std::string* o, uint32_t v) { o->push_back(static_cast<char>(v & 0xFF)); o->push_back(static_cast<char>((v >> 8) & 0xFF)); }
inline void Put32(std::string* o, uint32_t v) { Put16(o, v & 0xFFFF); Put16(o, v >> 16); }
inline uint32_t Get16(const std::string& s, size_t at) {
    return static_cast<uint32_t>(static_cast<unsigned char>(s[at])) | static_cast<uint32_t>(static_cast<unsigned char>(s[at + 1])) << 8;
}
inline uint32_t Get32(const std::string& s, size_t at) { return Get16(s, at) | Get16(s, at + 2) << 16; }
}  // namespace detail

// The manifest's text: which app made it, when, on what.
inline std::string Manifest(const std::string& version, const std::string& platform, long long unixTime) {
    return "Bunbun backup\nformat=1\nversion=" + version + "\nplatform=" + platform +
           "\ncreated=" + std::to_string(unixTime) + "\n";
}

// A stored .zip of `files` (UTF-8 names), with the manifest first.
inline std::string Zip(const std::vector<File>& files, const std::string& manifest, long long unixTime) {
    using namespace detail;
    std::tm t{};
    const time_t when = static_cast<time_t>(unixTime);
#ifdef _WIN32
    localtime_s(&t, &when);
#else
    localtime_r(&when, &t);
#endif
    const uint32_t dosTime = static_cast<uint32_t>((t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec / 2));
    const uint32_t dosDate = static_cast<uint32_t>((((std::max)(1980, t.tm_year + 1900) - 1980) << 9) | ((t.tm_mon + 1) << 5) | t.tm_mday);
    std::vector<File> all;
    all.push_back({kManifest, manifest});
    all.insert(all.end(), files.begin(), files.end());

    std::string out, central;
    for (const File& f : all) {
        const uint32_t crc = Crc32(f.data);
        const uint32_t offset = static_cast<uint32_t>(out.size());
        Put32(&out, 0x04034b50); Put16(&out, 20); Put16(&out, 0x0800); Put16(&out, 0);
        Put16(&out, dosTime); Put16(&out, dosDate); Put32(&out, crc);
        Put32(&out, static_cast<uint32_t>(f.data.size())); Put32(&out, static_cast<uint32_t>(f.data.size()));
        Put16(&out, static_cast<uint32_t>(f.name.size())); Put16(&out, 0);
        out += f.name;
        out += f.data;

        Put32(&central, 0x02014b50); Put16(&central, 20); Put16(&central, 20); Put16(&central, 0x0800); Put16(&central, 0);
        Put16(&central, dosTime); Put16(&central, dosDate); Put32(&central, crc);
        Put32(&central, static_cast<uint32_t>(f.data.size())); Put32(&central, static_cast<uint32_t>(f.data.size()));
        Put16(&central, static_cast<uint32_t>(f.name.size())); Put16(&central, 0); Put16(&central, 0);
        Put16(&central, 0); Put16(&central, 0); Put32(&central, 0); Put32(&central, offset);
        central += f.name;
    }
    const uint32_t centralAt = static_cast<uint32_t>(out.size());
    out += central;
    Put32(&out, 0x06054b50); Put16(&out, 0); Put16(&out, 0);
    Put16(&out, static_cast<uint32_t>(all.size())); Put16(&out, static_cast<uint32_t>(all.size()));
    Put32(&out, static_cast<uint32_t>(central.size())); Put32(&out, centralAt); Put16(&out, 0);
    return out;
}

// Reads a backup made by Zip: its files (only allowed names) and its manifest.
// False with a reason for anything else (not a zip, compressed, damaged, not a Bunbun backup).
inline bool Unzip(const std::string& zip, std::vector<File>* files, std::string* manifest, std::string* err) {
    using namespace detail;
    files->clear();
    if (zip.size() > kMaxBytes) { *err = "the file is too large to be a Bunbun backup"; return false; }
    if (zip.size() < 22) { *err = "this isn't a zip file"; return false; }
    size_t eocd = std::string::npos;
    for (size_t i = zip.size() - 22 + 1; i-- > 0 && zip.size() - i <= 22 + 65535;) {
        if (Get32(zip, i) == 0x06054b50) { eocd = i; break; }
    }
    if (eocd == std::string::npos) { *err = "this isn't a zip file"; return false; }
    const uint32_t count = Get16(zip, eocd + 10);
    size_t at = Get32(zip, eocd + 16);
    bool sawManifest = false;
    for (uint32_t i = 0; i < count; ++i) {
        if (at + 46 > zip.size() || Get32(zip, at) != 0x02014b50) { *err = "the backup is damaged"; return false; }
        const uint32_t method = Get16(zip, at + 10);
        const uint32_t crc = Get32(zip, at + 16);
        const uint32_t size = Get32(zip, at + 20);
        const uint32_t nameLen = Get16(zip, at + 28), extraLen = Get16(zip, at + 30), commentLen = Get16(zip, at + 32);
        const uint32_t local = Get32(zip, at + 42);
        if (at + 46 + nameLen > zip.size()) { *err = "the backup is damaged"; return false; }
        const std::string name = zip.substr(at + 46, nameLen);
        at += 46 + nameLen + extraLen + commentLen;
        if (!name.empty() && name.back() == '/') continue;  // a folder entry
        if (method != 0) { *err = "this zip is compressed; use a backup made by Bunbun (Create backup)"; return false; }
        if (local + 30 > zip.size() || Get32(zip, local) != 0x04034b50) { *err = "the backup is damaged"; return false; }
        const size_t dataAt = local + 30 + Get16(zip, local + 26) + Get16(zip, local + 28);
        if (dataAt + size > zip.size()) { *err = "the backup is damaged"; return false; }
        std::string data = zip.substr(dataAt, size);
        if (Crc32(data) != crc) { *err = "the backup is damaged (" + name + ")"; return false; }
        if (name == kManifest) { *manifest = data; sawManifest = true; continue; }
        if (IsBackupName(name)) files->push_back({name, std::move(data)});
    }
    if (!sawManifest || manifest->rfind("Bunbun backup", 0) != 0) { *err = "this zip isn't a Bunbun backup"; return false; }
    return true;
}

// A value from the manifest ("version", "platform", "created"), or "".
inline std::string ManifestValue(const std::string& manifest, const std::string& key) {
    const std::string k = "\n" + key + "=";
    const size_t at = manifest.find(k);
    if (at == std::string::npos) return {};
    const size_t from = at + k.size();
    const size_t end = manifest.find('\n', from);
    return manifest.substr(from, end == std::string::npos ? std::string::npos : end - from);
}

}  // namespace bb::backup
