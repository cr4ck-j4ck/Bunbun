// The diff an approval card shows: the edit's old and new text as lines, for the
// Windows card (island_core.cpp) and the Linux one (island_linux.cpp) alike.
#pragma once

#include <string>
#include <vector>

#include "agent_view.h"

namespace bb {

struct DiffRow {
    int sign = 0;  // -1 removed, +1 added
    std::wstring text;
};

// Each block is split into lines and loses its shared indentation, so code nested
// deep in a file doesn't start halfway across the card.
inline void AppendDiffBlock(std::vector<DiffRow>& rows, const std::wstring& block, int sign) {
    if (block.empty()) return;
    std::vector<std::wstring> lines;
    size_t start = 0;
    while (start <= block.size()) {
        const size_t nl = block.find(L'\n', start);
        lines.push_back(block.substr(start, nl == std::wstring::npos ? std::wstring::npos : nl - start));
        if (nl == std::wstring::npos) break;
        start = nl + 1;
    }
    while (lines.size() > 1 && lines.back().find_first_not_of(L' ') == std::wstring::npos) lines.pop_back();
    size_t indent = std::wstring::npos;
    for (const auto& l : lines) {
        const size_t first = l.find_first_not_of(L' ');
        if (first != std::wstring::npos && first < indent) indent = first;
    }
    if (indent == std::wstring::npos) indent = 0;
    for (auto& l : lines) rows.push_back({sign, l.size() > indent ? l.substr(indent) : std::wstring()});
}

inline std::vector<DiffRow> DiffRows(const AgentView& a) {
    std::vector<DiffRow> rows;
    AppendDiffBlock(rows, a.diffOld, -1);
    AppendDiffBlock(rows, a.diffNew, +1);
    return rows;
}

// The first line of a diff snippet, for the one-line rows (Studio's Agent page).
inline std::wstring FirstDiffLine(const std::wstring& s) {
    const size_t start = s.find_first_not_of(L" \n");
    if (start == std::wstring::npos) return std::wstring();
    const size_t nl = s.find(L'\n', start);
    return nl == std::wstring::npos ? s.substr(start) : s.substr(start, nl - start) + L" \x2026";
}

}  // namespace bb
