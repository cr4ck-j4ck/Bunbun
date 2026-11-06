// Bunbun: the Connectivity tab's data side. Included in the core's anonymous
// namespace after SharedState; drawing lives in connectivity_draw.inc.

// Bunbun: Connectivity tab layout, shared by DrawConnectivityDashboard and the
// click / hover hit test so the two can't drift apart.
namespace ConnectivityLayout {
    constexpr float kPadX = 24.0f;
    constexpr float kCardsTop = 40.0f;
    constexpr float kCardHeight = 118.0f;
    constexpr float kCardGap = 9.0f;
    constexpr int kMaxCards = 4;  // network, Bluetooth, Do Not Disturb, and Battery on laptops
    // Long-press lists (Wi-Fi networks, Bluetooth devices).
    constexpr float kListTop = 42.0f;
    constexpr float kRowHeight = 25.0f;
    constexpr int kMaxRows = 5;
    constexpr float kHeaderBottom = 36.0f;   // back button and the Settings link sit above this
    constexpr float kBackWidth = 150.0f;
    constexpr float kSettingsLinkWidth = 74.0f;
}

// Render-thread state for the long-press lists. g_connectivityListWanted is the
// one piece the sampler thread reads, so it only fetches lists while one shows.
std::atomic<int> g_connectivityListWanted{-1};  // -1 none, 0 Wi-Fi, 1 Bluetooth
int g_connectivityList = -1;
double g_connectivityLastDrawnAt = 0.0;
int g_connectivityPressCard = -1;   // card held down, waiting for the long-press timer
bool g_connectivityLongFired = false;
std::wstring g_connectivityBusyName;  // row being connected / disconnected
double g_connectivityBusyUntil = 0.0;
constexpr UINT_PTR kConnectivityHoldTimer = 0xB0B1;
constexpr UINT kConnectivityHoldMs = 450;

// Card `i` of `count` in content pixels for a tab `width` wide. Three cards sit
// in a row; four (with Battery) make a 2x2 grid of compact cards, because the
// dashboard is only 380 px wide and four in a row would clip their text.
D2D1_RECT_F ConnectivityCardRect(int i, int count, float width) {
    using namespace ConnectivityLayout;
    if (count <= 3) {
        const float cardW = (width - kPadX * 2.0f - kCardGap * (count - 1)) / count;
        const float left = kPadX + static_cast<float>(i) * (cardW + kCardGap);
        return D2D1::RectF(left, kCardsTop, left + cardW, kCardsTop + kCardHeight);
    }
    constexpr float padX = 16.0f, gap = 8.0f;
    const float w = (width - padX * 2.0f - gap) / 2.0f;
    const float h = (kCardHeight - gap) / 2.0f;
    const float left = padX + static_cast<float>(i % 2) * (w + gap);
    const float top = kCardsTop + static_cast<float>(i / 2) * (h + gap);
    return D2D1::RectF(left, top, left + w, top + h);
}

int ConnectivityCardAtContentPoint(const MediaContentPoint& pt) {
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) {
        return -1;
    }
    const int cards = g_connectivityCards;
    for (int i = 0; i < cards; ++i) {
        const D2D1_RECT_F r = ConnectivityCardRect(i, cards, pt.width);
        if (pt.x >= r.left && pt.x <= r.right && pt.y >= r.top && pt.y <= r.bottom) return i;
    }
    return -1;
}

// ── Bunbun: connectivity sampler (Wi-Fi, Ethernet, Bluetooth) ────────────────
// Runs on its own thread: WLAN queries and the WinRT radio / device lookups can
// take tens of milliseconds, too slow for the render thread. It samples once at
// start, then only while the Connectivity tab is on screen (the render loop
// stamps g_connectivityWantedAt and wakes it when the tab appears).
std::atomic<double> g_connectivityWantedAt = 0.0;
HANDLE g_connectivityWake = nullptr;
HANDLE g_connectivityThread = nullptr;

static std::wstring WideFromBytes(const unsigned char* bytes, size_t length) {
    if (length == 0) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes), static_cast<int>(length), nullptr, 0);
    std::wstring out(static_cast<size_t>(std::max(n, 0)), L'\0');
    if (n > 0) {
        MultiByteToWideChar(CP_UTF8, 0, reinterpret_cast<const char*>(bytes), static_cast<int>(length), out.data(), n);
    }
    return out;
}

