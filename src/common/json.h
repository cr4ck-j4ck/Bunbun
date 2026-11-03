// Bunbun — minimal JSON DOM (parse + serialize).
// Strict RFC 8259 parser: used for agent hook payloads and for the
// settings.json merge, so it must never silently accept malformed input.
// Object key order is preserved so rewritten config files diff cleanly.
#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace bb::json {

struct Value;
using Member = std::pair<std::string, Value>;

struct Value {
    enum class Type { Null, Bool, Number, String, Array, Object };

    Type type = Type::Null;
    bool b = false;
    double num = 0;
    std::string str;            // String payload, or raw number text when parsed
    std::vector<Value> arr;
    std::vector<Member> obj;

    Value() = default;
    static Value Null() { return {}; }
    static Value Bool(bool v) { Value x; x.type = Type::Bool; x.b = v; return x; }
    static Value Number(double v) { Value x; x.type = Type::Number; x.num = v; return x; }
    static Value String(std::string v) { Value x; x.type = Type::String; x.str = std::move(v); return x; }
    static Value Array() { Value x; x.type = Type::Array; return x; }
    static Value Object() { Value x; x.type = Type::Object; return x; }

    bool isNull() const { return type == Type::Null; }
    bool isBool() const { return type == Type::Bool; }
    bool isNumber() const { return type == Type::Number; }
    bool isString() const { return type == Type::String; }
    bool isArray() const { return type == Type::Array; }
    bool isObject() const { return type == Type::Object; }

    const Value* find(const std::string& key) const {
        if (type != Type::Object) return nullptr;
        for (auto& m : obj) if (m.first == key) return &m.second;
        return nullptr;
    }
    Value* find(const std::string& key) {
        if (type != Type::Object) return nullptr;
        for (auto& m : obj) if (m.first == key) return &m.second;
        return nullptr;
    }
    // Insert-or-get; converts Null into Object.
    Value& operator[](const std::string& key) {
        if (type == Type::Null) type = Type::Object;
        if (Value* v = find(key)) return *v;
        obj.emplace_back(key, Value());
        return obj.back().second;
    }
    bool erase(const std::string& key) {
        for (size_t i = 0; i < obj.size(); ++i)
            if (obj[i].first == key) { obj.erase(obj.begin() + i); return true; }
        return false;
    }
    void push(Value v) { if (type == Type::Null) type = Type::Array; arr.push_back(std::move(v)); }

    std::string getString(const std::string& key, const std::string& def = {}) const {
        const Value* v = find(key);
        return (v && v->isString()) ? v->str : def;
    }
    double getNumber(const std::string& key, double def = 0) const {
        const Value* v = find(key);
        return (v && v->isNumber()) ? v->num : def;
    }
    bool getBool(const std::string& key, bool def = false) const {
        const Value* v = find(key);
        return (v && v->isBool()) ? v->b : def;
    }
};

// ── Parser ──────────────────────────────────────────────────────────────────
class Parser {
public:
    explicit Parser(const std::string& s) : s_(s) {}

    bool parse(Value* out, std::string* err) {
        skipWs();
        if (!parseValue(out, 0)) { if (err) *err = err_; return false; }
        skipWs();
        if (i_ != s_.size()) { if (err) *err = "trailing characters at " + std::to_string(i_); return false; }
        return true;
    }

private:
    const std::string& s_;
    size_t i_ = 0;
    std::string err_;

    bool fail(const char* m) { err_ = std::string(m) + " at " + std::to_string(i_); return false; }
    void skipWs() { while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_; }
    bool lit(const char* w) {
        size_t n = strlen(w);
        if (s_.compare(i_, n, w) != 0) return false;
        i_ += n; return true;
    }

    bool parseValue(Value* v, int depth) {
        if (depth > 256) return fail("nesting too deep");
        if (i_ >= s_.size()) return fail("unexpected end");
        char c = s_[i_];
        if (c == '{') return parseObject(v, depth);
        if (c == '[') return parseArray(v, depth);
        if (c == '"') { v->type = Value::Type::String; return parseString(&v->str); }
        if (c == 't') { if (!lit("true")) return fail("bad literal"); *v = Value::Bool(true); return true; }
        if (c == 'f') { if (!lit("false")) return fail("bad literal"); *v = Value::Bool(false); return true; }
        if (c == 'n') { if (!lit("null")) return fail("bad literal"); *v = Value(); return true; }
        if (c == '-' || (c >= '0' && c <= '9')) return parseNumber(v);
        return fail("unexpected character");
    }

