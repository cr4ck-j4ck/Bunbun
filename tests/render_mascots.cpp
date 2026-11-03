// Renders every character x mood into one PNG contact sheet, using the real
// mascot code. Handy for reviewing artwork changes without running the app.
//   build/render_mascots.exe out.png
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdio>
#include <iterator>

#include "../src/mascot/mascot.h"

using Microsoft::WRL::ComPtr;
using namespace bb::mascot;

int wmain(int argc, wchar_t** argv) {
    const bool venom = argc > 2 && wcscmp(argv[2], L"--venom") == 0;  // Spidey's row in his Venom suit
    // --celebrate: every character's Happy mood at twelve moments through the celebration.
    const bool celebrate = argc > 2 && wcscmp(argv[2], L"--celebrate") == 0;
    // --pack <name>: a skin pack instead: every mood at four moments, then idle looking around.
    const std::wstring pack = argc > 3 && wcscmp(argv[2], L"--pack") == 0 ? argv[3] : L"";
    // --pack <name> <cell>: draw it that many pixels per cell (default 120), to check small sizes.
    const int packCell = argc > 4 ? std::max(24, _wtoi(argv[4])) : 120;
    if (argc < 2) { std::printf("usage: render_mascots <out.png>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());

    const Character chars[] = {Character::Bunny, Character::Cat, Character::Dog, Character::Fox, Character::Panda, Character::Robot, Character::Spiderman,
                               Character::Luffy, Character::Dazai};
    const Mood moods[] = {Mood::Idle, Mood::Working, Mood::Thinking, Mood::Alert, Mood::Happy, Mood::Music, Mood::Sad, Mood::Sleepy,
                          Mood::Snack, Mood::Listening, Mood::Talking, Mood::Confused, Mood::Streaming};
    const int cell = pack.empty() ? 120 : packCell, cols = static_cast<int>(std::size(moods));
    const int rows = pack.empty() ? static_cast<int>(std::size(chars)) : 6;
    const double moments[] = {0.05, 0.5, 1.3, 2.5};
    // Idle looking around: ahead, the four sides and the four diagonals.
    const D2D1_POINT_2F looks[] = {{0, 0}, {-1, 0}, {1, 0}, {0, -1}, {0, 1}, {-0.8f, -0.8f}, {0.8f, -0.8f}, {-0.8f, 0.8f}, {0.8f, 0.8f}};

    ComPtr<IWICBitmap> bmp;
    wic->CreateBitmap(cell * cols, cell * rows, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
    ComPtr<ID2D1RenderTarget> rt;
    d2d->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &rt);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0x1A1622));
    for (int r = 0; r < rows; ++r) {
        for (int c = 0; c < cols; ++c) {
            Style st;
            if (!pack.empty()) {
                st.character = Character::Custom;
                st.customPack = pack;
                const float x = c * cell + cell * 0.5f, y = r * cell + cell * 0.45f;
                if (r < 4) DrawStyled(rt.Get(), st, D2D1::Point2F(x, y), cell * 0.8f, moods[c], moments[r], 0.0, D2D1::Point2F(0, 0));
                else if (r == 4 && c < 9) DrawStyled(rt.Get(), st, D2D1::Point2F(x, y), cell * 0.8f, Mood::Idle, 0.05, 0.0, looks[c]);
                else if (r == 5) {
                    // A blink, frame by frame (blinking every half second here).
                    st.blinkSeconds = 0.5f;
                    DrawStyled(rt.Get(), st, D2D1::Point2F(x, y), cell * 0.8f, Mood::Idle, 0.6 + c * 0.04, 0.0, D2D1::Point2F(0, 0));
                }
                continue;
            }
            st.character = chars[r];
            if (celebrate) {
                const double since = 0.05 + c * 0.2;  // 0.05 s to 2.25 s
                DrawStyled(rt.Get(), st, D2D1::Point2F(c * cell + cell * 0.5f, r * cell + cell * 0.45f), cell * 0.8f, Mood::Happy,
                           10.0 + since, 10.0, D2D1::Point2F(0, 0));
                continue;
            }
            if (venom && st.character == Character::Spiderman) { st.hasColor = true; st.color = D2D1::ColorF(0x17161C); }
            const float x = c * cell + cell * 0.5f, y = r * cell + cell * 0.45f;
            // t = 0.3 s into the mood: mid-hop for Happy, mid-nod for Music.
            DrawStyled(rt.Get(), st, D2D1::Point2F(x, y), cell * 0.8f, moods[c], 10.3, 10.0, D2D1::Point2F(0, 0));
        }
    }
    rt->EndDraw();
    bb::mascot::ReleaseThreadResources();  // rt is short-lived; drop the mascot's cached brush

    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    wic->CreateStream(&stream);
    stream->InitializeFromFilename(argv[1], GENERIC_WRITE);
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    enc->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->WriteSource(bmp.Get(), nullptr);
    frame->Commit();
    enc->Commit();
    std::printf("wrote %ls\n", argv[1]);
    return 0;
}
