// Bunbun: Image decoding and app / window icon lookup.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

bool DecodeImageBytesToPixels(const std::vector<uint8_t>& bytes, BitmapPixels* outPixels) {
    if (!outPixels || bytes.empty()) {
        return false;
    }

    ComPtr<IWICImagingFactory> factory;
    HRESULT hr = CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                  IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        return false;
    }

    HGLOBAL mem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!mem) {
        return false;
    }

    void* locked = GlobalLock(mem);
    memcpy(locked, bytes.data(), bytes.size());
    GlobalUnlock(mem);

    ComPtr<IStream> stream;
    hr = CreateStreamOnHGlobal(mem, TRUE, &stream);
    if (FAILED(hr)) {
        GlobalFree(mem);
        return false;
    }

    ComPtr<IWICBitmapDecoder> decoder;
    hr = factory->CreateDecoderFromStream(stream.Get(), nullptr, WICDecodeMetadataCacheOnLoad,
                                          &decoder);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, &frame);
    if (FAILED(hr)) {
        return false;
    }

    ComPtr<IWICFormatConverter> converter;
    hr = factory->CreateFormatConverter(&converter);
    if (FAILED(hr)) {
        return false;
    }

    hr = converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppPBGRA,
                               WICBitmapDitherTypeNone, nullptr, 0.0,
                               WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) {
        return false;
    }

    UINT width = 0;
    UINT height = 0;
    converter->GetSize(&width, &height);
    if (!width || !height || width > 2048 || height > 2048) {
        return false;
    }

    BitmapPixels pixels;
    pixels.width = width;
    pixels.height = height;
    pixels.bgra.resize(static_cast<size_t>(width) * height * 4);

    hr = converter->CopyPixels(nullptr, width * 4,
                               static_cast<UINT>(pixels.bgra.size()),
                               pixels.bgra.data());
    if (FAILED(hr)) {
        return false;
    }

    struct Bucket {
        uint32_t count = 0;
        uint32_t r = 0;
        uint32_t g = 0;
        uint32_t b = 0;
        uint32_t satSum = 0;  // accumulated saturation for vibrancy-weighted winner
    };

    std::array<Bucket, 16 * 16 * 16> buckets{};
    for (size_t i = 0; i + 3 < pixels.bgra.size(); i += 4) {
        const uint8_t alpha = pixels.bgra[i + 3];
        const uint8_t blue  = pixels.bgra[i + 0];
        const uint8_t green = pixels.bgra[i + 1];
        const uint8_t red   = pixels.bgra[i + 2];
        if (alpha < 32) {
            continue;
        }

        const int maxc = std::max({red, green, blue});
        const int minc = std::min({red, green, blue});
        const int luminance = (54 * red + 183 * green + 19 * blue) / 256;
        const int saturation = maxc - minc;
        // Reject near-black, near-white, and near-gray pixels.
        // Raise luminance floor slightly (36 instead of 28) so very dark-but-colorful
        // pixels don't swamp vibrancy scoring on dark-mood album art.
        if (luminance < 36 || luminance > 220 || saturation < 28) {
            continue;
        }

        const size_t bucketIndex = ((red >> 4) << 8) | ((green >> 4) << 4) | (blue >> 4);
        Bucket& bucket = buckets[bucketIndex];
        const uint32_t weight = 1 + static_cast<uint32_t>(saturation / 32);  // stronger vibrancy weight
        bucket.count  += weight;
        bucket.r      += red   * weight;
        bucket.g      += green * weight;
        bucket.b      += blue  * weight;
        bucket.satSum += static_cast<uint32_t>(saturation) * weight;
    }

    // --- Vibrancy-weighted winner selection ---
    // Raw frequency alone lets large muted regions (e.g. a beige background)
    // beat a smaller vivid color that's visually "the" accent.
    // Strategy: collect the top 3 by count, then pick whichever has the
    // highest average saturation — cheap, no extra image pass required.
    struct Candidate { const Bucket* b = nullptr; size_t idx = 0; };
    Candidate top[3];
    for (size_t i = 0; i < buckets.size(); ++i) {
        const Bucket& bk = buckets[i];
        if (bk.count == 0) continue;
        for (int s = 0; s < 3; ++s) {
            if (!top[s].b || bk.count > top[s].b->count) {
                for (int t = 2; t > s; --t) top[t] = top[t - 1];
                top[s] = {&bk, i};
                break;
            }
        }
    }

    // Among those top-3, pick the one with the highest avg saturation.
    const Bucket* best = nullptr;
    size_t bestIdx = 0;
    float bestVibrancy = -1.0f;
    for (int s = 0; s < 3; ++s) {
        if (!top[s].b) break;
        const float vibrancy = static_cast<float>(top[s].b->satSum) /
                               static_cast<float>(top[s].b->count);
        if (vibrancy > bestVibrancy) {
            bestVibrancy = vibrancy;
            best = top[s].b;
            bestIdx = top[s].idx;
        }
    }

    if (best && best->count > 0) {
        // --- Neighbor-bucket merging ---
        // A color straddling a bucket boundary splits its votes across up to 8
        // adjacent cells. Pool the winning bucket with all 26 face/edge/corner
        // neighbors before averaging so the final RGB is stable and representative.
        uint64_t poolCount = 0;
        double poolR = 0, poolG = 0, poolB = 0;

        const int bi = static_cast<int>((bestIdx >> 8) & 0xF);  // red index
        const int gi = static_cast<int>((bestIdx >> 4) & 0xF);  // green index
        const int bli = static_cast<int>( bestIdx       & 0xF); // blue index

        for (int dr = -1; dr <= 1; ++dr) {
            for (int dg = -1; dg <= 1; ++dg) {
                for (int db = -1; db <= 1; ++db) {
                    const int ni = bi + dr, nj = gi + dg, nk = bli + db;
                    if (ni < 0 || ni > 15 || nj < 0 || nj > 15 || nk < 0 || nk > 15) continue;
                    const size_t nIdx = (static_cast<size_t>(ni) << 8) |
                                        (static_cast<size_t>(nj) << 4) |
                                         static_cast<size_t>(nk);
                    const Bucket& nb = buckets[nIdx];
                    if (nb.count == 0) continue;
                    poolCount += nb.count;
                    poolR += nb.r;
                    poolG += nb.g;
                    poolB += nb.b;
                }
            }
        }

        if (poolCount > 0) {
            const float invC = 1.0f / static_cast<float>(poolCount);
            const float rawR = static_cast<float>(poolR * invC) / 255.0f;
            const float rawG = static_cast<float>(poolG * invC) / 255.0f;
            const float rawB = static_cast<float>(poolB * invC) / 255.0f;

            D2D1_COLOR_F bgColor = g_settings.pillBgColor;
            if (bgColor.a <= 0.0f) {
                bgColor = D2D1::ColorF(0.031f, 0.031f, 0.039f, 1.0f); // Obsidian default #08080A
            }

            pixels.sampledAccent = EnsureContrastAgainstBackground(
                D2D1::ColorF(rawR, rawG, rawB, 1.0f),
                bgColor);
        }
        // If poolCount is somehow 0 (all neighbors empty and center also cleared),
        // the existing sampledAccent default (#4cc9f0) is kept unchanged — explicit
        // fallback for fully-grayscale/near-black-and-white album art.
    }
    // else: no qualifying colorful pixel found (B&W / fully-filtered image)
    // — leave sampledAccent at the BitmapPixels default (#4cc9f0).

    pixels.generation = ++g_artGenerationCounter;
    *outPixels = std::move(pixels);
    return true;
}

