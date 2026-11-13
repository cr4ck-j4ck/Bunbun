// Bunbun: Notification listener, DND and Bluetooth device tracking.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

void SubscribeDndNotification() {
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (!hNtdll) return;

    auto pfnQuery = reinterpret_cast<PFN_NtQueryWnfStateData>(
        GetProcAddress(hNtdll, "NtQueryWnfStateData"));
    if (pfnQuery) {
        ULONG stamp = 0;
        ULONG size = sizeof(int);
        int val = 0;
        ULONG64 stateName = kWnfQuietHoursActiveProfileChanged;
        if (pfnQuery(&stateName, nullptr, nullptr, &stamp, &val, &size) == 0 && size >= sizeof(int)) {
            g_isDnDActive.store(val != 0);
        }
    }

    auto pfnSubscribe = reinterpret_cast<PFN_RtlSubscribeWnfStateChangeNotification>(
        GetProcAddress(hNtdll, "RtlSubscribeWnfStateChangeNotification"));
    if (!pfnSubscribe) return;

    pfnSubscribe(&g_wnfDndSubscription, kWnfQuietHoursActiveProfileChanged, 0,
                 WnfDndCallback, nullptr, nullptr, 0, 0);
}

void UnsubscribeDndNotification() {
    if (!g_wnfDndSubscription) return;
    HMODULE hNtdll = GetModuleHandleW(L"ntdll.dll");
    if (hNtdll) {
        auto pfnUnsubscribe = reinterpret_cast<PFN_RtlUnsubscribeWnfStateChangeNotification>(
            GetProcAddress(hNtdll, "RtlUnsubscribeWnfStateChangeNotification"));
        if (pfnUnsubscribe) {
            pfnUnsubscribe(g_wnfDndSubscription);
        }
    }
    g_wnfDndSubscription = nullptr;
}

