// Bunbun — Sound check: the Mirror page's microphone and speaker test, for a quick
// look before a call.
//
// #included inside island_core.cpp's anonymous namespace, after the Mirror module.
// Like the camera, the microphone only opens when you press "Test mic" and closes
// about 3 s after the page stops being drawn, so scrolling past never lights the
// microphone indicator. The meter is WASAPI's peak value on the chosen capture
// device while a shared-mode stream is open; nothing is recorded or saved. The
// speaker test plays a short two-note tone through the chosen output. The device
// lists are re-read every few seconds while the page is drawn. Choosing a device
// here only picks what is tested; Windows' own default is changed in Sound settings.

namespace SoundCheck {

// audioclient / endpointvolume.h in MinGW only forward-declare IAudioMeterInformation,
// so its (stable) layout is declared here.
struct IBbAudioMeter : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float* peak) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT* count) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32 count, float* peaks) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD* mask) = 0;
};
constexpr IID kIID_AudioMeter = {0xC02216F6, 0x8C67, 0x4B5B, {0x9D, 0x00, 0xD0, 0x08, 0xE7, 0x3E, 0x00, 0x64}};

struct Device {
    std::wstring id, name;
};

std::mutex g_mutex;
std::vector<Device> g_mics, g_speakers;          // active devices, guarded by g_mutex
std::wstring g_defaultMic, g_defaultSpeaker;     // the system defaults' ids
std::wstring g_micSel, g_spkSel;                 // chosen device ids; empty = the system default
std::wstring g_error;
std::atomic<float> g_level{0.0f};                // smoothed mic peak, 0..1
std::atomic<bool> g_running{false};              // the mic worker is alive
std::atomic<bool> g_stop{false};
std::atomic<bool> g_refreshing{false};           // a device read is in flight
std::atomic<bool> g_open{false};                 // the panel is expanded while the camera is on
std::atomic<double> g_lastWanted{0.0};           // NowSeconds() the panel last drew with the mic on
std::atomic<double> g_namesAt{-100.0};
std::atomic<double> g_toneUntil{0.0};            // the speaker tone is playing until then

std::wstring NameOf(IMMDevice* device) {
    ComPtr<IPropertyStore> props;
    if (FAILED(device->OpenPropertyStore(STGM_READ, &props))) return {};
    PROPVARIANT v;
    PropVariantInit(&v);
    std::wstring name;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) name = v.pwszVal;
    PropVariantClear(&v);
    return name;
}

void ReadDevices(IMMDeviceEnumerator* enumerator, EDataFlow flow, std::vector<Device>* out, std::wstring* defaultId) {
    ComPtr<IMMDevice> def;
    if (SUCCEEDED(enumerator->GetDefaultAudioEndpoint(flow, eConsole, &def))) {
        LPWSTR id = nullptr;
        if (SUCCEEDED(def->GetId(&id))) {
            *defaultId = id;
            CoTaskMemFree(id);
        }
    }
    ComPtr<IMMDeviceCollection> list;
    UINT count = 0;
    if (FAILED(enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, &list)) || FAILED(list->GetCount(&count))) return;
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> device;
        LPWSTR id = nullptr;
        if (FAILED(list->Item(i, &device)) || FAILED(device->GetId(&id))) continue;
        out->push_back({id, NameOf(device.Get())});
        CoTaskMemFree(id);
    }
}

void RefreshDevices() {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        std::vector<Device> mics, speakers;
        std::wstring defMic, defSpeaker;
        ReadDevices(enumerator.Get(), eCapture, &mics, &defMic);
        ReadDevices(enumerator.Get(), eRender, &speakers, &defSpeaker);
        std::lock_guard lock(g_mutex);
        g_mics = std::move(mics);
        g_speakers = std::move(speakers);
        g_defaultMic = defMic;
        g_defaultSpeaker = defSpeaker;
    }
    if (com) CoUninitialize();
    g_refreshing = false;
    g_layoutDirty = true;
}

// Called every frame the panel is drawn; re-reads the devices at most every 5 s.
void Poke() {
    const double now = NowSeconds();
    if (now - g_namesAt.load() < 5.0 || g_refreshing.exchange(true)) return;
    g_namesAt = now;
    std::thread(RefreshDevices).detach();
}

void MicWorker() {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    std::wstring chosen;
    {
        std::lock_guard lock(g_mutex);
        chosen = g_micSel;
    }
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioCaptureClient> capture;
    ComPtr<IBbAudioMeter> meter;
    WAVEFORMATEX* format = nullptr;
    std::wstring err;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
        err = L"No microphone found.";
    } else if (FAILED(chosen.empty() ? enumerator->GetDefaultAudioEndpoint(eCapture, eConsole, &device)
                                     : enumerator->GetDevice(chosen.c_str(), &device))) {
        err = chosen.empty() ? L"No microphone found." : L"That microphone isn't available.";
    } else if (FAILED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()))) ||
               FAILED(client->GetMixFormat(&format)) ||
               FAILED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, format, nullptr)) ||
               FAILED(client->GetService(IID_PPV_ARGS(&capture))) ||
               FAILED(device->Activate(kIID_AudioMeter, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(meter.GetAddressOf()))) ||
               FAILED(client->Start())) {
        err = L"The microphone is busy, or blocked in Windows' privacy settings.";
    }
    if (format) CoTaskMemFree(format);
    {
        std::lock_guard lock(g_mutex);
        g_error = err;
    }
    float level = 0.0f, loudest = 0.0f;
    // 3 s of grace: an alert covering the page for a moment shouldn't stop the test.
    while (err.empty() && !g_stop.load() && NowSeconds() - g_lastWanted.load() < 3.0) {
        // Nobody reads the audio, but the buffer is drained so the stream stays healthy.
        UINT32 packet = 0;
        while (SUCCEEDED(capture->GetNextPacketSize(&packet)) && packet) {
            BYTE* data = nullptr;
            UINT32 frames = 0;
            DWORD flags = 0;
            if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) break;
            capture->ReleaseBuffer(frames);
        }
        float peak = 0.0f;
        meter->GetPeakValue(&peak);
        level = std::max(peak, level * 0.8f);  // quick rise, slower fall
        loudest = std::max(loudest, peak);
        g_level = level;
        g_layoutDirty = true;
        Sleep(40);
    }
    if (client) client->Stop();
    Wh_Log(L"SoundCheck: mic test ended, loudest peak %.3f%s", loudest, err.empty() ? L"" : (L", " + err).c_str());
    g_level = 0.0f;
    g_running = false;
    g_layoutDirty = true;
    if (com) CoUninitialize();
}

