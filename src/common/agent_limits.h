// Bunbun: what the Agent page's limits row shows for each coding agent, from
// what that agent keeps on this PC (no sign-ins, no network):
//   Claude Code  - its statusLine: the 5-hour and weekly limits, the context window
//   Antigravity  - its cached quota: one bar per pool of models (common/ag_quota.h)
//   Cursor       - only its plan; Cursor doesn't keep usage numbers locally
// Pure, shared by Windows (studio_draw.inc) and Linux (studio_linux.cpp).
#pragma once

#include <cwctype>
#include <string>
#include <vector>

#include "ag_quota.h"

namespace bb::limits {

struct Bar {
    std::wstring label;     // "5-hour", "Gemini 3.8 Flash"
    float used = -1.0f;     // percent used, -1 unknown
    long long resetAt = 0;  // Unix seconds, 0 unknown
};

struct Row {
    std::vector<Bar> bars;  // up to three
    std::wstring note;      // when there are no numbers to show
};

// "resets in 45m", "resets in 2h 10m", "resets in 3d"; "" when unknown or past.
inline std::wstring ResetsIn(long long resetAt, long long now) {
    if (resetAt <= now) return L"";
    const long long m = (resetAt - now + 59) / 60;
    if (m < 60) return L"resets in " + std::to_wstring(m) + L"m";
    const long long h = m / 60;
    if (h < 24) return L"resets in " + std::to_wstring(h) + L"h" + (m % 60 ? L" " + std::to_wstring(m % 60) + L"m" : L"");
    return L"resets in " + std::to_wstring((h + 12) / 24) + L"d";
}

// The statusLine's numbers are only current while Claude Code runs: a limit
// whose reset time has passed is back at zero.
inline float UsedNow(float used, long long resetAt, long long now) { return resetAt > 0 && resetAt <= now && used >= 0 ? 0.0f : used; }

inline Row Claude(float used5h, long long reset5h, float used7d, long long reset7d, float context, long long now) {
    Row r;
    if (used5h < 0 && used7d < 0 && context < 0) {
        r.note = L"Connect Claude Code in Settings to see its limits here";
        return r;
    }
    r.bars.push_back({L"5-hour", UsedNow(used5h, reset5h, now), reset5h});
    r.bars.push_back({L"Week", UsedNow(used7d, reset7d, now), reset7d});
    r.bars.push_back({L"Context", context, 0});
    return r;
}

inline Row Antigravity(const std::vector<bb::agquota::Pool>& pools, long long now) {
    Row r;
    if (pools.empty()) {
        r.note = L"Antigravity's quota shows here once it has run on this PC";
        return r;
    }
    for (size_t i = 0; i < pools.size() && i < 3; ++i) {
        const auto& p = pools[i];
        r.bars.push_back({std::wstring(p.name.begin(), p.name.end()),
                          (1.0f - bb::agquota::RemainingNow(p.remaining, p.resetAt, now)) * 100.0f,
                          p.resetAt > now ? p.resetAt : 0});
    }
    return r;
}

// Cursor's plan ("free", "pro") as shown: "Free plan".
inline std::wstring PlanName(std::wstring plan) {
    if (plan.empty()) return L"";
    for (auto& c : plan) c = static_cast<wchar_t>(towlower(c));
    if (plan == L"free_trial") return L"Pro trial";
    plan[0] = static_cast<wchar_t>(towupper(plan[0]));
    for (auto& c : plan) if (c == L'_') c = L' ';
    return plan + L" plan";
}

inline Row Cursor(const std::wstring& plan) {
    Row r;
    const std::wstring name = PlanName(plan);
    r.note = (name.empty() ? L"" : name + L" \x00B7 ") + std::wstring(L"Cursor doesn't keep its usage on this PC");
    return r;
}

}  // namespace bb::limits
