// Character sheets → pack frames, from the command line (the Mascot studio does
// the same through the settings window).
//   pack_tool slice <sheet.png|webp> <out-dir>     writes r<row>_f<frame>.png
//   pack_tool eyes <frame.png> <out.png>            eye rig: original, irises left, right, up
#include <windows.h>
#include <objbase.h>

#include <cstdio>
#include <string>

#include "../src/mascot/pack_sheet.h"

using namespace bb::mascot;

// Draws `src` onto `dst` at (ox, oy), alpha-blended.
static void Blit(Image& dst, const Image& src, float ox, float oy, const Image* clip = nullptr) {
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            const int dx = static_cast<int>(x + ox + 0.5f), dy = static_cast<int>(y + oy + 0.5f);
            if (dx < 0 || dy < 0 || dx >= dst.w || dy >= dst.h) continue;
            const uint8_t* s = src.at(x, y);
            if (clip && clip->at(dx % clip->w, dy)[3] == 0) continue;
            uint8_t* d = dst.at(dx, dy);
            const int a = s[3];
            for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>((s[c] * a + d[c] * (255 - a)) / 255);
            d[3] = static_cast<uint8_t>(std::max<int>(d[3], a));
        }
}

int wmain(int argc, wchar_t** argv) {
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (argc >= 4 && std::wstring(argv[1]) == L"slice") {
        Image sheet;
        if (!DecodeImageFile(argv[2], &sheet)) { std::printf("can't read the sheet\n"); return 1; }
        const auto rows = SliceSheet(sheet);
        CreateDirectoryW(argv[3], nullptr);
        for (size_t r = 0; r < rows.size(); ++r) {
            std::printf("row %zu: %zu frames\n", r + 1, rows[r].frames.size());
            for (size_t f = 0; f < rows[r].frames.size(); ++f)
                EncodePng(rows[r].frames[f], std::wstring(argv[3]) + L"\\r" + std::to_wstring(r + 1) + L"_f" + std::to_wstring(f + 1) + L".png");
        }
        return rows.empty() ? 1 : 0;
    }
    if (argc >= 4 && std::wstring(argv[1]) == L"eyes") {
        Image frame;
        if (!DecodeImageFile(argv[2], &frame)) return 1;
        const EyeRig rig = FindEyes(frame);
        std::printf("eyes %s, max dx %.1f dy %.1f\n", rig.found ? "found" : "not found", rig.maxDx, rig.maxDy);
        if (!rig.found) return 1;
        // Four copies side by side: original, looking left, right, up-left.
        Image out;
        out.w = frame.w * 4;
        out.h = frame.h;
        out.px.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
        for (int i = 0; i < out.w * out.h; ++i) { out.px[i * 4] = 120; out.px[i * 4 + 1] = 200; out.px[i * 4 + 2] = 160; out.px[i * 4 + 3] = 255; }
        const float shifts[4][2] = {{0, 0}, {-1, 0}, {1, 0}, {-1, -1}};
        for (int k = 0; k < 4; ++k) {
            const float ox = static_cast<float>(k * frame.w);
            if (k == 0) { Blit(out, frame, ox, 0); continue; }
            Blit(out, rig.base, ox, 0);
            // Irises shifted, kept inside the eye whites.
            for (int y = 0; y < frame.h; ++y)
                for (int x = 0; x < frame.w; ++x) {
                    const int sx = static_cast<int>(x - shifts[k][0] * rig.maxDx), sy = static_cast<int>(y - shifts[k][1] * rig.maxDy);
                    if (rig.mask.at(x, y)[3] == 0 || sx < 0 || sy < 0 || sx >= frame.w || sy >= frame.h) continue;
                    const uint8_t* s = rig.irises.at(sx, sy);
                    if (!s[3]) continue;
                    uint8_t* d = out.at(static_cast<int>(ox) + x, y);
                    for (int c = 0; c < 3; ++c) d[c] = static_cast<uint8_t>((s[c] * s[3] + d[c] * (255 - s[3])) / 255);
                }
        }
        EncodePng(out, argv[3]);
        const std::wstring stem = std::wstring(argv[3]).substr(0, std::wstring(argv[3]).size() - 4);
        EncodePng(rig.base, stem + L"_base.png");
        EncodePng(rig.irises, stem + L"_irises.png");
        EncodePng(rig.mask, stem + L"_mask.png");
        return 0;
    }
    std::printf("usage: pack_tool slice <sheet> <out-dir> | pack_tool eyes <frame.png> <out.png>\n");
    return 2;
}
