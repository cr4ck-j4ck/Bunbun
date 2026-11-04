// Bunbun — Antigravity's model quota, from the status it caches in its VS Code-
// style state database (state.vscdb, key antigravityUnifiedStateSync.userStatus):
// base64 of a small wrapper holding base64 of a protobuf. Each model config sits
// at field 33 -> 1, with its name as a string and its quota at field 15:
// 1 = fraction remaining (float), 2 = reset time (a Timestamp, 1 = Unix seconds).
// Pure decoding, shared by Windows (studio_module.inc, winsqlite3) and Linux
// (studio_linux.cpp, libsqlite3 loaded at run time). No network, no credentials.
#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace bb::agquota {

inline std::string Base64Decode(const std::string& in) {
    static const std::string kAlphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    int val = 0, bits = -8;
    for (char c : in) {
        const size_t d = kAlphabet.find(c);
        if (d == std::string::npos) continue;  // padding, whitespace
        val = (val << 6) + static_cast<int>(d);
        bits += 6;
        if (bits >= 0) {
            out.push_back(static_cast<char>((val >> bits) & 0xFF));
            bits -= 8;
        }
    }
    return out;
}

struct Field {
    uint32_t num = 0;
    int wire = 0;
    uint64_t varint = 0;
    float f32 = 0.0f;
    std::string_view bytes;
};

// One level of a protobuf message. False when the bytes aren't a clean message.
inline bool Parse(std::string_view b, std::vector<Field>* out) {
    size_t i = 0;
    auto readVarint = [&](uint64_t* v) {
        *v = 0;
        for (int shift = 0; shift < 64 && i < b.size(); shift += 7) {
            const uint8_t c = static_cast<uint8_t>(b[i++]);
            *v |= static_cast<uint64_t>(c & 0x7F) << shift;
            if (!(c & 0x80)) return true;
        }
        return false;
    };
    while (i < b.size()) {
        uint64_t key = 0;
        if (!readVarint(&key) || (key >> 3) == 0) return false;
        Field f;
        f.num = static_cast<uint32_t>(key >> 3);
        f.wire = static_cast<int>(key & 7);
        if (f.wire == 0) {
            if (!readVarint(&f.varint)) return false;
        } else if (f.wire == 1) {
            if (i + 8 > b.size()) return false;
            i += 8;
        } else if (f.wire == 5) {
            if (i + 4 > b.size()) return false;
            memcpy(&f.f32, b.data() + i, 4);
            i += 4;
        } else if (f.wire == 2) {
            uint64_t n = 0;
            if (!readVarint(&n) || i + n > b.size()) return false;
            f.bytes = b.substr(i, static_cast<size_t>(n));
            i += static_cast<size_t>(n);
        } else {
            return false;
        }
        out->push_back(f);
    }
    return true;
}

inline bool Printable(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (static_cast<unsigned char>(c) < 32 || static_cast<unsigned char>(c) > 126) return false;
    return true;
}

struct Quota {
    float remaining = 1.0f;  // lowest fraction left across models
    std::string model;       // the model with that lowest fraction
    long long resetAt = 0;   // its reset time, Unix seconds
};

// Decodes the stored value. False when the layout isn't the expected one.
inline bool Decode(const std::string& value, Quota* out) {
    // Outer layer: base64 of a wrapper whose longest string is base64 of the status.
    const std::string wrapper = Base64Decode(value);
    std::string inner;
    {
        size_t best = 0, start = std::string::npos;
        for (size_t i = 0, run = 0; i <= wrapper.size(); ++i) {
            const bool b64 = i < wrapper.size() &&
                             (isalnum(static_cast<unsigned char>(wrapper[i])) || wrapper[i] == '+' || wrapper[i] == '/' || wrapper[i] == '=');
            if (b64) { if (!run) start = i; ++run; }
            else { if (run > best) { best = run; inner = wrapper.substr(start, run); } run = 0; }
        }
    }
    if (inner.size() < 100) return false;
    // The run can start with a protobuf length byte that happens to be a base64
    // character, so try each alignment and keep the one that parses cleanly.
    std::string status;
    std::vector<Field> root;
    for (size_t cut = 0; cut < 4 && root.empty(); ++cut) {
        std::string candidate = Base64Decode(inner.substr(cut));
        std::vector<Field> fields;
        if (Parse(candidate, &fields) &&
            std::any_of(fields.begin(), fields.end(), [](const Field& f) { return f.num == 33 && f.wire == 2; })) {
            status = std::move(candidate);
            root = std::move(fields);
        }
    }
    if (root.empty()) return false;
    root.clear();  // the fields point into the decoded text: parse again from where it now lives
    if (!Parse(status, &root)) return false;
    bool valid = false;
    Quota q;
    for (const Field& f33 : root) {
        if (f33.num != 33 || f33.wire != 2) continue;
        std::vector<Field> group;
        if (!Parse(f33.bytes, &group)) continue;
        for (const Field& model : group) {
            if (model.num != 1 || model.wire != 2) continue;
            std::vector<Field> cfg;
            if (!Parse(model.bytes, &cfg)) continue;
            std::string name;
            float remaining = -1.0f;
            long long reset = 0;
            for (const Field& c : cfg) {
                if (c.wire != 2) continue;
                if (name.empty() && Printable(c.bytes) && c.bytes.size() < 80) name = std::string(c.bytes);
                if (c.num == 15) {
                    std::vector<Field> quota;
                    if (!Parse(c.bytes, &quota)) continue;
                    for (const Field& f : quota) {
                        if (f.num == 1 && f.wire == 5) remaining = f.f32;
                        if (f.num == 2 && f.wire == 2) {
                            std::vector<Field> ts;
                            if (Parse(f.bytes, &ts))
                                for (const Field& t : ts) if (t.num == 1 && t.wire == 0) reset = static_cast<long long>(t.varint);
                        }
                    }
                }
            }
            if (remaining < 0.0f || remaining > 1.0001f) continue;
            if (!valid || remaining < q.remaining) {
                valid = true;
                q.remaining = remaining;
                q.model = name;
                q.resetAt = reset;
            }
        }
    }
    if (valid) *out = q;
    return valid;
}

}  // namespace bb::agquota
