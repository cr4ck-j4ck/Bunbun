// Bunbun — the agent channel on Linux: the same protocol as pipe.h, over a
// Unix domain socket at $XDG_RUNTIME_DIR/bunbun/agent.sock.
//
// The hook connects, writes one JSON object terminated by '\n', and for
// blocking events waits for one reply line. Only the current user can reach
// the socket (its folder is mode 0700), and both sides check the peer's uid
// with SO_PEERCRED, as the Windows pipe checks the server's SID.
#pragma once

#include <errno.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <string>

namespace bb::usock {

constexpr size_t kMaxMessage = 4u * 1024u * 1024u;

inline std::string Dir() {
    const char* rt = getenv("XDG_RUNTIME_DIR");
    std::string base = rt && rt[0] == '/' ? rt : "/tmp/bunbun-" + std::to_string(getuid());
    mkdir(base.c_str(), 0700);
    return base + "/bunbun";
}

inline std::string Path() { return Dir() + "/agent.sock"; }

inline bool FillAddress(const std::string& path, sockaddr_un* addr) {
    memset(addr, 0, sizeof *addr);
    addr->sun_family = AF_UNIX;
    if (path.size() >= sizeof addr->sun_path) return false;
    memcpy(addr->sun_path, path.c_str(), path.size() + 1);
    return true;
}

// True when the other end of `fd` runs as the same user as we do.
inline bool PeerIsSameUser(int fd) {
    ucred cred{};
    socklen_t len = sizeof cred;
    return getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &cred, &len) == 0 && cred.uid == getuid();
}

// -1 when the island isn't listening.
inline int Connect() {
    sockaddr_un addr;
    if (!FillAddress(Path(), &addr)) return -1;
    const int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    if (connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0 || !PeerIsSameUser(fd)) {
        close(fd);
        return -1;
    }
    return fd;
}

inline bool WriteAll(int fd, const std::string& data) {
    size_t off = 0;
    while (off < data.size()) {
        const ssize_t n = send(fd, data.data() + off, data.size() - off, MSG_NOSIGNAL);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        off += static_cast<size_t>(n);
    }
    return true;
}

// Reads until '\n', EOF, kMaxMessage or the timeout (-1 = none). Returns false
// on error or timeout with no complete line.
inline bool ReadLine(int fd, std::string* out, int timeoutMs = -1) {
    out->clear();
    char buf[4096];
    while (out->size() < kMaxMessage) {
        pollfd p{fd, POLLIN, 0};
        const int r = poll(&p, 1, timeoutMs);
        if (r < 0 && errno == EINTR) continue;
        if (r <= 0) return false;
        const ssize_t n = recv(fd, buf, sizeof buf, 0);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        out->append(buf, static_cast<size_t>(n));
        const size_t nl = out->find('\n');
        if (nl != std::string::npos) {
            out->resize(nl);
            return true;
        }
    }
    return !out->empty();
}

}  // namespace bb::usock
