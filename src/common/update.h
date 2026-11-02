// Bunbun — update check.
//
// Asks GitHub for the latest published release and compares it with the running
// version. Read-only: nothing is downloaded or installed; callers only tell the
// user and open the release page. Shared by bunbun.exe (daily check, tray) and
// bunbun-settings.exe ("Check for updates").
#pragma once

#include <windows.h>
#include <winhttp.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

#include "json.h"

#ifndef BUNBUN_REPO_STR
#define BUNBUN_REPO_STR "iaryasharma/Bunbun"
#endif

namespace bb::update {

struct Release {
    bool ok = false;
    std::string version;  // "0.2.0" (tag without the leading v)
    std::string url;      // release page
    std::string name;     // release title
    std::string error;    // why it failed, for the settings app
};

// Numeric compare of dotted versions; "1.2" == "1.2.0". A pre-release suffix
// ("0.2.0-beta") sorts before the plain version. <0, 0, >0 like strcmp.
inline int CompareVersions(const std::string& a, const std::string& b) {
    auto parse = [](const std::string& v, std::vector<long>* parts, bool* pre) {
        std::string s = v;
        if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
        const size_t dash = s.find_first_of("-+");
        *pre = dash != std::string::npos && s[dash] == '-';
        if (dash != std::string::npos) s.resize(dash);
        size_t pos = 0;
        while (pos <= s.size()) {
            const size_t dot = s.find('.', pos);
            const std::string piece = s.substr(pos, dot == std::string::npos ? std::string::npos : dot - pos);
            parts->push_back(piece.empty() ? 0 : std::strtol(piece.c_str(), nullptr, 10));
            if (dot == std::string::npos) break;
            pos = dot + 1;
        }
    };
    std::vector<long> pa, pb;
    bool preA = false, preB = false;
    parse(a, &pa, &preA);
    parse(b, &pb, &preB);
    for (size_t i = 0; i < std::max(pa.size(), pb.size()); ++i) {
        const long x = i < pa.size() ? pa[i] : 0, y = i < pb.size() ? pb[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    if (preA != preB) return preA ? -1 : 1;
    return 0;
}

// Packs "1.2.3" into one int (1002003) so it fits Bunbun's int-only state store.
inline int PackVersion(const std::string& v) {
    std::string s = v;
    if (!s.empty() && (s[0] == 'v' || s[0] == 'V')) s.erase(0, 1);
    long part[3] = {0, 0, 0};
    size_t pos = 0;
    for (int i = 0; i < 3 && pos <= s.size(); ++i) {
        part[i] = std::strtol(s.c_str() + pos, nullptr, 10);
        const size_t dot = s.find('.', pos);
        if (dot == std::string::npos) break;
        pos = dot + 1;
    }
    return static_cast<int>(part[0] * 1000000 + part[1] * 1000 + part[2]);
}

inline std::string Narrow(const std::wstring& s) {
    if (s.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n, nullptr, nullptr);
    return out;
}

inline std::wstring Widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring out(static_cast<size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
    return out;
}

// GET https://api.github.com/repos/<repo>/releases/latest. Blocking, with short
// timeouts; call it off the UI thread.
inline Release FetchLatest(const wchar_t* userAgent) {
    Release r;
    HINTERNET session = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                                    WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) { r.error = "couldn't start a web session"; return r; }
    WinHttpSetTimeouts(session, 5000, 5000, 8000, 8000);
    const std::wstring path = L"/repos/" + Widen(BUNBUN_REPO_STR) + L"/releases/latest";
    HINTERNET connect = WinHttpConnect(session, L"api.github.com", INTERNET_DEFAULT_HTTPS_PORT, 0);
    HINTERNET request = connect ? WinHttpOpenRequest(connect, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                                     WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)
                                : nullptr;
    std::string body;
    DWORD status = 0;
    if (request &&
        WinHttpSendRequest(request, L"Accept: application/vnd.github+json\r\n", static_cast<DWORD>(-1L),
                           WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(request, nullptr)) {
        DWORD size = sizeof(status);
        WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                            &status, &size, WINHTTP_NO_HEADER_INDEX);
        for (;;) {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(request, &avail) || avail == 0) break;
            if (body.size() + avail > 512 * 1024) break;  // a release JSON is a few KB
            std::string chunk(avail, '\0');
            DWORD read = 0;
            if (!WinHttpReadData(request, chunk.data(), avail, &read) || read == 0) break;
            body.append(chunk.data(), read);
        }
    } else {
        r.error = "couldn't reach GitHub (offline?)";
    }
    if (request) WinHttpCloseHandle(request);
    if (connect) WinHttpCloseHandle(connect);
    WinHttpCloseHandle(session);
    if (!r.error.empty()) return r;
    if (status == 404) { r.error = "no published release yet"; return r; }
    if (status == 403 || status == 429) { r.error = "GitHub is rate-limiting checks; try again later"; return r; }
    if (status != 200) { r.error = "GitHub answered HTTP " + std::to_string(status); return r; }

    bb::json::Value root;
    if (!bb::json::Parse(body, &root) || !root.isObject()) { r.error = "unexpected reply from GitHub"; return r; }
    std::string tag = root.getString("tag_name");
    if (!tag.empty() && (tag[0] == 'v' || tag[0] == 'V')) tag.erase(0, 1);
    if (tag.empty()) { r.error = "the latest release has no version tag"; return r; }
    r.version = tag;
    r.url = root.getString("html_url");
    r.name = root.getString("name");
    // Only ever open a GitHub page from this reply.
    if (r.url.rfind("https://github.com/", 0) != 0) r.url = "https://github.com/" BUNBUN_REPO_STR "/releases/latest";
    r.ok = true;
    return r;
}

}  // namespace bb::update
