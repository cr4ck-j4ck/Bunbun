// bunbun-brain.exe — Bun's optional local AI.
//
// A small helper the island starts on demand. It loads a GGUF language model
// with llama.cpp (MIT) and answers one request at a time over its stdin/stdout,
// so only the island that started it can talk to it. It has no network code.
//
// Protocol, one JSON object per line:
//   in:  {"id":7,"messages":[{"role":"system","content":"..."},...],"max_tokens":400}
//        (or the short form {"id":7,"system":"...","user":"..."})
//   out: {"id":7,"t":"text piece"}   ... streamed as the model writes
//        {"id":7,"done":true}
//        {"error":"..."}             a request or the model failed
// The island closes our stdin to shut us down (idle timeout, island exit).
//
// The reply is constrained by a grammar, so even a small model can't wander
// off: "SAY: <answer>", or one to five "RUN: <Ask Bun command>" lines with an
// optional closing "SAY: <note>".
//
//   bunbun-brain.exe --model <file.gguf> [--threads N] [--ctx 4096]
#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "../common/json.h"
#include "llama.h"

namespace {

using bb::json::Value;

// Replies are either an answer, or a plan: up to five Ask Bun command lines,
// optionally closed by a short note. "say only" mode (a retry, or the answer
// after the plan's lookups came back) allows just the answer.
constexpr const char* kGrammar = R"(root ::= ("SAY: " say) | (step ("\n" step){0,4} ("\nSAY: " say)?)
step ::= "RUN: " run
say ::= [^\x00]{1,2000}
run ::= [^\n]{1,200}
)";
constexpr const char* kSayGrammar = R"(root ::= "SAY: " say
say ::= [^\x00]{1,2000}
)";

void WriteLine(const std::string& line) {
    static HANDLE out = GetStdHandle(STD_OUTPUT_HANDLE);
    std::string s = line + "\n";
    DWORD written = 0;
    WriteFile(out, s.data(), static_cast<DWORD>(s.size()), &written, nullptr);
}

void Send(Value v) {
    WriteLine(bb::json::Serialize(v));
}

// Reads one '\n'-terminated line from stdin; false at end of input.
bool ReadLine(std::string* line) {
    static HANDLE in = GetStdHandle(STD_INPUT_HANDLE);
    static std::string pending;
    for (;;) {
        const size_t nl = pending.find('\n');
        if (nl != std::string::npos) {
            *line = pending.substr(0, nl);
            pending.erase(0, nl + 1);
            return true;
        }
        char buf[4096];
        DWORD read = 0;
        if (!ReadFile(in, buf, sizeof(buf), &read, nullptr) || read == 0) return false;
        pending.append(buf, read);
    }
}

// Tokens can end in the middle of a multi-byte UTF-8 character; hold those
// bytes back until the character is complete, so every piece sent is valid.
size_t CompleteUtf8Prefix(const std::string& s) {
    size_t i = s.size();
    // Walk back over at most 3 continuation bytes to the lead byte.
    size_t back = 0;
    while (back < 4 && back < s.size() && (static_cast<unsigned char>(s[s.size() - 1 - back]) & 0xC0) == 0x80) ++back;
    if (back == s.size()) return s.size();
    const unsigned char lead = static_cast<unsigned char>(s[s.size() - 1 - back]);
    size_t need = 1;
    if ((lead & 0xE0) == 0xC0) need = 2;
    else if ((lead & 0xF0) == 0xE0) need = 3;
    else if ((lead & 0xF8) == 0xF0) need = 4;
    if (back + 1 < need) i = s.size() - 1 - back;  // incomplete: stop before the lead byte
    return i;
}

struct Brain {
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    const llama_vocab* vocab = nullptr;
    std::string chatTemplate;
    // What the model's memory currently holds, so the next request only has to
    // read what's new. The instructions and examples are the same every time and
    // come first, so after the first question they cost nothing.
    std::vector<llama_token> cached;
};