bool IconToPixels(HICON icon, UINT size, BitmapPixels* outPixels) {
    if (!icon || !outPixels || !size) {
        return false;
    }

    HDC screen = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen);
    ReleaseDC(nullptr, screen);
    if (!dc) {
        return false;
    }

    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = static_cast<LONG>(size);
    bi.bmiHeader.biHeight = -static_cast<LONG>(size);
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;

    void* bits = nullptr;
    HBITMAP bitmap = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!bitmap) {
        DeleteDC(dc);
        return false;
    }

    // Fill with transparent black (alpha=0) so icon edges don't bleed dark fringe.
    HGDIOBJ old = SelectObject(dc, bitmap);
    RECT fill = {0, 0, static_cast<LONG>(size), static_cast<LONG>(size)};
    // Use NULL_BRUSH (transparent) then manually zero-fill the BGRA buffer after copy.
    FillRect(dc, &fill, reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH)));
    // Zero out the bits so background is fully transparent before drawing icon.
    ZeroMemory(bits, static_cast<size_t>(size) * size * 4);
    DrawIconEx(dc, 0, 0, icon, size, size, 0, nullptr, DI_NORMAL);

    BitmapPixels pixels;
    pixels.width = size;
    pixels.height = size;
    pixels.bgra.resize(static_cast<size_t>(size) * size * 4);
    memcpy(pixels.bgra.data(), bits, pixels.bgra.size());

    // Older icons can have no alpha in the color bitmap. Treat black pixels as
    // transparent only when the icon did not write any alpha at all.
    bool hasAlpha = false;
    for (size_t i = 3; i < pixels.bgra.size(); i += 4) {
        if (pixels.bgra[i] != 0) {
            hasAlpha = true;
            break;
        }
    }
    if (!hasAlpha) {
        // No alpha channel: treat near-black as transparent, rest as opaque.
        for (size_t i = 0; i + 3 < pixels.bgra.size(); i += 4) {
            const bool black = pixels.bgra[i] < 4 && pixels.bgra[i + 1] < 4 && pixels.bgra[i + 2] < 4;
            pixels.bgra[i + 3] = black ? 0 : 255;
        }
    } else {
        // Convert to premultiplied alpha so D2D renders edges cleanly without dark fringing.
        for (size_t i = 0; i + 3 < pixels.bgra.size(); i += 4) {
            const uint8_t a = pixels.bgra[i + 3];
            if (a < 255 && a > 0) {
                pixels.bgra[i + 0] = static_cast<uint8_t>(pixels.bgra[i + 0] * a / 255);
                pixels.bgra[i + 1] = static_cast<uint8_t>(pixels.bgra[i + 1] * a / 255);
                pixels.bgra[i + 2] = static_cast<uint8_t>(pixels.bgra[i + 2] * a / 255);
            }
        }
    }

    pixels.generation = ++g_artGenerationCounter;
    *outPixels = std::move(pixels);

    SelectObject(dc, old);
    DeleteObject(bitmap);
    DeleteDC(dc);
    return true;
}