// ── Wi-Fi networks ──────────────────────────────────────────────────────────
// The first WLAN interface is the one Bunbun lists and connects; laptops have one.
bool FirstWlanInterface(HANDLE wlan, GUID* out) {
    PWLAN_INTERFACE_INFO_LIST list = nullptr;
    if (WlanEnumInterfaces(wlan, nullptr, &list) != ERROR_SUCCESS || !list) return false;
    const bool found = list->dwNumberOfItems > 0;
    if (found) *out = list->InterfaceInfo[0].InterfaceGuid;
    WlanFreeMemory(list);
    return found;
}

void SampleWifiNetworks(HANDLE wlan, ConnectivitySnapshot& c) {
    GUID guid{};
    if (!FirstWlanInterface(wlan, &guid)) return;
    // A fresh scan every few seconds while the list is open; results arrive later,
    // so the list fills in over the next samples.
    static double s_lastScan = 0.0;
    if (NowSeconds() - s_lastScan > 8.0) {
        WlanScan(wlan, &guid, nullptr, nullptr, nullptr);
        s_lastScan = NowSeconds();
    }
    PWLAN_AVAILABLE_NETWORK_LIST nets = nullptr;
    if (WlanGetAvailableNetworkList(wlan, &guid, 0, nullptr, &nets) != ERROR_SUCCESS || !nets) return;
    for (DWORD i = 0; i < nets->dwNumberOfItems; ++i) {
        const WLAN_AVAILABLE_NETWORK& n = nets->Network[i];
        WifiNetwork w;
        w.ssid = WideFromBytes(n.dot11Ssid.ucSSID, std::min<ULONG>(n.dot11Ssid.uSSIDLength, DOT11_SSID_MAX_LENGTH));
        if (w.ssid.empty()) continue;  // hidden network
        w.signal = static_cast<int>(n.wlanSignalQuality);
        w.secured = n.bSecurityEnabled != FALSE;
        w.connected = (n.dwFlags & WLAN_AVAILABLE_NETWORK_CONNECTED) != 0;
        if (n.dwFlags & WLAN_AVAILABLE_NETWORK_HAS_PROFILE) w.profile = n.strProfileName;
        // One row per name: Windows lists a network once per profile / security type.
        auto it = std::find_if(c.wifiNetworks.begin(), c.wifiNetworks.end(), [&](const WifiNetwork& e) { return e.ssid == w.ssid; });
        if (it == c.wifiNetworks.end()) {
            c.wifiNetworks.push_back(std::move(w));
        } else {
            it->connected = it->connected || w.connected;
            it->signal = std::max(it->signal, w.signal);
            if (it->profile.empty()) it->profile = w.profile;
        }
    }
    WlanFreeMemory(nets);
    std::stable_sort(c.wifiNetworks.begin(), c.wifiNetworks.end(), [](const WifiNetwork& a, const WifiNetwork& b) {
        if (a.connected != b.connected) return a.connected;
        if (a.profile.empty() != b.profile.empty()) return !a.profile.empty();  // known networks first
        return a.signal > b.signal;
    });
}

// Connects to a saved network, or disconnects. Runs on a worker thread.
void WifiConnectAsync(std::wstring profile, bool disconnect) {
    std::thread([profile = std::move(profile), disconnect]() {
        HANDLE wlan = nullptr;
        DWORD negotiated = 0;
        if (WlanOpenHandle(2, nullptr, &negotiated, &wlan) != ERROR_SUCCESS) return;
        GUID guid{};
        if (FirstWlanInterface(wlan, &guid)) {
            if (disconnect) {
                WlanDisconnect(wlan, &guid, nullptr);
            } else {
                WLAN_CONNECTION_PARAMETERS p{};
                p.wlanConnectionMode = wlan_connection_mode_profile;
                p.strProfile = profile.c_str();
                p.dot11BssType = dot11_BSS_type_any;
                WlanConnect(wlan, &guid, &p, nullptr);
            }
        }
        WlanCloseHandle(wlan, nullptr);
        Sleep(1500);  // let Windows associate before the list refreshes
        if (g_connectivityWake) SetEvent(g_connectivityWake);
    }).detach();
}