bool Load(Brain* b, const std::string& path, int threads, int nctx) {
    llama_model_params mp = llama_model_default_params();
    mp.n_gpu_layers = 0;  // CPU only for now
    b->model = llama_model_load_from_file(path.c_str(), mp);
    if (!b->model) return false;
    b->vocab = llama_model_get_vocab(b->model);
    if (const char* t = llama_model_chat_template(b->model, nullptr)) b->chatTemplate = t;
    llama_context_params cp = llama_context_default_params();
    cp.n_ctx = static_cast<uint32_t>(nctx);
    cp.n_batch = static_cast<uint32_t>(nctx);
    cp.n_threads = threads;
    cp.n_threads_batch = threads;
    b->ctx = llama_init_from_model(b->model, cp);
    return b->ctx != nullptr;
}

// The conversation in the model's own chat format. The island sends the system
// prompt, a few worked examples (small models follow examples far better than
// rules) and the question.
std::string ApplyTemplate(const Brain& b, const Value& req) {
    std::vector<std::pair<std::string, std::string>> turns;
    if (const Value* m = req.find("messages"); m && m->isArray()) {
        for (const Value& t : m->arr) turns.emplace_back(t.getString("role"), t.getString("content"));
    } else {
        turns.emplace_back("system", req.getString("system"));
        turns.emplace_back("user", req.getString("user"));
    }
    std::vector<llama_chat_message> msgs;
    size_t chars = 0;
    for (const auto& [role, content] : turns) {
        msgs.push_back({role.c_str(), content.c_str()});
        chars += content.size();
    }
    const char* tmpl = b.chatTemplate.empty() ? nullptr : b.chatTemplate.c_str();
    std::vector<char> buf(chars * 2 + 1024);
    int n = llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, buf.data(), static_cast<int32_t>(buf.size()));
    if (n > static_cast<int>(buf.size())) {
        buf.resize(static_cast<size_t>(n) + 1);
        n = llama_chat_apply_template(tmpl, msgs.data(), msgs.size(), true, buf.data(), static_cast<int32_t>(buf.size()));
    }
    if (n >= 0) return std::string(buf.data(), static_cast<size_t>(n));
    std::string plain;  // no usable template: a plain transcript
    for (const auto& [role, content] : turns) plain += role + ": " + content + "\n";
    return plain + "assistant: ";
}