PixelsRef IconToPixelsRef(HICON icon, UINT size) {
    BitmapPixels px;
    return IconToPixels(icon, size, &px) ? PixelsRef(std::move(px)) : PixelsRef();
}

bool ProcessImageNameForPid(DWORD pid, std::wstring* imageName) {
    if (!pid || !imageName) {
        return false;
    }

    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process) {
        return false;
    }

    wchar_t path[MAX_PATH] = {};
    DWORD size = ARRAYSIZE(path);
    const bool ok = QueryFullProcessImageNameW(process, 0, path, &size) != FALSE;
    CloseHandle(process);
    if (ok) {
        *imageName = path;
    }
    return ok;
}

HICON CopyWindowIcon(HWND hwnd, WPARAM iconType) {
    DWORD_PTR result = 0;
    SendMessageTimeoutW(hwnd, WM_GETICON, iconType, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 80, &result);
    return result ? CopyIcon(reinterpret_cast<HICON>(result)) : nullptr;
}

HICON getProcessIcon(DWORD pid) {
    std::wstring path;
    if (ProcessImageNameForPid(pid, &path) && !path.empty()) {
        HICON hIcon = nullptr;
        UINT iconId = 0;
        // Try to fetch a high-res 64x64 icon first to avoid pixelated icons
        using PrivateExtractIconsW_t = UINT(WINAPI*)(LPCWSTR, int, int, int, HICON*, UINT*, UINT, UINT);
        static auto pPrivateExtractIconsW = reinterpret_cast<PrivateExtractIconsW_t>(
            GetProcAddress(GetModuleHandleW(L"user32.dll"), "PrivateExtractIconsW"));
        if (pPrivateExtractIconsW && pPrivateExtractIconsW(path.c_str(), 0, 64, 64, &hIcon, &iconId, 1, 0) == 1 && hIcon) {
            return hIcon;
        }

        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(path.c_str(), 0, &sfi, sizeof(sfi),
                           SHGFI_ICON | SHGFI_LARGEICON)) {
            return sfi.hIcon;
        }

        HICON large = nullptr;
        HICON small = nullptr;
        if (ExtractIconExW(path.c_str(), 0, &large, &small, 1) > 0) {
            if (small) {
                DestroyIcon(small);
            }
            if (large) {
                return large;
            }
        }
    }

    return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
}

