// Bunbun: Audio thread, battery, GPU and network snapshots.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

DWORD WINAPI AudioThreadProc(void*) {
    HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);

    while (WaitForSingleObject(g_stopEvent, 0) == WAIT_TIMEOUT) {
        // Don't touch the audio engine at all unless a playing waveform is
        // actually going to be shown right now. Cheap poll, no capture open.
        if (!g_audioCaptureNeeded.load(std::memory_order_relaxed)) {
            WaitForSingleObject(g_stopEvent, 250);
            continue;
        }

        ComPtr<IMMDeviceEnumerator> enumerator;
        ComPtr<IMMDevice> device;
        ComPtr<IAudioClient> client;
        ComPtr<IAudioCaptureClient> capture;
        WAVEFORMATEX* mixFormat = nullptr;

        HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr,
                                      CLSCTX_ALL, IID_PPV_ARGS(&enumerator));
        if (SUCCEEDED(hr)) {
            hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &device);
        }
        if (SUCCEEDED(hr)) {
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void**>(client.GetAddressOf()));
        }
        if (SUCCEEDED(hr)) {
            hr = client->GetMixFormat(&mixFormat);
        }
        if (SUCCEEDED(hr)) {
            REFERENCE_TIME bufferDuration = 10000000 / 5;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    AUDCLNT_STREAMFLAGS_LOOPBACK,
                                    bufferDuration, 0, mixFormat, nullptr);
        }
        if (SUCCEEDED(hr)) {
            hr = client->GetService(IID_PPV_ARGS(&capture));
        }
        if (SUCCEEDED(hr)) {
            hr = client->Start();
        }

        if (FAILED(hr)) {
            // Bunbun: say why once, so flat music bars can be explained from the log.
            static HRESULT s_lastLogged = S_OK;
            if (hr != s_lastLogged) {
                s_lastLogged = hr;
                Wh_Log(L"Audio loopback capture failed (0x%08X); music bars stay flat.", static_cast<unsigned>(hr));
            }
            if (mixFormat) {
                CoTaskMemFree(mixFormat);
            }
            WaitForSingleObject(g_stopEvent, 2500);
            continue;
        }

        // Watchdog state — reset every time a capture session (re)starts so we
        // don't flag a stall before the first packet has even arrived.
        double lastPacketTime = NowSeconds();
        double nextWatchdogCheck = lastPacketTime + 1.0;
        // Bunbun: what the capture actually hears, logged every 5 s (BUNBUN_LOG=1),
        // so "the music bars are flat" can be told apart from "no sound reaches us".
        float peakSinceLog = 0.0f;
        int silentPackets = 0, loudPackets = 0;
        double nextLevelLog = lastPacketTime + 5.0;

        while (WaitForSingleObject(g_stopEvent, 16) == WAIT_TIMEOUT) {
            if (!g_audioCaptureNeeded.load(std::memory_order_relaxed)) {
                break;
            }
            UINT32 packetFrames = 0;
            if (FAILED(capture->GetNextPacketSize(&packetFrames))) {
                break;
            }

            float amplitude = 0.0f;
            int packets = 0;
            while (packetFrames > 0) {
                BYTE* data = nullptr;
                UINT32 frames = 0;
                DWORD flags = 0;
                if (FAILED(capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr))) {
                    break;
                }

                lastPacketTime = NowSeconds();

                if (flags & AUDCLNT_BUFFERFLAGS_SILENT) ++silentPackets;
                else ++loudPackets;
                if (!(flags & AUDCLNT_BUFFERFLAGS_SILENT)) {
                    amplitude = std::max(amplitude, SampleAudioAmplitude(data, frames, mixFormat));
                    peakSinceLog = std::max(peakSinceLog, amplitude);
                    PushAudioChunks(data, frames, mixFormat);
                } else {
                    PushWaveformSample(0.0f);
                    DecayAndPublishSpectrum();
                }
                capture->ReleaseBuffer(frames);
                ++packets;

                if (FAILED(capture->GetNextPacketSize(&packetFrames))) {
                    packetFrames = 0;
                }
            }

            if (packets > 0) {
                if (amplitude <= 0.001f) {
                    PushWaveformSample(0.0f);
                    DecayAndPublishSpectrum();
                }
            } else {
                PushWaveformSample(0.0f);
                DecayAndPublishSpectrum();
            }

            // --- Stall watchdog -------------------------------------------
            // Checked at most once a second (cheap: one time comparison plus
            // a mutex-protected bool read, same cost as work already done
            // elsewhere in this loop). Only forces a reconnect if SMTC says
            // media is playing but we've had zero packets for 5+ seconds —
            // that combination almost never happens unless the loopback
            // engine has stalled, so it won't fire during normal silence.
            const double now = NowSeconds();
            if (now >= nextLevelLog) {
                nextLevelLog = now + 5.0;
                Wh_Log(L"Audio loopback: peak %.3f, %d sound / %d silent packets in 5 s.", peakSinceLog, loudPackets, silentPackets);
                peakSinceLog = 0.0f;
                silentPackets = loudPackets = 0;
            }
            if (now >= nextWatchdogCheck) {
                nextWatchdogCheck = now + 1.0;
                bool mediaPlayingNow = false;
                {
                    std::lock_guard lock(g_stateMutex);
                    mediaPlayingNow = g_state.media.playing;
                }
                if (mediaPlayingNow && (now - lastPacketTime) > 5.0) {
                    Wh_Log(L"Audio loopback capture appears stalled while media is playing; reconnecting...");
                    break;
                }
            }
        }

        client->Stop();
        if (mixFormat) {
            CoTaskMemFree(mixFormat);
        }
    }

    if (SUCCEEDED(hrCo)) {
        CoUninitialize();
    }

    return 0;
}
// Bunbun: when the mascot last cheered for a full battery (NowSeconds clock).
std::atomic<double> g_batteryCheerAt{-100.0};

