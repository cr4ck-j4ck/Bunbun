// Bunbun — little UI sounds.
//
// #included inside island_core.cpp's anonymous namespace. The chimes themselves
// (short sine notes as in-memory WAVs) live in common/chime_wav.h, shared with
// Linux; here they're played with PlaySound(SND_MEMORY | SND_ASYNC). Each sound
// is rebuilt only when the volume changes, and kept alive while it plays, as
// SND_MEMORY requires.

using bb::chime::Chime;

namespace Sounds {

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
    if (wav.empty()) wav = bb::chime::MakeWav(bb::chime::NotesFor(c), static_cast<float>(volume) / 100.0f);
    // The previous buffer may still be playing: keep it until PlaySound has
    // stopped it in favour of the new one.
    auto previous = Sounds::g_playing;
    Sounds::g_playing = std::make_shared<std::vector<uint8_t>>(wav);
    PlaySoundW(reinterpret_cast<LPCWSTR>(Sounds::g_playing->data()), nullptr, SND_MEMORY | SND_ASYNC | SND_NODEFAULT);
    previous.reset();
}
