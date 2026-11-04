// Bunbun — lists of apps typed in Settings ("chrome, vlc; steam"), shared by the
// Windows island and the Linux one. An entry matches when it appears anywhere in
// the app's name (case-insensitive), so "chrome" matches "chrome.exe" and
// "Google-chrome"; a trailing ".exe" in an entry is ignored.
#pragma once

#include <cwctype>
#include <string>

namespace bb::applist {

template <typename Str>
Str Lower(Str s) {
    for (auto& c : s) c = static_cast<typename Str::value_type>(std::towlower(static_cast<wint_t>(c)));
    return s;
}

// True when one of the entries in `list` (separated by commas, semicolons or new
// lines) is part of `app`.
template <typename Str>
bool Matches(const Str& list, const Str& app) {
    if (list.empty() || app.empty()) return false;
    const Str name = Lower(app);
    size_t at = 0;
    while (at <= list.size()) {
        size_t end = list.find_first_of(Str{static_cast<typename Str::value_type>(','), static_cast<typename Str::value_type>(';'),
                                            static_cast<typename Str::value_type>('\n')}, at);
        if (end == Str::npos) end = list.size();
        Str entry = Lower(list.substr(at, end - at));
        while (!entry.empty() && (entry.back() == ' ' || entry.back() == '\t' || entry.back() == '\r')) entry.pop_back();
        while (!entry.empty() && (entry.front() == ' ' || entry.front() == '\t')) entry.erase(entry.begin());
        if (entry.size() > 4 && entry.compare(entry.size() - 4, 4, Str{'.', 'e', 'x', 'e'}) == 0) entry.resize(entry.size() - 4);
        if (!entry.empty() && name.find(entry) != Str::npos) return true;
        at = end + 1;
    }
    return false;
}

}  // namespace bb::applist
