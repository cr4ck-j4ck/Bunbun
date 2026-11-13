// Bunbun: the Mirror page's sound check panel (sound_check_module.inc).
// Included inside class Renderer. Two rows, one per device kind: a picker
// ("‹ device ›") and a test button, plus a live level for the microphone and a
// shortcut to Windows' Sound settings. With the camera off the panel is always
// shown; with it on, it overlays the picture and starts folded to a small chip.

static constexpr float kSoundPanelH = 78.0f;

void DrawSoundPanel(D2D1_RECT_F r, bool cameraOn) {
    SoundCheck::Poke();
    const bool mic = SoundCheck::g_running.load();
    if (mic) SoundCheck::g_lastWanted.store(NowSeconds());
    if (cameraOn && !SoundCheck::g_open.load()) {
        const D2D1_RECT_F chip = D2D1::RectF(r.left + 8.0f, r.bottom - 34.0f, r.left + 80.0f, r.bottom - 8.0f);
        SFillRound(chip, 13, D2D1::ColorF(0, 0, 0, SLerp(0.55f, 0.75f, SHover(StudioAction::SoundToggle))));
        SText(L"Sound", chip, SFLabel, SCol(0xffffff), 1);
        SHit(chip, StudioAction::SoundToggle);
        return;
    }

    std::vector<SoundCheck::Device> mics, speakers;
    std::wstring micSel, spkSel, defMic, defSpeaker, err;
    {
        std::lock_guard lock(SoundCheck::g_mutex);
        mics = SoundCheck::g_mics;
        speakers = SoundCheck::g_speakers;
        micSel = SoundCheck::g_micSel;
        spkSel = SoundCheck::g_spkSel;
        defMic = SoundCheck::g_defaultMic;
        defSpeaker = SoundCheck::g_defaultSpeaker;
        err = SoundCheck::g_error;
    }
    // The device being tested: the chosen one, else Windows' default. Its name is tinted
    // with the accent when it isn't the default.
    auto chipText = [](const std::vector<SoundCheck::Device>& list, const std::wstring& sel, const std::wstring& defId) {
        auto nameOf = [&](const std::wstring& id) {
            for (const SoundCheck::Device& d : list)
                if (d.id == id) return d.name;
            return std::wstring();
        };
        const std::wstring n = nameOf(sel.empty() ? defId : sel);
        return n.empty() ? std::wstring(L"No device") : n;
    };
    auto notDefault = [](const std::wstring& sel, const std::wstring& defId) { return !sel.empty() && sel != defId; };

    const D2D1_RECT_F panel = D2D1::RectF(r.left, r.bottom - kSoundPanelH, r.right, r.bottom);
    SFillRound(panel, 14, cameraOn ? D2D1::ColorF(0, 0, 0, 0.72f) : D2D1::ColorF(1, 1, 1, 0.07f));
    if (cameraOn) {
        const D2D1_RECT_F hide = D2D1::RectF(panel.right - 60.0f, panel.top - 28.0f, panel.right - 8.0f, panel.top - 4.0f);
        SFillRound(hide, 12, D2D1::ColorF(0, 0, 0, SLerp(0.55f, 0.75f, SHover(StudioAction::SoundToggle))));
        SText(L"Hide", hide, SFLabel, SCol(0xffffff), 1);
        SHit(hide, StudioAction::SoundToggle);
    }

    const float rowH = 28.0f;
    auto drawRow = [&](float y, const wchar_t* label, int argBase, const std::wstring& text, bool problem, bool tinted, bool isMic) {
        float x = panel.left + 8.0f;
        SText(label, D2D1::RectF(x, y, x + 46.0f, y + rowH), SFSmall, SCol(0x8e8e93), 0);
        x += 48.0f;
        auto arrow = [&](float left, const wchar_t* glyph, int arg) {
            const D2D1_RECT_F b = D2D1::RectF(left, y, left + 24.0f, y + rowH);
            SFillRound(b, 12, D2D1::ColorF(1, 1, 1, SLerp(0.12f, 0.26f, SHover(StudioAction::SoundDevice, arg))));
            SText(glyph, b, SFLabel, SCol(0xffffff), 1);
            SHit(b, StudioAction::SoundDevice, arg);
        };
        arrow(x, L"\x2039", argBase);
        const float nameLeft = x + 24.0f + 6.0f;
        const float nameRight = panel.right - 8.0f - 76.0f - 8.0f - 84.0f - 8.0f - 24.0f - 6.0f;
        const D2D1_RECT_F name = D2D1::RectF(nameLeft, y, nameRight, y + rowH);
        SFillRound(name, 12, D2D1::ColorF(1, 1, 1, 0.07f));
        SText(text, D2D1::RectF(name.left + 8.0f, name.top, name.right - 8.0f, name.bottom), SFSmall,
              problem ? SCol(0xff6961) : tinted ? SAccent() : SCol(0xd1d1d6), 0);
        arrow(nameRight + 6.0f, L"\x203A", argBase + 1);
        // Test button, then the microphone's level or the Sound settings shortcut.
        const float testX = nameRight + 6.0f + 24.0f + 8.0f;
        const D2D1_RECT_F test = D2D1::RectF(testX, y, testX + 84.0f, y + rowH);
        const bool tone = NowSeconds() < SoundCheck::g_toneUntil.load();
        const bool on = isMic ? mic : tone;
        const StudioAction action = isMic ? StudioAction::SoundMic : StudioAction::SoundTest;
        SFillRound(test, 14, on ? SAccent() : D2D1::ColorF(1, 1, 1, SLerp(0.16f, 0.28f, SHover(action))));
        SText(isMic ? (mic ? L"Stop mic" : L"Test mic") : (tone ? L"Playing\x2026" : L"Test tone"), test, SFLabel,
              on ? SCol(0x141416) : SCol(0xffffff), 1);
        SHit(test, action);
        const float tailX = testX + 84.0f + 8.0f;
        if (isMic) {
            const D2D1_RECT_F track = D2D1::RectF(tailX, y + 10.0f, tailX + 76.0f, y + 18.0f);
            SFillRound(track, 4, D2D1::ColorF(1, 1, 1, 0.16f));
            const float level = mic ? std::min(1.0f, std::sqrt(SoundCheck::g_level.load())) : 0.0f;
            if (level > 0.02f) {
                // Green, then amber, then red as the input gets hot.
                const D2D1_COLOR_F c = level > 0.85f ? SCol(0xff6961) : level > 0.6f ? SCol(0xffd60a) : SCol(0x34c759);
                SFillRound(D2D1::RectF(track.left, track.top, track.left + 76.0f * level, track.bottom), 4, c);
            }
        } else {
            const D2D1_RECT_F settings = D2D1::RectF(tailX, y, tailX + 76.0f, y + rowH);
            SFillRound(settings, 14, D2D1::ColorF(1, 1, 1, SLerp(0.10f, 0.22f, SHover(StudioAction::SoundOpen))));
            SText(L"Settings", settings, SFLabel, SCol(0xffffff), 1);
            SHit(settings, StudioAction::SoundOpen);
        }
    };
    const float y1 = panel.top + 8.0f, y2 = y1 + rowH + 6.0f;
    const bool micProblem = !err.empty() && !mic;
    drawRow(y1, L"Mic", 0, micProblem ? err : chipText(mics, micSel, defMic), micProblem, notDefault(micSel, defMic), true);
    drawRow(y2, L"Speaker", 2, chipText(speakers, spkSel, defSpeaker), false, notDefault(spkSel, defSpeaker), false);
}