// ── Bluetooth audio connect / disconnect ────────────────────────────────────
// Windows has no public "connect this paired device" API. Audio devices are the
// exception: their driver takes one-shot reconnect / disconnect requests through
// IKsControl on the endpoint's KS filter, which is what Windows' own Connect
// button does for headphones. Declared here to avoid ksproxy.h.
struct BbKsProperty {
    GUID set;
    ULONG id;
    ULONG flags;
};
struct IBbKsControl : public IUnknown {
    virtual HRESULT STDMETHODCALLTYPE KsProperty(void* property, ULONG propertyLength, void* data, ULONG dataLength, ULONG* returned) = 0;
    virtual HRESULT STDMETHODCALLTYPE KsMethod(void* method, ULONG methodLength, void* data, ULONG dataLength, ULONG* returned) = 0;
    virtual HRESULT STDMETHODCALLTYPE KsEvent(void* event, ULONG eventLength, void* data, ULONG dataLength, ULONG* returned) = 0;
};
constexpr IID kIID_IKsControl = {0x28F54685, 0x06FD, 0x11D2, {0xB2, 0x7A, 0x00, 0xA0, 0xC9, 0x22, 0x31, 0x96}};
constexpr GUID kKsPropSetBtAudio = {0x7FA06C40, 0xB8F6, 0x4C7E, {0x85, 0x56, 0xE8, 0xC3, 0x3A, 0x12, 0xE5, 0x4D}};
constexpr ULONG kKsBtOneshotReconnect = 0;
constexpr ULONG kKsBtOneshotDisconnect = 1;
constexpr ULONG kKsPropertyTypeGet = 1;

struct BtAudioEndpoint {
    std::wstring name;   // the device's name, matches the Bluetooth device name
    std::wstring ksId;   // KS filter behind the endpoint
    bool active = false; // connected
};

// Audio endpoints backed by a Bluetooth driver, connected or not. COM must be
// initialised on the calling thread.
std::vector<BtAudioEndpoint> EnumBtAudioEndpoints() {
    std::vector<BtAudioEndpoint> out;
    ComPtr<IMMDeviceEnumerator> enumerator;
    if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) return out;
    ComPtr<IMMDeviceCollection> all;
    if (FAILED(enumerator->EnumAudioEndpoints(eAll, DEVICE_STATE_ACTIVE | DEVICE_STATE_UNPLUGGED, &all))) return out;
    UINT count = 0;
    all->GetCount(&count);
    for (UINT i = 0; i < count; ++i) {
        ComPtr<IMMDevice> endpoint;
        if (FAILED(all->Item(i, &endpoint))) continue;
        ComPtr<IDeviceTopology> topology;
        ComPtr<IConnector> connector;
        LPWSTR ksId = nullptr;
        if (FAILED(endpoint->Activate(__uuidof(IDeviceTopology), CLSCTX_ALL, nullptr, reinterpret_cast<void**>(topology.GetAddressOf()))) ||
            FAILED(topology->GetConnector(0, &connector)) || FAILED(connector->GetDeviceIdConnectedTo(&ksId)) || !ksId) {
            continue;
        }
        BtAudioEndpoint e;
        e.ksId = ksId;
        CoTaskMemFree(ksId);
        if (ToLowerCopy(e.ksId).find(L"bth") == std::wstring::npos) continue;  // BTHENUM / BTHHFENUM
        DWORD state = 0;
        endpoint->GetState(&state);
        e.active = state == DEVICE_STATE_ACTIVE;
        ComPtr<IPropertyStore> props;
        if (SUCCEEDED(endpoint->OpenPropertyStore(STGM_READ, &props))) {
            PROPVARIANT v;
            PropVariantInit(&v);
            if (SUCCEEDED(props->GetValue(PKEY_DeviceInterface_FriendlyName, &v)) && v.vt == VT_LPWSTR) e.name = v.pwszVal;
            PropVariantClear(&v);
        }
        if (!e.name.empty()) out.push_back(std::move(e));
    }
    return out;
}