// Bunbun: minutes until the battery is full, from Windows' battery report; -1 if
// the firmware doesn't say.
int BatteryMinutesToFull() {
#if DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER
    try {
        const auto report = winrt::Windows::Devices::Power::Battery::AggregateBattery().GetReport();
        const auto rate = report.ChargeRateInMilliwatts();
        const auto remaining = report.RemainingCapacityInMilliwattHours();
        const auto full = report.FullChargeCapacityInMilliwattHours();
        if (rate && remaining && full && rate.Value() > 0 && full.Value() > remaining.Value()) {
            const int minutes = static_cast<int>((full.Value() - remaining.Value()) * 60LL / rate.Value());
            return minutes > 0 && minutes < 24 * 60 ? minutes : -1;
        }
    } catch (...) {
    }
#endif
    return -1;
}

void UpdateBatterySnapshot() {
    SYSTEM_POWER_STATUS status = {};
    if (!GetSystemPowerStatus(&status)) {
        return;
    }

    bool newCharging = (status.ACLineStatus == 1);
    int newPercent = status.BatteryLifePercent == 255 ? 100 : status.BatteryLifePercent;

    bool triggerAlert = false;

    {
        std::lock_guard lock(g_stateMutex);
        static bool s_batteryInit = false;
        if (!s_batteryInit) {
            g_state.battery.charging = newCharging;
            g_state.battery.percent = newPercent;
            s_batteryInit = true;
        }

        if (g_state.battery.charging != newCharging) {
            triggerAlert = true;
        }

        if (!newCharging && newPercent < g_state.battery.percent && (newPercent == 20 || newPercent == 10)) {
            triggerAlert = true;
        }

        g_state.battery.charging = newCharging;
        g_state.battery.percent = newPercent;
        g_state.battery.secondsRemaining = status.BatteryLifeTime;
        g_state.battery.low = (!newCharging && newPercent <= 20);

        // Bunbun: laptops get a "still charging" pill every few minutes while the
        // battery takes charge, and one when it fills up (the mascot cheers).
        const bool hasBattery = status.BatteryFlag != 128 && status.BatteryFlag != 255;
        const bool reallyCharging = hasBattery && (status.BatteryFlag & 8) != 0;
        const double now = NowSeconds();
        static double s_lastReminder = 0.0;
        if (reallyCharging && !g_state.battery.reallyCharging) s_lastReminder = now;  // the plug-in alert counts as the first
        const bool justFull = hasBattery && newCharging && newPercent >= 100 && g_state.battery.reallyCharging && !reallyCharging;
        g_state.battery.hasBattery = hasBattery;
        g_state.battery.reallyCharging = reallyCharging;
        g_state.battery.minutesToFull = reallyCharging ? BatteryMinutesToFull() : -1;
        if (!triggerAlert) {
            g_state.battery.reminder = false;
            g_state.battery.full = false;
        }
        const int every = g_settings.chargingReminderMinutes;
        if (reallyCharging && every > 0 && now - s_lastReminder >= every * 60.0) {
            s_lastReminder = now;
            triggerAlert = true;
            g_state.battery.reminder = true;
        }
        if (justFull) {
            triggerAlert = true;
            g_state.battery.full = true;
            g_batteryCheerAt.store(now, std::memory_order_relaxed);
        }
        bb::mascot::SetCharging(g_settings.batteryMoods && reallyCharging);

        if (triggerAlert) {
            g_state.battery.active = true;
            g_state.battery.expiresAt = NowSeconds() + ((g_state.battery.reminder || g_state.battery.full) ? 5.0 : 4.0);
        }
    }

    if (triggerAlert) {
        TriggerNudge();
    }
}

ULONGLONG FileTimeToUInt64(FILETIME ft) {
    ULARGE_INTEGER value = {};
    value.LowPart = ft.dwLowDateTime;
    value.HighPart = ft.dwHighDateTime;
    return value.QuadPart;
}

static PDH_HQUERY g_gpuQuery = NULL;
static PDH_HCOUNTER g_gpuCounter = NULL;

