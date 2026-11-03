// Bunbun — Linux platform basics (see platform.h).
#include "platform.h"

#include <fcntl.h>
#include <spawn.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

extern char** environ;

namespace bb::plat {
namespace {

std::string Env(const char* name) {
    const char* v = getenv(name);
    return v ? v : "";
}

std::string Xdg(const char* var, const char* fallback) {
    const std::string v = Env(var);
    return !v.empty() && v[0] == '/' ? v : HomeDir() + fallback;
}

}  // namespace

std::string Utf8(const std::wstring& w) {
    std::string out;
    out.reserve(w.size());
    for (wchar_t wc : w) {
        auto c = static_cast<uint32_t>(wc);
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
        if (c < 0x80) {
            out += static_cast<char>(c);
        } else if (c < 0x800) {
            out += static_cast<char>(0xC0 | (c >> 6));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else if (c < 0x10000) {
            out += static_cast<char>(0xE0 | (c >> 12));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (c >> 18));
            out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (c & 0x3F));
        }
    }
    return out;
}

std::wstring Wide(const std::string& s) {
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        const auto b = static_cast<unsigned char>(s[i]);
        uint32_t c = 0xFFFD;
        size_t n = 1;
        if (b < 0x80) { c = b; }
        else if ((b >> 5) == 6 && i + 1 < s.size()) { c = ((b & 0x1F) << 6) | (s[i + 1] & 0x3F); n = 2; }
        else if ((b >> 4) == 14 && i + 2 < s.size()) { c = ((b & 0x0F) << 12) | ((s[i + 1] & 0x3F) << 6) | (s[i + 2] & 0x3F); n = 3; }
        else if ((b >> 3) == 30 && i + 3 < s.size()) {
            c = ((b & 0x07) << 18) | ((s[i + 1] & 0x3F) << 12) | ((s[i + 2] & 0x3F) << 6) | (s[i + 3] & 0x3F);
            n = 4;
        }
        out += static_cast<wchar_t>(c);
        i += n;
    }
    return out;
}

std::string Native(const std::wstring& path) {
    std::string p = Utf8(path);
    for (char& c : p) if (c == '\\') c = '/';
    return p;
}

std::string HomeDir() {
    const std::string h = Env("HOME");
    return h.empty() ? "/tmp" : h;
}

std::string ConfigDir() { return Xdg("XDG_CONFIG_HOME", "/.config") + "/Bunbun"; }
std::string StateDir() { return Xdg("XDG_STATE_HOME", "/.local/state") + "/Bunbun"; }

std::string RuntimeDir() {
    std::string base = Env("XDG_RUNTIME_DIR");
    if (base.empty() || base[0] != '/') base = "/tmp/bunbun-" + std::to_string(getuid());
    const std::string dir = base + "/bunbun";
    mkdir(base.c_str(), 0700);
    mkdir(dir.c_str(), 0700);
    return dir;
}

std::string ExeDir() {
    char buf[4096];
    const ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ".";
    std::string p(buf, static_cast<size_t>(n));
    return p.substr(0, p.find_last_of('/'));
}

std::string DataDir() {
    if (std::string d = Env("BUNBUN_DATA_DIR"); !d.empty()) return d;
    const std::string exe = ExeDir();
    if (FileExists(exe + "/settings-ui/index.html")) return exe;
    // Installed: <prefix>/lib/bunbun/bunbun -> <prefix>/share/bunbun;
    //            <prefix>/bin/bunbun       -> <prefix>/share/bunbun.
    for (const char* rel : {"/../../share/bunbun", "/../share/bunbun"})
        if (FileExists(exe + rel + "/settings-ui/index.html")) return exe + rel;
    return exe;
}

bool FileExists(const std::string& path) { return access(path.c_str(), F_OK) == 0; }

bool MakeDirs(const std::string& path) {
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return !ec;
}

bool ReadFile(const std::string& path, std::string* out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    *out = ss.str();
    return true;
}

bool WriteFileAtomic(const std::string& path, const std::string& data) {
    MakeDirs(path.substr(0, path.find_last_of('/')));
    const std::string tmp = path + ".bunbun-tmp";
    const int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) return false;
    size_t done = 0;
    while (done < data.size()) {
        const ssize_t n = write(fd, data.data() + done, data.size() - done);
        if (n <= 0) { close(fd); unlink(tmp.c_str()); return false; }
        done += static_cast<size_t>(n);
    }
    fsync(fd);
    close(fd);
    return rename(tmp.c_str(), path.c_str()) == 0;
}

bool Spawn(const std::vector<std::string>& argv) {
    if (argv.empty()) return false;
    std::vector<char*> args;
    for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
    args.push_back(nullptr);
    // Double fork, so the child is reparented and never becomes a zombie of ours.
    const pid_t mid = fork();
    if (mid < 0) return false;
    if (mid == 0) {
        setsid();
        pid_t child = 0;
        const int rc = posix_spawnp(&child, args[0], nullptr, nullptr, args.data(), environ);
        _exit(rc == 0 ? 0 : 127);
    }
    int status = 0;
    waitpid(mid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

bool OpenWithDefault(const std::string& target) { return Spawn({"xdg-open", target}); }

}  // namespace bb::plat