void BtAudioConnectAsync(std::wstring name, bool connect) {
    std::thread([name = std::move(name), connect]() {
        const HRESULT co = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ComPtr<IMMDeviceEnumerator> enumerator;
        if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, IID_PPV_ARGS(&enumerator)))) {
            std::vector<std::wstring> done;  // one request per KS filter (a headset has several endpoints)
            for (const BtAudioEndpoint& e : EnumBtAudioEndpoints()) {
                if (e.name != name || std::find(done.begin(), done.end(), e.ksId) != done.end()) continue;
                done.push_back(e.ksId);
                ComPtr<IMMDevice> filter;
                ComPtr<IBbKsControl> ks;
                if (SUCCEEDED(enumerator->GetDevice(e.ksId.c_str(), &filter)) &&
                    SUCCEEDED(filter->Activate(kIID_IKsControl, CLSCTX_ALL, nullptr, reinterpret_cast<void**>(ks.GetAddressOf()))) && ks) {
                    BbKsProperty prop{kKsPropSetBtAudio, connect ? kKsBtOneshotReconnect : kKsBtOneshotDisconnect, kKsPropertyTypeGet};
                    ULONG returned = 0;
                    ks->KsProperty(&prop, sizeof(prop), nullptr, 0, &returned);
                }
            }
        }
        if (SUCCEEDED(co)) CoUninitialize();
        Sleep(2500);  // Bluetooth audio takes a moment to (dis)connect
        if (g_connectivityWake) SetEvent(g_connectivityWake);
    }).detach();
}

void SampleBluetoothList(ConnectivitySnapshot& c) {
    if (c.bluetoothRadio != 1) return;
    const std::vector<BtAudioEndpoint> audio = EnumBtAudioEndpoints();
#if DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER
    try {
        using namespace winrt::Windows::Devices::Bluetooth;
        using winrt::Windows::Devices::Enumeration::DeviceInformation;
        for (const DeviceInformation& d : DeviceInformation::FindAllAsync(BluetoothDevice::GetDeviceSelectorFromPairingState(true)).get()) {
            BluetoothListDevice b;
            b.name = d.Name();
            if (b.name.empty()) continue;
            try {
                if (const BluetoothDevice dev = BluetoothDevice::FromIdAsync(d.Id()).get())
                    b.connected = dev.ConnectionStatus() == BluetoothConnectionStatus::Connected;
            } catch (...) {
            }
            for (const BtAudioEndpoint& e : audio) {
                if (e.name != b.name) continue;
                b.audio = true;
                b.connected = b.connected || e.active;  // the audio state is the one that matters for headphones
            }
            c.bluetoothList.push_back(std::move(b));
        }
        for (const DeviceInformation& d : DeviceInformation::FindAllAsync(BluetoothLEDevice::GetDeviceSelectorFromPairingState(true)).get()) {
            const std::wstring name(d.Name());
            if (name.empty() || std::any_of(c.bluetoothList.begin(), c.bluetoothList.end(), [&](const BluetoothListDevice& e) { return e.name == name; }))
                continue;
            BluetoothListDevice b;
            b.name = name;
            try {
                if (const BluetoothLEDevice dev = BluetoothLEDevice::FromIdAsync(d.Id()).get())
                    b.connected = dev.ConnectionStatus() == BluetoothConnectionStatus::Connected;
            } catch (...) {
            }
            c.bluetoothList.push_back(std::move(b));
        }
    } catch (...) {
    }
#endif
    std::stable_sort(c.bluetoothList.begin(), c.bluetoothList.end(), [](const BluetoothListDevice& a, const BluetoothListDevice& b) {
        if (a.connected != b.connected) return a.connected;
        return a.audio && !b.audio;  // connectable devices next
    });
}

// ── Radios ──────────────────────────────────────────────────────────────────
// Turns the Wi-Fi or Bluetooth radio on or off, like the quick-settings tiles.
// Falls back to the Settings page when Windows refuses (policy, or no access).
void SetRadioAsync(bool bluetooth, bool on) {
    std::thread([bluetooth, on]() {
        bool done = false;
#if DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
        try {
            using namespace winrt::Windows::Devices::Radios;
            if (Radio::RequestAccessAsync().get() == RadioAccessStatus::Allowed) {
                for (const Radio& radio : Radio::GetRadiosAsync().get()) {
                    if (radio.Kind() == (bluetooth ? RadioKind::Bluetooth : RadioKind::WiFi)) {
                        done = radio.SetStateAsync(on ? RadioState::On : RadioState::Off).get() == RadioAccessStatus::Allowed;
                    }
                }
            }
        } catch (...) {
        }
        winrt::uninit_apartment();
#endif
        if (!done) {
            ShellExecuteW(nullptr, L"open", bluetooth ? L"ms-settings:bluetooth" : L"ms-settings:network-wifi", nullptr, nullptr, SW_SHOWNORMAL);
        }
        Sleep(800);
        if (g_connectivityWake) SetEvent(g_connectivityWake);
    }).detach();
}