    bool parseNumber(Value* v) {
        size_t st = i_;
        if (s_[i_] == '-') ++i_;
        if (i_ >= s_.size()) return fail("bad number");
        if (s_[i_] == '0') ++i_;
        else if (s_[i_] >= '1' && s_[i_] <= '9') { while (i_ < s_.size() && isdigit((unsigned char)s_[i_])) ++i_; }
        else return fail("bad number");
        if (i_ < s_.size() && s_[i_] == '.') {
            ++i_; if (i_ >= s_.size() || !isdigit((unsigned char)s_[i_])) return fail("bad fraction");
            while (i_ < s_.size() && isdigit((unsigned char)s_[i_])) ++i_;
        }
        if (i_ < s_.size() && (s_[i_] == 'e' || s_[i_] == 'E')) {
            ++i_; if (i_ < s_.size() && (s_[i_] == '+' || s_[i_] == '-')) ++i_;
            if (i_ >= s_.size() || !isdigit((unsigned char)s_[i_])) return fail("bad exponent");
            while (i_ < s_.size() && isdigit((unsigned char)s_[i_])) ++i_;
        }
        v->type = Value::Type::Number;
        v->str = s_.substr(st, i_ - st);   // keep original text for lossless round-trip
        v->num = strtod(v->str.c_str(), nullptr);
        return true;
    }

    static void appendUtf8(std::string* o, uint32_t cp) {
        if (cp < 0x80) o->push_back((char)cp);
        else if (cp < 0x800) { o->push_back((char)(0xC0 | (cp >> 6))); o->push_back((char)(0x80 | (cp & 0x3F))); }
        else if (cp < 0x10000) { o->push_back((char)(0xE0 | (cp >> 12))); o->push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o->push_back((char)(0x80 | (cp & 0x3F))); }
        else { o->push_back((char)(0xF0 | (cp >> 18))); o->push_back((char)(0x80 | ((cp >> 12) & 0x3F))); o->push_back((char)(0x80 | ((cp >> 6) & 0x3F))); o->push_back((char)(0x80 | (cp & 0x3F))); }
    }
    bool hex4(uint32_t* out) {
        if (i_ + 4 > s_.size()) return fail("bad \\u escape");
        uint32_t v = 0;
        for (int k = 0; k < 4; ++k) {
            char c = s_[i_++]; v <<= 4;
            if (c >= '0' && c <= '9') v |= c - '0';
            else if (c >= 'a' && c <= 'f') v |= c - 'a' + 10;
            else if (c >= 'A' && c <= 'F') v |= c - 'A' + 10;
            else return fail("bad hex digit");
        }
        *out = v; return true;
    }
    bool parseString(std::string* out) {
        ++i_;  // opening quote
        out->clear();
        while (true) {
            if (i_ >= s_.size()) return fail("unterminated string");
            unsigned char c = (unsigned char)s_[i_++];
            if (c == '"') return true;
            if (c < 0x20) return fail("control character in string");
            if (c != '\\') { out->push_back((char)c); continue; }
            if (i_ >= s_.size()) return fail("bad escape");
            char e = s_[i_++];
            switch (e) {
                case '"': out->push_back('"'); break;
                case '\\': out->push_back('\\'); break;
                case '/': out->push_back('/'); break;
                case 'b': out->push_back('\b'); break;
                case 'f': out->push_back('\f'); break;
                case 'n': out->push_back('\n'); break;
                case 'r': out->push_back('\r'); break;
                case 't': out->push_back('\t'); break;
                case 'u': {
                    uint32_t cp; if (!hex4(&cp)) return false;
                    if (cp >= 0xD800 && cp <= 0xDBFF) {
                        uint32_t lo;
                        if (i_ + 2 <= s_.size() && s_[i_] == '\\' && s_[i_ + 1] == 'u') {
                            i_ += 2; if (!hex4(&lo)) return false;
                            if (lo >= 0xDC00 && lo <= 0xDFFF) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                            else { appendUtf8(out, 0xFFFD); cp = lo; }
                        } else cp = 0xFFFD;
                    } else if (cp >= 0xDC00 && cp <= 0xDFFF) cp = 0xFFFD;
                    appendUtf8(out, cp);
                    break;
                }
                default: return fail("bad escape");
            }
        }
    }
    bool parseArray(Value* v, int depth) {
        ++i_; *v = Value::Array(); skipWs();
        if (i_ < s_.size() && s_[i_] == ']') { ++i_; return true; }
        while (true) {
            Value e; skipWs();
            if (!parseValue(&e, depth + 1)) return false;
            v->arr.push_back(std::move(e)); skipWs();
            if (i_ >= s_.size()) return fail("unterminated array");
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == ']') { ++i_; return true; }
            return fail("expected , or ]");
        }
    }
    bool parseObject(Value* v, int depth) {
        ++i_; *v = Value::Object(); skipWs();
        if (i_ < s_.size() && s_[i_] == '}') { ++i_; return true; }
        while (true) {
            skipWs();
            if (i_ >= s_.size() || s_[i_] != '"') return fail("expected key");
            std::string key; if (!parseString(&key)) return false;
            skipWs();
            if (i_ >= s_.size() || s_[i_] != ':') return fail("expected :");
            ++i_; skipWs();
            Value e; if (!parseValue(&e, depth + 1)) return false;
            // Duplicate keys: last one wins (matches JSON.parse), position of first kept.
            if (Value* ex = v->find(key)) *ex = std::move(e);
            else v->obj.emplace_back(std::move(key), std::move(e));
            skipWs();
            if (i_ >= s_.size()) return fail("unterminated object");
            if (s_[i_] == ',') { ++i_; continue; }
            if (s_[i_] == '}') { ++i_; return true; }
            return fail("expected , or }");
        }
    }
};