void StartMic() {
    g_lastWanted.store(NowSeconds());
    if (g_running.exchange(true)) return;
    g_stop = false;
    std::thread(MicWorker).detach();
}

void StopMic() { g_stop = true; }

// Two soft notes through one output device, on its own thread. Shared-mode WASAPI, so
// it plays at the device's mix format (float or 16-bit PCM; anything else is skipped).
void ToneWorker(std::wstring chosen) {
    const bool com = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
    ComPtr<IMMDeviceEnumerator> enumerator;
    ComPtr<IMMDevice> device;
    ComPtr<IAudioClient> client;
    ComPtr<IAudioRenderClient> render;
    WAVEFORMATEX* format = nullptr;
    bool ok = SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator))) &&
              SUCCEEDED(chosen.empty() ? enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device)
                                       : enumerator->GetDevice(chosen.c_str(), &device)) &&
              SUCCEEDED(device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(client.GetAddressOf()))) &&
              SUCCEEDED(client->GetMixFormat(&format)) &&
              SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 10000000, 0, format, nullptr)) &&
              SUCCEEDED(client->GetService(IID_PPV_ARGS(&render)));
    if (ok) {
        const WORD channels = format->nChannels;
        const DWORD rate = format->nSamplesPerSec;
        bool isFloat = format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
        if (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE && format->cbSize >= 22) {
            static const GUID kFloat = {0x00000003, 0x0000, 0x0010, {0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71}};
            isFloat = IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat, kFloat) != FALSE;
        }
        const bool isPcm16 = !isFloat && format->wBitsPerSample == 16;
        UINT32 buffer = 0;
        client->GetBufferSize(&buffer);
        const UINT32 frames = std::min<UINT32>(buffer, static_cast<UINT32>(rate * 0.8));
        BYTE* data = nullptr;
        if ((isFloat || isPcm16) && channels > 0 && SUCCEEDED(render->GetBuffer(frames, &data))) {
            constexpr double kNote = 0.4, kPi = 3.14159265358979;
            double phase = 0.0;
            for (UINT32 i = 0; i < frames; ++i) {
                const double t = static_cast<double>(i) / rate;
                const double local = std::fmod(t, kNote);
                const double hz = t < kNote ? 659.25 : 880.0;
                const double env = std::min({1.0, local / 0.02, (kNote - local) / 0.06});
                phase += 2.0 * kPi * hz / rate;
                const float sample = static_cast<float>(0.28 * env * std::sin(phase));
                for (WORD c = 0; c < channels; ++c) {
                    if (isFloat) reinterpret_cast<float*>(data)[i * channels + c] = sample;
                    else reinterpret_cast<int16_t*>(data)[i * channels + c] = static_cast<int16_t>(sample * 32767.0f);
                }
            }
            render->ReleaseBuffer(frames, 0);
            if (SUCCEEDED(client->Start())) {
                Sleep(static_cast<DWORD>(frames * 1000.0 / rate) + 150);
                client->Stop();
            }
        }
    }
    if (format) CoTaskMemFree(format);
    if (com) CoUninitialize();
    g_toneUntil = 0.0;
    g_layoutDirty = true;
}

void TestSpeaker() {
    if (NowSeconds() < g_toneUntil.load()) return;  // one tone at a time
    std::wstring chosen;
    {
        std::lock_guard lock(g_mutex);
        chosen = g_spkSel;
    }
    g_toneUntil = NowSeconds() + 0.95;
    std::thread(ToneWorker, chosen).detach();
}

// dir: +1 next, -1 previous, through the active devices. Landing on Windows' default
// clears the choice again, so the test keeps following the default.
void CycleDevice(bool mic, int dir) {
    {
        std::lock_guard lock(g_mutex);
        const std::vector<Device>& list = mic ? g_mics : g_speakers;
        const std::wstring& defId = mic ? g_defaultMic : g_defaultSpeaker;
        std::wstring& sel = mic ? g_micSel : g_spkSel;
        if (list.empty()) return;
        const std::wstring& current = sel.empty() ? defId : sel;
        int pos = 0;
        for (size_t i = 0; i < list.size(); ++i)
            if (list[i].id == current) pos = static_cast<int>(i);
        const int count = static_cast<int>(list.size());
        pos = (pos + dir + count) % count;
        const std::wstring& next = list[static_cast<size_t>(pos)].id;
        sel = next == defId ? std::wstring() : next;
    }
    if (mic && g_running.load()) {  // reopen on the new device
        g_stop = true;
        std::thread([] {
            for (int i = 0; i < 50 && g_running.load(); ++i) Sleep(20);
            StartMic();
        }).detach();
    }
    g_layoutDirty = true;
}

void OpenSoundSettings() { ShellExecuteW(nullptr, L"open", L"ms-settings:sound", nullptr, nullptr, SW_SHOWNORMAL); }

}  // namespace SoundCheck