inline std::wstring CleanBluetoothName(std::wstring name) {
    const wchar_t* suffixes[] = {
        L" Hands-Free AG", L" Hands-Free HF", L" Hands-Free",
        L" Avrcp Transport", L" A2DP SNK", L" Stereo"
    };
    for (const wchar_t* suf : suffixes) {
        size_t len = wcslen(suf);
        if (name.size() > len && name.compare(name.size() - len, len, suf) == 0) {
            name.erase(name.size() - len);
        }
    }
    return name;
}

// Queries connected Bluetooth device name and battery percentage (0-100) via SetupAPI
inline void QueryBluetoothBattery(std::wstring& outName, int& outBattery) {
    outName.clear();
    outBattery = -1;
    static const DEVPROPKEY kDevPropKeyBattery = {
        {0x104ea319, 0x6ee2, 0x4701, {0xbd, 0x47, 0x8d, 0xdb, 0xf4, 0x25, 0xbb, 0xe5}}, 2};
    static const DEVPROPKEY kDevPropKeyFriendlyName = {
        {0xa45c254e, 0xdf1c, 0x4efd, {0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0}}, 14};
    static const DEVPROPKEY kDevPropKeyIsConnected = {
        {0x83da6326, 0x97a6, 0x4088, {0x94, 0x53, 0xa1, 0x92, 0x3f, 0x57, 0x3b, 0x29}}, 15};

    const wchar_t* enumerators[] = {L"BTHENUM", L"BTHLE"};
    for (const wchar_t* en : enumerators) {
        HDEVINFO hDevInfo = SetupDiGetClassDevsW(nullptr, en, nullptr, DIGCF_ALLCLASSES | DIGCF_PRESENT);
        if (hDevInfo == INVALID_HANDLE_VALUE) continue;

        SP_DEVINFO_DATA devData{};
        devData.cbSize = sizeof(devData);
        for (DWORD i = 0; SetupDiEnumDeviceInfo(hDevInfo, i, &devData); ++i) {
            DEVPROPTYPE propType = 0;
            DEVPROP_BOOLEAN isConnected = DEVPROP_FALSE;
            if (SetupDiGetDevicePropertyW(hDevInfo, &devData, &kDevPropKeyIsConnected, &propType,
                                          reinterpret_cast<PBYTE>(&isConnected), sizeof(isConnected), nullptr, 0)) {
                if (isConnected == DEVPROP_FALSE) continue;
            }
            BYTE batteryByte = 0;
            if (SetupDiGetDevicePropertyW(hDevInfo, &devData, &kDevPropKeyBattery, &propType,
                                          &batteryByte, sizeof(batteryByte), nullptr, 0)) {
                if (batteryByte <= 100) {
                    wchar_t nameBuf[256] = {};
                    if (SetupDiGetDevicePropertyW(hDevInfo, &devData, &kDevPropKeyFriendlyName, &propType,
                                                  reinterpret_cast<PBYTE>(nameBuf), sizeof(nameBuf), nullptr, 0)) {
                        outName = CleanBluetoothName(nameBuf);
                        outBattery = static_cast<int>(batteryByte);
                        SetupDiDestroyDeviceInfoList(hDevInfo);
                        return;
                    }
                }
            }
        }
        SetupDiDestroyDeviceInfoList(hDevInfo);
    }
}