HICON getWindowIcon(HWND hwnd) {
    if (!hwnd) {
        return CopyIcon(LoadIconW(nullptr, IDI_APPLICATION));
    }

    if (HICON icon = CopyWindowIcon(hwnd, ICON_BIG)) {
        return icon;
    }
    if (HICON icon = CopyWindowIcon(hwnd, ICON_SMALL)) {
        return icon;
    }

    if (auto icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICON))) {
        return CopyIcon(icon);
    }
    if (auto icon = reinterpret_cast<HICON>(GetClassLongPtrW(hwnd, GCLP_HICONSM))) {
        return CopyIcon(icon);
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    return getProcessIcon(pid);
}

std::wstring ToLowerCopy(std::wstring value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](wchar_t ch) { return static_cast<wchar_t>(towlower(ch)); });
    return value;
}

std::wstring BaseNameFromPath(std::wstring path) {
    const size_t slash = path.find_last_of(L"\\/");
    if (slash != std::wstring::npos) {
        path.erase(0, slash + 1);
    }
    return path;
}

std::wstring StripExtension(std::wstring value) {
    const size_t dot = value.find_last_of(L'.');
    if (dot != std::wstring::npos) {
        value.resize(dot);
    }
    return value;
}

bool ProcessImageNameForWindow(HWND hwnd, std::wstring* imageName) {
    if (!imageName) {
        return false;
    }

    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) {
        return false;
    }

    return ProcessImageNameForPid(pid, imageName);
}

std::wstring FriendlyMediaSourceName(std::wstring_view source) {
    const std::wstring lower = ToLowerCopy(std::wstring(source));
    if (lower.find(L"youtube") != std::wstring::npos) return L"YouTube";
    if (lower.find(L"spotify") != std::wstring::npos) return L"Spotify";
    if (lower.find(L"chrome") != std::wstring::npos) return L"Chrome";
    if (lower.find(L"msedge") != std::wstring::npos || lower.find(L"edge") != std::wstring::npos) return L"Edge";
    if (lower.find(L"firefox") != std::wstring::npos) return L"Firefox";
    if (lower.find(L"vlc") != std::wstring::npos) return L"VLC";
    if (lower.find(L"wmplayer") != std::wstring::npos) return L"Windows Media";
    if (lower.find(L"zune") != std::wstring::npos || lower.find(L"media") != std::wstring::npos) return L"Media Player";

    std::wstring text(source);
    const size_t bang = text.find(L'!');
    if (bang != std::wstring::npos && bang + 1 < text.size()) {
        text.erase(0, bang + 1);
    }
    const size_t dot = text.find(L'.');
    if (dot != std::wstring::npos) {
        text.resize(dot);
    }
    return text.empty() ? L"Media" : text;
}

std::wstring MediaSourceBadge(std::wstring_view sourceName) {
    const std::wstring lower = ToLowerCopy(std::wstring(sourceName));
    if (lower.find(L"youtube") != std::wstring::npos) return L"YT";
    if (lower.find(L"spotify") != std::wstring::npos) return L"SP";
    if (lower.find(L"chrome") != std::wstring::npos) return L"CH";
    if (lower.find(L"edge") != std::wstring::npos) return L"ED";
    if (lower.find(L"firefox") != std::wstring::npos) return L"FF";
    if (lower.find(L"vlc") != std::wstring::npos) return L"VLC";
    if (sourceName.empty()) return L"\u25b6";
    std::wstring badge;
    badge.push_back(static_cast<wchar_t>(towupper(sourceName[0])));
    return badge;
}

bool WindowLooksLikeMediaSource(HWND hwnd, const std::wstring& sourceLower) {
    if (!IsWindowVisible(hwnd) || hwnd == g_hwnd) {
        return false;
    }

    std::wstring image;
    if (!ProcessImageNameForWindow(hwnd, &image)) {
        return false;
    }

    const std::wstring base = ToLowerCopy(BaseNameFromPath(image));
    if (base.empty()) {
        return false;
    }

    const std::wstring stem = StripExtension(base);
    return sourceLower.find(base) != std::wstring::npos ||
           (stem.size() >= 4 && sourceLower.find(stem) != std::wstring::npos) ||
           (base.find(L"chrome") != std::wstring::npos && sourceLower.find(L"chrome") != std::wstring::npos) ||
           (base.find(L"msedge") != std::wstring::npos && sourceLower.find(L"edge") != std::wstring::npos) ||
           (base.find(L"vlc") != std::wstring::npos && sourceLower.find(L"vlc") != std::wstring::npos);
}