#if DYNAMIC_ISLAND_HAS_USER_NOTIFICATION_LISTENER
DWORD WINAPI NotificationThreadProc(void*) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    using winrt::Windows::UI::Notifications::KnownNotificationBindings;
    using winrt::Windows::UI::Notifications::NotificationKinds;
    using winrt::Windows::UI::Notifications::Management::UserNotificationListener;
    using winrt::Windows::UI::Notifications::Management::UserNotificationListenerAccessStatus;

    std::set<uint32_t> seenIds;
    bool firstPoll = true;
    bool accessLogged = false;

    // The Windows Notification Service (WNS) and UWP subsystem take time to initialize on boot.
    // If this runs too early, instantiating UserNotificationListener::Current()
    // can permanently bind to an uninitialized COM proxy, permanently breaking notifications for the process.
    // To prevent this, we enforce a strict 30-second delay from process creation before touching the API.
    FILETIME creationTime, exitTime, kernelTime, userTime;
    if (GetProcessTimes(GetCurrentProcess(), &creationTime, &exitTime, &kernelTime, &userTime)) {
        ULARGE_INTEGER ct;
        ct.LowPart = creationTime.dwLowDateTime;
        ct.HighPart = creationTime.dwHighDateTime;

        FILETIME systemTime;
        GetSystemTimeAsFileTime(&systemTime);
        ULARGE_INTEGER st;
        st.LowPart = systemTime.dwLowDateTime;
        st.HighPart = systemTime.dwHighDateTime;

        uint64_t msSinceProcessStart = (st.QuadPart - ct.QuadPart) / 10000;
        if (msSinceProcessStart < 30000) {
            DWORD waitTime = 30000 - (DWORD)msSinceProcessStart;
            Wh_Log(L"Process started recently. Delaying UserNotificationListener init by %d ms to let UWP subsystem load...", waitTime);
            WaitForSingleObject(g_stopEvent, waitTime);
        }
    }

    while (WaitForSingleObject(g_stopEvent, 0) == WAIT_TIMEOUT) {
        try {
            auto listener = UserNotificationListener::Current();
            auto access = listener.RequestAccessAsync().get();
            if (access != UserNotificationListenerAccessStatus::Allowed) {
                if (!accessLogged) {
                    if (access == UserNotificationListenerAccessStatus::Denied) {
                        // Actionable: this is a Windows privacy switch, not a
                        // mod setting, and it is the usual reason the module
                        // stays silent when everything else is configured.
                        Wh_Log(L"Notification listener permission DENIED by Windows. "
                               L"Enable Settings > Privacy & security > Notifications > "
                               L"\"Let apps access your notifications\", then restart the mod. "
                               L"Retrying meanwhile...");
                    } else {
                        Wh_Log(L"Notification listener permission not granted or UWP subsystem not ready on boot (status: %d); retrying connection loop...", (int)access);
                    }
                    accessLogged = true;
                }
                WaitForSingleObject(g_stopEvent, 3000);
                continue;
            }

            if (accessLogged) {
                Wh_Log(L"WinRT UserNotificationListener successfully connected.");
                accessLogged = false;
            }

            while (WaitForSingleObject(g_stopEvent, 0) == WAIT_TIMEOUT) {
                try {
                    auto notifications = listener.GetNotificationsAsync(NotificationKinds::Toast).get();
                    std::set<uint32_t> currentIds;

                    for (uint32_t i = 0; i < notifications.Size(); ++i) {
                        currentIds.insert(notifications.GetAt(i).Id());
                    }

                    if (firstPoll) {
                        seenIds = std::move(currentIds);
                        firstPoll = false;
                        WaitForSingleObject(g_stopEvent, 1000);
                        continue;
                    }

                    for (uint32_t i = 0; i < notifications.Size(); ++i) {
                        try {
                            auto userNotification = notifications.GetAt(i);
                            const uint32_t id = userNotification.Id();

                            if (seenIds.count(id)) {
                                continue;
                            }

                            // Immediately mark as seen so we don't process it again
                            seenIds.insert(id);

                            if (g_settings.notificationRespectDnD && g_isDnDActive.load()) {
                                Wh_Log(L"Notification %u: held back, Windows' Do Not Disturb is on.", id);  // Bunbun
                                continue;
                            }
                            // Bunbun: Focus mode holds notifications back (they stay in Windows'
                            // notification centre) and counts them for the summary.
                            if (FocusModeActive()) {
                                HoldNotificationForFocus();
                                continue;
                            }

                            NotificationSnapshot snapshot;
                            snapshot.active = true;
                            snapshot.expiresAt = NowSeconds() + 4.0;
                            snapshot.shownAt = NowSeconds();  // Bunbun: the pill's animation counts from here
                            auto appInfo = userNotification.AppInfo();
                            auto displayInfo = appInfo.DisplayInfo();
                            snapshot.app = displayInfo.DisplayName().c_str();

                            // Filter out noisy background console/script engines like PowerShell that fire system toasts unexpectedly
                            const std::wstring lowerApp = ToLowerCopy(snapshot.app);
                            if (lowerApp.find(L"powershell") != std::wstring::npos ||
                                lowerApp.find(L"pwsh") != std::wstring::npos ||
                                lowerApp.find(L"cmd.exe") != std::wstring::npos) {
                                Wh_Log(L"Notification %u from \"%ls\": skipped (console app).", id, snapshot.app.c_str());  // Bunbun
                                continue;
                            }
                            Wh_Log(L"Notification %u from \"%ls\".", id, snapshot.app.c_str());  // Bunbun
                            try {
                                // Bunbun: 64 px, sharp on the pill; Store apps' logos have a wide
                                // transparent margin, trimmed so the logo fills its tile.
                                auto logo = displayInfo.GetLogo({64.0f, 64.0f});
                                std::vector<uint8_t> logoBytes = ReadWinRtStreamBytes(logo);
                                if (!logoBytes.empty()) {
                                    BitmapPixels decoded;
                                    if (DecodeImageBytesToPixels(logoBytes, &decoded) && TrimTransparentMargin(&decoded)) {
                                        snapshot.icon = PixelsRef(std::move(decoded));
                                    }
                                }
                            } catch (...) {
                            }

                            if (!snapshot.icon && !snapshot.app.empty()) {
                                snapshot.icon = FindAppIconByName(snapshot.app, 64);
                            }

                            auto notification = userNotification.Notification();
                            auto binding = notification.Visual().GetBinding(KnownNotificationBindings::ToastGeneric());
                            if (binding) {
                                auto textElements = binding.GetTextElements();
                                if (textElements.Size() > 0) {
                                    snapshot.title = textElements.GetAt(0).Text().c_str();
                                }
                                if (textElements.Size() > 1) {
                                    snapshot.body = textElements.GetAt(1).Text().c_str();
                                }
                            }

                            if (snapshot.title.empty()) {
                                snapshot.title = snapshot.app.empty() ? L"New notification" : snapshot.app;
                            }
                            // Bunbun: the title and the message on lines of their own (they
                            // were joined with " - " into one scrolling line).
                            for (std::wstring* s : {&snapshot.title, &snapshot.body}) {
                                for (wchar_t& c : *s) if (c == L'\n' || c == L'\r') c = L' ';
                                if (s->size() > 120) {
                                    s->resize(119);
                                    *s += L'\x2026';
                                }
                            }

                            {
                                std::lock_guard lock(g_stateMutex);
                                g_state.notification = std::move(snapshot);
                            }
                            TriggerNudge();
                        } catch (const winrt::hresult_error& nex) {
                            if (nex.to_abi() == static_cast<int32_t>(0x80004001) || nex.to_abi() == static_cast<int32_t>(0x80040154)) { // E_NOTIMPL or REGDB_E_CLASSNOTREG
                                // UWP subsystem not ready, skip without spamming logs
                            } else {
                                Wh_Log(L"Failed to parse a notification (0x%08X); skipping.", nex.to_abi());
                            }
                        } catch (...) {
                            Wh_Log(L"Failed to parse a notification; skipping.");
                        }
                    }

                    seenIds = std::move(currentIds);
                } catch (const winrt::hresult_error& ex) {
                    const HRESULT hr = ex.to_abi();
                    if (hr == static_cast<HRESULT>(0x80004001) || hr == static_cast<HRESULT>(0x80040154)) { // E_NOTIMPL or REGDB_E_CLASSNOTREG
                        if (!accessLogged) {
                            Wh_Log(L"Notification listener UWP subsystem not fully ready (0x%08X). Retrying in background...", hr);
                            accessLogged = true;
                        }
                    } else {
                        Wh_Log(L"NotificationThreadProc inner loop WinRT error: %s (0x%08X); reconnecting...", ex.message().c_str(), hr);
                    }
                    WaitForSingleObject(g_stopEvent, 3000);
                    break;
                } catch (...) {
                    Wh_Log(L"NotificationThreadProc inner loop unknown exception; reconnecting...");
                    WaitForSingleObject(g_stopEvent, 3000);
                    break;
                }

                WaitForSingleObject(g_stopEvent, 1000);
            }
        } catch (const winrt::hresult_error& ex) {
            if (!accessLogged) {
                Wh_Log(L"NotificationThreadProc connection error: %s (0x%08X). Retrying in 3s...", ex.message().c_str(), ex.to_abi());
                accessLogged = true;
            }
            WaitForSingleObject(g_stopEvent, 3000);
        } catch (...) {
            if (!accessLogged) {
                Wh_Log(L"NotificationThreadProc unknown connection exception. Retrying in 3s...");
                accessLogged = true;
            }
            WaitForSingleObject(g_stopEvent, 3000);
        }
    }

    winrt::uninit_apartment();
    return 0;
}
#endif