static ConnectivitySnapshot SampleConnectivity(HANDLE& wlan) {
    ConnectivitySnapshot c;
    c.sampled = true;

    if (!wlan) {
        DWORD negotiated = 0;
        if (WlanOpenHandle(2, nullptr, &negotiated, &wlan) != ERROR_SUCCESS) {
            wlan = nullptr;
        }
    }
    if (wlan) {
        PWLAN_INTERFACE_INFO_LIST list = nullptr;
        if (WlanEnumInterfaces(wlan, nullptr, &list) == ERROR_SUCCESS && list) {
            for (DWORD i = 0; i < list->dwNumberOfItems; ++i) {
                const WLAN_INTERFACE_INFO& info = list->InterfaceInfo[i];
                if (c.wifiRadio < 0) c.wifiRadio = 0;

                DWORD size = 0;
                PWLAN_RADIO_STATE radio = nullptr;
                if (WlanQueryInterface(wlan, &info.InterfaceGuid, wlan_intf_opcode_radio_state, nullptr, &size,
                                       reinterpret_cast<PVOID*>(&radio), nullptr) == ERROR_SUCCESS && radio) {
                    for (DWORD phy = 0; phy < radio->dwNumberOfPhys; ++phy) {
                        if (radio->PhyRadioState[phy].dot11SoftwareRadioState == dot11_radio_state_on &&
                            radio->PhyRadioState[phy].dot11HardwareRadioState == dot11_radio_state_on) {
                            c.wifiRadio = 1;
                        }
                    }
                    WlanFreeMemory(radio);
                } else {
                    c.wifiRadio = 1;  // adapter present, state unknown: assume on
                }

                if (info.isState == wlan_interface_state_connected) {
                    c.wifiConnected = true;
                    c.wifiRadio = 1;
                    PWLAN_CONNECTION_ATTRIBUTES conn = nullptr;
                    // Windows 11 24H2+ withholds the network name unless location
                    // access is on for desktop apps; the card then just says Connected.
                    if (WlanQueryInterface(wlan, &info.InterfaceGuid, wlan_intf_opcode_current_connection, nullptr, &size,
                                           reinterpret_cast<PVOID*>(&conn), nullptr) == ERROR_SUCCESS && conn) {
                        const DOT11_SSID& ssid = conn->wlanAssociationAttributes.dot11Ssid;
                        c.wifiName = WideFromBytes(ssid.ucSSID, std::min<ULONG>(ssid.uSSIDLength, DOT11_SSID_MAX_LENGTH));
                        c.wifiSignal = static_cast<int>(conn->wlanAssociationAttributes.wlanSignalQuality);
                        WlanFreeMemory(conn);
                    }
                }
            }
            WlanFreeMemory(list);
        }
    }

    ULONG64 ignoredIn = 0, ignoredOut = 0;
    ReadPhysicalOctets(ignoredIn, ignoredOut, &c.ethernetConnected);

#if DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER
    try {
        using namespace winrt::Windows::Devices::Radios;
        for (const Radio& radio : Radio::GetRadiosAsync().get()) {
            if (radio.Kind() == RadioKind::Bluetooth) {
                c.bluetoothRadio = radio.State() == RadioState::On ? 1 : 0;
            }
        }
        if (c.bluetoothRadio == 1) {
            using winrt::Windows::Devices::Bluetooth::BluetoothConnectionStatus;
            using winrt::Windows::Devices::Bluetooth::BluetoothDevice;
            using winrt::Windows::Devices::Bluetooth::BluetoothLEDevice;
            using winrt::Windows::Devices::Enumeration::DeviceInformation;
            const winrt::hstring selectors[] = {
                BluetoothDevice::GetDeviceSelectorFromConnectionStatus(BluetoothConnectionStatus::Connected),
                BluetoothLEDevice::GetDeviceSelectorFromConnectionStatus(BluetoothConnectionStatus::Connected),
            };
            for (const auto& selector : selectors) {
                for (const DeviceInformation& device : DeviceInformation::FindAllAsync(selector).get()) {
                    std::wstring name(device.Name());
                    if (!name.empty() &&
                        std::find(c.bluetoothDevices.begin(), c.bluetoothDevices.end(), name) == c.bluetoothDevices.end()) {
                        c.bluetoothDevices.push_back(std::move(name));
                    }
                }
            }
        }
    } catch (...) {
        // Radio access can be blocked by policy; the card then shows "Unavailable".
    }
#endif

    // Bunbun: connected Bluetooth device & battery level (for Studio System page)
    QueryBluetoothBattery(c.connectedBtName, c.connectedBtBattery);
    if (c.connectedBtName.empty()) {
        for (const auto& dev : c.bluetoothList) {
            if (dev.connected) {
                c.connectedBtName = CleanBluetoothName(dev.name);
                break;
            }
        }
    }

    // Bunbun: battery. BatteryFlag 128 = no system battery (a desktop), 255 = unknown.
    SYSTEM_POWER_STATUS ps{};
    if (GetSystemPowerStatus(&ps) && ps.BatteryFlag != 128 && ps.BatteryFlag != 255 && ps.BatteryLifePercent <= 100) {
        c.hasBattery = true;
        c.batteryPercent = ps.BatteryLifePercent;
        c.onPower = ps.ACLineStatus == 1;
        c.charging = (ps.BatteryFlag & 8) != 0;
        c.batterySaver = (ps.SystemStatusFlag & 1) != 0;
        c.batterySecondsLeft = ps.BatteryLifeTime;
#if DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER
        // Charge rate and capacities: time to full, and wear (full vs. design capacity).
        try {
            const auto report = winrt::Windows::Devices::Power::Battery::AggregateBattery().GetReport();
            if (const auto v = report.ChargeRateInMilliwatts()) c.chargeRateMw = v.Value();
            if (const auto v = report.RemainingCapacityInMilliwattHours()) c.remainingMwh = v.Value();
            if (const auto v = report.FullChargeCapacityInMilliwattHours()) c.fullMwh = v.Value();
            if (const auto v = report.DesignCapacityInMilliwattHours()) c.designMwh = v.Value();
        } catch (...) {
            // Some firmware doesn't report these; the card then skips the extras.
        }
#endif
    }

    // Long-press lists, fetched only while one is on screen.
    const int list = g_connectivityListWanted.load(std::memory_order_relaxed);
    if (list == 0 && wlan) SampleWifiNetworks(wlan, c);
    if (list == 1) SampleBluetoothList(c);
    return c;
}

