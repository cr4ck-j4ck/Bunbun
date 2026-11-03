// Bunbun — little UI sounds.
//
// #included inside island_core.cpp's anonymous namespace. Short sine chimes made
// in memory as 16-bit mono WAVs (no sound files to ship) and played with
// PlaySound(SND_MEMORY | SND_ASYNC). Each sound is rebuilt only when the volume
// changes, and kept alive while it plays, as SND_MEMORY requires.
//
//   Done      a rising two-note chime     Question  two soft pings
//   Error     a falling tone              Open/Close a quick blip up / down

enum class Chime { Done, Question, Error, Open, Close, Count };

namespace Sounds {

struct Note {
    float from, to;    // Hz, glided over the note
    float seconds;
    float gap;         // silence after it
};

std::vector<uint8_t> Make(const std::vector<Note>& notes, float volume) {
    constexpr int kRate = 44100;
    std::vector<int16_t> pcm;
    for (const Note& n : notes) {
        const int count = static_cast<int>(n.seconds * kRate);
        double phase = 0.0;
        for (int i = 0; i < count; ++i) {
            const float t = static_cast<float>(i) / kRate;
            const float f = n.from + (n.to - n.from) * (static_cast<float>(i) / count);
            phase += 2.0 * 3.14159265358979 * f / kRate;
            const float attack = std::min(1.0f, t / 0.008f);
            const float decay = std::exp(-t * 5.5f / n.seconds);
            // A touch of the octave makes it ring rather than beep.
            const float v = (std::sin(static_cast<float>(phase)) * 0.85f + std::sin(static_cast<float>(phase * 2.0)) * 0.15f) *
                            attack * decay * volume;
            pcm.push_back(static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f * 0.5f));
        }
        pcm.insert(pcm.end(), static_cast<size_t>(n.gap * kRate), int16_t{0});
    }
    // RIFF header + data.
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
    std::vector<uint8_t> wav(44 + dataBytes);
    auto put32 = [&](size_t at, uint32_t v) { memcpy(&wav[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { memcpy(&wav[at], &v, 2); };
    memcpy(&wav[0], "RIFF", 4);
    put32(4, 36 + dataBytes);
    memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);            // PCM
    put16(22, 1);            // mono
    put32(24, kRate);
    put32(28, kRate * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(&wav[36], "data", 4);
    put32(40, dataBytes);
    memcpy(&wav[44], pcm.data(), dataBytes);
    return wav;
}

std::vector<Note> NotesFor(Chime c) {
    switch (c) {
        case Chime::Done: return {{660, 680, 0.11f, 0.02f}, {880, 900, 0.22f, 0.0f}};
        case Chime::Question: return {{760, 760, 0.09f, 0.06f}, {760, 760, 0.12f, 0.0f}};
        case Chime::Error: return {{520, 360, 0.28f, 0.0f}};
        case Chime::Open: return {{480, 720, 0.07f, 0.0f}};
        case Chime::Close: return {{640, 420, 0.07f, 0.0f}};
        default: return {};
    }
}

std::mutex g_mutex;
std::vector<uint8_t> g_cache[static_cast<int>(Chime::Count)];
int g_cacheVolume = -1;
std::shared_ptr<std::vector<uint8_t>> g_playing;  // the buffer PlaySound is reading
ULONGLONG g_lastAt = 0;

}  // namespace Sounds

// Plays a chime if sounds are on (and open/close sounds for those). Any thread.
void PlayChime(Chime c) {
    const Settings s = GetSettingsCopy();
    if (!s.sounds) return;
    if ((c == Chime::Open || c == Chime::Close) && !s.soundOpenClose) return;
    const int volume = std::clamp(s.soundVolume, 0, 100);
    if (volume == 0) return;
    std::lock_guard lock(Sounds::g_mutex);
    const ULONGLONG now = GetTickCount64();
    // Open/close never stack up; other chimes win over them.
    if ((c == Chime::Open || c == Chime::Close) && now - Sounds::g_lastAt < 450) return;
    Sounds::g_lastAt = now;
    if (Sounds::g_cacheVolume != volume) {
        for (auto& b : Sounds::g_cache) b.clear();
        Sounds::g_cacheVolume = volume;
    }
    auto& wav = Sounds::g_cache[static_cast<int>(c)];
    if (wav.empty()) wav = Sounds::Make(Sounds::NotesFor(c), static_cast<float>(volume) / 100.0f);
    // The previous buffer may still be playing: keep it until PlaySound has
    // stopped it in favour of the new one.
    auto previous = Sounds::g_playing;
    Sounds::g_playing = std::make_shared<std::vector<uint8_t>>(wav);
    PlaySoundW(reinterpret_cast<LPCWSTR>(Sounds::g_playing->data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    previous.reset();
}