#if DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER

BluetoothDeviceCategory ClassifyBluetoothDevice(
    const winrt::Windows::Devices::Enumeration::DeviceInformation& info) {
    // 1) Name-based heuristic first — cheap, synchronous, and correct for
    //    the overwhelming majority of consumer devices, which advertise a
    //    descriptive friendly name.
    std::wstring name = ToLowerCopy(std::wstring(info.Name().c_str()));

    if (name.find(L"headphone") != std::wstring::npos ||
        name.find(L"headset") != std::wstring::npos ||
        name.find(L"earbud") != std::wstring::npos ||
        name.find(L"buds") != std::wstring::npos ||
        name.find(L"airpods") != std::wstring::npos) {
        return BluetoothDeviceCategory::Headphones;
    }
    if (name.find(L"speaker") != std::wstring::npos ||
        name.find(L"soundbar") != std::wstring::npos ||
        name.find(L"boombox") != std::wstring::npos) {
        return BluetoothDeviceCategory::Speaker;
    }
    if (name.find(L"mouse") != std::wstring::npos) {
        return BluetoothDeviceCategory::Mouse;
    }
    if (name.find(L"keyboard") != std::wstring::npos) {
        return BluetoothDeviceCategory::Keyboard;
    }
    if (name.find(L"iphone") != std::wstring::npos ||
        name.find(L"phone") != std::wstring::npos ||
        name.find(L"galaxy") != std::wstring::npos ||
        name.find(L"pixel") != std::wstring::npos) {
        return BluetoothDeviceCategory::Phone;
    }

    // 2) Fall back to the classic Bluetooth Class-of-Device major-class bits.
    try {
        using winrt::Windows::Devices::Bluetooth::BluetoothDevice;
        using winrt::Windows::Devices::Bluetooth::BluetoothMajorClass;
        auto device = BluetoothDevice::FromIdAsync(info.Id()).get();
        if (device) {
            switch (device.ClassOfDevice().MajorClass()) {
                case BluetoothMajorClass::Phone:
                    return BluetoothDeviceCategory::Phone;
                case BluetoothMajorClass::AudioVideo:
                    return BluetoothDeviceCategory::Headphones;
                case BluetoothMajorClass::Peripheral:
                    return BluetoothDeviceCategory::Mouse;
                default:
                    break;
            }
        }
    } catch (...) {
        // Not every device id resolves to a classic BluetoothDevice — BLE-only
        // peripherals in particular will throw here. Fall through.
    }

    return BluetoothDeviceCategory::Generic;
}