DWORD WINAPI ConnectivityThreadProc(void*) {
    winrt::init_apartment(winrt::apartment_type::multi_threaded);
    HANDLE wlan = nullptr;
    const HANDLE waits[2] = {g_stopEvent, g_connectivityWake};
    DWORD timeout = 0;  // sample once straight away
    for (;;) {
        const DWORD r = WaitForMultipleObjects(g_connectivityWake ? 2 : 1, waits, FALSE, timeout);
        if (r == WAIT_OBJECT_0 || r == WAIT_FAILED) {
            break;
        }
        ConnectivitySnapshot snapshot = SampleConnectivity(wlan);
        {
            std::lock_guard lock(g_stateMutex);
            g_state.connectivity = std::move(snapshot);
        }
        g_layoutDirty = true;
        const bool wanted = NowSeconds() - g_connectivityWantedAt.load(std::memory_order_relaxed) < 3.0;
        timeout = wanted ? 2000 : INFINITE;
    }
    if (wlan) WlanCloseHandle(wlan, nullptr);
    winrt::uninit_apartment();
    return 0;
}

// ── Input: click toggles, long-press lists, right-click opens Settings ───────
// All of this runs on the render thread (the window proc).

const wchar_t* ConnectivitySettingsPage(int card, bool wired) {
    switch (card) {
        case 0: return wired ? L"ms-settings:network-ethernet" : L"ms-settings:network-wifi";
        case 1: return L"ms-settings:bluetooth";
        case 2: return L"ms-settings:notifications";
        default: return L"ms-settings:batterysaver";  // Power & battery
    }
}

// In the list view: a row index, kListBack, kListSettings, or -1.
constexpr int kListBack = -2;
constexpr int kListSettings = -3;
int ConnectivityListHitTest(const MediaContentPoint& pt, int rows) {
    using namespace ConnectivityLayout;
    if (!pt.valid || pt.height <= MediaLayout::kExpandedMinHeight) return -1;
    if (pt.y < kHeaderBottom) {
        if (pt.x >= kPadX - 6.0f && pt.x <= kPadX + kBackWidth) return kListBack;
        if (pt.x >= pt.width - kPadX - kSettingsLinkWidth) return kListSettings;
        return -1;
    }
    if (pt.x < kPadX || pt.x > pt.width - kPadX || pt.y < kListTop) return -1;
    const int row = static_cast<int>((pt.y - kListTop) / kRowHeight);
    return row < std::min(rows, kMaxRows) ? row : -1;
}

void CloseConnectivityList() {
    g_connectivityList = -1;
    g_connectivityListWanted.store(-1, std::memory_order_relaxed);
    g_layoutDirty = true;
}

// Button down on the Connectivity tab: a Wi-Fi or Bluetooth card starts the
// long-press timer. Returns true when it took the press.
bool ConnectivityOnButtonDown(HWND hwnd, const MediaContentPoint& cp) {
    g_connectivityPressCard = -1;
    g_connectivityLongFired = false;
    if (g_connectivityList >= 0) return false;
    const int card = ConnectivityCardAtContentPoint(cp);
    if (card != 0 && card != 1) return false;
    bool wired = false;
    {
        std::lock_guard lock(g_stateMutex);
        wired = !g_state.connectivity.wifiConnected && g_state.connectivity.ethernetConnected;
    }
    if (card == 0 && wired) return false;  // Ethernet has no list
    g_connectivityPressCard = card;
    SetTimer(hwnd, kConnectivityHoldTimer, kConnectivityHoldMs, nullptr);
    return true;
}

