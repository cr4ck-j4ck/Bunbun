// Renders Bun into res/bunbun.ico (16-256 px, PNG-compressed entries), the icon
// of bunbun.exe and bunbun-settings.exe. Uses the real mascot code, so the icon
// always matches the island. Run scripts/make_icon.sh after changing Bun's look.
#include <windows.h>
#include <d2d1.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <cstdio>
#include <string>
#include <vector>

#include "../src/mascot/mascot.h"

using Microsoft::WRL::ComPtr;

static std::vector<BYTE> RenderPng(IWICImagingFactory* wic, ID2D1Factory* d2d, int px) {
    ComPtr<IWICBitmap> bmp;
    wic->CreateBitmap(px, px, GUID_WICPixelFormat32bppPBGRA, WICBitmapCacheOnLoad, &bmp);
    ComPtr<ID2D1RenderTarget> rt;
    d2d->CreateWicBitmapRenderTarget(bmp.Get(), D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED)), &rt);
    rt->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    rt->BeginDraw();
    rt->Clear(D2D1::ColorF(0, 0, 0, 0));
    bb::mascot::Style style;  // Bun, default colours
    const float s = static_cast<float>(px);
    bb::mascot::DrawStyled(rt.Get(), style, D2D1::Point2F(s * 0.5f, s * 0.47f), s * 1.02f, bb::mascot::Mood::Idle,
                           0.5, 0.0, D2D1::Point2F(0, 0));
    rt->EndDraw();
    bb::mascot::ReleaseThreadResources();

    ComPtr<IStream> stream;
    CreateStreamOnHGlobal(nullptr, TRUE, &stream);
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc);
    enc->Initialize(stream.Get(), WICBitmapEncoderNoCache);
    enc->CreateNewFrame(&frame, nullptr);
    frame->Initialize(nullptr);
    frame->WriteSource(bmp.Get(), nullptr);
    frame->Commit();
    enc->Commit();
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    std::vector<BYTE> bytes(static_cast<size_t>(st.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(bytes.data(), static_cast<ULONG>(bytes.size()), &read);
    return bytes;
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) { std::printf("usage: make_icon <out.ico>\n"); return 2; }
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ComPtr<IWICImagingFactory> wic;
    ComPtr<ID2D1Factory> d2d;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2d.GetAddressOf());

    const int sizes[] = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    std::vector<std::vector<BYTE>> pngs;
    for (int px : sizes) pngs.push_back(RenderPng(wic.Get(), d2d.Get(), px));

    // ICONDIR + ICONDIRENTRY[] + PNG payloads (Vista+ reads PNG entries at every size).
    std::vector<BYTE> ico;
    auto put16 = [&](unsigned v) { ico.push_back(v & 0xFF); ico.push_back((v >> 8) & 0xFF); };
    auto put32 = [&](unsigned v) { put16(v & 0xFFFF); put16(v >> 16); };
    const unsigned count = static_cast<unsigned>(pngs.size());
    put16(0); put16(1); put16(count);
    unsigned offset = 6 + 16 * count;
    for (size_t i = 0; i < pngs.size(); ++i) {
        const int px = sizes[i];
        ico.push_back(px >= 256 ? 0 : static_cast<BYTE>(px));
        ico.push_back(px >= 256 ? 0 : static_cast<BYTE>(px));
        ico.push_back(0); ico.push_back(0);
        put16(1); put16(32);
        put32(static_cast<unsigned>(pngs[i].size()));
        put32(offset);
        offset += static_cast<unsigned>(pngs[i].size());
    }
    for (auto& p : pngs) ico.insert(ico.end(), p.begin(), p.end());

    FILE* f = _wfopen(argv[1], L"wb");
    if (!f) { std::printf("cannot write %ls\n", argv[1]); return 1; }
    fwrite(ico.data(), 1, ico.size(), f);
    fclose(f);
    std::printf("wrote %ls (%u sizes, %zu bytes)\n", argv[1], count, ico.size());
    return 0;
}