// Reads the standard GATT Battery Service (0x180F) / Battery Level
// characteristic (0x2A19). Only works for devices that expose battery this
// way — mostly BLE and BLE-dual-mode devices. Classic-only devices will
// fail the GetGattServicesForUuidAsync call and we just report -1 (unknown).
int TryReadBluetoothBatteryPercentBLE(winrt::hstring const& deviceId) {
    using namespace winrt::Windows::Devices::Bluetooth;
    using namespace winrt::Windows::Devices::Bluetooth::GenericAttributeProfile;

    try {
        auto bleDevice = BluetoothLEDevice::FromIdAsync(deviceId).get();
        if (!bleDevice) {
            return -1;
        }

        auto servicesResult = bleDevice.GetGattServicesForUuidAsync(
            GattServiceUuids::Battery(), BluetoothCacheMode::Uncached).get();
        if (servicesResult.Status() != GattCommunicationStatus::Success ||
            servicesResult.Services().Size() == 0) {
            return -1;
        }

        auto service = servicesResult.Services().GetAt(0);
        auto charsResult = service.GetCharacteristicsForUuidAsync(
            GattCharacteristicUuids::BatteryLevel(), BluetoothCacheMode::Uncached).get();
        if (charsResult.Status() != GattCommunicationStatus::Success ||
            charsResult.Characteristics().Size() == 0) {
            return -1;
        }

        auto characteristic = charsResult.Characteristics().GetAt(0);
        auto readResult = characteristic.ReadValueAsync(BluetoothCacheMode::Uncached).get();
        if (readResult.Status() != GattCommunicationStatus::Success) {
            return -1;
        }

        auto buffer = readResult.Value();
        if (buffer.Length() < 1) {
            return -1;
        }

        auto reader = winrt::Windows::Storage::Streams::DataReader::FromBuffer(buffer);
        uint8_t raw = reader.ReadByte();
        return ClampInt(static_cast<int>(raw), 0, 100);
    } catch (...) {
        return -1;
    }
}

// Windows' own Settings > Bluetooth & devices page shows a battery percentage
// for most classic (non-BLE) headphones/earbuds/speakers using an
// undocumented per-devnode property exposed by the Microsoft Bluetooth
// classic driver stack — not GATT. This is the same property those battery
// tray-icon utilities read. Query it via SetupAPI on the Bluetooth-class
// devnode whose instance ID embeds the device's Bluetooth address.
static const GUID kGuidDevClassBluetooth = {
    0xe0cbf06c, 0xcd8b, 0x4647, {0xbb, 0x8a, 0x26, 0x3b, 0x43, 0xf0, 0xf9, 0x74}};

static const DEVPROPKEY PKEY_Bluetooth_Battery = {
    {0x104ea319, 0x6ee2, 0x4701, {0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5}}, 2};

