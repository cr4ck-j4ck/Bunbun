// Bunbun — character sheets to skin packs, and eye tracking for pack art (see pack_sheet.h).
#include "pack_sheet.h"

#include <windows.h>
#include <shlobj.h>
#include <wincodec.h>
#include <wrl/client.h>

#include <algorithm>
#include <cmath>
#include <deque>

using Microsoft::WRL::ComPtr;

namespace bb::mascot {
namespace {

ComPtr<IWICImagingFactory> Wic() {
    ComPtr<IWICImagingFactory> wic;
    CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic));
    return wic;
}

bool FromDecoder(IWICImagingFactory* wic, IWICBitmapDecoder* dec, Image* out) {
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> conv;
    UINT w = 0, h = 0;
    if (FAILED(dec->GetFrame(0, &frame)) || FAILED(wic->CreateFormatConverter(&conv)) ||
        FAILED(conv->Initialize(frame.Get(), GUID_WICPixelFormat32bppRGBA, WICBitmapDitherTypeNone, nullptr, 0,
                                WICBitmapPaletteTypeCustom)) ||
        FAILED(conv->GetSize(&w, &h)) || w == 0 || h == 0 || w > 8192 || h > 8192)
        return false;
    out->w = static_cast<int>(w);
    out->h = static_cast<int>(h);
    out->px.assign(static_cast<size_t>(w) * h * 4, 0);
    return SUCCEEDED(conv->CopyPixels(nullptr, w * 4, static_cast<UINT>(out->px.size()), out->px.data()));
}

bool Encode(const Image& img, IStream* stream) {
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (!wic || FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream, WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->SetSize(img.w, img.h)))
        return false;
    // PNG takes BGRA everywhere; swap the channels.
    std::vector<uint8_t> bgra(img.px);
    for (size_t i = 0; i < bgra.size(); i += 4) std::swap(bgra[i], bgra[i + 2]);
    WICPixelFormatGUID fmt = GUID_WICPixelFormat32bppBGRA;
    return SUCCEEDED(frame->SetPixelFormat(&fmt)) &&
           SUCCEEDED(frame->WritePixels(img.h, img.w * 4, static_cast<UINT>(bgra.size()), bgra.data())) &&
           SUCCEEDED(frame->Commit()) && SUCCEEDED(enc->Commit());
}

int Diff(const uint8_t* p, const int bg[3]) {
    return std::abs(p[0] - bg[0]) + std::abs(p[1] - bg[1]) + std::abs(p[2] - bg[2]);
}

// Runs where `profile` is above `thresh`, at least `minLen` long.
std::vector<std::pair<int, int>> Runs(const std::vector<float>& profile, float thresh, int minLen) {
    std::vector<std::pair<int, int>> out;
    int start = -1;
    for (int i = 0; i <= static_cast<int>(profile.size()); ++i) {
        const bool on = i < static_cast<int>(profile.size()) && profile[static_cast<size_t>(i)] > thresh;
        if (on && start < 0) start = i;
        if (!on && start >= 0) {
            if (i - start >= minLen) out.emplace_back(start, i);
            start = -1;
        }
    }
    return out;
}

// 4-connected components of `mask` (w x h); returns labels (0 = none) and the count.
int Label(const std::vector<uint8_t>& mask, int w, int h, std::vector<int>* labels) {
    labels->assign(mask.size(), 0);
    int n = 0;
    std::deque<int> q;
    for (int i = 0; i < w * h; ++i) {
        if (!mask[static_cast<size_t>(i)] || (*labels)[static_cast<size_t>(i)]) continue;
        (*labels)[static_cast<size_t>(i)] = ++n;
        q.push_back(i);
        while (!q.empty()) {
            const int p = q.front();
            q.pop_front();
            const int x = p % w, y = p / w;
            const int nb[4] = {x > 0 ? p - 1 : -1, x + 1 < w ? p + 1 : -1, y > 0 ? p - w : -1, y + 1 < h ? p + w : -1};
            for (int k : nb)
                if (k >= 0 && mask[static_cast<size_t>(k)] && !(*labels)[static_cast<size_t>(k)]) {
                    (*labels)[static_cast<size_t>(k)] = n;
                    q.push_back(k);
                }
        }
    }
    return n;
}