// The hold timer: still pressed, so open that card's list.
bool ConnectivityOnTimer(HWND hwnd, WPARAM id) {
    if (id != kConnectivityHoldTimer) return false;
    KillTimer(hwnd, kConnectivityHoldTimer);
    if (g_connectivityPressCard >= 0 && (GetKeyState(VK_LBUTTON) & 0x8000)) {
        g_connectivityList = g_connectivityPressCard;
        g_connectivityListWanted.store(g_connectivityList, std::memory_order_relaxed);
        g_connectivityLongFired = true;
        if (g_connectivityWake) SetEvent(g_connectivityWake);
        g_layoutDirty = true;
    }
    g_connectivityPressCard = -1;
    return true;
}

// Button up on the Connectivity tab. Cards toggle (Do Not Disturb and Battery
// open Settings: Windows has no public switch for DND), list rows connect or
// disconnect. Returns true when it handled the click.
bool ConnectivityOnButtonUp(HWND hwnd, const MediaContentPoint& cp) {
    KillTimer(hwnd, kConnectivityHoldTimer);
    g_connectivityPressCard = -1;
    if (g_connectivityLongFired) {  // the press opened a list; the release is part of it
        g_connectivityLongFired = false;
        return true;
    }

    ConnectivitySnapshot c;
    {
        std::lock_guard lock(g_stateMutex);
        c = g_state.connectivity;
    }

    if (g_connectivityList >= 0) {
        const bool wifi = g_connectivityList == 0;
        const int rows = static_cast<int>(wifi ? c.wifiNetworks.size() : c.bluetoothList.size());
        const int hit = ConnectivityListHitTest(cp, rows);
        if (hit == kListBack) {
            CloseConnectivityList();
            return true;
        }
        if (hit == kListSettings) {
            ShellExecuteW(nullptr, L"open", wifi ? L"ms-settings:network-wifi" : L"ms-settings:bluetooth", nullptr, nullptr, SW_SHOWNORMAL);
            return true;
        }
        if (hit < 0) return true;  // inside the list but not on a row: nothing to do
        if (wifi) {
            const WifiNetwork& n = c.wifiNetworks[static_cast<size_t>(hit)];
            if (n.connected) {
                WifiConnectAsync({}, true);
            } else if (!n.profile.empty()) {
                WifiConnectAsync(n.profile, false);
            } else {
                // A network Windows has no password for: its own flyout asks for one.
                ShellExecuteW(nullptr, L"open", L"ms-availablenetworks:", nullptr, nullptr, SW_SHOWNORMAL);
                return true;
            }
            g_connectivityBusyName = n.ssid;
        } else {
            const BluetoothListDevice& d = c.bluetoothList[static_cast<size_t>(hit)];
            if (!d.audio) {
                ShellExecuteW(nullptr, L"open", L"ms-settings:bluetooth", nullptr, nullptr, SW_SHOWNORMAL);
                return true;
            }
            BtAudioConnectAsync(d.name, !d.connected);
            g_connectivityBusyName = d.name;
        }
        g_connectivityBusyUntil = NowSeconds() + 6.0;
        g_layoutDirty = true;
        return true;
    }

    const int card = ConnectivityCardAtContentPoint(cp);
    if (card < 0) return false;
    const bool wired = !c.wifiConnected && c.ethernetConnected;
    if (card == 0 && !wired && c.wifiRadio >= 0) {
        SetRadioAsync(false, c.wifiRadio != 1);
    } else if (card == 1 && c.bluetoothRadio >= 0) {
        SetRadioAsync(true, c.bluetoothRadio != 1);
    } else {
        ShellExecuteW(nullptr, L"open", ConnectivitySettingsPage(card, wired), nullptr, nullptr, SW_SHOWNORMAL);
    }
    return true;
}

// Right-click on a card opens its Windows Settings page.
bool ConnectivityOnRightClick(const MediaContentPoint& cp) {
    if (g_connectivityList >= 0) return false;
    const int card = ConnectivityCardAtContentPoint(cp);
    if (card < 0) return false;
    bool wired = false;
    {
        std::lock_guard lock(g_stateMutex);
        wired = !g_state.connectivity.wifiConnected && g_state.connectivity.ethernetConnected;
    }
    ShellExecuteW(nullptr, L"open", ConnectivitySettingsPage(card, wired), nullptr, nullptr, SW_SHOWNORMAL);
    return true;
}