int TryReadClassicBluetoothBatteryPercent(uint64_t address) {
    if (!address) {
        Wh_Log(L"BT battery: no address to search for.");
        return -1;
    }

    wchar_t addrHex[16] = {};
    swprintf_s(addrHex, L"%012llX", static_cast<unsigned long long>(address));
    const std::wstring addrLower = ToLowerCopy(addrHex);
    Wh_Log(L"BT battery: searching devnodes for address %s", addrLower.c_str());

    // Enumerate everything the Bluetooth bus driver (BTHENUM) exposes,
    // regardless of which device setup class it landed in. This is broader
    // than filtering by GUID_DEVCLASS_BLUETOOTH and matches what battery
    // tray utilities do.
    HDEVINFO deviceInfoSet = SetupDiGetClassDevsExW(
        nullptr, L"BTHENUM", nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT,
        nullptr, nullptr, nullptr);

    if (deviceInfoSet == INVALID_HANDLE_VALUE) {
        Wh_Log(L"BT battery: SetupDiGetClassDevsExW(BTHENUM) failed (0x%lx), falling back to class GUID.", GetLastError());
        deviceInfoSet = SetupDiGetClassDevsW(&kGuidDevClassBluetooth, nullptr, nullptr, DIGCF_PRESENT);
        if (deviceInfoSet == INVALID_HANDLE_VALUE) {
            Wh_Log(L"BT battery: fallback enumeration also failed.");
            return -1;
        }
    }

    int result = -1;
    int matchedCount = 0;
    SP_DEVINFO_DATA devInfoData = {};
    devInfoData.cbSize = sizeof(devInfoData);

    for (DWORD i = 0; SetupDiEnumDeviceInfo(deviceInfoSet, i, &devInfoData); ++i) {
        wchar_t instanceId[512] = {};
        if (!SetupDiGetDeviceInstanceIdW(deviceInfoSet, &devInfoData, instanceId,
                                         ARRAYSIZE(instanceId), nullptr)) {
            continue;
        }

        if (ToLowerCopy(instanceId).find(addrLower) == std::wstring::npos) {
            continue;
        }

        ++matchedCount;
        Wh_Log(L"BT battery: matched devnode %s", instanceId);

        DEVPROPTYPE propType = 0;
        BYTE battery = 0;
        DWORD required = 0;
        if (SetupDiGetDevicePropertyW(deviceInfoSet, &devInfoData, &PKEY_Bluetooth_Battery,
                                      &propType, &battery, sizeof(battery), &required, 0)) {
            Wh_Log(L"BT battery: property present on this devnode, type=%lu value=%u", propType, battery);
            if (propType == DEVPROP_TYPE_BYTE && battery != 0xFF) {
                result = ClampInt(static_cast<int>(battery), 0, 100);
                break;
            }
        } else {
            // Bunbun: this repeats for every device on every poll; once in five minutes is plenty.
            static double s_lastNotSetLog = -1e9;
            if (NowSeconds() - s_lastNotSetLog > 300.0) {
                s_lastNotSetLog = NowSeconds();
                Wh_Log(L"BT battery: PKEY_Bluetooth_Battery not set on this devnode yet (error 0x%lx).", GetLastError());
            }
        }
    }

    if (matchedCount == 0) {
        Wh_Log(L"BT battery: no devnode instance ID contained address %s.", addrLower.c_str());
    }

    SetupDiDestroyDeviceInfoList(deviceInfoSet);
    return result;
}

int TryReadBluetoothBatteryPercent(winrt::hstring const& deviceId) {
    int result = -1;

    // Try the classic per-devnode battery property first — this is what
    // Settings > Bluetooth & devices reads, and it's what covers most
    // headsets/earbuds/speakers that pair over classic Bluetooth (BR/EDR)
    // rather than BLE.
    try {
        using winrt::Windows::Devices::Bluetooth::BluetoothDevice;
        auto classicDevice = BluetoothDevice::FromIdAsync(deviceId).get();
        if (classicDevice) {
            result = TryReadClassicBluetoothBatteryPercent(classicDevice.BluetoothAddress());
        }
    } catch (...) {
        // Not resolvable as a classic BluetoothDevice (BLE-only peripheral) — fall through.
    }

    if (result < 0) {
        // Fall back to BLE GATT Battery Service for BLE / dual-mode devices.
        result = TryReadBluetoothBatteryPercentBLE(deviceId);
    }

    if (result >= 0) {
        // Remember this reading so a later disconnect (when the device can
        // no longer be queried) can still show the last known level.
        std::lock_guard lock(g_bluetoothBatteryCacheMutex);
        g_bluetoothBatteryCache[std::wstring(deviceId.c_str())] = result;
    }

    return result;
}