struct Box {
    int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1, count = 0;
    long long sr = 0, sg = 0, sb = 0;
    void add(int x, int y) { x0 = std::min(x0, x); y0 = std::min(y0, y); x1 = std::max(x1, x); y1 = std::max(y1, y); ++count; }
};

// Drops stray bits from a cut-out frame: dashed separators (very flat pieces),
// specks, and the sheet's purple arrows. Effects like "?" or "zzz" stay.
void Clean(Image* img) {
    const int w = img->w, h = img->h;
    std::vector<uint8_t> solid(static_cast<size_t>(w) * h);
    for (int i = 0; i < w * h; ++i) solid[static_cast<size_t>(i)] = img->px[static_cast<size_t>(i) * 4 + 3] > 40;
    std::vector<int> lab;
    const int n = Label(solid, w, h, &lab);
    std::vector<Box> boxes(static_cast<size_t>(n) + 1);
    for (int i = 0; i < w * h; ++i) {
        if (!lab[static_cast<size_t>(i)]) continue;
        Box& b = boxes[static_cast<size_t>(lab[static_cast<size_t>(i)])];
        b.add(i % w, i / w);
        const uint8_t* p = &img->px[static_cast<size_t>(i) * 4];
        b.sr += p[0]; b.sg += p[1]; b.sb += p[2];
    }
    std::vector<uint8_t> drop(static_cast<size_t>(n) + 1, 0);
    for (int k = 1; k <= n; ++k) {
        const Box& b = boxes[static_cast<size_t>(k)];
        const double r = static_cast<double>(b.sr) / b.count, g = static_cast<double>(b.sg) / b.count,
                     bl = static_cast<double>(b.sb) / b.count;
        const bool purple = bl > 170 && r < 190 && g < 140 && b.count < w * h / 16;
        drop[static_cast<size_t>(k)] = (b.y1 - b.y0 + 1 <= 5) || b.count < 12 || purple;
    }
    for (int i = 0; i < w * h; ++i)
        if (drop[static_cast<size_t>(lab[static_cast<size_t>(i)])] && lab[static_cast<size_t>(i)]) img->px[static_cast<size_t>(i) * 4 + 3] = 0;
}

// Bilinear sample with premultiplied interpolation, so edges don't pick up the
// colour of transparent pixels.
void Sample(const Image& src, float x, float y, uint8_t out[4]) {
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float fx = x - x0, fy = y - y0;
    float acc[4] = {0, 0, 0, 0};
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i) {
            const int sx = x0 + i, sy = y0 + j;
            if (sx < 0 || sy < 0 || sx >= src.w || sy >= src.h) continue;
            const float wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
            const uint8_t* p = src.at(sx, sy);
            const float a = p[3] / 255.0f * wgt;
            acc[0] += p[0] * a; acc[1] += p[1] * a; acc[2] += p[2] * a; acc[3] += a;
        }
    if (acc[3] <= 0.0001f) { out[0] = out[1] = out[2] = out[3] = 0; return; }
    for (int c = 0; c < 3; ++c) out[c] = static_cast<uint8_t>(std::clamp(acc[c] / acc[3], 0.0f, 255.0f));
    out[3] = static_cast<uint8_t>(std::clamp(acc[3] * 255.0f, 0.0f, 255.0f));
}

// Convex hull (monotone chain), counter-clockwise in image coordinates.
long long Cross(std::pair<int, int> o, std::pair<int, int> a, std::pair<int, int> b) {
    return static_cast<long long>(a.first - o.first) * (b.second - o.second) -
           static_cast<long long>(a.second - o.second) * (b.first - o.first);
}

std::vector<std::pair<int, int>> ConvexHull(std::vector<std::pair<int, int>> pts) {
    std::sort(pts.begin(), pts.end());
    pts.erase(std::unique(pts.begin(), pts.end()), pts.end());
    if (pts.size() < 3) return pts;
    std::vector<std::pair<int, int>> hull(pts.size() * 2);
    size_t m = 0;
    for (size_t i = 0; i < pts.size(); ++i) {
        while (m >= 2 && Cross(hull[m - 2], hull[m - 1], pts[i]) <= 0) --m;
        hull[m++] = pts[i];
    }
    for (size_t i = pts.size() - 1, t = m + 1; i-- > 0;) {
        while (m >= t && Cross(hull[m - 2], hull[m - 1], pts[i]) <= 0) --m;
        hull[m++] = pts[i];
    }
    hull.resize(m - 1);
    return hull;
}