// #62: short-form video feeds change "track" every few seconds, which turned
// auto-expand into a constant popup. A blocklist entry is matched loosely
// against the friendly source name, the raw AUMID and the title, so a single
// entry like "tiktok" catches both the desktop app and a browser tab. The
// collapsed pill still updates -- only the expansion is suppressed.
//
// Reads the blocklist under g_settingsMutex rather than taking a Settings
// reference: the caller is the render loop, and walking a std::vector<std::wstring>
// that LoadSettings() may be reassigning is a use-after-free, not just a stale
// read. The haystack is built outside the lock because that part allocates.
bool MediaExpandBlocked(const MediaSnapshot& media) {
    {
        // Fast path for the overwhelmingly common empty-blocklist case: one
        // uncontended lock, no allocation, no string work.
        std::lock_guard lock(g_settingsMutex);
        if (g_settings.mediaExpandBlocklist.empty()) {
            return false;
        }
    }

    const std::wstring haystack = ToLowerCopy(
        media.sourceName + L"\n" + media.sourceAppUserModelId + L"\n" + media.title);

    std::lock_guard lock(g_settingsMutex);
    for (const std::wstring& needle : g_settings.mediaExpandBlocklist) {
        if (!needle.empty() && haystack.find(needle) != std::wstring::npos) {
            return true;
        }
    }
    return false;
}

bool IsBrowserMediaSource(std::wstring_view source) {
    const std::wstring lower = ToLowerCopy(std::wstring(source));
    return lower.find(L"chrome") != std::wstring::npos ||
           lower.find(L"edge") != std::wstring::npos ||
           lower.find(L"msedge") != std::wstring::npos ||
           lower.find(L"firefox") != std::wstring::npos ||
           lower.find(L"youtube") != std::wstring::npos;
}

std::wstring SiteBadgeFromTitle(std::wstring_view title) {
    const std::wstring lower = ToLowerCopy(std::wstring(title));
    if (lower.find(L"youtube") != std::wstring::npos) return L"YT";
    if (lower.find(L"netflix") != std::wstring::npos) return L"NF";
    if (lower.find(L"prime video") != std::wstring::npos || lower.find(L"amazon") != std::wstring::npos) return L"PV";
    if (lower.find(L"disney") != std::wstring::npos) return L"D+";
    if (lower.find(L"hotstar") != std::wstring::npos) return L"HS";
    if (lower.find(L"spotify") != std::wstring::npos) return L"SP";
    if (lower.find(L"soundcloud") != std::wstring::npos) return L"SC";
    return L"WEB";
}

struct IconCacheEntry {
    DWORD pid = 0;
    std::wstring exePath;
    PixelsRef pixels;
    uint64_t lastUsed = 0;
};

std::mutex g_iconCacheMutex;
std::unordered_map<DWORD, IconCacheEntry> g_iconCacheByPid;
uint64_t g_iconCacheClock = 0;

PixelsRef GetCachedProcessIconPixels(DWORD pid, UINT size) {
    std::wstring exePath;
    ProcessImageNameForPid(pid, &exePath);

    {
        std::lock_guard lock(g_iconCacheMutex);
        auto it = g_iconCacheByPid.find(pid);
        if (it != g_iconCacheByPid.end() && it->second.exePath == exePath &&
            it->second.pixels->width == size) {
            it->second.lastUsed = ++g_iconCacheClock;
            return it->second.pixels;
        }
    }

    PixelsRef pixels;
    HICON icon = getProcessIcon(pid);
    if (icon) {
        pixels = IconToPixelsRef(icon, size);
        DestroyIcon(icon);
    }

    if (pixels) {
        std::lock_guard lock(g_iconCacheMutex);
        g_iconCacheByPid[pid] = IconCacheEntry{pid, exePath, pixels, ++g_iconCacheClock};
        if (g_iconCacheByPid.size() > 32) {
            auto oldest = g_iconCacheByPid.begin();
            for (auto it = g_iconCacheByPid.begin(); it != g_iconCacheByPid.end(); ++it) {
                if (it->second.lastUsed < oldest->second.lastUsed) {
                    oldest = it;
                }
            }
            g_iconCacheByPid.erase(oldest);
        }
    }

    return pixels;
}