// Returns the last battery percent we successfully read for this device
// while it was connected, or -1 if we never learned one.
int GetLastKnownBluetoothBatteryPercent(const std::wstring& deviceId) {
    std::lock_guard lock(g_bluetoothBatteryCacheMutex);
    auto it = g_bluetoothBatteryCache.find(deviceId);
    return it != g_bluetoothBatteryCache.end() ? it->second : -1;
}

// Small per-device cache so a Removed event (which only carries an Id, not
// a full DeviceInformation) can still show a name/icon on disconnect.
struct BluetoothTrackedDevice {
    std::wstring name;
    BluetoothDeviceCategory category;
};

void HandleBluetoothConnected(
    const winrt::Windows::Devices::Enumeration::DeviceInformation& info,
    std::unordered_map<std::wstring, BluetoothTrackedDevice>& cache,
    std::mutex& cacheMutex) {
    std::wstring name = info.Name().c_str();
    if (name.empty()) {
        return;
    }

    // Re-read the default output once (event-driven, no polling).
    g_volumeEndpointStale = true;
    if (HWND recheckHwnd = g_hwnd) {
        PostMessageW(recheckHwnd, WM_NULL, 0, 0);
    }

    BluetoothDeviceCategory category = ClassifyBluetoothDevice(info);
    {
        std::lock_guard lock(cacheMutex);
        cache[std::wstring(info.Id().c_str())] = BluetoothTrackedDevice{name, category};
    }

    if (!g_settings.bluetoothIndicator) {
        return;
    }

    Wh_Log(L"Bluetooth: connected - %s", name.c_str());

    const uint64_t myGeneration = ++g_bluetoothConnectGeneration;
    const std::wstring deviceId = info.Id().c_str();

    BluetoothDeviceSnapshot snapshot;
    snapshot.active = true;
    snapshot.connected = true;
    snapshot.deviceName = name;
    snapshot.category = category;
    snapshot.expiresAt = NowSeconds() + 4.0;
    snapshot.batteryPercent = g_settings.bluetoothShowBattery
        ? TryReadBluetoothBatteryPercent(info.Id())
        : -1;

    {
        std::lock_guard lock(g_stateMutex);
        g_state.bluetoothDevice = std::move(snapshot);
    }
    TriggerNudge();

    // Windows often hasn't populated the battery property at the exact
    // instant the connection event fires — it needs a moment to actually
    // query the device. Keep retrying in the background for a while; if a
    // value shows up and this connection is still the one being displayed,
    // patch it into the live state and re-render.
    if (g_settings.bluetoothShowBattery && snapshot.batteryPercent < 0) {
        std::thread([deviceId, myGeneration]() {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            for (int attempt = 0; attempt < 6; ++attempt) {
                Sleep(1500);
                if (g_bluetoothConnectGeneration.load() != myGeneration) {
                    break;  // a newer connect/disconnect event superseded this one
                }

                int battery = TryReadBluetoothBatteryPercent(winrt::hstring(deviceId));
                if (battery >= 0) {
                    std::lock_guard lock(g_stateMutex);
                    if (g_bluetoothConnectGeneration.load() == myGeneration &&
                        g_state.bluetoothDevice.connected) {
                        g_state.bluetoothDevice.batteryPercent = battery;
                        Wh_Log(L"Bluetooth: battery arrived late (%d%%) on retry %d.", battery, attempt + 1);
                    }
                    TriggerNudge();
                    break;
                }
            }
            winrt::uninit_apartment();
        }).detach();
    }
}

