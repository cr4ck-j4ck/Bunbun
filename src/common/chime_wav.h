// Bunbun — the little UI sounds, as in-memory WAVs. Shared by the Windows island
// (PlaySound from memory, sounds_module.inc) and the Linux island (written to a
// file and played with paplay / pw-play / aplay). Short sine chimes, so there are
// no sound files to ship.
//
//   Done      a rising two-note chime     Question  two soft pings
//   Error     a falling tone              Open/Close a quick blip up / down
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

namespace bb::chime {

enum class Chime { Done, Question, Error, Open, Close, Count };

struct Note {
    float from, to;  // Hz, glided over the note
    float seconds;
    float gap;       // silence after it
};

inline std::vector<Note> NotesFor(Chime c) {
    switch (c) {
        case Chime::Done: return {{660, 680, 0.11f, 0.02f}, {880, 900, 0.22f, 0.0f}};
        case Chime::Question: return {{760, 760, 0.09f, 0.06f}, {760, 760, 0.12f, 0.0f}};
        case Chime::Error: return {{520, 360, 0.28f, 0.0f}};
        case Chime::Open: return {{480, 720, 0.07f, 0.0f}};
        case Chime::Close: return {{640, 420, 0.07f, 0.0f}};
        default: return {};
    }
}

// A 16-bit mono 44.1 kHz WAV (RIFF header + data). volume: 0..1.
inline std::vector<uint8_t> MakeWav(const std::vector<Note>& notes, float volume) {
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
    const uint32_t dataBytes = static_cast<uint32_t>(pcm.size() * 2);
    std::vector<uint8_t> wav(44 + dataBytes);
    auto put32 = [&](size_t at, uint32_t v) { memcpy(&wav[at], &v, 4); };
    auto put16 = [&](size_t at, uint16_t v) { memcpy(&wav[at], &v, 2); };
    memcpy(&wav[0], "RIFF", 4);
    put32(4, 36 + dataBytes);
    memcpy(&wav[8], "WAVEfmt ", 8);
    put32(16, 16);
    put16(20, 1);  // PCM
    put16(22, 1);  // mono
    put32(24, kRate);
    put32(28, kRate * 2);
    put16(32, 2);
    put16(34, 16);
    memcpy(&wav[36], "data", 4);
    put32(40, dataBytes);
    memcpy(&wav[44], pcm.data(), dataBytes);
    return wav;
}

}  // namespace bb::chime