inline bool Parse(const std::string& text, Value* out, std::string* err = nullptr) {
    std::string t = text;
    if (t.size() >= 3 && (unsigned char)t[0] == 0xEF && (unsigned char)t[1] == 0xBB && (unsigned char)t[2] == 0xBF) t.erase(0, 3);
    Parser p(t);
    return p.parse(out, err);
}

// ── Serializer ──────────────────────────────────────────────────────────────
inline void EscapeTo(std::string* o, const std::string& s) {
    o->push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"': *o += "\\\""; break;
            case '\\': *o += "\\\\"; break;
            case '\n': *o += "\\n"; break;
            case '\r': *o += "\\r"; break;
            case '\t': *o += "\\t"; break;
            case '\b': *o += "\\b"; break;
            case '\f': *o += "\\f"; break;
            default:
                if (c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); *o += b; }
                else o->push_back((char)c);
        }
    }
    o->push_back('"');
}

inline void SerializeTo(std::string* o, const Value& v, int indent, int level) {
    auto nl = [&](int lv) { if (indent > 0) { o->push_back('\n'); o->append((size_t)(lv * indent), ' '); } };
    switch (v.type) {
        case Value::Type::Null: *o += "null"; break;
        case Value::Type::Bool: *o += v.b ? "true" : "false"; break;
        case Value::Type::Number:
            if (!v.str.empty()) *o += v.str;
            else if (std::isfinite(v.num) && v.num == std::floor(v.num) && std::fabs(v.num) < 1e15) { char b[32]; snprintf(b, sizeof b, "%lld", (long long)v.num); *o += b; }
            else if (std::isfinite(v.num)) { char b[32]; snprintf(b, sizeof b, "%.17g", v.num); *o += b; }
            else *o += "null";
            break;
        case Value::Type::String: EscapeTo(o, v.str); break;
        case Value::Type::Array:
            if (v.arr.empty()) { *o += "[]"; break; }
            o->push_back('[');
            for (size_t k = 0; k < v.arr.size(); ++k) {
                if (k) o->push_back(',');
                nl(level + 1); SerializeTo(o, v.arr[k], indent, level + 1);
            }
            nl(level); o->push_back(']');
            break;
        case Value::Type::Object:
            if (v.obj.empty()) { *o += "{}"; break; }
            o->push_back('{');
            for (size_t k = 0; k < v.obj.size(); ++k) {
                if (k) o->push_back(',');
                nl(level + 1); EscapeTo(o, v.obj[k].first);
                *o += indent > 0 ? ": " : ":";
                SerializeTo(o, v.obj[k].second, indent, level + 1);
            }
            nl(level); o->push_back('}');
            break;
    }
}

inline std::string Serialize(const Value& v, int indent = 0) {
    std::string o; SerializeTo(&o, v, indent, 0); return o;
}

}  // namespace bb::json
