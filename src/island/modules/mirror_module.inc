// Bunbun — Mirror: a camera preview page, for a quick look before a call.
//
// #included inside island_core.cpp's anonymous namespace. The camera only starts
// when you press "Turn on camera" on the page, and stops a second after the page
// is no longer drawn (you left it, or the island folded), so scrolling past the
// page never turns the camera light on. Frames come from Media Foundation's source
// reader as RGB32 on a worker thread; the renderer copies the latest one into a
// bitmap. Nothing is saved or sent anywhere.

namespace Mirror {

std::mutex g_mutex;
std::vector<uint8_t> g_frame;   // latest frame, BGRX, guarded by g_mutex
UINT g_width = 0, g_height = 0;
uint64_t g_frameId = 0;
std::atomic<bool> g_running{false};
std::atomic<bool> g_stop{false};
std::atomic<double> g_lastWanted{0.0};  // NowSeconds() the page last drew while on
std::wstring g_error;                   // guarded by g_mutex: why it couldn't start

// Media Foundation is loaded at run time: Windows "N" editions ship without it,
// and a hard link would stop the whole island from starting there.
struct Mf {
    HRESULT(WINAPI* startup)(ULONG, DWORD) = nullptr;
    HRESULT(WINAPI* shutdown)() = nullptr;
    HRESULT(WINAPI* createAttributes)(IMFAttributes**, UINT32) = nullptr;
    HRESULT(WINAPI* createMediaType)(IMFMediaType**) = nullptr;
    HRESULT(WINAPI* enumDeviceSources)(IMFAttributes*, IMFActivate***, UINT32*) = nullptr;
    HRESULT(WINAPI* createReader)(IMFMediaSource*, IMFAttributes*, IMFSourceReader**) = nullptr;
    bool ok = false;
};

const Mf& LoadMf() {
    static Mf mf = [] {
        Mf m;
        HMODULE plat = LoadLibraryExW(L"mfplat.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE core = LoadLibraryExW(L"mf.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        HMODULE rw = LoadLibraryExW(L"mfreadwrite.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!plat || !core || !rw) return m;
        m.startup = reinterpret_cast<decltype(m.startup)>(GetProcAddress(plat, "MFStartup"));
        m.shutdown = reinterpret_cast<decltype(m.shutdown)>(GetProcAddress(plat, "MFShutdown"));
        m.createAttributes = reinterpret_cast<decltype(m.createAttributes)>(GetProcAddress(plat, "MFCreateAttributes"));
        m.createMediaType = reinterpret_cast<decltype(m.createMediaType)>(GetProcAddress(plat, "MFCreateMediaType"));
        m.enumDeviceSources = reinterpret_cast<decltype(m.enumDeviceSources)>(GetProcAddress(core, "MFEnumDeviceSources"));
        m.createReader = reinterpret_cast<decltype(m.createReader)>(GetProcAddress(rw, "MFCreateSourceReaderFromMediaSource"));
        m.ok = m.startup && m.shutdown && m.createAttributes && m.createMediaType && m.enumDeviceSources && m.createReader;
        return m;
    }();
    return mf;
}

void Worker() {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    const bool comOk = SUCCEEDED(hr);
    const Mf& mf = LoadMf();
    hr = mf.ok ? mf.startup(MF_VERSION, MFSTARTUP_LITE) : E_NOTIMPL;
    ComPtr<IMFSourceReader> reader;
    std::wstring err;
    if (SUCCEEDED(hr)) {
        ComPtr<IMFAttributes> attrs;
        mf.createAttributes(&attrs, 1);
        attrs->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE, MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID);
        IMFActivate** devices = nullptr;
        UINT32 count = 0;
        if (SUCCEEDED(mf.enumDeviceSources(attrs.Get(), &devices, &count)) && count > 0) {
            ComPtr<IMFMediaSource> source;
            if (SUCCEEDED(devices[0]->ActivateObject(IID_PPV_ARGS(&source)))) {
                ComPtr<IMFAttributes> readerAttrs;
                mf.createAttributes(&readerAttrs, 1);
                // Let MF convert whatever the camera gives (NV12, MJPG...) to RGB32.
                readerAttrs->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
                if (SUCCEEDED(mf.createReader(source.Get(), readerAttrs.Get(), &reader))) {
                    ComPtr<IMFMediaType> type;
                    mf.createMediaType(&type);
                    type->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
                    type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
                    if (FAILED(reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type.Get())))
                        reader.Reset();
                }
            }
            for (UINT32 i = 0; i < count; ++i) devices[i]->Release();
            CoTaskMemFree(devices);
            if (!reader) err = L"The camera is busy or can't be opened.";
        } else {
            if (devices) CoTaskMemFree(devices);
            err = L"No camera found.";
        }
    } else {
        err = L"Windows' camera support isn't available.";
    }

    UINT32 w = 0, h = 0;
    if (reader) {
        ComPtr<IMFMediaType> current;
        if (SUCCEEDED(reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &current)))
            MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE, &w, &h);
    }
    {
        std::lock_guard lock(g_mutex);
        g_error = err;
    }

    // 3 s of grace: an alert covering the page for a moment shouldn't stop the camera.
    while (reader && !g_stop.load() && NowSeconds() - g_lastWanted.load() < 3.0) {
        DWORD stream = 0, flags = 0;
        LONGLONG ts = 0;
        ComPtr<IMFSample> sample;
        if (FAILED(reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &stream, &flags, &ts, &sample))) break;
        if (flags & MF_SOURCE_READERF_ENDOFSTREAM) break;
        if (!sample) continue;
        ComPtr<IMFMediaBuffer> buffer;
        if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) continue;
        BYTE* data = nullptr;
        DWORD len = 0;
        if (SUCCEEDED(buffer->Lock(&data, nullptr, &len))) {
            if (w && h && len >= w * h * 4) {
                std::lock_guard lock(g_mutex);
                g_frame.assign(data, data + w * h * 4);
                g_width = w;
                g_height = h;
                ++g_frameId;
            }
            buffer->Unlock();
        }
        g_layoutDirty = true;
    }
    reader.Reset();
    if (mf.ok && SUCCEEDED(hr)) mf.shutdown();
    if (comOk) CoUninitialize();
    {
        std::lock_guard lock(g_mutex);
        g_frame.clear();
        g_width = g_height = 0;
    }
    g_running = false;
    g_layoutDirty = true;
}

void Start() {
    g_lastWanted.store(NowSeconds());
    if (g_running.exchange(true)) return;
    g_stop = false;
    std::thread(Worker).detach();
}

// Called every frame the page is drawn while the camera is on.
void KeepAlive() { g_lastWanted.store(NowSeconds()); }

void Stop() { g_stop = true; }

}  // namespace Mirror