PixelsRef GetWindowIconPixels(HWND hwnd, UINT size) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    PixelsRef pixels = GetCachedProcessIconPixels(pid, size);
    if (pixels) {
        return pixels;
    }

    HICON icon = getWindowIcon(hwnd);
    if (icon) {
        pixels = IconToPixelsRef(icon, size);
        DestroyIcon(icon);
    }
    return pixels;
}

bool IsIgnorableForegroundWindow(HWND hwnd, const std::wstring& title) {
    wchar_t className[128] = {};
    GetClassNameW(hwnd, className, ARRAYSIZE(className));
    const std::wstring cls = ToLowerCopy(className);
    const std::wstring lowerTitle = ToLowerCopy(title);

    std::wstring image;
    ProcessImageNameForWindow(hwnd, &image);
    const std::wstring base = ToLowerCopy(BaseNameFromPath(image));

    return title.empty() ||
           hwnd == g_hwnd ||
           cls == L"shell_traywnd" ||
           cls == L"workerw" ||
           cls == L"progman" ||
           base == L"explorer.exe";
}

BOOL CALLBACK FindMediaSourceWindowProc(HWND hwnd, LPARAM lParam) {
    auto* data = reinterpret_cast<std::pair<const std::wstring*, HWND*>*>(lParam);
    if (WindowLooksLikeMediaSource(hwnd, *data->first)) {
        *data->second = hwnd;
        return FALSE;
    }
    return TRUE;
}

std::wstring FindBrowserMediaSiteBadge(const std::wstring& sourceAppUserModelId) {
    const std::wstring sourceLower = ToLowerCopy(sourceAppUserModelId);
    HWND found = nullptr;
    std::pair<const std::wstring*, HWND*> data{&sourceLower, &found};
    EnumWindows(FindMediaSourceWindowProc, reinterpret_cast<LPARAM>(&data));
    if (found) {
        wchar_t title[192] = {};
        GetWindowTextW(found, title, ARRAYSIZE(title));
        return SiteBadgeFromTitle(title);
    }
    return L"WEB";
}

PixelsRef FindMediaSourceIcon(const std::wstring& sourceAppUserModelId) {
    PixelsRef pixels;
    if (IsBrowserMediaSource(sourceAppUserModelId)) {
        // Continue searching for the browser window anyway to retrieve
        // the browser's app icon or the PWA's app icon.
    }

    const std::wstring sourceLower = ToLowerCopy(sourceAppUserModelId);
    HWND found = nullptr;
    std::pair<const std::wstring*, HWND*> data{&sourceLower, &found};
    EnumWindows(FindMediaSourceWindowProc, reinterpret_cast<LPARAM>(&data));
    if (found) {
        pixels = GetWindowIconPixels(found, 32);
    }
    return pixels;
}

struct FindAppIconData {
    const std::wstring* targetName;
    HWND bestHwnd = nullptr;
    std::unordered_map<DWORD, std::wstring> pidToProcessName;
};

BOOL CALLBACK FindAppIconWindowProc(HWND hwnd, LPARAM lParam) {
    auto* d = reinterpret_cast<FindAppIconData*>(lParam);
    if (!IsWindowVisible(hwnd)) {
        return TRUE;
    }

    if (hwnd == g_hwnd) {
        return TRUE;
    }

    // 1. Fast path: Check window title first (lightweight check without opening process handles)
    wchar_t title[128] = {};
    GetWindowTextW(hwnd, title, ARRAYSIZE(title));
    std::wstring titleLower = ToLowerCopy(title);
    if (!titleLower.empty() && titleLower.find(*d->targetName) != std::wstring::npos) {
        d->bestHwnd = hwnd;
        return FALSE; // Found via title, stop enumeration
    }

    // 2. Slow path fallback: Check process executable name
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (!pid) {
        return TRUE;
    }

    std::wstring baseNoExe;
    auto it = d->pidToProcessName.find(pid);
    if (it != d->pidToProcessName.end()) {
        baseNoExe = it->second;
    } else {
        std::wstring image;
        if (ProcessImageNameForPid(pid, &image)) {
            std::wstring base = ToLowerCopy(BaseNameFromPath(image));
            baseNoExe = base;
            if (baseNoExe.size() > 4 && baseNoExe.substr(baseNoExe.size() - 4) == L".exe") {
                baseNoExe = baseNoExe.substr(0, baseNoExe.size() - 4);
            }
            d->pidToProcessName[pid] = baseNoExe;
        } else {
            d->pidToProcessName[pid] = L"";
        }
    }

    if (!baseNoExe.empty()) {
        if (baseNoExe.find(*d->targetName) != std::wstring::npos ||
            d->targetName->find(baseNoExe) != std::wstring::npos) {
            d->bestHwnd = hwnd;
            return FALSE; // Found via process name, stop enumeration
        }
    }

    return TRUE;
}