static void InitGpuQuery() {
    if (g_gpuQuery == NULL) {
        if (PdhOpenQueryW(NULL, 0, &g_gpuQuery) == ERROR_SUCCESS) {
            PdhAddEnglishCounterW(g_gpuQuery, L"\\GPU Engine(*)\\Utilization Percentage", 0, &g_gpuCounter);
            PdhCollectQueryData(g_gpuQuery);
        }
    }
}

static int GetGpuUsage() {
    InitGpuQuery();
    if (!g_gpuQuery || !g_gpuCounter) return 0;

    PdhCollectQueryData(g_gpuQuery);

    DWORD bufferSize = 0;
    DWORD itemCount = 0;
    PdhGetFormattedCounterArrayW(g_gpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, NULL);

    if (bufferSize > 0) {
        std::vector<BYTE> buffer(bufferSize);
        PDH_FMT_COUNTERVALUE_ITEM_W* items = reinterpret_cast<PDH_FMT_COUNTERVALUE_ITEM_W*>(buffer.data());

        if (PdhGetFormattedCounterArrayW(g_gpuCounter, PDH_FMT_DOUBLE, &bufferSize, &itemCount, items) == ERROR_SUCCESS) {
            double total = 0;
            for (DWORD i = 0; i < itemCount; i++) {
                if (items[i].szName && wcsstr(items[i].szName, L"engtype_3D")) {
                    total += items[i].FmtValue.doubleValue;
                }
            }
            return ClampInt(static_cast<int>(total), 0, 100);
        }
    }
    return 0;
}

// Bunbun: network speed from the physical adapters' byte counters (MIB_IF_ROW2).
// The old PDH "Network Interface(*)" sum had two problems:
//  - on machines with Hyper-V switches or some VPNs, the same traffic shows up on
//    both the physical and the virtual adapter, so it was counted twice;
//  - PDH rates are averaged since the previous collection, and collection only
//    ran while the Hardware tab was visible, so the first number after opening
//    the tab was an average over however long it had been hidden.
// Filter interfaces (QoS, WFP and friends) also report HardwareInterface, so they
// are skipped too, or each byte would be counted once per filter.
static bool ReadPhysicalOctets(ULONG64& inOctets, ULONG64& outOctets, bool* ethernetUp = nullptr) {
    PMIB_IF_TABLE2 table = nullptr;
    if (GetIfTable2(&table) != NO_ERROR || !table) {
        return false;
    }
    inOctets = 0;
    outOctets = 0;
    for (ULONG i = 0; i < table->NumEntries; ++i) {
        const MIB_IF_ROW2& row = table->Table[i];
        if (!row.InterfaceAndOperStatusFlags.HardwareInterface || row.InterfaceAndOperStatusFlags.FilterInterface ||
            row.OperStatus != IfOperStatusUp) {
            continue;
        }
        inOctets += row.InOctets;
        outOctets += row.OutOctets;
        if (ethernetUp && row.Type == IF_TYPE_ETHERNET_CSMACD && row.PhysicalMediumType == NdisPhysicalMedium802_3) {
            *ethernetUp = true;
        }
    }
    FreeMibTable(table);
    return true;
}

// Returns false (and zeros) until two samples about a second apart exist, so a
// stale or partial window is never shown as a speed.
static bool GetNetworkUsage(float& outUpMbps, float& outDownMbps) {
    static ULONG64 lastIn = 0, lastOut = 0;
    static double lastAt = 0.0;
    outUpMbps = 0.0f;
    outDownMbps = 0.0f;

    ULONG64 in = 0, out = 0;
    if (!ReadPhysicalOctets(in, out)) {
        lastAt = 0.0;
        return false;
    }
    const double now = NowSeconds();
    const double dt = now - lastAt;
    // An adapter going down makes the sum shrink; treat that as a fresh start.
    const bool valid = lastAt > 0.0 && dt >= 0.25 && dt <= 3.0 && in >= lastIn && out >= lastOut;
    if (valid) {
        outDownMbps = static_cast<float>(static_cast<double>(in - lastIn) * 8.0 / dt / 1000000.0);
        outUpMbps = static_cast<float>(static_cast<double>(out - lastOut) * 8.0 / dt / 1000000.0);
    }
    lastIn = in;
    lastOut = out;
    lastAt = now;
    return valid;
}

// Bunbun: Task Manager style units, so light traffic doesn't read "0.0 Mbps".
template <size_t N>
void FormatBitRate(wchar_t (&buf)[N], float mbps, bool fresh) {
    if (!fresh) {
        swprintf_s(buf, L"–");  // measuring
    } else if (mbps < 1.0f) {
        swprintf_s(buf, L"%.0f Kbps", mbps * 1000.0f);
    } else if (mbps < 100.0f) {
        swprintf_s(buf, L"%.1f Mbps", mbps);
    } else if (mbps < 1000.0f) {
        swprintf_s(buf, L"%.0f Mbps", mbps);
    } else {
        swprintf_s(buf, L"%.2f Gbps", mbps / 1000.0f);
    }
}

