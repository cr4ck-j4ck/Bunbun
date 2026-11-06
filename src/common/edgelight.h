// Bunbun: what plays the edge light (Bunbun.EdgeLightOn). Pure, shared with the
// tests; the light itself is src/island/modules/edgelight_*.inc.
#pragma once

#include <cwctype>
#include <string>
#include <utility>

namespace bb::edgelight {

enum : unsigned {
    kOnHover = 1u << 0,         // the cursor comes onto the island
    kOnOpen = 1u << 1,          // the island opens (hover-expand, click or pin)
    kOnClose = 1u << 2,         // it folds back
    kOnTrack = 1u << 3,         // the song changes
    kOnNotification = 1u << 4,  // notifications, reminders, meetings, downloads, copies
    kOnAgent = 1u << 5,         // a coding agent shows up or needs you
    kOnSystem = 1u << 6,        // volume, brightness, battery, Bluetooth, Caps Lock, timers...
    kOnAlways = 1u << 7,        // runs all the time
};

constexpr const wchar_t* kDefaultOn = L"open,notification,agent";

// "hover, Open,track" -> bits. Unknown words are skipped; empty means the default.
inline unsigned ParseOn(std::wstring text) {
    for (auto& c : text) c = static_cast<wchar_t>(towlower(c));
    if (text.find_first_not_of(L" \t") == std::wstring::npos) text = kDefaultOn;
    static const std::pair<const wchar_t*, unsigned> kWords[] = {
        {L"hover", kOnHover}, {L"open", kOnOpen}, {L"close", kOnClose}, {L"track", kOnTrack},
        {L"notification", kOnNotification}, {L"agent", kOnAgent}, {L"system", kOnSystem}, {L"always", kOnAlways}};
    unsigned bits = 0;
    size_t at = 0;
    while (at <= text.size()) {
        size_t comma = text.find(L',', at);
        if (comma == std::wstring::npos) comma = text.size();
        std::wstring w = text.substr(at, comma - at);
        const size_t a = w.find_first_not_of(L" \t");
        w = a == std::wstring::npos ? std::wstring() : w.substr(a, w.find_last_not_of(L" \t") - a + 1);
        for (const auto& [name, bit] : kWords)
            if (w == name) bits |= bit;
        at = comma + 1;
    }
    return bits;
}

}  // namespace bb::edgelight