// Bunbun: crops an icon to its visible pixels plus a small margin, square, so a Store
// app's logo (drawn small inside a big transparent tile) fills the notification's
// tile like a desktop app's icon does. False when nothing is visible.
bool TrimTransparentMargin(BitmapPixels* px) {
    if (!px || px->width == 0 || px->height == 0 || px->bgra.size() < size_t(px->width) * px->height * 4) return false;
    UINT left = px->width, top = px->height, right = 0, bottom = 0;
    for (UINT y = 0; y < px->height; ++y)
        for (UINT x = 0; x < px->width; ++x)
            if (px->bgra[(size_t(y) * px->width + x) * 4 + 3] > 16) {
                left = std::min(left, x);
                right = std::max(right, x);
                top = std::min(top, y);
                bottom = std::max(bottom, y);
            }
    if (right < left || bottom < top) return false;
    const UINT side = std::max(right - left, bottom - top) + 1;
    const UINT pad = side / 10;
    const UINT out = side + pad * 2;
    if (out >= std::min(px->width, px->height)) return true;  // already fills its tile
    const int cx = static_cast<int>(left + right) / 2, cy = static_cast<int>(top + bottom) / 2;
    const int x0 = cx - static_cast<int>(out) / 2, y0 = cy - static_cast<int>(out) / 2;
    std::vector<uint8_t> cropped(size_t(out) * out * 4, 0);
    for (UINT y = 0; y < out; ++y)
        for (UINT x = 0; x < out; ++x) {
            const int sx = x0 + static_cast<int>(x), sy = y0 + static_cast<int>(y);
            if (sx < 0 || sy < 0 || sx >= static_cast<int>(px->width) || sy >= static_cast<int>(px->height)) continue;
            memcpy(&cropped[(size_t(y) * out + x) * 4], &px->bgra[(size_t(sy) * px->width + sx) * 4], 4);
        }
    px->bgra = std::move(cropped);
    px->width = px->height = out;
    return true;
}

PixelsRef FindAppIconByName(const std::wstring& appName, UINT size) {
    PixelsRef pixels;
    if (appName.empty()) {
        return pixels;
    }

    const std::wstring appNameLower = ToLowerCopy(appName);
    FindAppIconData data;
    data.targetName = &appNameLower;

    EnumWindows(FindAppIconWindowProc, reinterpret_cast<LPARAM>(&data));

    if (data.bestHwnd) {
        pixels = GetWindowIconPixels(data.bestHwnd, size);
    }
    return pixels;
}

std::vector<uint8_t> ReadWinRtStreamBytes(
    const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& reference) {
    std::vector<uint8_t> bytes;
    if (!reference) {
        return bytes;
    }

    auto stream = reference.OpenReadAsync().get();
    if (!stream) {
        return bytes;
    }

    const uint64_t size64 = stream.Size();
    if (size64 == 0 || size64 > 8 * 1024 * 1024) {
        return bytes;
    }

    const uint32_t size = static_cast<uint32_t>(size64);
    winrt::Windows::Storage::Streams::DataReader reader(stream.GetInputStreamAt(0));
    reader.LoadAsync(size).get();
    bytes.resize(size);
    reader.ReadBytes(winrt::array_view<uint8_t>(bytes.data(), bytes.data() + bytes.size()));
    return bytes;
}

void TriggerNudge() {
    // If we're parked (auto-hidden), any real event must wake us regardless
    // of the normal throttle, or it'd never be seen until the next hook/
    // hotkey/fullscreen-recheck wake.
    const bool wasParked = g_autoHiddenParked.exchange(false, std::memory_order_relaxed);
    const double now = NowSeconds();
    const double previous = g_lastNudgeTime.load();
    if (!wasParked && now - previous < 0.45) {
        return;
    }
    g_lastNudgeTime = now;
    HWND hwnd = g_hwnd;
    if (hwnd) {
        PostMessageW(hwnd, WM_APP_NEW_EVENT, 0, 0);
    }
}