void HandleBluetoothDisconnected(
    winrt::hstring const& id,
    std::unordered_map<std::wstring, BluetoothTrackedDevice>& cache,
    std::mutex& cacheMutex) {
    ++g_bluetoothConnectGeneration;  // cancel any pending battery retry for the old connection

    // Re-read the default output once (event-driven, no polling).
    g_volumeEndpointStale = true;
    if (HWND recheckHwnd = g_hwnd) {
        PostMessageW(recheckHwnd, WM_NULL, 0, 0);
    }

    if (!g_settings.bluetoothIndicator) {
        return;
    }

    std::wstring name;
    BluetoothDeviceCategory category = BluetoothDeviceCategory::Generic;
    {
        std::lock_guard lock(cacheMutex);
        auto it = cache.find(std::wstring(id.c_str()));
        if (it != cache.end()) {
            name = it->second.name;
            category = it->second.category;
        }
    }
    if (name.empty()) {
        name = L"Bluetooth Device";
    }

    Wh_Log(L"Bluetooth: disconnected - %s", name.c_str());

    BluetoothDeviceSnapshot snapshot;
    snapshot.active = true;
    snapshot.connected = false;
    snapshot.deviceName = name;
    snapshot.category = category;
    snapshot.batteryPercent = GetLastKnownBluetoothBatteryPercent(std::wstring(id.c_str()));
    snapshot.expiresAt = NowSeconds() + 4.0;

    {
        std::lock_guard lock(g_stateMutex);
        g_state.bluetoothDevice = std::move(snapshot);
    }
    TriggerNudge();
}

DWORD WINAPI BluetoothThreadProc(void*) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);

    using winrt::Windows::Devices::Enumeration::DeviceInformation;
    using winrt::Windows::Devices::Enumeration::DeviceInformationUpdate;
    using winrt::Windows::Devices::Enumeration::DeviceWatcher;
    using winrt::Windows::Devices::Bluetooth::BluetoothDevice;
    using winrt::Windows::Devices::Bluetooth::BluetoothLEDevice;
    using winrt::Windows::Devices::Bluetooth::BluetoothConnectionStatus;

    std::unordered_map<std::wstring, BluetoothTrackedDevice> deviceCache;
    std::mutex cacheMutex;
    std::vector<DeviceWatcher> watchers;
    // Bunbun: which devices are connected right now (the cache keeps names of
    // disconnected ones too), for the low-battery check below.
    auto connected = std::make_shared<std::unordered_set<std::wstring>>();

    // Watching the "Connected" selector directly means a device APPEARING
    // in the watcher (Added) is a connect, and DISAPPEARING (Removed) is a
    // disconnect — no property polling or IsConnected lookups required.
    auto startWatcher = [&](winrt::hstring const& selector, const wchar_t* label) {
        try {
            DeviceWatcher watcher = DeviceInformation::CreateWatcher(selector);
            auto enumDone = std::make_shared<std::atomic<bool>>(false);

            watcher.EnumerationCompleted(
                [enumDone](DeviceWatcher const&, winrt::Windows::Foundation::IInspectable const&) {
                    *enumDone = true;
                });

            watcher.Added([&deviceCache, &cacheMutex, enumDone, connected](
                              DeviceWatcher const&, DeviceInformation const& info) {
                {
                    std::lock_guard lock(cacheMutex);
                    connected->insert(std::wstring(info.Id().c_str()));  // Bunbun
                }
                // Devices reported before EnumerationCompleted are the
                // watcher's initial snapshot (already connected when the mod
                // started) — cache them silently so a later disconnect still
                // resolves a name, but don't pop a card for a connection the
                // user didn't just cause.
                if (!*enumDone) {
                    std::wstring name = info.Name().c_str();
                    if (!name.empty()) {
                        std::lock_guard lock(cacheMutex);
                        deviceCache[std::wstring(info.Id().c_str())] =
                            BluetoothTrackedDevice{name, ClassifyBluetoothDevice(info)};
                    }
                    return;
                }
                HandleBluetoothConnected(info, deviceCache, cacheMutex);
            });

            watcher.Removed([&deviceCache, &cacheMutex, connected](
                                DeviceWatcher const&, DeviceInformationUpdate const& update) {
                {
                    std::lock_guard lock(cacheMutex);
                    connected->erase(std::wstring(update.Id().c_str()));  // Bunbun
                }
                HandleBluetoothDisconnected(update.Id(), deviceCache, cacheMutex);
            });

            watcher.Start();
            watchers.push_back(watcher);
            Wh_Log(L"Bluetooth: %s watcher started.", label);
        } catch (...) {
            Wh_Log(L"Bluetooth: failed to start %s watcher.", label);
        }
    };

    startWatcher(
        BluetoothDevice::GetDeviceSelectorFromConnectionStatus(BluetoothConnectionStatus::Connected),
        L"classic");
    startWatcher(
        BluetoothLEDevice::GetDeviceSelectorFromConnectionStatus(BluetoothConnectionStatus::Connected),
        L"BLE");

    // DeviceWatcher does its work via WinRT callbacks on background threads;
    // this thread just needs to stay alive to keep the watchers rooted
    // until shutdown is signaled.
    // Bunbun: every 5 minutes, a heads-up for connected devices at 20% or below,
    // once a day per device.
    std::unordered_map<std::wstring, int> warnedDay;
    int tick = 0;
    while (WaitForSingleObject(g_stopEvent, 1000) == WAIT_TIMEOUT) {
        if (++tick % 300 != 30) continue;  // first check 30 s after start
        if (!GetSettingsCopy().bluetoothLowWarning) continue;
        std::vector<std::pair<std::wstring, std::wstring>> devices;
        {
            std::lock_guard lock(cacheMutex);
            for (const auto& id : *connected) {
                auto it = deviceCache.find(id);
                devices.emplace_back(id, it != deviceCache.end() ? it->second.name : std::wstring(L"Bluetooth device"));
            }
        }
        SYSTEMTIME st{};
        GetLocalTime(&st);
        const int today = st.wYear * 400 + st.wMonth * 32 + st.wDay;
        for (const auto& [id, name] : devices) {
            const int pct = TryReadBluetoothBatteryPercent(winrt::hstring(id));
            if (pct < 0 || pct > 20 || warnedDay[id] == today) continue;
            warnedDay[id] = today;
            {
                std::lock_guard lock(g_stateMutex);
                g_state.notification.active = true;
                g_state.notification.app = L"Bluetooth";
                g_state.notification.title = name + L" battery is low";
                g_state.notification.body = std::to_wstring(pct) + L"% left \x00B7 charge it soon";
                g_state.notification.icon = {};
                g_state.notification.expiresAt = NowSeconds() + 5.0;
                g_state.notification.shownAt = NowSeconds();  // Bunbun: the pill's animation counts from here
            }
            g_layoutDirty = true;
            if (HWND hwnd = g_hwnd) PostMessageW(hwnd, WM_APP_NEW_EVENT, 0, 0);
            Wh_Log(L"Bluetooth: %s battery low (%d%%).", name.c_str(), pct);
        }
    }

    for (auto& watcher : watchers) {
        try {
            watcher.Stop();
        } catch (...) {
        }
    }

    winrt::uninit_apartment();
    return 0;
}