bool InHull(const std::vector<std::pair<int, int>>& hull, int x, int y) {
    for (size_t i = 0; i < hull.size(); ++i)
        if (Cross(hull[i], hull[(i + 1) % hull.size()], {x, y}) < 0) return false;
    return true;
}

}  // namespace

bool DecodeImage(const void* data, size_t size, Image* out) {
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICStream> stream;
    ComPtr<IWICBitmapDecoder> dec;
    return wic && size > 0 && size < 64u * 1024 * 1024 && SUCCEEDED(wic->CreateStream(&stream)) &&
           SUCCEEDED(stream->InitializeFromMemory(static_cast<BYTE*>(const_cast<void*>(data)), static_cast<DWORD>(size))) &&
           SUCCEEDED(wic->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &dec)) &&
           FromDecoder(wic.Get(), dec.Get(), out);
}

bool DecodeImageFile(const std::wstring& path, Image* out) {
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICBitmapDecoder> dec;
    return wic && SUCCEEDED(wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, &dec)) &&
           FromDecoder(wic.Get(), dec.Get(), out);
}

bool EncodePng(const Image& img, const std::wstring& path) {
    ComPtr<IWICImagingFactory> wic = Wic();
    ComPtr<IWICStream> stream;
    return wic && SUCCEEDED(wic->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
           Encode(img, stream.Get());
}

bool EncodePngBytes(const Image& img, std::string* bytes) {
    ComPtr<IStream> stream;
    if (FAILED(CreateStreamOnHGlobal(nullptr, TRUE, &stream)) || !Encode(img, stream.Get())) return false;
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    bytes->resize(static_cast<size_t>(st.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    return SUCCEEDED(stream->Read(bytes->data(), static_cast<ULONG>(bytes->size()), &read)) && read == bytes->size();
}

std::vector<SheetRow> SliceSheet(const Image& sheet) {
    const int W = sheet.w, H = sheet.h;
    if (W < 32 || H < 32) return {};
    const size_t N = static_cast<size_t>(W) * H;

    // Background: transparent (or see-through) if a good part of the sheet is,
    // otherwise the colour around the border.
    std::vector<int> alphaHist(256, 0);
    for (size_t i = 0; i < N; ++i) ++alphaHist[sheet.px[i * 4 + 3]];
    int low = 0;
    for (int a = 0; a < 200; ++a) low += alphaHist[static_cast<size_t>(a)];
    const bool alphaMode = low > static_cast<int>(N / 10);
    auto percentile = [&](double q) {  // of the alpha values below 200
        int need = static_cast<int>(low * q), seen = 0;
        for (int a = 0; a < 200; ++a) if ((seen += alphaHist[static_cast<size_t>(a)]) > need) return a;
        return 199;
    };
    const int bgA = alphaMode ? percentile(0.5) : 255;
    // A see-through background (not fully clear) can be uneven: opacity starts just above it.
    const int lo = alphaMode && bgA > 60 ? std::min(230, percentile(0.99) + 15) : 0;
    int bg[3] = {0, 0, 0};
    if (!alphaMode) {
        std::vector<int> hist[3] = {std::vector<int>(256), std::vector<int>(256), std::vector<int>(256)};
        int count = 0;
        auto addPx = [&](int x, int y) { const uint8_t* p = sheet.at(x, y); for (int c = 0; c < 3; ++c) ++hist[c][p[c]]; ++count; };
        for (int x = 0; x < W; ++x) for (int y : {0, 1, H - 2, H - 1}) addPx(x, y);
        for (int y = 0; y < H; ++y) for (int x : {0, 1, W - 2, W - 1}) addPx(x, y);
        for (int c = 0; c < 3; ++c) {
            int seen = 0;
            for (int v = 0; v < 256; ++v) if ((seen += hist[c][static_cast<size_t>(v)]) > count / 2) { bg[c] = v; break; }
        }
    }
    std::vector<uint8_t> fg(N);
    for (size_t i = 0; i < N; ++i) {
        const uint8_t* p = &sheet.px[i * 4];
        fg[i] = alphaMode ? p[3] > (bgA + 255) / 2 : Diff(p, bg) > 30;
    }

    // Rows: horizontal bands with something in them (thin dashed separators are too short).
    std::vector<float> rowProfile(static_cast<size_t>(H));
    for (int y = 0; y < H; ++y) {
        int c = 0;
        for (int x = 0; x < W; ++x) c += fg[static_cast<size_t>(y) * W + x];
        rowProfile[static_cast<size_t>(y)] = static_cast<float>(c) / W;
    }
    const auto bands = Runs(rowProfile, 0.004f, std::max(24, H / 40));
    if (bands.empty()) return {};
    std::vector<int> heights;
    for (const auto& b : bands) heights.push_back(b.second - b.first);
    std::nth_element(heights.begin(), heights.begin() + heights.size() / 2, heights.end());
    const float bandH = static_cast<float>(heights[heights.size() / 2]);
    const float scale = kPackCanvasH / bandH;  // every sheet ends up the same size

    std::vector<SheetRow> rows;
    for (const auto& [y0, y1] : bands) {
        std::vector<float> colProfile(static_cast<size_t>(W));
        for (int x = 0; x < W; ++x) {
            int c = 0;
            for (int y = y0; y < y1; ++y) c += fg[static_cast<size_t>(y) * W + x];
            colProfile[static_cast<size_t>(x)] = static_cast<float>(c) / (y1 - y0);
        }
        // Frames: wide column runs, with small gaps (hair spikes, "zzz") merged in.
        std::vector<std::pair<int, int>> cols;
        for (const auto& r : Runs(colProfile, 0.01f, 1)) {
            if (!cols.empty() && r.first - cols.back().second < 8) cols.back().second = r.second;
            else cols.push_back(r);
        }
        std::vector<std::pair<int, int>> frames;
        for (const auto& c : cols)
            if (c.second - c.first >= (y1 - y0) * 0.45f) frames.push_back(c);
        // A label box on the left: much wider than the frames after it.
        if (frames.size() >= 3) {
            std::vector<int> widths;
            for (size_t i = 1; i < frames.size(); ++i) widths.push_back(frames[i].second - frames[i].first);
            std::nth_element(widths.begin(), widths.begin() + widths.size() / 2, widths.end());
            if (frames[0].second - frames[0].first > widths[widths.size() / 2] * 1.6f) frames.erase(frames.begin());
        }
        if (frames.empty()) continue;

        SheetRow row;
        for (const auto& [fx0, fx1] : frames) {
            // Cut the frame out.
            const int pad = 4;
            const int cx0 = std::max(0, fx0 - pad), cy0 = std::max(0, y0 - pad);
            const int cx1 = std::min(W, fx1 + pad), cy1 = std::min(H, y1 + pad);
            Image cut;
            cut.w = cx1 - cx0;
            cut.h = cy1 - cy0;
            cut.px.assign(static_cast<size_t>(cut.w) * cut.h * 4, 0);
            for (int y = 0; y < cut.h; ++y)
                for (int x = 0; x < cut.w; ++x) {
                    const uint8_t* p = sheet.at(cx0 + x, cy0 + y);
                    uint8_t* q = cut.at(x, y);
                    q[0] = p[0]; q[1] = p[1]; q[2] = p[2];
                    q[3] = alphaMode ? static_cast<uint8_t>(std::clamp((p[3] - lo) * 255 / std::max(1, 250 - lo), 0, 255)) : 255;
                }
            if (!alphaMode) {
                // Flood the background in from the edges. A tight tolerance stops
                // at the outline even where hair is nearly the background's colour.
                std::vector<uint8_t> isBg(static_cast<size_t>(cut.w) * cut.h), seen(isBg.size());
                for (int y = 0; y < cut.h; ++y)
                    for (int x = 0; x < cut.w; ++x) isBg[static_cast<size_t>(y) * cut.w + x] = Diff(cut.at(x, y), bg) <= 10;
                std::deque<int> q;
                auto seed = [&](int x, int y) {
                    const size_t i = static_cast<size_t>(y) * cut.w + x;
                    if (isBg[i] && !seen[i]) { seen[i] = 1; q.push_back(static_cast<int>(i)); }
                };
                for (int x = 0; x < cut.w; ++x) { seed(x, 0); seed(x, cut.h - 1); }
                for (int y = 0; y < cut.h; ++y) { seed(0, y); seed(cut.w - 1, y); }
                while (!q.empty()) {
                    const int p = q.front();
                    q.pop_front();
                    const int x = p % cut.w, y = p / cut.w;
                    if (x > 0) seed(x - 1, y);
                    if (x + 1 < cut.w) seed(x + 1, y);
                    if (y > 0) seed(x, y - 1);
                    if (y + 1 < cut.h) seed(x, y + 1);
                }
                for (int y = 0; y < cut.h; ++y)
                    for (int x = 0; x < cut.w; ++x) {
                        const size_t i = static_cast<size_t>(y) * cut.w + x;
                        if (seen[i]) { cut.at(x, y)[3] = 0; continue; }
                        // Soften the pixels along the edge by how background-like they are.
                        const bool edge = (x > 0 && seen[i - 1]) || (x + 1 < cut.w && seen[i + 1]) ||
                                          (y > 0 && seen[i - static_cast<size_t>(cut.w)]) ||
                                          (y + 1 < cut.h && seen[i + static_cast<size_t>(cut.w)]);
                        if (edge) cut.at(x, y)[3] = static_cast<uint8_t>(std::clamp(Diff(cut.at(x, y), bg) * 6, 90, 255));
                    }
            }
            Clean(&cut);

            // Anchor: the middle of the head. Midpoints of the widest piece's rows
            // in its upper half; separate effects (a bulb, "?") don't move it.
            std::vector<uint8_t> solid(static_cast<size_t>(cut.w) * cut.h);
            for (size_t i = 0; i < solid.size(); ++i) solid[i] = cut.px[i * 4 + 3] > 128;
            std::vector<int> lab;
            const int n = Label(solid, cut.w, cut.h, &lab);
            std::vector<Box> boxes(static_cast<size_t>(n) + 1);
            for (int i = 0; i < cut.w * cut.h; ++i)
                if (lab[static_cast<size_t>(i)]) boxes[static_cast<size_t>(lab[static_cast<size_t>(i)])].add(i % cut.w, i / cut.w);
            int big = 0;
            for (int k = 1; k <= n; ++k) if (!big || boxes[static_cast<size_t>(k)].count > boxes[static_cast<size_t>(big)].count) big = k;
            float anchorX = cut.w * 0.5f;
            if (big) {
                const Box& b = boxes[static_cast<size_t>(big)];
                std::vector<float> mids;
                for (int y = b.y0; y <= b.y0 + (b.y1 - b.y0) / 2; ++y) {
                    int l = -1, r = -1;
                    for (int x = b.x0; x <= b.x1; ++x)
                        if (lab[static_cast<size_t>(y) * cut.w + x] == big) { if (l < 0) l = x; r = x; }
                    if (l >= 0) mids.push_back((l + r) * 0.5f);
                }
                if (!mids.empty()) {
                    std::nth_element(mids.begin(), mids.begin() + mids.size() / 2, mids.end());
                    anchorX = mids[mids.size() / 2];
                }
            }

            // Onto the shared canvas: head centred, the row's bottom on the bottom edge.
            Image out;
            out.w = kPackCanvasW;
            out.h = kPackCanvasH;
            out.px.assign(static_cast<size_t>(out.w) * out.h * 4, 0);
            const float bottom = static_cast<float>(y1 - cy0);  // the row's bottom, in `cut`
            for (int y = 0; y < out.h; ++y)
                for (int x = 0; x < out.w; ++x)
                    Sample(cut, (x + 0.5f - out.w * 0.5f) / scale + anchorX - 0.5f, (y + 0.5f - out.h) / scale + bottom - 0.5f,
                           out.at(x, y));
            row.frames.push_back(std::move(out));
        }
        rows.push_back(std::move(row));
    }
    return rows;
}

bool WriteMoodFrames(const std::wstring& packDir, const std::wstring& mood, const std::vector<Image>& frames,
                     std::wstring* err) {
    if (mood.empty() || mood.find_first_of(L"\\/:.") != std::wstring::npos) { *err = L"bad mood name"; return false; }
    SHCreateDirectoryExW(nullptr, packDir.c_str(), nullptr);
    DeleteFileW((packDir + L"\\" + mood + L".png").c_str());
    for (int i = 1; i <= 32; ++i) DeleteFileW((packDir + L"\\" + mood + L"_" + std::to_wstring(i) + L".png").c_str());
    for (size_t i = 0; i < frames.size(); ++i) {
        const std::wstring path = packDir + L"\\" + mood + L"_" + std::to_wstring(i + 1) + L".png";
        if (!EncodePng(frames[i], path)) { *err = L"couldn't write " + path; return false; }
    }
    return true;
}

EyeRig FindEyes(const Image& f) {
    EyeRig rig;
    const int w = f.w, h = f.h;
    if (w < 16 || h < 16) return rig;
    // Where the character is.
    int vx0 = w, vy0 = h, vx1 = -1, vy1 = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            if (f.at(x, y)[3] > 128) { vx0 = std::min(vx0, x); vy0 = std::min(vy0, y); vx1 = std::max(vx1, x); vy1 = std::max(vy1, y); }
    if (vx1 < 0) return rig;
    const int vArea = (vx1 - vx0 + 1) * (vy1 - vy0 + 1);
    const int eyeZone = vy0 + (vy1 - vy0) * 7 / 10;  // eyes are in the upper part

    auto isWhite = [&](int x, int y) {
        const uint8_t* p = f.at(x, y);
        const int mn = std::min({p[0], p[1], p[2]}), mx = std::max({p[0], p[1], p[2]});
        return p[3] > 200 && mn > 185 && mx - mn < 50;
    };
    auto isIris = [&](int x, int y) {
        const uint8_t* p = f.at(x, y);
        return p[3] > 200 && (p[0] * 299 + p[1] * 587 + p[2] * 114) / 1000 < 140;
    };
    std::vector<uint8_t> white(static_cast<size_t>(w) * h);
    for (int y = vy0; y < eyeZone; ++y)
        for (int x = vx0; x <= vx1; ++x) white[static_cast<size_t>(y) * w + x] = isWhite(x, y);
    std::vector<int> lab;
    const int n = Label(white, w, h, &lab);
    std::vector<Box> boxes(static_cast<size_t>(n) + 1);
    for (int i = 0; i < w * h; ++i)
        if (lab[static_cast<size_t>(i)]) boxes[static_cast<size_t>(lab[static_cast<size_t>(i)])].add(i % w, i / w);

    // Big irises leave only crescents of white, often in two or three pieces per
    // eye: pieces close together are one eye, and the eye is their convex hull.
    const int gap = std::max(3, w / 60);
    std::vector<int> group(static_cast<size_t>(n) + 1, 0);
    std::vector<Box> groups;
    for (int k = 1; k <= n; ++k) {
        const Box& b = boxes[static_cast<size_t>(k)];
        if (b.count < 4 || b.count > vArea / 25) continue;
        int g = -1;
        for (size_t i = 0; i < groups.size(); ++i) {
            const Box& o = groups[i];
            if (b.x0 <= o.x1 + gap && b.x1 >= o.x0 - gap && b.y0 <= o.y1 + gap && b.y1 >= o.y0 - gap) { g = static_cast<int>(i); break; }
        }
        if (g < 0) { groups.push_back(Box{}); g = static_cast<int>(groups.size()) - 1; }
        Box& o = groups[static_cast<size_t>(g)];
        o.x0 = std::min(o.x0, b.x0); o.y0 = std::min(o.y0, b.y0); o.x1 = std::max(o.x1, b.x1); o.y1 = std::max(o.y1, b.y1);
        o.count += b.count;
        group[static_cast<size_t>(k)] = g + 1;
    }

    struct Eye {
        std::vector<uint8_t> inside;  // hull mask over the eye's box
        Box box;
        int whiteCount = 0;
        long long sr = 0, sg = 0, sb = 0;
        std::vector<int> iris;
        int ix0 = 1 << 30, ix1 = -1, iy0 = 1 << 30, iy1 = -1;
    };
    std::vector<Eye> eyes;
    for (size_t g = 0; g < groups.size(); ++g) {
        const Box& gb = groups[g];
        const int gw = gb.x1 - gb.x0 + 1, gh = gb.y1 - gb.y0 + 1;
        if (gw < 4 || gh < 4) continue;
        Eye e;
        std::vector<std::pair<int, int>> pts;  // the white, then the iris: the eye is their hull
        for (int y = gb.y0; y <= gb.y1; ++y)
            for (int x = gb.x0; x <= gb.x1; ++x) {
                const int k = lab[static_cast<size_t>(y) * w + x];
                if (!k || group[static_cast<size_t>(k)] != static_cast<int>(g) + 1) continue;
                pts.emplace_back(x, y);
                const uint8_t* p = f.at(x, y);
                e.sr += p[0]; e.sg += p[1]; e.sb += p[2]; ++e.whiteCount;
            }
        // Search a little around the white for the iris (it can stick out past a crescent of white).
        const int size = std::max(gw, gh);
        const int sx0 = std::max(0, gb.x0 - size * 6 / 10), sx1 = std::min(w - 1, gb.x1 + size * 6 / 10);
        const int sy0 = std::max(0, gb.y0 - gh * 3 / 10), sy1 = std::min(h - 1, gb.y1 + gh * 3 / 10);
        const int bw = sx1 - sx0 + 1, bh = sy1 - sy0 + 1;
        std::vector<uint8_t> dark(static_cast<size_t>(bw) * bh, 0);
        for (int y = 0; y < bh; ++y)
            for (int x = 0; x < bw; ++x) dark[static_cast<size_t>(y) * bw + x] = isIris(sx0 + x, sy0 + y);
        // Opening: thin dark lines (the eye's outline, lashes) vanish, the iris disc stays.
        const int r = std::max(1, size / 9);
        auto morph = [&](const std::vector<uint8_t>& in, bool erode) {
            std::vector<uint8_t> out(in.size(), 0);
            for (int y = 0; y < bh; ++y)
                for (int x = 0; x < bw; ++x) {
                    bool v = erode;
                    for (int dy = -r; dy <= r && v == erode; ++dy)
                        for (int dx = -r; dx <= r && v == erode; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            const bool on = nx >= 0 && ny >= 0 && nx < bw && ny < bh && in[static_cast<size_t>(ny) * bw + nx];
                            if (erode && !on) v = false;
                            if (!erode && on) v = true;
                        }
                    out[static_cast<size_t>(y) * bw + x] = v;
                }
            return out;
        };
        const std::vector<uint8_t> disc = morph(morph(dark, true), false);
        std::vector<int> dl;
        const int dn = Label(disc, bw, bh, &dl);
        // The iris: the biggest disc piece whose middle lies within the white's reach.
        std::vector<Box> db(static_cast<size_t>(dn) + 1);
        for (int i = 0; i < bw * bh; ++i) if (dl[static_cast<size_t>(i)]) db[static_cast<size_t>(dl[static_cast<size_t>(i)])].add(i % bw, i / bw);
        int best = 0;
        for (int k = 1; k <= dn; ++k) {
            const Box& b = db[static_cast<size_t>(k)];
            const int cx = sx0 + (b.x0 + b.x1) / 2, cy = sy0 + (b.y0 + b.y1) / 2;
            if (cy < gb.y0 || cy > gb.y1 || cx < gb.x0 - size / 2 || cx > gb.x1 + size / 2) continue;
            if (!best || b.count > db[static_cast<size_t>(best)].count) best = k;
        }
        if (!best) continue;
        // Rounded out to a hull, so highlights on it move with it, and one pixel
        // more for its soft edge (more would take the eye's outline with it).
        std::vector<std::pair<int, int>> blob;
        for (int i = 0; i < bw * bh; ++i)
            if (dl[static_cast<size_t>(i)] == best) blob.emplace_back(sx0 + i % bw, sy0 + i / bw);
        std::vector<std::pair<int, int>> irisHull = ConvexHull(blob);
        if (irisHull.size() < 3) continue;
        std::vector<std::pair<int, int>> grown;
        for (const auto& [x, y] : irisHull)
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) grown.emplace_back(x + dx, y + dy);
        irisHull = ConvexHull(grown);
        int ix0 = w, ix1 = 0, iy0 = h, iy1 = 0;
        for (const auto& [x, y] : irisHull) { ix0 = std::min(ix0, x); ix1 = std::max(ix1, x); iy0 = std::min(iy0, y); iy1 = std::max(iy1, y); }
        for (const auto& pt : irisHull) pts.push_back(pt);
        const std::vector<std::pair<int, int>> eyeHull = ConvexHull(pts);
        if (eyeHull.size() < 3) continue;
        e.box = Box{};
        for (const auto& [x, y] : eyeHull) e.box.add(std::clamp(x, 0, w - 1), std::clamp(y, 0, h - 1));
        const int ew = e.box.x1 - e.box.x0 + 1, eh = e.box.y1 - e.box.y0 + 1;
        e.inside.assign(static_cast<size_t>(ew) * eh, 0);
        for (int y = e.box.y0; y <= e.box.y1; ++y)
            for (int x = e.box.x0; x <= e.box.x1; ++x) {
                if (!InHull(eyeHull, x, y)) continue;
                e.inside[static_cast<size_t>(y - e.box.y0) * ew + (x - e.box.x0)] = 1;
                if (x >= ix0 && x <= ix1 && y >= iy0 && y <= iy1 && InHull(irisHull, x, y) && f.at(x, y)[3] > 200) {
                    e.iris.push_back(y * w + x);
                    e.ix0 = std::min(e.ix0, x); e.ix1 = std::max(e.ix1, x);
                    e.iy0 = std::min(e.iy0, y); e.iy1 = std::max(e.iy1, y);
                }
            }
        const int area = static_cast<int>(std::count(e.inside.begin(), e.inside.end(), 1));
        if (e.iris.size() >= 8 && static_cast<int>(e.iris.size()) * 4 >= area) eyes.push_back(std::move(e));
    }
    if (eyes.size() < 2) return rig;
    std::sort(eyes.begin(), eyes.end(), [](const Eye& a, const Eye& b) { return a.iris.size() > b.iris.size(); });
    const Eye& a = eyes[0];
    const Eye& b = eyes[1];
    const float acy = (a.box.y0 + a.box.y1) * 0.5f, bcy = (b.box.y0 + b.box.y1) * 0.5f;
    const float acx = (a.box.x0 + a.box.x1) * 0.5f, bcx = (b.box.x0 + b.box.x1) * 0.5f;
    const int eyeH = std::max(a.box.y1 - a.box.y0, b.box.y1 - b.box.y0) + 1;
    const int eyeW = std::max(a.box.x1 - a.box.x0, b.box.x1 - b.box.x0) + 1;
    if (std::fabs(acy - bcy) > eyeH || std::fabs(acx - bcx) < eyeW) return rig;

    rig.base = f;
    rig.irises.w = rig.mask.w = w;
    rig.irises.h = rig.mask.h = h;
    rig.irises.px.assign(f.px.size(), 0);
    rig.mask.px.assign(f.px.size(), 0);
    rig.maxDx = rig.maxDy = 1e9f;
    for (const Eye* e : {&a, &b}) {
        const int cnt = std::max(1, e->whiteCount);
        const uint8_t sclera[3] = {static_cast<uint8_t>(e->sr / cnt), static_cast<uint8_t>(e->sg / cnt), static_cast<uint8_t>(e->sb / cnt)};
        for (int i : e->iris) {
            const uint8_t* p = &f.px[static_cast<size_t>(i) * 4];
            std::copy(p, p + 4, &rig.irises.px[static_cast<size_t>(i) * 4]);
            uint8_t* o = &rig.base.px[static_cast<size_t>(i) * 4];
            o[0] = sclera[0]; o[1] = sclera[1]; o[2] = sclera[2];
        }
        const int bw = e->box.x1 - e->box.x0 + 1;
        for (int y = e->box.y0; y <= e->box.y1; ++y)
            for (int x = e->box.x0; x <= e->box.x1; ++x)
                if (e->inside[static_cast<size_t>(y - e->box.y0) * bw + (x - e->box.x0)]) {
                    uint8_t* q = rig.mask.at(x, y);
                    q[0] = q[1] = q[2] = q[3] = 255;
                }
        // The iris may slide part-way behind the lid, the way drawn eyes glance aside.
        const int irisW = e->ix1 - e->ix0 + 1, irisH = e->iy1 - e->iy0 + 1;
        const int ew = e->box.x1 - e->box.x0 + 1, eh = e->box.y1 - e->box.y0 + 1;
        rig.maxDx = std::min(rig.maxDx, std::max((ew - irisW) * 0.5f, ew * 0.16f));
        rig.maxDy = std::min(rig.maxDy, std::max((eh - irisH) * 0.5f, eh * 0.10f));
    }
    rig.found = true;
    return rig;
}

}  // namespace bb::mascot
