// Renders every character x mood into one PNG contact sheet, using the real
// mascot code. Handy for reviewing artwork changes without running the app.
//   build/render_mascots.exe out.png
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>

#include "../src/mascot/mascot.h"

using Microsoft::WRL::ComPtr;
using namespace bb::mascot;

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: render_mascots <out.png>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());

    const Character chars[] = {Character::Bunny, Character::Cat, Character::Dog, Character::Fox, Character::Panda, Character::Robot, Character::Spiderman};
    const Mood moods[] = {Mood::Idle, Mood::Working, Mood::Thinking, Mood::Alert, Mood::Happy, Mood::Music, Mood::Sad, Mood::Sleepy};
    const int cell = 120, cols = 8, rows = 7;

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
            st.character = chars[r];
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