#else  // !DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER

DWORD WINAPI BluetoothThreadProc(void*) {
    // SDK used to build this mod doesn't expose the WinRT Bluetooth headers;
    // the indicator silently stays inactive instead of failing the mod.
    return 0;
}

#endif  // DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER

float SampleAudioAmplitude(BYTE* data, UINT32 frames, WAVEFORMATEX* format) {
    if (!data || !frames || !format || !format->nChannels) {
        return 0.0f;
    }

    double sum = 0.0;
    size_t samples = static_cast<size_t>(frames) * format->nChannels;

    if (format->wFormatTag == WAVE_FORMAT_IEEE_FLOAT ||
        (format->wFormatTag == WAVE_FORMAT_EXTENSIBLE &&
         format->cbSize >= sizeof(WAVEFORMATEXTENSIBLE) - sizeof(WAVEFORMATEX) &&
         IsEqualGUID(reinterpret_cast<WAVEFORMATEXTENSIBLE*>(format)->SubFormat,
                     kSubTypeIeeeFloat))) {
        auto* f = reinterpret_cast<float*>(data);
        for (size_t i = 0; i < samples; ++i) {
            sum += f[i] * f[i];
        }
    } else if (format->wBitsPerSample == 16) {
        auto* s = reinterpret_cast<int16_t*>(data);
        for (size_t i = 0; i < samples; ++i) {
            const double v = s[i] / 32768.0;
            sum += v * v;
        }
    }

    const double rms = samples ? std::sqrt(sum / samples) : 0.0;
    return Clamp(static_cast<float>(rms * 4.0), 0.0f, 1.0f);
}

// Goertzel filter bank over a sliding window of the mixed-down system audio.
// Produces kSpectrumBands log-spaced magnitudes (50 Hz .. 14 kHz) with an
// attack/release envelope and a slow auto-gain so quiet and loud tracks both
// fill the visualizer. Only ever touched from the audio thread.