// Chooses which SMTC session the island should follow.
//
// GetCurrentSession() returns whatever Windows last treated as the foreground
// media app, which is often a stale or paused session belonging to a different
// player. Following it blindly meant a player that was genuinely playing could
// be ignored entirely -- the reason VLC frequently produced no reaction at all
// while Spotify worked. Prefer the current session when it is actually playing,
// otherwise the first session that is, and only then fall back to the current
// one so paused/idle state still shows.
winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession SelectActiveMediaSession(
    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager const& manager,
    const std::wstring& preferredAumid) {
    using PlaybackStatus =
        winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionPlaybackStatus;

    auto sessionIsPlaying = [](auto const& candidate) -> bool {
        if (!candidate) {
            return false;
        }
        try {
            return candidate.GetPlaybackInfo().PlaybackStatus() == PlaybackStatus::Playing;
        } catch (...) {
            return false;
        }
    };

    winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession current = nullptr;
    try {
        current = manager.GetCurrentSession();
    } catch (...) {
    }

    // The user picked a source (dock / menu): follow it, playing or not.
    if (!preferredAumid.empty()) {
        try {
            for (auto const& candidate : manager.GetSessions()) {
                if (preferredAumid == candidate.SourceAppUserModelId().c_str()) {
                    return candidate;
                }
            }
        } catch (...) {
        }
    }

    if (sessionIsPlaying(current)) {
        return current;
    }

    try {
        for (auto const& candidate : manager.GetSessions()) {
            if (sessionIsPlaying(candidate)) {
                return candidate;
            }
        }
    } catch (...) {
    }

    return current;
}

// Exe name for path-style AUMIDs (classic desktop apps), friendly name otherwise.
// Bunbun: is this session a video rather than music? Players that say so are
// believed (Netflix, VLC, Media Player report Video or Music). Browsers report
// everything alike, so there a track with an album is music (YouTube Music,
// Spotify on the web) and one without is a video (YouTube, streaming sites).
// Music apps always count as music.
bool IsVideoMedia(const std::wstring& aumid, int playbackType, const std::wstring& albumTitle,
                  const std::wstring& musicApps, const std::wstring& videoApps) {
    std::wstring id = aumid;
    for (wchar_t& c : id) c = static_cast<wchar_t>(towlower(c));
    // Your own lists first (Bunbun.MusicApps / VideoApps), for when the guess below is wrong.
    if (bb::applist::Matches(videoApps, id)) return true;
    if (bb::applist::Matches(musicApps, id)) return false;
    static const wchar_t* kMusicApps[] = {L"spotify", L"applemusic", L"apple music", L"itunes", L"youtube music", L"youtube-music",
                                          L"th-ch", L"tidal", L"deezer", L"amazon music", L"amazonmusic", L"foobar", L"musicbee",
                                          L"aimp", L"winamp", L"soundcloud", L"pandora", L"qobuz", L"audacious"};
    for (const wchar_t* m : kMusicApps)
        if (id.find(m) != std::wstring::npos) return false;
    static const wchar_t* kBrowsers[] = {L"chrome", L"msedge", L"firefox", L"brave", L"opera", L"vivaldi", L"arc", L"zen", L"librewolf"};
    for (const wchar_t* b : kBrowsers)
        if (id.find(b) != std::wstring::npos) return albumTitle.empty();
    if (playbackType == 2) return true;   // MediaPlaybackType::Video
    if (playbackType == 1) return false;  // MediaPlaybackType::Music
    static const wchar_t* kVideoApps[] = {L"vlc", L"mpv", L"potplayer", L"mpc-hc", L"netflix", L"primevideo", L"disney", L"hulu",
                                          L"plex", L"kodi", L"stremio", L"crunchyroll", L"hotstar", L"jiocinema", L"zune"};
    for (const wchar_t* v : kVideoApps)
        if (id.find(v) != std::wstring::npos) return true;
    return false;
}

std::wstring MediaSourceDisplayName(const std::wstring& aumid) {
    std::wstring name = FriendlyMediaSourceName(aumid);
    if (name.find(L'\\') != std::wstring::npos || name.find(L'/') != std::wstring::npos ||
        name.find(L':') != std::wstring::npos) {
        name = StripExtension(BaseNameFromPath(aumid));
        if (!name.empty()) {
            name[0] = static_cast<wchar_t>(towupper(name[0]));
        }
    }
    return name.empty() ? std::wstring(L"Media") : name;
}