void Answer(Brain& b, const Value& req) {
    const double id = req.getNumber("id");
    auto send = [&](Value v) {
        v["id"] = Value::Number(id);
        Send(std::move(v));
    };
    const std::string prompt = ApplyTemplate(b, req);
    const int maxTokens = std::clamp(static_cast<int>(req.getNumber("max_tokens", 400)), 16, 2048);

    // Tokenize (a first call with no buffer reports the size as a negative count).
    int n = -llama_tokenize(b.vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), nullptr, 0, true, true);
    std::vector<llama_token> tokens(static_cast<size_t>(std::max(n, 0)));
    if (n <= 0 || llama_tokenize(b.vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()), tokens.data(), n, true, true) < 0) {
        Value e = Value::Object();
        e["error"] = Value::String("couldn't tokenize the prompt");
        send(std::move(e));
        return;
    }
    if (tokens.size() + static_cast<size_t>(maxTokens) > llama_n_ctx(b.ctx)) {
        Value e = Value::Object();
        e["error"] = Value::String("the question is too long");
        send(std::move(e));
        return;
    }

    // Keep the part of the memory that matches this prompt's start; drop the rest.
    size_t keep = 0;
    while (keep < b.cached.size() && keep < tokens.size() && b.cached[keep] == tokens[keep]) ++keep;
    if (keep == tokens.size() && keep > 0) --keep;  // at least one token must be decoded to sample from
    llama_memory_t mem = llama_get_memory(b.ctx);
    if (keep == 0 || !llama_memory_seq_rm(mem, 0, static_cast<llama_pos>(keep), -1)) {
        llama_memory_clear(mem, true);
        keep = 0;
    }
    b.cached.assign(tokens.begin(), tokens.begin() + static_cast<std::ptrdiff_t>(keep));
    llama_sampler* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
    const char* grammar = req.getString("mode") == "say" ? kSayGrammar : kGrammar;
    if (llama_sampler* g = llama_sampler_init_grammar(b.vocab, grammar, "root")) llama_sampler_chain_add(smpl, g);
    llama_sampler_chain_add(smpl, llama_sampler_init_top_k(40));
    llama_sampler_chain_add(smpl, llama_sampler_init_top_p(0.9f, 1));
    llama_sampler_chain_add(smpl, llama_sampler_init_temp(static_cast<float>(req.getNumber("temperature", 0.3))));
    llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

    llama_batch batch = llama_batch_get_one(tokens.data() + keep, static_cast<int32_t>(tokens.size() - keep));
    b.cached.insert(b.cached.end(), tokens.begin() + static_cast<std::ptrdiff_t>(keep), tokens.end());
    std::string held;  // bytes waiting for the rest of a UTF-8 character
    llama_token tok = 0;
    for (int produced = 0; produced < maxTokens; ++produced) {
        if (llama_decode(b.ctx, batch) != 0) {
            Value e = Value::Object();
            e["error"] = Value::String("the model stopped unexpectedly");
            send(std::move(e));
            b.cached.clear();  // the memory's state is unknown now
            llama_memory_clear(llama_get_memory(b.ctx), true);
            break;
        }
        tok = llama_sampler_sample(smpl, b.ctx, -1);
        if (llama_vocab_is_eog(b.vocab, tok)) break;
        b.cached.push_back(tok);
        char piece[256];
        const int len = llama_token_to_piece(b.vocab, tok, piece, sizeof(piece), 0, false);
        if (len > 0) {
            held.append(piece, static_cast<size_t>(len));
            const size_t ready = CompleteUtf8Prefix(held);
            if (ready > 0) {
                Value t = Value::Object();
                t["t"] = Value::String(held.substr(0, ready));
                send(std::move(t));
                held.erase(0, ready);
            }
        }
        batch = llama_batch_get_one(&tok, 1);
    }
    llama_sampler_free(smpl);
    Value d = Value::Object();
    d["done"] = Value::Bool(true);
    send(std::move(d));
}

void QuietLog(ggml_log_level, const char*, void*) {}

}  // namespace

int main(int argc, char** argv) {
    std::string model;
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int threads = std::clamp(static_cast<int>(si.dwNumberOfProcessors) / 2, 1, 8);  // physical cores, roughly
    int nctx = 4096;
    for (int i = 1; i + 1 < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--model") model = argv[++i];
        else if (a == "--threads") threads = std::clamp(atoi(argv[++i]), 1, 64);
        else if (a == "--ctx") nctx = std::clamp(atoi(argv[++i]), 512, 32768);
    }
    if (model.empty()) {
        std::fprintf(stderr, "usage: bunbun-brain --model <file.gguf> [--threads N] [--ctx N]\n");
        return 2;
    }
    SetPriorityClass(GetCurrentProcess(), BELOW_NORMAL_PRIORITY_CLASS);  // never slow the desktop down
    llama_log_set(QuietLog, nullptr);
    llama_backend_init();
    Brain b;
    if (!Load(&b, model, threads, nctx)) {
        Value e = Value::Object();
        e["error"] = Value::String("couldn't load the model file");
        Send(std::move(e));
        return 1;
    }
    Value ready = Value::Object();
    ready["ready"] = Value::Bool(true);
    Send(std::move(ready));

    std::string line;
    while (ReadLine(&line)) {
        if (line.empty() || line == "\r") continue;
        Value req;
        std::string err;
        if (!bb::json::Parse(line, &req, &err) || !req.isObject()) {
            Value e = Value::Object();
            e["error"] = Value::String("bad request: " + err);
            Send(std::move(e));
            continue;
        }
        Answer(b, req);
    }
    llama_free(b.ctx);
    llama_model_free(b.model);
    return 0;
}
