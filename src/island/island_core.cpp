// Bunbun island core — native Direct2D Dynamic Island for Windows.
// Settings schema lives in src/island/settings_schema.yaml.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif

#include "wh_shim.h"
#include "island.h"
#include "../mascot/mascot.h"
#include "../assistant/assistant.h"
#include "../common/todos.h"  // Bunbun: to-do file and repeats, shared with the settings app
#include "../common/timers.h"  // Bunbun: the Timers page's rules, shared with Linux
#include "../common/json.h"   // Bunbun: talking to the local AI helper
#include "../common/applist.h"  // Bunbun: app lists typed in Settings (full screen, music / video)
#include "../common/chime_wav.h"  // Bunbun: the UI chimes, shared with Linux
#include "../common/ics.h"        // Bunbun: calendar links (.ics), shared with Linux
#include "../common/ag_quota.h"   // Bunbun: Antigravity's quota, shared with Linux
#include "../common/agent_limits.h"  // Bunbun: what the Agent page's limits row shows per agent
#include "../agents/hook_installer.h"  // Bunbun: which agents are connected (studio_module.inc)
#include "../agents/agent_diff.h"      // Bunbun: the approval card's diff lines, shared with Linux
#include "../common/water.h"           // Bunbun: the water reminder's rules, shared with Linux
#include "../common/work.h"            // Bunbun: Work mode's schedule and day log, shared with Linux
#include "../common/screentime.h"      // Bunbun: Screen Time's bookkeeping (screentime_module.inc)
#include "../common/edgelight.h"       // Bunbun: what plays the edge light (edgelight_module.inc)
#include "../updater.h"  // Bunbun: one-click updates from the island's update notice
#include "studio_icons.h"  // Bunbun: Studio page model and icons, shared with the Linux island
#include <windows.h>
#include <unknwn.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shlwapi.h>  // Bunbun: SHCreateMemStream for the preview snapshot
#include <ole2.h>
#include <shlobj.h>
#include <sddl.h>
#include <setupapi.h>
#include <devpropdef.h>
#include <dbt.h>
#include <d2d1.h>
#include <dwrite.h>
#include <wincodec.h>
#include <shcore.h>
#include <windowsx.h>
#include <audioclient.h>
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <devicetopology.h>                // Bunbun: Bluetooth audio connect / disconnect
#include <functiondiscoverykeys_devpkey.h>
#include <mmreg.h>
#include <mmsystem.h>
#include <objbase.h>
#include <wrl/client.h>
#include <uiautomation.h>
#include <winhttp.h>
#include <pdh.h>
#include <pdhmsg.h>
#include <winsock2.h>  // Bunbun: ws2ipdef.h must precede iphlpapi.h for GetIfTable2 / MIB_IF_ROW2
#include <ws2ipdef.h>
#include <iphlpapi.h>  // Bunbun: per-adapter byte counters (network speed, Ethernet state)
#include <wlanapi.h>   // Bunbun: Wi-Fi radio, network name and signal for the Connectivity tab
#include <propsys.h>   // Bunbun: a window's app ID, to find a player's window
#include <appmodel.h>  // Bunbun: a process's app ID and package
#include <tlhelp32.h>  // Bunbun: screen recorders running (privacy_module.inc)

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cwchar>
#include <cstring>
#include <memory>
#include <mutex>
#include <optional>
#include <functional>
#include <regex>   // Bunbun: meeting links in calendar events
#include <string>
#include <string_view>
#include <condition_variable>
#include <thread>
#include <future>
#include <unordered_map>
#include <unordered_set>  // Bunbun: connected Bluetooth devices
#include <mfapi.h>        // Bunbun: Mirror page camera (mirror_module.inc)
#include <mfidl.h>
#include <mfreadwrite.h>
#include <utility>
#include <vector>
#include <set>
#include <winrt/base.h>
#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#if __has_include(<winrt/Windows.UI.Notifications.Management.h>) && \
    __has_include(<winrt/Windows.UI.Notifications.h>)
#define DYNAMIC_ISLAND_HAS_USER_NOTIFICATION_LISTENER 1
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>
#else
#define DYNAMIC_ISLAND_HAS_USER_NOTIFICATION_LISTENER 0
#endif

#if __has_include(<winrt/Windows.Devices.Enumeration.h>) && \
    __has_include(<winrt/Windows.Devices.Bluetooth.h>) && \
    __has_include(<winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>)
#define DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER 1
#include <winrt/Windows.Devices.Enumeration.h>
#include <winrt/Windows.Devices.Bluetooth.h>
#include <winrt/Windows.Devices.Bluetooth.GenericAttributeProfile.h>
#include <winrt/Windows.Devices.Radios.h>  // Bunbun: Bluetooth radio on/off
#include <winrt/Windows.Devices.Power.h>   // Bunbun: battery report (charge rate, wear)
#include <winrt/Windows.Devices.Geolocation.h>  // Bunbun: where "here" is, for the weather
#else
#define DYNAMIC_ISLAND_HAS_BLUETOOTH_WATCHER 0
#endif

using Microsoft::WRL::ComPtr;
using namespace std::chrono_literals;

namespace {

constexpr wchar_t kWindowClass[] = L"Bunbun.IslandOverlay";
constexpr UINT WM_APP_LAYOUT_CHANGED = WM_APP + 0x442;
constexpr UINT WM_APP_NEW_EVENT = WM_APP + 0x443;
constexpr UINT WM_APP_MOUSE_WAKE = WM_APP + 0x446;
// RegisterHotKey cannot associate a hot key with a window created by another
// thread, and the same applies to tearing one down. LoadSettings runs on
// the settings thread while the overlay window belongs to the render
// thread, so hotkey and backdrop changes are posted across and applied there.
constexpr UINT WM_APP_APPLY_HOTKEY = WM_APP + 0x447;
constexpr UINT WM_APP_APPLY_BACKDROP = WM_APP + 0x448;
constexpr UINT WM_APP_SUBMIT_ASK = WM_APP + 0x4B2;
constexpr UINT WM_APP_TEST_HITS = WM_APP + 0x4B3;  // Bunbun: IslandTestHits, answered on the render thread
constexpr UINT WM_APP_TEST_CLOSE = WM_APP + 0x4B4;  // Bunbun: IslandTestOpen(-1) also closes Ask Bun
constexpr int ID_HIDE_SHOW_HOTKEY = 1;
constexpr int ID_LOOKUP_HOTKEY = 2;
constexpr int ID_TUCK_HOTKEY = 3;  // Bunbun: Ctrl+Alt+H tucks the island away
constexpr float kRenderPadX = 28.0f;
constexpr float kRenderPadY = 22.0f;
constexpr UINT kClipboardImageThumbMaxDim = 160;  // longer-side cap for the clipboard image thumbnail

#include "modules/layout_constants.inc"  // Bunbun: Geometry constants for the media, immersive, tray, idle, pill, game and shape layouts

#include "modules/enums_themes.inc"  // Bunbun: Content points, enums (kinds, positions, accents) and the theme palettes

#include "modules/settings_struct.inc"  // Bunbun: The Settings struct

#include "modules/snapshot_types.inc"  // Bunbun: Pixel buffers and the media, battery, timer and system snapshots

#include "modules/bunbun_types.inc"  // Bunbun: types for the Bunbun modules

#include "modules/state_types.inc"  // Bunbun: Connectivity, weather, lyrics, Quick Lookup and agent state types

#include "modules/shared_state.inc"  // Bunbun: SharedState, springs, settings copies, power saving and tab / source helpers

#include "modules/localization_clock.inc"  // Bunbun: UI language, Loc() and the clock / date presentation

#include "modules/media_hit_test.inc"  // Bunbun: Media dashboard hit-testing on content coordinates

#include "modules/dock_colors_monitors.inc"  // Bunbun: Source dock, colour maths, fullscreen checks, monitors and accent

#include "modules/settings_load.inc"  // Bunbun: LoadSettings: reading settings.ini into the Settings struct


void EnableBlurBehind(HWND hwnd) {
    DWM_BLURBEHIND blur = {};
    blur.dwFlags = DWM_BB_ENABLE;
    blur.fEnable = FALSE;
    DwmEnableBlurBehindWindow(hwnd, &blur);
}

#include "modules/backdrop_dwm.inc"  // Bunbun: Backdrop material, DWM blur windows and the hide/show hotkey

// ── Fixed stage ──────────────────────────────────────────────────────────────
// The layered window is one fixed size (the largest pill the island can draw) and
// the pill animates inside it. The window never resizes or moves while animating.
// Split layouts (two alert pills) are the widest case, Quick Lookup the tallest.
constexpr float kStageContentWidth = 880.0f;   // content px, multiplied by sizeScale
constexpr float kStageContentHeight = 320.0f;

// Set to false to force a full repaint every frame (useful if you ever see ghosting).
constexpr bool kUseDirtyRect = true;

struct StageLayout {
    POINT origin = {0, 0};   // screen position of the window
    int width = 0;           // window size in px
    int height = 0;
    float pillLeft = 0.0f;   // pill's left edge inside the window (fractional)
    float pillTop = 0.0f;    // pill's top edge inside the window, before the nudge
};

// Where the pill was last drawn, in client px. Written by Renderer::Render and read
// by WM_NCHITTEST. Both run on the render thread, so no lock is needed.
struct PillClientRect {
    float left = 0.0f, top = 0.0f, right = 0.0f, bottom = 0.0f;
    bool valid = false;
};
PillClientRect g_pillClientRect;

// The island always uses the fixed stage; the backdrop is painted by separate DWM
// windows (see DwmBackdrop) and does not depend on the window size.
inline bool UsesFixedStage(const Settings&) {
    return true;
}

StageLayout ComputeStageLayout(const Settings& settings, float pillW, float pillH) {
    StageLayout s;
    const bool flushTop = settings.notchStyle || settings.borderMergedMode;
    pillW = std::max(pillW, 0.0f);

    if (!UsesFixedStage(settings)) {
        // Legacy path, identical to what Render() did before.
        s.height = std::max(1, static_cast<int>(std::ceil(pillH + kRenderPadY * 2.0f)));
        float subX = 0.0f;
        s.origin = ComputeOverlayOrigin(s.height, pillW, &subX);
        s.width = std::max(1, static_cast<int>(std::ceil(pillW + subX + kRenderPadX * 2.0f)));
        s.pillLeft = kRenderPadX + subX;
        s.pillTop = flushTop ? 0.0f : kRenderPadY;
        return s;
    }

    const RECT work = GetAnchorWorkRect();
    const int workW = work.right - work.left;
    s.width = static_cast<int>(std::ceil(kStageContentWidth * settings.sizeScale + kRenderPadX * 2.0f));
    s.height = static_cast<int>(std::ceil(kStageContentHeight * settings.sizeScale + kRenderPadY * 2.0f));

    // Horizontal: the stage is anchored, and the pill sits inside it the same way
    // the old window-per-pill code anchored it on screen.
    switch (settings.position) {
        case Position::TopLeft:
        case Position::BottomLeft:
            s.origin.x = work.left + 16 - static_cast<int>(kRenderPadX);
            s.pillLeft = kRenderPadX;
            break;
        case Position::TopRight:
        case Position::BottomRight:
            s.origin.x = work.right - 16 + static_cast<int>(kRenderPadX) - s.width;
            s.pillLeft = static_cast<float>(s.width) - kRenderPadX - pillW;
            break;
        default:  // centered
            s.origin.x = work.left + (workW - s.width) / 2;
            s.pillLeft = (static_cast<float>(s.width) - pillW) * 0.5f;
            break;
    }
    s.origin.x += settings.offsetX;

    // Vertical. Bottom positions keep the pill's bottom edge fixed, so it grows upward.
    const int offsetY = ResolveOffsetY(pillH + kRenderPadY * 2.0f);
    if (IsBottomPosition(settings.position)) {
        s.origin.y = work.bottom - s.height - 40 + offsetY;
        s.pillTop = static_cast<float>(s.height) - kRenderPadY - pillH;
    } else {
        s.origin.y = (flushTop ? work.top : work.top + 8) + offsetY;
        s.pillTop = flushTop ? 0.0f : kRenderPadY;
    }
    return s;
}

#include "modules/display_mirrors.inc"  // Bunbun: Per-display values and the mirror islands on other displays

#include "modules/overlay_zorder.inc"  // Bunbun: Overlay z-order, topmost handling and the dock rect

#include "modules/icons_images.inc"  // Bunbun: Image decoding and app / window icon lookup

#include "modules/media_source.inc"  // Bunbun: Which media source (app) is playing: tracking and choice

#include "modules/notifications_bluetooth.inc"  // Bunbun: Notification listener, DND and Bluetooth device tracking

#include "modules/audio_spectrum.inc"  // Bunbun: Spectrum analyser and waveform samples

#include "modules/weather_module.inc"

#include "modules/lyrics_module.inc"
#include "modules/audio_battery_system.inc"  // Bunbun: Audio thread, battery, GPU and network snapshots

// Bunbun: connectivity sampler, Wi-Fi / Bluetooth lists and actions, battery report.
#include "modules/connectivity_module.inc"
// Bunbun: next meeting from the calendar link, and browser downloads.
#include "modules/calendar_module.inc"
#include "modules/downloads_module.inc"
// Bunbun: to-dos and reminders.
#include "modules/todos_module.inc"
// Bunbun: the water reminder (it shows the reminder pill above).
#include "modules/water_module.inc"
#include "modules/work_module.inc"  // Bunbun: Work mode (Bunbun.UseCase = work or both)

#include "modules/volume_brightness.inc"  // Bunbun: Endpoint volume notifications and the brightness snapshot

// A ConsentStore entry counts as in-use only when Windows has recorded a start
// and has not yet recorded a stop. Testing LastUsedTimeStop == 0 on its own
// also matched entries that had never been used at all (both timestamps zero),
// which is what left the microphone dot lit permanently for some users even
// though Windows' own privacy indicator showed nothing in use.
bool ConsentStoreEntryInUse(HKEY key) {
    uint64_t stopTime = 0;
    if (!ReadRegQword(key, L"LastUsedTimeStop", &stopTime) || stopTime != 0) {
        return false;
    }

    uint64_t startTime = 0;
    if (ReadRegQword(key, L"LastUsedTimeStart", &startTime)) {
        return startTime != 0;
    }

    // Stop says "not stopped" but there is no readable start timestamp. This is
    // the shape of a never-used entry, so it is reported as not in use, but log
    // it once: if some Windows build stores these differently it would otherwise
    // be an indicator that silently stops working with nothing to explain it.
    static std::atomic<bool> loggedMissingStart{false};
    if (!loggedMissingStart.exchange(true)) {
        Wh_Log(L"Privacy indicator: a ConsentStore entry has LastUsedTimeStop=0 but no "
               L"readable REG_QWORD LastUsedTimeStart; treating it as not in use.");
    }
    return false;
}

bool IsDeviceActiveViaRegistry(const wchar_t* capability, std::wstring* outAppName = nullptr) {
    bool isActive = false;
    std::wstring basePath = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\CapabilityAccessManager\\ConsentStore\\";
    basePath += capability;

    auto CheckSubkeys = [&](HKEY hKeyParent) -> bool {
        constexpr REGSAM kSam = KEY_READ | KEY_WOW64_64KEY;
        DWORD index = 0;
        wchar_t subKeyName[256];
        DWORD nameLen = ARRAYSIZE(subKeyName);
        while (RegEnumKeyExW(hKeyParent, index, subKeyName, &nameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
            HKEY hSub;
            if (RegOpenKeyExW(hKeyParent, subKeyName, 0, kSam, &hSub) == ERROR_SUCCESS) {
                if (_wcsicmp(subKeyName, L"NonPackaged") == 0) {
                    DWORD npIndex = 0;
                    wchar_t npSubKeyName[256];
                    DWORD npNameLen = ARRAYSIZE(npSubKeyName);
                    while (RegEnumKeyExW(hSub, npIndex, npSubKeyName, &npNameLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                        HKEY hNpSub;
                        if (RegOpenKeyExW(hSub, npSubKeyName, 0, kSam, &hNpSub) == ERROR_SUCCESS) {
                            if (ConsentStoreEntryInUse(hNpSub)) {
                                if (outAppName && outAppName->empty()) {
                                    *outAppName = CleanActiveAppName(npSubKeyName);
                                }
                                RegCloseKey(hNpSub);
                                RegCloseKey(hSub);
                                return true;
                            }
                            RegCloseKey(hNpSub);
                        }
                        npIndex++;
                        npNameLen = ARRAYSIZE(npSubKeyName);
                    }
                } else {
                    if (ConsentStoreEntryInUse(hSub)) {
                        if (outAppName && outAppName->empty()) {
                            *outAppName = CleanActiveAppName(subKeyName);
                        }
                        RegCloseKey(hSub);
                        return true;
                    }
                }
                RegCloseKey(hSub);
            }
            index++;
            nameLen = ARRAYSIZE(subKeyName);
        }
        return false;
    };

    HKEY hKey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, basePath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
        isActive = CheckSubkeys(hKey);
        RegCloseKey(hKey);
    }

    if (!isActive) {
        if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, basePath.c_str(), 0, KEY_READ | KEY_WOW64_64KEY, &hKey) == ERROR_SUCCESS) {
            isActive = CheckSubkeys(hKey);
            RegCloseKey(hKey);
        }
    }

    return isActive;
}

bool IsMicrophoneActive(std::wstring* outAppName = nullptr) {
    return IsDeviceActiveViaRegistry(L"microphone", outAppName);
}

bool IsCameraActive(std::wstring* outAppName = nullptr) {
    return IsDeviceActiveViaRegistry(L"webcam", outAppName);
}

void UpdateProgressSnapshot() {
    const int progress = Wh_GetIntValue(L"ProgressPercent", -1);
    std::lock_guard lock(g_stateMutex);
    g_state.progress.active = progress >= 0 && progress <= 100;
    g_state.progress.percent = ClampInt(progress, 0, 100);
}

// Bunbun: privacy dots (mic from live capture sessions, camera / screen from ConsentStore).
#include "modules/privacy_module.inc"

#include "modules/clipboard_capture.inc"  // Bunbun: Clipboard and shell-notification capture

#include "modules/lookup_helpers.inc"  // Bunbun: Quick Lookup rows and hit tests

// ── Quick Lookup ─────────────────────────────────────────────────────────────
// Press the Quick Lookup hotkey to look up whatever text is on the clipboard.
#include "modules/lookup_module.inc"
// ── Bunbun: agent card buttons ──────────────────────────────────────────────
bool FocusAgentApp(const bb::AgentView& agent);
bool FocusNotificationApp(const std::wstring& appName, const std::wstring& notifTitle);

#include "modules/click_handlers.inc"  // Bunbun: Agent and Lookup clicks, hotkey, status clicks and timers

// Bunbun: Focus mode (timer + mascot DND + held notifications + summary).
#include "modules/focus_module.inc"

#include "modules/media_transport_focus.inc"  // Bunbun: Media transport, window / app focusing and the media source menu items

// Bunbun: undo for Ask Bun's commands, and setting the screen brightness.
#include "modules/bun_actions.inc"
#include "modules/sounds_module.inc"  // Bunbun: little UI chimes
#include "modules/mirror_module.inc"  // Bunbun: camera preview page
#include "modules/sound_check_module.inc"  // Bunbun: the Mirror page's microphone and speaker test
#include "modules/color_picker_module.inc"  // Bunbun: pick a colour anywhere on screen
int FocusPickerMinutes();                                  // Bunbun: studio_module.inc
void AddFocusMinutes(int minutes);                         // Bunbun: studio_module.inc
void WriteBunbunSetting(const wchar_t* key, const wchar_t* value);  // Bunbun: studio_module.inc
#include "modules/screentime_module.inc"
#include "modules/timers_module.inc"  // Bunbun: the Timers page (Pomodoro, countdown, stopwatch, water)
#include "modules/edgelight_module.inc"  // Bunbun: the light along the island's outline
#include "modules/studio_module.inc"  // Bunbun: Studio layout, extra pages, tuck away

#include "modules/run_bun.inc"  // Bunbun: RunBun: executing Ask Bun commands

// Bunbun: the optional local AI behind Ask Bun (bunbun-brain.exe).
#include "modules/brain_module.inc"

void ApplyMediaSourceMenuCommand(UINT cmd, const std::vector<MediaSourceMenuItem>& items) {
    if (cmd < kMediaSourceMenuIdBase) return;
    if (cmd == kMediaSourceMenuIdBase) {
        LockMediaSource(L"");
        return;
    }
    const size_t index = cmd - kMediaSourceMenuIdBase - 1;
    if (index < items.size()) {
        LockMediaSource(items[index].aumid);
    }
}

void ShowMediaSourcePopup(HWND hwnd, POINT screenPoint) {
    HMENU menu = CreatePopupMenu();
    const std::vector<MediaSourceMenuItem> items = AppendMediaSourceItems(menu);
    SetForegroundWindow(hwnd);
    DwmBackdrop::EnsureBehind(hwnd);
    const UINT cmd = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON,
                                    screenPoint.x, screenPoint.y, 0, hwnd, nullptr);
    DestroyMenu(menu);
    PostMessageW(hwnd, WM_NULL, 0, 0);
    DwmBackdrop::EnsureBehind(hwnd);
    g_layoutDirty = true;
    ApplyMediaSourceMenuCommand(cmd, items);
}

#include "modules/context_menu.inc"  // Bunbun: Right-click menu and media source menu commands

enum class WeatherVisual {
    Clear,
    PartlyCloudy,
    Cloudy,
    Fog,
    Storm,
    Rain,
    Snow,
    Unknown,
};

#include "modules/renderer_support_types.inc"  // Bunbun: Weather visuals, layout caches and small types the Renderer uses

class Renderer {
   public:
#include "modules/renderer_init.inc"  // Bunbun: Renderer: construction, device setup and shutdown

#include "modules/renderer_surface.inc"  // Bunbun: Renderer: backing bitmap, brushes, shape cache, shadows and masks

#include "modules/renderer_weather_draw.inc"  // Bunbun: Renderer: vector weather icons and the weather dashboard

    // Little clipboard for the File Tray header: board, clip and two text lines.
    void DrawTrayPasteButton(D2D1_RECT_F box, bool hovered) {
        const float s = box.right - box.left;
        const D2D1_POINT_2F c = D2D1::Point2F((box.left + box.right) * 0.5f,
                                              (box.top + box.bottom) * 0.5f);

        if (hovered) {
            ComPtr<ID2D1SolidColorBrush> bg;
            if (SUCCEEDED(target_->CreateSolidColorBrush(
                    WithAlpha(material_.accent, 0.20f * settingsOpacity_), &bg)) && bg) {
                target_->FillRoundedRectangle(D2D1::RoundedRect(box, s * 0.32f, s * 0.32f), bg.Get());
            }
        }

        D2D1_COLOR_F ink = hovered ? material_.accent : material_.textSecondary;
        ink.a *= settingsOpacity_;
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(target_->CreateSolidColorBrush(ink, &brush)) || !brush) {
            return;
        }

        ComPtr<ID2D1StrokeStyle> round;
        d2dFactory_->CreateStrokeStyle(
            D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                        D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
            nullptr, 0, &round);

        const float stroke = std::max(1.2f, 0.075f * s);

        // Board.
        target_->DrawRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(c.x - 0.25f * s, c.y - 0.24f * s,
                                          c.x + 0.25f * s, c.y + 0.31f * s),
                              0.07f * s, 0.07f * s),
            brush.Get(), stroke, round.Get());

        // Clip.
        target_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(c.x - 0.11f * s, c.y - 0.32f * s,
                                          c.x + 0.11f * s, c.y - 0.15f * s),
                              0.05f * s, 0.05f * s),
            brush.Get());

        // Text lines.
        target_->DrawLine(D2D1::Point2F(c.x - 0.12f * s, c.y + 0.00f * s),
                          D2D1::Point2F(c.x + 0.12f * s, c.y + 0.00f * s),
                          brush.Get(), stroke * 0.85f, round.Get());
        target_->DrawLine(D2D1::Point2F(c.x - 0.12f * s, c.y + 0.14f * s),
                          D2D1::Point2F(c.x + 0.05f * s, c.y + 0.14f * s),
                          brush.Get(), stroke * 0.85f, round.Get());
    }

    // Small round "x" at the right end of a File Tray row.
    void DrawTrayRemoveButton(D2D1_POINT_2F c, float size, bool hovered, bool rowHovered) {
        const D2D1_COLOR_F danger = D2D1::ColorF(1.0f, 0.38f, 0.36f, 1.0f);

        if (hovered) {
            ComPtr<ID2D1SolidColorBrush> bg;
            if (SUCCEEDED(target_->CreateSolidColorBrush(
                    WithAlpha(danger, 0.22f * settingsOpacity_), &bg)) && bg) {
                target_->FillEllipse(D2D1::Ellipse(c, size * 0.5f, size * 0.5f), bg.Get());
            }
        }

        D2D1_COLOR_F ink = hovered ? danger
                                   : WithAlpha(material_.textSecondary, rowHovered ? 0.85f : 0.55f);
        ink.a *= settingsOpacity_;
        ComPtr<ID2D1SolidColorBrush> brush;
        if (FAILED(target_->CreateSolidColorBrush(ink, &brush)) || !brush) {
            return;
        }

        ComPtr<ID2D1StrokeStyle> round;
        d2dFactory_->CreateStrokeStyle(
            D2D1::StrokeStyleProperties(D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND,
                                        D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND),
            nullptr, 0, &round);

        const float r = size * 0.22f;
        const float stroke = std::max(1.2f, size * 0.085f);
        target_->DrawLine(D2D1::Point2F(c.x - r, c.y - r), D2D1::Point2F(c.x + r, c.y + r),
                          brush.Get(), stroke, round.Get());
        target_->DrawLine(D2D1::Point2F(c.x - r, c.y + r), D2D1::Point2F(c.x + r, c.y - r),
                          brush.Get(), stroke, round.Get());
    }

    // File Tray (#33): a shelf for files dragged onto the island. Rows are built
    // from the shared DrawCard primitive so the shelf matches every other
    // dashboard, with the real Explorer icon for each file.
    void DrawFileTrayDashboard(const SharedState& state, D2D1_RECT_F rect,
                               const Settings& settings, float scale) {
        const float padX = FileTrayLayout::kPadX * scale;

        // Header: title on the left, count chip on the right.
        const D2D1_RECT_F headerRect = D2D1::RectF(rect.left + padX, rect.top + 18.0f * scale,
                                                   rect.right - padX, rect.top + 38.0f * scale);
        textBrush_->SetOpacity(0.96f);
        if (boldTextFormat_) {
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            const wchar_t* title = Loc(L"File Tray");
            target_->DrawTextW(title, static_cast<UINT32>(wcslen(title)), boldTextFormat_.Get(),
                               headerRect, textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            boldTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
        }

        const size_t total = state.fileTrayItems.size();

        const float clearSize = FileTrayLayout::kClearBtnSize * scale;
        const float clearTop = rect.top + FileTrayLayout::kClearBtnTop * scale;
        const D2D1_RECT_F clearBox = D2D1::RectF(rect.right - padX - clearSize, clearTop,
                                                 rect.right - padX, clearTop + clearSize);
        const float pasteGap = FileTrayLayout::kPasteBtnGap * scale;
        const D2D1_RECT_F pasteBox = D2D1::RectF(clearBox.left - pasteGap - clearSize, clearTop,
                                                 clearBox.left - pasteGap, clearTop + clearSize);

        if (total > 0 && smallTextFormat_) {
            wchar_t countBuf[48] = {};
            swprintf_s(countBuf, L"%zu %s", total,
                       Loc(total == 1 ? L"item" : L"items"));
            const D2D1_RECT_F countRect = D2D1::RectF(headerRect.left, headerRect.top,
                                                      pasteBox.left - 8.0f * scale, headerRect.bottom);
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            mutedBrush_->SetOpacity(0.75f);
            target_->DrawTextW(countBuf, static_cast<UINT32>(wcslen(countBuf)),
                               smallTextFormat_.Get(), countRect, mutedBrush_.Get(),
                               D2D1_DRAW_TEXT_OPTIONS_NONE);
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        DrawTrayPasteButton(pasteBox, g_hoveredFileTrayAction.load(std::memory_order_relaxed) == -3);
        DrawTrayBinButton(clearBox, total > 0,
                          g_hoveredFileTrayAction.load(std::memory_order_relaxed) == -2);

        const float listTop = rect.top + FileTrayLayout::kListTop * scale;
        const float listBottom = rect.bottom - FileTrayLayout::kListBottomInset * scale;

        if (state.fileTrayItems.empty()) {
            // Empty state: a dashed drop target rather than a bare label, so it
            // reads as an invitation.
            const D2D1_RECT_F dropRect = D2D1::RectF(rect.left + padX, listTop,
                                                      rect.right - padX, listBottom);
            ComPtr<ID2D1SolidColorBrush> dash;
            if (SUCCEEDED(target_->CreateSolidColorBrush(
                    WithAlpha(material_.hairline, material_.hairline.a * 1.6f), &dash)) && dash) {
                ComPtr<ID2D1StrokeStyle> dashStyle;
                D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties();
                props.dashStyle = D2D1_DASH_STYLE_DASH;
                props.dashCap = D2D1_CAP_STYLE_ROUND;
                d2dFactory_->CreateStrokeStyle(props, nullptr, 0, &dashStyle);
                target_->DrawRoundedRectangle(D2D1::RoundedRect(dropRect, 12.0f * scale, 12.0f * scale),
                                              dash.Get(), 1.4f, dashStyle.Get());
            }

            if (textFormat_) {
                textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_CENTER);
                const wchar_t* hint = Loc(L"Drag files here");
                mutedBrush_->SetOpacity(0.80f);
                target_->DrawTextW(hint, static_cast<UINT32>(wcslen(hint)), textFormat_.Get(),
                                   dropRect, mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
                textFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
            }
            mutedBrush_->SetOpacity(0.75f);
            return;
        }

        const float rowH = FileTrayLayout::kRowHeight * scale;
        const float rowGap = FileTrayLayout::kRowGap * scale;
        const int capacity = FileTrayLayout::VisibleRowCapacity((rect.bottom - rect.top) / scale);

        // Newest first: the file just dropped is the one the user wants.
        int drawn = 0;
        for (auto it = state.fileTrayItems.rbegin();
             it != state.fileTrayItems.rend() && drawn < capacity; ++it, ++drawn) {
            const FileTrayItem& item = *it;
            const float rowTop = listTop + drawn * (rowH + rowGap);
            const D2D1_RECT_F row = D2D1::RectF(rect.left + padX, rowTop, rect.right - padX, rowTop + rowH);

            const bool hovered = (g_hoveredFileTrayRow.load(std::memory_order_relaxed) == drawn);
            DrawCard(row, 9.0f * scale, hovered);

            // Real shell icon when we managed to extract one, otherwise a glyph.
            const D2D1_RECT_F iconRect = D2D1::RectF(row.left + 8.0f * scale, row.top + 6.0f * scale,
                                                      row.left + 26.0f * scale, row.bottom - 6.0f * scale);
            if (item.icon) {
                DrawBitmapPixels(*item.icon, iconRect, fileTrayIconBitmap_, fileTrayIconGeneration_, 0.98f);
            } else if (iconFormat_) {
                const wchar_t* glyph = item.isText ? L"\uE8D2"
                                                   : (item.isDirectory ? L"\uE8B7" : L"\uE7C3");
                accentBrush_->SetOpacity(0.85f);
                target_->DrawTextW(glyph, 1, iconFormat_.Get(), iconRect, accentBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_NONE);
                accentBrush_->SetOpacity(1.0f);
            }

            wchar_t sizeBuf[40] = {};
            if (item.isText) {
                swprintf_s(sizeBuf, L"%zu ch", item.text.size());
            } else if (item.isDirectory) {
                wcscpy_s(sizeBuf, L"—");
            } else if (item.sizeBytes >= 1073741824ull) {
                swprintf_s(sizeBuf, L"%.1f GB", static_cast<double>(item.sizeBytes) / 1073741824.0);
            } else if (item.sizeBytes >= 1048576ull) {
                swprintf_s(sizeBuf, L"%.1f MB", static_cast<double>(item.sizeBytes) / 1048576.0);
            } else if (item.sizeBytes >= 1024ull) {
                swprintf_s(sizeBuf, L"%.0f KB", static_cast<double>(item.sizeBytes) / 1024.0);
            } else {
                swprintf_s(sizeBuf, L"%llu B", static_cast<unsigned long long>(item.sizeBytes));
            }

            const float removeSize = FileTrayLayout::kRemoveBtnSize * scale;
            const float removeMargin = FileTrayLayout::kRemoveBtnMargin * scale;
            const float removeSpace = removeSize + removeMargin + 4.0f * scale;
            const bool removeHovered =
                (g_hoveredFileTrayAction.load(std::memory_order_relaxed) == drawn);
            DrawTrayRemoveButton(D2D1::Point2F(row.right - removeMargin - removeSize * 0.5f,
                                               (row.top + row.bottom) * 0.5f),
                                 removeSize, removeHovered, hovered);

            const float sizeW = 66.0f * scale;
            const D2D1_RECT_F nameRect = D2D1::RectF(row.left + 32.0f * scale, row.top,
                                                      row.right - removeSpace - sizeW - 4.0f * scale, row.bottom);
            if (smallTextFormat_) {
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                textBrush_->SetOpacity(0.94f);
                target_->DrawTextW(item.name.c_str(), static_cast<UINT32>(item.name.size()),
                                   smallTextFormat_.Get(), nameRect, textBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_CLIP);

                const D2D1_RECT_F sizeRect = D2D1::RectF(row.right - removeSpace - sizeW, row.top,
                                                          row.right - removeSpace, row.bottom);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                mutedBrush_->SetOpacity(0.70f);
                target_->DrawTextW(sizeBuf, static_cast<UINT32>(wcslen(sizeBuf)),
                                   smallTextFormat_.Get(), sizeRect, mutedBrush_.Get(),
                                   D2D1_DRAW_TEXT_OPTIONS_NONE);
                smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
            }
        }

        // "+N more" when the shelf holds more than fits.
        if (static_cast<int>(total) > capacity && smallTextFormat_) {
            wchar_t moreBuf[32] = {};
            swprintf_s(moreBuf, L"+%d", static_cast<int>(total) - capacity);
            const D2D1_RECT_F moreRect = D2D1::RectF(rect.left + padX, listBottom - 12.0f * scale,
                                                      rect.right - padX, listBottom);
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
            mutedBrush_->SetOpacity(0.65f);
            target_->DrawTextW(moreBuf, static_cast<UINT32>(wcslen(moreBuf)), smallTextFormat_.Get(),
                               moreRect, mutedBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
            smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
        }

        textBrush_->SetOpacity(0.96f);
        mutedBrush_->SetOpacity(0.75f);
    }

#include "modules/renderer_hardware_draw.inc"  // Bunbun: Renderer: hardware monitor dashboard

    // Bunbun: the Connectivity tab and its long-press device lists.
#include "modules/connectivity_draw.inc"
    // Bunbun: meeting and download pills, the calendar tab's "Up next".
#include "modules/bunbun_pills_draw.inc"
    // Bunbun: the To-do tab and the reminder pill.
#include "modules/todos_draw.inc"
#include "modules/studio_draw.inc"  // Bunbun: Studio frame, Home tiles, Agent / Clipboard / Note pages
#include "modules/sound_check_draw.inc"  // Bunbun: the Mirror page's sound check panel
#include "modules/edgelight_draw.inc"  // Bunbun: drawing the edge light

#include "modules/renderer_time_game_draw.inc"  // Bunbun: Renderer: time dashboard, tray buttons and game overlay

    // ── Tab transition (spring-driven carousel) ─────────────────────────────
    // tabPos_ is a continuous "which tab is in view" value that a damped spring
    // chases toward the selected index. Every visible tab is drawn from its
    // distance to that position (slide + shrink + fade), so the motion is
    // interruptible, keeps its momentum on fast scrolling, and never restarts.
    void ResetTabTransition() {
        tabInit_ = false;
        tabAnimating_ = false;
        g_tabAnimating.store(false, std::memory_order_relaxed);
        g_tabEdgeBump.store(0, std::memory_order_relaxed);
    }

    template <typename DrawFn>
    void DrawTabTransition(int context, int tabIdx, const std::vector<int>& tabIds,
                           D2D1_RECT_F rect, double now, DrawFn&& drawTab) {
        const int count = static_cast<int>(tabIds.size());
        if (count <= 0) {
            ResetTabTransition();
            return;
        }
        tabIdx = ClampInt(tabIdx, 0, count - 1);

        // ── Feel: tweak these ──
        constexpr float kStiffness = 250.0f;        // higher = faster (settles in ~0.29s)
        constexpr float kDamping = 31.0f;           // ~0.92 damping ratio: no visible overshoot
        constexpr float kTravelPx = 34.0f;          // vertical gap between neighbouring tabs
        constexpr float kMinScale = 0.965f;         // size of a tab that is fully out of view
        constexpr float kFadePower = 1.4f;          // >1 thins the mid-point slightly so text never ghosts

        float dt = static_cast<float>(now - tabLastDrawn_);
        tabLastDrawn_ = now;
        // First frame of a motion: the previous paint may be long ago, so don't
        // let a big gap swallow the start of the animation.
        dt = tabAnimating_ ? Clamp(dt, 0.001f, 0.05f) : (1.0f / 120.0f);

        // Snap (no animation) when the tab view was just opened, switched between
        // media/idle views, or the number of tabs changed.
        if (!tabInit_ || context != tabContext_ || count != tabCount_) {
            tabInit_ = true;
            tabContext_ = context;
            tabCount_ = count;
            tabPos_ = static_cast<float>(tabIdx);
            tabTarget_ = tabPos_;
            tabVel_ = 0.0f;
            tabDir_ = 1.0f;
            tabAnimating_ = false;
        }
        const float newTarget = static_cast<float>(tabIdx);
        if (newTarget != tabTarget_) {
            tabDir_ = newTarget > tabTarget_ ? 1.0f : -1.0f;  // remembered for the whole move
            tabTarget_ = newTarget;
        }

        // The first and last tab are hard stops: there is no edge bump any more.
        g_tabEdgeBump.store(0, std::memory_order_relaxed);

        if (tabPos_ != tabTarget_ || tabVel_ != 0.0f) {
            float remaining = dt;
            while (remaining > 0.0f) {
                const float h = std::min(remaining, 0.004f);
                tabVel_ += (-kStiffness * (tabPos_ - tabTarget_) - kDamping * tabVel_) * h;
                tabPos_ += tabVel_ * h;

                // Never travel past the first or last tab, and kill any velocity
                // pushing further out, so the edge tabs cannot overshoot or twitch.
                const float maxTabPos = static_cast<float>(count - 1);
                if (tabPos_ < 0.0f) {
                    tabPos_ = 0.0f;
                    if (tabVel_ < 0.0f) tabVel_ = 0.0f;
                } else if (tabPos_ > maxTabPos) {
                    tabPos_ = maxTabPos;
                    if (tabVel_ > 0.0f) tabVel_ = 0.0f;
                }
                remaining -= h;
            }
            if (std::fabs(tabPos_ - tabTarget_) < 0.0008f && std::fabs(tabVel_) < 0.02f) {
                tabPos_ = tabTarget_;
                tabVel_ = 0.0f;
            }
        }

        const bool moving = (tabPos_ != tabTarget_ || tabVel_ != 0.0f);
        tabAnimating_ = moving;
        g_tabAnimating.store(moving, std::memory_order_relaxed);

        if (!moving) {
            drawTab(tabIds[tabIdx]);
            return;
        }
        g_tabLastPaintAt.store(now, std::memory_order_relaxed);  // render loop keeps frames coming until it settles

        const D2D1_POINT_2F center =
            D2D1::Point2F((rect.left + rect.right) * 0.5f, (rect.top + rect.bottom) * 0.5f);
        D2D1_MATRIX_3X2_F oldTransform;
        target_->GetTransform(&oldTransform);

        if (!tabLayer_) {
            target_->CreateLayer(&tabLayer_);  // cached: re-creating it every frame is costly
        }

        // Every tab is a pure function of its distance to tabPos_, with no
        // "arriving" / "leaving" roles. Motion is therefore continuous even when you
        // reverse mid-scroll, and the tabs travel as one rigid strip (a fixed gap
        // apart), which is what reads as real scrolling rather than two fading cards.
        auto drawLayer = [&](int index) {
            if (index < 0 || index >= count) {
                return;
            }
            const float d = static_cast<float>(index) - tabPos_;  // <0 above, >0 below
            const float ad = Clamp(std::fabs(d), 0.0f, 1.0f);
            const float eased = SmoothStep01(ad);

            const float alpha = std::pow(1.0f - eased, kFadePower);
            if (alpha <= 0.004f) {
                return;
            }

            const float scale = 1.0f - (1.0f - kMinScale) * eased;
            const float dy = d * kTravelPx;

            target_->SetTransform(D2D1::Matrix3x2F::Scale(scale, scale, center) *
                                  D2D1::Matrix3x2F::Translation(0.0f, dy) * oldTransform);
            if (alpha < 0.995f && tabLayer_) {
                target_->PushLayer(
                    D2D1::LayerParameters(rect, nullptr, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE,
                                          D2D1::IdentityMatrix(), alpha, nullptr,
                                          D2D1_LAYER_OPTIONS_NONE),
                    tabLayer_.Get());
                drawTab(tabIds[index]);
                target_->PopLayer();
            } else {
                drawTab(tabIds[index]);  // fully opaque: skip the layer, much cheaper
            }
            target_->SetTransform(oldTransform);
        };

        // Farther tab first, so the one closer to centre ends up on top.
        const int lo = static_cast<int>(std::floor(tabPos_));
        if ((tabPos_ - static_cast<float>(lo)) < 0.5f) {
            drawLayer(lo + 1);
            drawLayer(lo);
        } else {
            drawLayer(lo);
            drawLayer(lo + 1);
        }
    }

    // Vertical pagination dots on the right edge. The active dot stretches into a
    // pill and slides with the transition instead of jumping.
    void DrawTabPagination(D2D1_RECT_F rect, int tabCount) {
        if (tabCount <= 1) {
            return;
        }
        ID2D1SolidColorBrush* dotBrush = AcquireBrush(WithAlpha(material_.textPrimary, 1.0f));
        if (!dotBrush) {
            return;
        }

        const float dotX = rect.right - 10.0f;
        const float spacing = 9.0f;
        const float r = 2.5f;
        const float startY = (rect.top + rect.bottom) * 0.5f - spacing * (tabCount - 1) * 0.5f;

        // Resting dots. The one under the indicator fades out, so the pill reads
        // as travelling over the track instead of sitting on top of a dot.
        for (int i = 0; i < tabCount; ++i) {
            const float under = SmoothStep01(Clamp(1.0f - std::fabs(tabPos_ - static_cast<float>(i)), 0.0f, 1.0f));
            dotBrush->SetOpacity(0.22f * (1.0f - under) * settingsOpacity_);
            target_->FillEllipse(
                D2D1::Ellipse(D2D1::Point2F(dotX, startY + spacing * static_cast<float>(i)), r, r),
                dotBrush);
        }

        // Travelling indicator: follows the spring exactly (so it also gives a little
        // at the ends) and stretches slightly with speed.
        const float pos = Clamp(tabPos_, -0.3f, static_cast<float>(tabCount) - 0.7f);
        const float stretch = Clamp(std::fabs(tabVel_) * 0.55f, 0.0f, 3.5f);
        const float halfH = r + 2.2f + stretch;
        const float cy = startY + spacing * pos;
        dotBrush->SetOpacity(0.90f * settingsOpacity_);
        target_->FillRoundedRectangle(
            D2D1::RoundedRect(D2D1::RectF(dotX - r, cy - halfH, dotX + r, cy + halfH), r, r),
            dotBrush);
    }

    // Publishes the content-space rect currently being painted so
    // OverlayWndProc hit-tests against the real geometry rather than assuming
    // the pill is centered in the client area. Seqlock write: bump to odd,
    // store, bump to even, so a reader can never mix two frames.
    void PublishContentGeometry(D2D1_RECT_F rect, float rightTrim = 0.0f) {
        if (offscreen_) return;  // Bunbun: another display's island; clicks map to the real one
        const unsigned seq = g_mediaHitSeq.load(std::memory_order_relaxed);
        g_mediaHitSeq.store(seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence(std::memory_order_release);

        publishedOrigin_ = D2D1::Point2F(rect.left, rect.top);  // Bunbun: Studio hit rects are relative to this
        g_mediaHitLeft.store(rect.left, std::memory_order_relaxed);
        g_mediaHitTop.store(rect.top, std::memory_order_relaxed);
        g_mediaHitRight.store(rect.right, std::memory_order_relaxed);
        g_mediaHitBottom.store(rect.bottom, std::memory_order_relaxed);
        g_mediaHitScale.store(g_settings.sizeScale, std::memory_order_relaxed);
        g_mediaHitTrim.store(rightTrim, std::memory_order_relaxed);
        g_mediaHitStamp.store(GetTickCount64(), std::memory_order_relaxed);

        std::atomic_thread_fence(std::memory_order_release);
        g_mediaHitSeq.store(seq + 2, std::memory_order_relaxed);
    }

    // Corner radius for the island mask in *content* space -- the coordinate
    // system DrawMedia and DrawIdleDashboard are handed, which DrawPill scales
    // by sizeScale afterwards. Because that scale is applied later, these radii
    // must not be pre-multiplied by sizeScale. They previously were, so the
    // stadium corner arc grew with the size scale until it cut across the album
    // art's top-left corner (visible from roughly 2x upwards).
    float ContentIslandRadius(float contentHeight) const {
        if (g_settings.notchStyle) {
            return NotchBottomRadius(contentHeight);  // Bunbun: grows with the island
        }
        if (g_settings.w11Style) {
            return 8.0f;
        }
        return std::min(contentHeight * 0.5f, 44.0f);
    }

    // Bunbun: the notch's bottom corners. A small notch is fully round (half its
    // height), and the corners open up to 30 as the island grows, so an open
    // island reads as a soft card instead of a tight capsule.
    static float NotchBottomRadius(float h) {
        if (h <= 44.0f) return h * 0.5f;
        return std::min(30.0f, 22.0f + (h - 44.0f) * 0.1f);
    }

    // Takes settings by reference from Render()'s private copy rather than
    // reaching for g_settings. The dashboards it delegates to read std::wstring
    // members (DrawCalendarDashboard -> settings.dateFormat), which would
    // otherwise race with LoadSettings() replacing the struct mid-frame.
    void DrawMedia(const SharedState& state, D2D1_RECT_F rect, const Settings& settings,
                   double now) {
        const float height = rect.bottom - rect.top;

        // Island resize tracking: "settled" once the size has stopped changing for 0.15s.
        // Small per-frame drifts accumulate against the last latched size, so the
        // spring's tail can't sneak past the check.
        {
            const float curW = rect.right - rect.left;
            if (std::fabs(curW - islandSizeLastW_) > 0.25f ||
                std::fabs(height - islandSizeLastH_) > 0.25f) {
                islandSizeLastW_ = curW;
                islandSizeLastH_ = height;
                islandSizeMovedAt_ = now;
            }
            islandSettled_ = (now - islandSizeMovedAt_) > 0.15;
        }

        expandedAnim_ = settings.expandedMediaAnim;
        pillCoverAnim_ = settings.pillCoverAnim;

        // The old side panel is replaced by the immersive lyrics view, so the island never widens.
        const bool sideLyricsOn = false;
        // 0 = normal player, 1 = immersive lyrics (always 0 unless "Lyrics in the media tab" is on).
        const float immersiveMix = UpdateImmersiveMix(settings, height, now);
        g_immersiveMixPub.store(immersiveMix, std::memory_order_relaxed);
        const float sideLyricsExtra =
            sideLyricsOn ? std::max(0.0f, (rect.right - rect.left) - MediaLayout::kExpandedWidth)
                         : 0.0f;

        PublishContentGeometry(rect, sideLyricsExtra);

        const float radius = ContentIslandRadius(height);
        // Bunbun: in Studio the page sits inside the frame; no island-sized rounding there.
        ComPtr<ID2D1Geometry> mask = studio_.on ? GetShapeGeometry(rect, 0.0f, false) : CreateIslandMaskGeometry(rect, radius, settings.notchStyle);
        ID2D1Layer* layer = AcquireLayer();

        float expandedAlpha = std::clamp((height - MediaLayout::kExpandedMinHeight) / 60.0f, 0.0f, 1.0f);
        float collapsedAlpha = std::clamp((80.0f - height) / 30.0f, 0.0f, 1.0f);

        // ── Shared album art ─────────────────────────────────────────────────
        // One art element that morphs between the collapsed geometry (small circle
        // on the left) and the expanded geometry (64px rounded square) as the island
        // grows or shrinks, instead of cross-fading two different shapes.
        // artMorph: 0 = collapsed pill, 1 = fully expanded (reached at 120px, where
        // the expanded layer is already fully opaque and takes over drawing the art).
        constexpr float kCollapsedPillHeight = 44.0f;
        constexpr float kArtMorphEndHeight = 120.0f;
        constexpr float kCollapsedArtPad = 6.0f;
        constexpr float kCollapsedArtSize = kCollapsedPillHeight - kCollapsedArtPad * 2.0f;
        // Raw progress straight from the island's height.
        const float artMorphRaw = Clamp((height - kCollapsedPillHeight) /
                                        (kArtMorphEndHeight - kCollapsedPillHeight), 0.0f, 1.0f);

        // Shrinking follows the height exactly (unchanged). Expanding is not
        // allowed to run faster than kArtExpandSec for the full circle -> square
        // change, because the expand spring is so fast that the morph would
        // otherwise be over in a few frames.
        constexpr float kArtExpandSec = 0.26f;
        // The art only travels to its expanded spot on the Media tab. On any other
        // first tab (e.g. pinned lyrics) it just follows the height, with no throttle.
        const bool artOnMediaTab = NormalizedTabIndex(settings) == MediaTabIndex(settings);
        const bool freshMorph = artMorphLast_ >= 0.0 && (now - artMorphLast_) < 0.25;
        const float dtMorph = freshMorph ? Clamp(static_cast<float>(now - artMorphLast_), 0.001f, 0.05f) : 0.0f;
        artMorphLast_ = now;

        if (!freshMorph || artMorphRaw <= artMorphLin_ || !artOnMediaTab) {
            artMorphLin_ = artMorphRaw;
        } else {
            artMorphLin_ = std::min(artMorphRaw, artMorphLin_ + dtMorph / kArtExpandSec);
            g_layoutDirty = true;  // keep frames coming until the morph catches up
        }
        const float artMorph = SmoothStep01(artMorphLin_);

        // Source badge: hidden during the morph, fades in once the art hands over
        // to the expanded layer (this used to pop in instantly).
        if (artMorph >= 1.0f) {
            if (!freshMorph) {
                artBadgeAlpha_ = 1.0f;
            } else if (artBadgeAlpha_ < 1.0f) {
                artBadgeAlpha_ = std::min(1.0f, artBadgeAlpha_ + dtMorph / 0.20f);
                g_layoutDirty = true;
            }
        } else {
            artBadgeAlpha_ = 0.0f;
        }
        float pillClockSection = 0.0f;
        if (settings.mediaPillClock && idleTextFormat_) {
            pillClockSection = MeasureMediaPill(state, settings).sectionWidth;
        }
        const D2D1_RECT_F collapsedArtRect = D2D1::RectF(
            rect.left + pillClockSection + kCollapsedArtPad, rect.top + kCollapsedArtPad,
            rect.left + pillClockSection + kCollapsedArtPad + kCollapsedArtSize,
            rect.top + kCollapsedArtPad + kCollapsedArtSize);
        auto lerpF = [](float a, float b, float t) { return a + (b - a) * t; };
        const D2D1_RECT_F normalArtRect = D2D1::RectF(
            rect.left + MediaLayout::kArtInsetX, rect.top + MediaLayout::kArtInsetY,
            rect.left + MediaLayout::kArtInsetX + MediaLayout::kArtSize,
            rect.top + MediaLayout::kArtInsetY + MediaLayout::kArtSize);
        const D2D1_RECT_F immersiveArtRect = D2D1::RectF(
            rect.left + ImmersiveLayout::kArtLeft, rect.top + ImmersiveLayout::kArtTop,
            rect.left + ImmersiveLayout::kArtLeft + ImmersiveLayout::kArtSize,
            rect.top + ImmersiveLayout::kArtTop + ImmersiveLayout::kArtSize);
        // Equals the original rect / radius exactly while immersiveMix == 0.
        const D2D1_RECT_F expandedArtRect = D2D1::RectF(
            lerpF(normalArtRect.left, immersiveArtRect.left, immersiveMix),
            lerpF(normalArtRect.top, immersiveArtRect.top, immersiveMix),
            lerpF(normalArtRect.right, immersiveArtRect.right, immersiveMix),
            lerpF(normalArtRect.bottom, immersiveArtRect.bottom, immersiveMix));
        const float expandedArtRadius =
            lerpF(16.0f, ImmersiveLayout::kArtSize * 0.5f, immersiveMix);
        const D2D1_RECT_F morphArtRect = D2D1::RectF(
            lerpF(collapsedArtRect.left, expandedArtRect.left, artMorph),
            lerpF(collapsedArtRect.top, expandedArtRect.top, artMorph),
            lerpF(collapsedArtRect.right, expandedArtRect.right, artMorph),
            lerpF(collapsedArtRect.bottom, expandedArtRect.bottom, artMorph));
        const float morphArtRadius = lerpF(kCollapsedArtSize * 0.5f, expandedArtRadius, artMorph);

        // Expanded UI
        if (expandedAlpha > 0.01f && mask && layer) {
            target_->PushLayer(D2D1::LayerParameters(rect, mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), expandedAlpha, nullptr, D2D1_LAYER_OPTIONS_NONE), layer);

            // Order must match ActiveTabCount()/FileTrayTabIndex().
            // Lyrics goes first when pinned; otherwise it stays last. Must match MediaTabIndex().
            // Bunbun: one list for both views (DashboardTabIds), plus the Studio pages.
            const std::vector<int> activeTabs = DashboardTabIds(0, settings);
            g_dashboardContext.store(0, std::memory_order_relaxed);

            // Same settings object as the list above, so the index can't be
            // normalised against a tab set that no longer matches.
            int maxTabs = static_cast<int>(activeTabs.size());
            int tabIdx = NormalizedTabIndex(settings);
            if (tabIdx >= maxTabs) tabIdx = maxTabs - 1;

            auto drawTabContent = [&](int activeTabId) {
            if (DrawStudioExtraPage(activeTabId, state, rect, settings, now)) return;  // Bunbun
            if (activeTabId == 0) {
                // With side lyrics on, the player lives in the left part of the island and
                // the rest of this branch (which uses `rect`) lays out exactly as before.
                const D2D1_RECT_F fullIslandRect = rect;
                const D2D1_RECT_F rect = D2D1::RectF(fullIslandRect.left, fullIslandRect.top,
                                                     fullIslandRect.right - sideLyricsExtra,
                                                     fullIslandRect.bottom);
                // Source switch envelope for everything that changes with the source.
                float swapAlpha = 1.0f, swapDx = 0.0f;
                UpdateSourceSwap(state, now, &swapAlpha, &swapDx);

                // Art, text and spectrum slide + fade as one group (closed in 13c).
                const bool contentGrouped = BeginSwapGroup(swapAlpha, swapDx);
                if (swapAlpha > 0.003f) {
                // Expanded Apple DI media: large square art on left, text center.
                const float artSize = MediaLayout::kArtSize;
                D2D1_RECT_F artRect = D2D1::RectF(rect.left + MediaLayout::kArtInsetX,
                                                  rect.top + MediaLayout::kArtInsetY,
                                                  rect.left + MediaLayout::kArtInsetX + artSize,
                                                  rect.top + MediaLayout::kArtInsetY + artSize);
                // Until the morph has finished, the shared art pass at the bottom of
                // DrawMedia owns the cover, so it is never drawn twice.
                if (artMorph >= 1.0f) {
                    DrawAlbumArtFlip(state.media, expandedArtRect, now, expandedArtRadius,
                                     immersiveMix < 0.01f);
                }

                // Immersive: title / artist / spectrum / source line cross-fade out.
                const bool normalFaded = BeginImmersiveFade(1.0f - immersiveMix);
                if (immersiveMix < 0.999f) {

                const float waveW = 32.0f;
                const float waveH = 20.0f;
                D2D1_RECT_F waveRect = D2D1::RectF(rect.right - 24.0f - waveW,
                                                   rect.top + 20.0f + (artSize - waveH) * 0.5f,
                                                   rect.right - 24.0f,
                                                   rect.top + 20.0f + (artSize + waveH) * 0.5f);

                const float textLeft = artRect.right + 18.0f;
                const float textRight = waveRect.left - 16.0f;

                // Title — bold, prominent.
                D2D1_RECT_F titleRect = D2D1::RectF(textLeft, rect.top + 34.0f, textRight, rect.top + 54.0f);
                const std::wstring titleText =
                    state.media.title.empty() ? std::wstring(Loc(L"Unknown")) : state.media.title;
                if (!DrawTitleTransition(titleText, titleRect, textBrush_.Get(), now, !state.media.title.empty())) {
                    DrawMarqueeText(titleText, titleRect, textFormat_.Get(), textBrush_.Get(), now, 42.0f,
                                    marqueeTitleCache_);
                }

                // Artist — muted below title.
                D2D1_RECT_F artistRect = D2D1::RectF(textLeft, rect.top + 54.0f, textRight, rect.top + 74.0f);
                mutedBrush_->SetOpacity(0.80f);
                DrawSwapMarquee(swapArtist_, state.media.artist, artistRect, smallTextFormat_.Get(),
                                mutedBrush_.Get(), now, 30.0f, marqueeArtistCache_, 0.06f);
                mutedBrush_->SetOpacity(0.75f);

                if (!state.media.albumTitle.empty()) {
                    D2D1_RECT_F albumRect = D2D1::RectF(textLeft, rect.top + 68.0f, textRight, rect.top + 84.0f);
                    mutedBrush_->SetOpacity(0.70f);
                    DrawSwapMarquee(swapAlbum_, state.media.albumTitle, albumRect, smallTextFormat_.Get(),
                                    mutedBrush_.Get(), now, 28.0f, marqueeAlbumCache_, 0.12f);
                    mutedBrush_->SetOpacity(0.75f);
                }

                if (state.media.playing) {
                    DrawSpectrum(state, waveRect, settings, now);
                } else {
                    const float gap = 2.5f;
                    const float availableW = waveRect.right - waveRect.left;
                    const int count = 7;
                    const float barWidth = (availableW - gap * (count - 1)) / count;
                    const float centerY = (waveRect.top + waveRect.bottom) * 0.5f;
                    mutedBrush_->SetOpacity(0.5f);
                    for (int i = 0; i < count; ++i) {
                        const float dotX = waveRect.left + i * (barWidth + gap) + barWidth * 0.5f;
                        target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, centerY), 1.2f, 1.2f), mutedBrush_.Get());
                    }
                }

                // Source line, under the artist (or under the album when there is one).
                const float srcLineTop = rect.top + (state.media.albumTitle.empty() ? 70.0f : 85.0f);
                DrawSourceLine(state.media, D2D1::RectF(textLeft, srcLineTop, textRight, srcLineTop + 14.0f), now);
                }  // end of `if (immersiveMix < 0.999f)`
                EndImmersiveFade(normalFaded);
                DrawImmersiveLyricsView(state, rect, settings, now, immersiveMix);
                }  // end of `if (swapAlpha > 0.003f)` opened in 13b
                EndSwapGroup(contentGrouped);

                // Immersive lyrics: draw the transport block a little smaller so the
                // lyrics get the room. Restored after the lyrics button below.
                D2D1_MATRIX_3X2_F playerZoneOldTransform;
                target_->GetTransform(&playerZoneOldTransform);
                if (immersiveMix > 0.001f) {
                    const float ps = 1.0f + (ImmersiveLayout::kPlayerScale - 1.0f) * immersiveMix;
                    target_->SetTransform(
                        D2D1::Matrix3x2F::Scale(
                            ps, ps,
                            D2D1::Point2F((rect.left + rect.right) * 0.5f,
                                          rect.top + ImmersiveLayout::kPlayerPivotY)) *
                        playerZoneOldTransform);
                }

                // Scrubber + time labels fade (no slide).
                const bool timelineGrouped = BeginSwapGroup(swapAlpha, 0.0f);

                // Timeline (Scrubber)
                const float scrubberY = rect.top + MediaLayout::kScrubberY;
                double currentPosition = state.media.positionTicks / 10000000.0;
                double duration = state.media.endTicks / 10000000.0;
                if (state.media.playing && state.media.lastUpdatedTicks > 0) {
                    currentPosition += (GetTickCount64() - state.media.lastUpdatedTicks) / 1000.0;
                }
                currentPosition = std::max(0.0, std::min(currentPosition, duration));

                const bool isDraggingThisBar = g_scrubbing.load(std::memory_order_relaxed);
                float progress;
                if (isDraggingThisBar) {
                    progress = Clamp(g_scrubDragFraction.load(std::memory_order_relaxed), 0.0f, 1.0f);
                    currentPosition = duration * progress;
                } else {
                    progress = duration > 0.0 ? static_cast<float>(currentPosition / duration) : 0.0f;
                }

                auto FormatTime = [](double seconds) -> std::wstring {
                    if (seconds <= 0.0 || _isnan(seconds)) return L"0:00";
                    int m = static_cast<int>(seconds) / 60;
                    int s = static_cast<int>(seconds) % 60;
                    wchar_t buf[16];
                    swprintf_s(buf, L"%d:%02d", m, s);
                    return buf;
                };

                std::wstring elapsedStr = FormatTime(currentPosition);
                std::wstring remainStr = L"-" + FormatTime(duration - currentPosition);

                const float scrubLeft = rect.left + MediaLayout::kScrubMargin;
                const float scrubRight = rect.right - MediaLayout::kScrubMargin;

                const float barLeft = scrubLeft + MediaLayout::kScrubBarLeftInset;
                const float barRight = scrubRight - MediaLayout::kScrubBarRightInset;
                const float timeGap = 8.0f;

                D2D1_RECT_F elRect = D2D1::RectF(scrubLeft, scrubberY - 10.0f, barLeft - timeGap, scrubberY + 10.0f);
                D2D1_RECT_F remRect = D2D1::RectF(barRight + timeGap, scrubberY - 10.0f, scrubRight, scrubberY + 10.0f);

                mutedBrush_->SetOpacity(0.8f);
                if (smallTextFormat_) {
                    smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
                    smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_TRAILING);
                    target_->DrawTextW(elapsedStr.c_str(), static_cast<UINT32>(elapsedStr.size()), smallTextFormat_.Get(), elRect, mutedBrush_.Get());

                    smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    target_->DrawTextW(remainStr.c_str(), static_cast<UINT32>(remainStr.size()), smallTextFormat_.Get(), remRect, mutedBrush_.Get());

                    smallTextFormat_->SetTextAlignment(DWRITE_TEXT_ALIGNMENT_LEADING);
                    smallTextFormat_->SetParagraphAlignment(DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
                }

                // The scrubber uses the shared accent track, so it matches the
                // volume, battery and timer bars exactly. The bar thickens while
                // dragging, which is the standard cue that it is grabbable.
                if (settings.progressStyle != ProgressStyle::Slim) {
                    DrawStyledProgress(settings.progressStyle, barLeft, barRight, scrubberY, progress,
                                       isDraggingThisBar, state.media.playing, now);
                } else {
                const float barHalf = isDraggingThisBar ? 3.5f : 2.5f;
                DrawAccentTrack(D2D1::RectF(barLeft, scrubberY - barHalf, barRight, scrubberY + barHalf),
                                progress, barHalf);

                const D2D1_COLOR_F scrubColor =
                    (currentAccent_.a > 0.0f) ? currentAccent_ : D2D1::ColorF(0x4cc9f0);
                const float scrubW = (barRight - barLeft) * progress;
                const float thumbX = barLeft + scrubW;
                const float thumbR = isDraggingThisBar ? 6.5f : 4.5f;

                // Halo first so the thumb sits on top of it.
                if (isDraggingThisBar) {
                    ComPtr<ID2D1SolidColorBrush> thumbHalo;
                    if (SUCCEEDED(target_->CreateSolidColorBrush(WithAlpha(scrubColor, 0.22f), &thumbHalo)) &&
                        thumbHalo) {
                        target_->FillEllipse(
                            D2D1::Ellipse(D2D1::Point2F(thumbX, scrubberY), thumbR * 2.2f, thumbR * 2.2f),
                            thumbHalo.Get());
                    }
                }

                // A white thumb with an accent ring reads more precisely against
                // album art than a solid accent dot.
                ComPtr<ID2D1SolidColorBrush> thumbFill;
                if (SUCCEEDED(target_->CreateSolidColorBrush(
                        D2D1::ColorF(1.0f, 1.0f, 1.0f, 0.98f), &thumbFill)) && thumbFill) {
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, scrubberY), thumbR, thumbR),
                                         thumbFill.Get());
                }
                ComPtr<ID2D1SolidColorBrush> thumbRing;
                if (SUCCEEDED(target_->CreateSolidColorBrush(WithAlpha(scrubColor, 0.85f), &thumbRing)) &&
                    thumbRing) {
                    target_->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(thumbX, scrubberY), thumbR, thumbR),
                                         thumbRing.Get(), 1.6f);
                }
                }  // end Slim style

                EndSwapGroup(timelineGrouped);

                // Controls. Positions come from MediaLayout so the hit test in
                // OverlayWndProc stays in lockstep with what is drawn here.
                const float cy = rect.top + MediaLayout::kControlsY;
                const float cx = (rect.left + rect.right) * 0.5f;
                DrawMediaControls(state.media.playing,
                                  D2D1::Point2F(cx - MediaLayout::kControlSpacing, cy),
                                  D2D1::Point2F(cx, cy),
                                  D2D1::Point2F(cx + MediaLayout::kControlSpacing, cy),
                                  now);
                DrawSourceDock(state, rect, now);  // stays still during a switch
                if (settings.mediaTabLyrics) {
                    DrawImmersiveLyricsButton(rect, immersiveMix);
                }
                target_->SetTransform(playerZoneOldTransform);
                if (sideLyricsExtra > 1.0f) {
                    DrawMediaSideLyrics(state,
                                        D2D1::RectF(rect.right, fullIslandRect.top,
                                                    fullIslandRect.right, fullIslandRect.bottom),
                                        settings, now);
                }
            } else if (activeTabId == 1) {
                SYSTEMTIME local = {}; GetLocalTime(&local);
                DrawCalendarDashboard(state, rect, settings, now, 1.0f, local);
            } else if (activeTabId == 2) {
                bool hasWeather = state.weather.hasData && (now - state.weather.lastUpdated < 3600.0);
                std::wstring wIcon = L"🌡️"; std::wstring wText = Loc(L"Loading...");
                if (hasWeather) {
                    wText = state.weather.weatherDesc;
                    GetWeatherIconAndText(state.weather.weatherCode, wIcon, wText);
                }
                DrawWeatherDashboard(state, rect, settings, now, 1.0f, hasWeather, wIcon, wText);
            } else if (activeTabId == 3) {
                DrawHardwareMonitorDashboard(state, rect, settings, 1.0f);
            } else if (activeTabId == 6) {
                DrawConnectivityDashboard(state, rect, 1.0f);  // Bunbun
            } else if (activeTabId == 7) {
                DrawTodoDashboard(state, rect, 1.0f);  // Bunbun
            } else if (activeTabId == 4) {
                DrawFileTrayDashboard(state, rect, settings, 1.0f);
            } else if (activeTabId == 5) {
                // Laid out at the final expanded size and pinned to the top, so growing
                // the island only reveals the lyrics instead of sliding them around.
                // Resting expanded size (380x184 times the hover scale), so the layout never
                // follows the spring's overshoot. At rest this equals the old max() result.
                const float lyricsW = MediaLayout::kExpandedWidth * hoverScale_;
                const float lyricsH = MediaLayout::kExpandedHeight * hoverScale_;
                const float lyricsCx = (rect.left + rect.right) * 0.5f;
                DrawLyricsDashboard(state,
                                    D2D1::RectF(lyricsCx - lyricsW * 0.5f, rect.top,
                                                lyricsCx + lyricsW * 0.5f, rect.top + lyricsH),
                                    settings, 1.0f);
            }
            };  // end drawTabContent

            DrawTabTransition(0, tabIdx, activeTabs, rect, now, drawTabContent);
            if (!settings.studioLayout) DrawTabPagination(rect, maxTabs);  // Bunbun: Studio has a tab bar

            target_->PopLayer();
        }

        // Collapsed UI
        if (collapsedAlpha > 0.01f && mask && layer && !g_trayDragOver.load(std::memory_order_relaxed)) {
            if (!offscreen_) {  // Bunbun: not for another display's island
                g_idleTab = 0;
                ResetTabTransition();
            }
            target_->PushLayer(D2D1::LayerParameters(rect, mask.Get(), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(), collapsedAlpha, nullptr, D2D1_LAYER_OPTIONS_NONE), layer);;

            const float cy = (rect.top + rect.bottom) * 0.5f;

            // Optional clock section on the left. Zero wide when the option is
            // off, so the cover sits exactly where it always did.
            MediaPillMetrics pillMetrics;
            if (settings.mediaPillClock && idleTextFormat_) {
                pillMetrics = MeasureMediaPill(state, settings);
            }
            const float clockSection = pillMetrics.sectionWidth;

            // The cover itself is drawn by the shared art pass at the end of
            // DrawMedia; this rect is only used to place the lyrics next to it.
            const D2D1_RECT_F artRect = collapsedArtRect;

            // Bunbun: clear of however many privacy dots are lit.
            const float shiftX = PrivacyDotsReserve(state, settings);

            D2D1_RECT_F waveRect = D2D1::RectF(rect.right - 42.0f - shiftX, cy - 10.0f,
                                               rect.right - 14.0f - shiftX, cy + 10.0f);
            if (state.media.playing) {
                DrawSpectrum(state, waveRect, settings, now);
            } else {
                const float gap = 2.5f;
                const float availableW = waveRect.right - waveRect.left;
                const int count = std::max(1, static_cast<int>((availableW + gap) / (2.0f + gap)));
                const float barWidth = (availableW - gap * (count - 1)) / count;
                mutedBrush_->SetOpacity(0.5f);
                for (int i = 0; i < count; ++i) {
                    const float dotX = waveRect.left + i * (barWidth + gap) + barWidth * 0.5f;
                    target_->FillEllipse(D2D1::Ellipse(D2D1::Point2F(dotX, cy), 1.2f, 1.2f), mutedBrush_.Get());
                }
            }

            // Lyrics between the album art and the visualiser. Fades in as the
            // pill widens, so there is no pop while the spring is still moving.
            if (CollapsedLyricsActive(state, settings)) {
                const float lyricsAlpha =
                    SmoothStep01(((rect.right - rect.left) - 175.0f) / 60.0f);
                if (lyricsAlpha > 0.01f) {
                    const float textLeft = artRect.right + 10.0f;
                    const float textRight = waveRect.left - 10.0f;
                    DrawCollapsedLyrics(state,
                                        D2D1::RectF(textLeft, rect.top + 4.0f, textRight, rect.bottom - 4.0f),
                                        now, lyricsAlpha, rect.right - rect.left);
                }
            }

            // Clock + divider on the left, same face, size and time format as the
            // idle strip. On hover the collapsed layer fades out and the normal
            // player shows.
            if (clockSection > 0.0f) {
                SYSTEMTIME local = {};
                GetLocalTime(&local);
                const std::wstring clockText = FormatIslandTime(local, settings.clockFollowSystem,
                                                                settings.use24HourClock,
                                                                settings.showSeconds);
                const float clockLeft = rect.left + MediaPillLayout::kClockPadLeft;
                textBrush_->SetOpacity(0.96f);
                target_->DrawTextW(clockText.c_str(), static_cast<UINT32>(clockText.size()),
                                   idleTextFormat_.Get(),
                                   D2D1::RectF(clockLeft, rect.top, clockLeft + pillMetrics.clockWidth, rect.bottom),
                                   textBrush_.Get(), D2D1_DRAW_TEXT_OPTIONS_NONE);
                textBrush_->SetOpacity(1.0f);

                ComPtr<ID2D1SolidColorBrush> divider;
                target_->CreateSolidColorBrush(
                    WithAlpha(material_.hairline, material_.hairline.a * settingsOpacity_), &divider);
                if (divider) {
                    const float divX = clockLeft + pillMetrics.clockWidth + MediaPillLayout::kSlotGap;
                    const float divW = MediaPillLayout::kDividerWidth;
                    target_->FillRoundedRectangle(
                        D2D1::RoundedRect(D2D1::RectF(divX, rect.top + MediaPillLayout::kDividerInsetY,
                                                      divX + divW, rect.bottom - MediaPillLayout::kDividerInsetY),
                                          divW * 0.5f, divW * 0.5f),
                        divider.Get());
                }
            }

            target_->PopLayer();
        }

        // ── Shared album art pass ────────────────────────────────────────────
        // Drawn once, outside both cross-fading layers, at full opacity, so the cover
        // morphs smoothly in position, size and corner radius while the island
        // resizes. Once fully expanded, the expanded layer takes over (flip animation,
        // source badge). Alpha is eased so switching tabs can't make it pop in.
        {
            const bool trayDrag = g_trayDragOver.load(std::memory_order_relaxed);
            const bool onMediaTab = NormalizedTabIndex(settings) == MediaTabIndex(settings);
            const float targetAlpha = trayDrag ? 0.0f : (onMediaTab ? 1.0f : collapsedAlpha);

            const bool recent = artOuterLast_ >= 0.0 && (now - artOuterLast_) < 0.25;
            const float dtArt = recent ? Clamp(static_cast<float>(now - artOuterLast_), 0.001f, 0.05f) : 0.016f;
            artOuterLast_ = now;
            // Off the Media tab the art has no business following a slow ease; it
            // fades exactly with the collapsed pill's contents.
            if (!recent || artMorph >= 1.0f || !onMediaTab) {
                artOuterAlpha_ = targetAlpha;
            } else {
                artOuterAlpha_ += (targetAlpha - artOuterAlpha_) * (1.0f - std::exp(-dtArt / 0.06f));
            }

            // While the island is hiding, the art follows the pill: it shrinks
            // toward the pill's centre and fades, instead of staying 32px and
            // sticking out until the window stops painting.
            float hideS = 1.0f;
            if (islandHiding_) {
                const float needW = pillClockSection + kCollapsedArtPad * 2.0f + kCollapsedArtSize;
                const float curW = rect.right - rect.left;
                hideS = SmoothStep01(std::min(height / kCollapsedPillHeight, curW / needW));
            }
            // On the Media tab the cover morphs circle -> square. On any other tab it
            // stays a small circle on the collapsed pill and never flies into the
            // expanded layout, so it can't show up on top of the lyrics.
            const D2D1_RECT_F artBaseRect = onMediaTab ? morphArtRect : collapsedArtRect;
            const float artBaseRadius = onMediaTab ? morphArtRadius : kCollapsedArtSize * 0.5f;

            D2D1_RECT_F artDrawRect = artBaseRect;
            float artDrawRadius = artBaseRadius;
            if (hideS < 1.0f) {
                const float pcx = (rect.left + rect.right) * 0.5f;
                const float pcy = (rect.top + rect.bottom) * 0.5f;
                const float acx = (artBaseRect.left + artBaseRect.right) * 0.5f;
                const float acy = (artBaseRect.top + artBaseRect.bottom) * 0.5f;
                const float ncx = pcx + (acx - pcx) * hideS;
                const float ncy = pcy + (acy - pcy) * hideS;
                const float hw = (artBaseRect.right - artBaseRect.left) * 0.5f * hideS;
                const float hh = (artBaseRect.bottom - artBaseRect.top) * 0.5f * hideS;
                artDrawRect = D2D1::RectF(ncx - hw, ncy - hh, ncx + hw, ncy + hh);
                artDrawRadius = artBaseRadius * hideS;
            }
            const float artAlphaEff = artOuterAlpha_ * hideS;

            if (artMorph < 1.0f && artAlphaEff > 0.01f) {
                // Bunbun: when music plays in the collapsed pill, alternate between
                // the cover art and our vibing mascot every few seconds in a loop.
                const bool allowMascotLoop = settings.bunbunMascot && settings.bunbunMascotOnIsland && state.media.playing;
                float mascotMix = 0.0f;
                if (StreamingPill(state, settings) && settings.bunbunMascot) {
                    mascotMix = 1.0f;  // Bunbun: a video shows the mascot, never the cover
                } else if (allowMascotLoop) {
                    constexpr double kCycle = 7.0; // 3.5s cover, 3.5s mascot
                    const double phase = std::fmod(now, kCycle);
                    if (phase < 3.0) {
                        mascotMix = 0.0f;
                    } else if (phase < 3.5) {
                        mascotMix = SmoothStep01(static_cast<float>((phase - 3.0) / 0.5));
                    } else if (phase < 6.5) {
                        mascotMix = 1.0f;
                    } else {
                        mascotMix = 1.0f - SmoothStep01(static_cast<float>((phase - 6.5) / 0.5));
                    }
                    g_layoutDirty = true; // keep frames rendering during transitions and music vibes
                }

                ID2D1Layer* artLayer = (artAlphaEff < 0.99f) ? AcquireLayer() : nullptr;
                if (artLayer) {
                    target_->PushLayer(
                        D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                              D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                              Clamp(artAlphaEff, 0.0f, 1.0f), nullptr,
                                              D2D1_LAYER_OPTIONS_NONE),
                        artLayer);
                }

                if (mascotMix < 0.99f) {
                    ID2D1Layer* coverLayer = (mascotMix > 0.01f) ? AcquireLayer() : nullptr;
                    if (coverLayer) {
                        target_->PushLayer(
                            D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                                  Clamp(1.0f - mascotMix, 0.0f, 1.0f), nullptr,
                                                  D2D1_LAYER_OPTIONS_NONE),
                            coverLayer);
                    }
                    DrawAlbumArtDrop(state.media, artDrawRect, now, artDrawRadius);
                    if (coverLayer) target_->PopLayer();
                }

                if (mascotMix > 0.01f) {
                    ID2D1Layer* mLayer = (mascotMix < 0.99f) ? AcquireLayer() : nullptr;
                    if (mLayer) {
                        target_->PushLayer(
                            D2D1::LayerParameters(D2D1::InfiniteRect(), nullptr,
                                                  D2D1_ANTIALIAS_MODE_PER_PRIMITIVE, D2D1::IdentityMatrix(),
                                                  Clamp(mascotMix, 0.0f, 1.0f), nullptr,
                                                  D2D1_LAYER_OPTIONS_NONE),
                            mLayer);
                    }
                    float size = std::min(artDrawRect.right - artDrawRect.left,
                                          artDrawRect.bottom - artDrawRect.top);
                    D2D1_POINT_2F c = D2D1::Point2F(
                        (artDrawRect.left + artDrawRect.right) * 0.5f,
                        (artDrawRect.top + artDrawRect.bottom) * 0.5f);
                    if (StreamingPill(state, settings) &&
                        bb::mascot::GetStyle().character != bb::mascot::Character::Custom) {
                        // Bunbun: the mascot stands alone here, so it gets a slot a third
                        // larger than the cover and its real outline (head, popcorn) fills it.
                        // Skin packs already fill their frame, so they keep the cover's slot.
                        const float side = size * 1.35f;
                        D2D1_POINT_2F fc{};
                        float fs = 0.0f;
                        bb::mascot::FitToBox(D2D1::RectF(c.x - side * 0.5f, c.y - side * 0.5f, c.x + side * 0.5f, c.y + side * 0.5f),
                                             &fc, &fs);
                        c = fc;
                        size = fs / 1.05f;
                    }
                    double moodSince = 0.0;  // Bunbun: a logged glass plays here too
                    bb::mascot::Mood base = MediaMascotMood(state.media, settings);
                    // Bunbun: an agent at work shows here too (with the agent pill off, this
                    // pill is all there is while music plays); else Work mode; and away or
                    // asleep over all of them.
                    if (settings.bunbunAgents && state.agent.phase != bb::AgentPhase::None) {
                        base = AgentMood(state.agent.phase);
                        g_layoutDirty = true;
                    } else if (WorkMood(state.work, &base)) {
                        g_layoutDirty = true;
                    }
                    AwayMood(settings, state, &base);
                    const bb::mascot::Mood pillMood = WithWaterMood(base, now, &moodSince);
                    if (pillMood == base && base != MediaMascotMood(state.media, settings)) moodSince = state.agent.phaseSince;
                    bb::mascot::Draw(target_.Get(), c, size * 1.05f, pillMood, now, moodSince, MascotLook(c));
                    if (mLayer) target_->PopLayer();
                }

                if (artLayer) {
                    target_->PopLayer();
                }
            }
        }
    }

    // ═══ Immersive lyrics (Modules.MediaTabLyrics) ═══════════════════════════
    // Cross-fades the 380x184 media card into a lyrics-focused view: small cover
    // circle, one-line header, spring-scrolled lyric lines, accent glow behind.
    // Scrubber, controls and source dock are untouched.

#include "modules/renderer_lyrics_draw.inc"  // Bunbun: Renderer: immersive lyrics and collapsed-pill lyrics

#include "modules/renderer_media_anim_draw.inc"  // Bunbun: Renderer: source switch and dock, skip / play morphs, title, progress and album-art animations

#include "modules/renderer_spectrum_draw.inc"  // Bunbun: Renderer: audio spectrum styles

#include "modules/renderer_banners_draw.inc"  // Bunbun: Renderer: brightness, volume, waveform, notification and device banners

#include "modules/renderer_agent_draw.inc"  // Bunbun: Renderer: agent pill and approval card

#include "modules/renderer_lookup_draw.inc"  // Bunbun: Renderer: Quick Lookup card and panel

   public:
    // Bunbun: copies the pill (plus room for its shadow) out of the last frame,
    // premultiplied BGRA, top-down, with its rect on screen. Callers hold
    // g_frameMutex. False when nothing is shown.
    bool Snapshot(std::vector<uint8_t>* out, int* w, int* h, RECT* screen) {
        if (!memDc_ || !stageValid_ || !g_pillClientRect.valid) return false;
        const PillClientRect& pr = g_pillClientRect;
        if (pr.right - pr.left < 2.0f || pr.bottom - pr.top < 2.0f) return false;
        constexpr int kMargin = 30;
        RECT r = {std::max(0, static_cast<int>(std::floor(pr.left)) - kMargin),
                  std::max(0, static_cast<int>(std::floor(pr.top)) - kMargin),
                  std::min(bitmapWidth_, static_cast<int>(std::ceil(pr.right)) + kMargin),
                  std::min(bitmapHeight_, static_cast<int>(std::ceil(pr.bottom)) + kMargin)};
        const int cw = r.right - r.left, ch = r.bottom - r.top;
        if (cw <= 0 || ch <= 0) return false;

        BITMAPINFO bi = {};
        bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bi.bmiHeader.biWidth = cw;
        bi.bmiHeader.biHeight = -ch;
        bi.bmiHeader.biPlanes = 1;
        bi.bmiHeader.biBitCount = 32;
        void* bits = nullptr;
        HBITMAP copy = CreateDIBSection(memDc_, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
        if (!copy || !bits) return false;
        HDC dc = CreateCompatibleDC(memDc_);
        HGDIOBJ old = SelectObject(dc, copy);
        BitBlt(dc, 0, 0, cw, ch, memDc_, r.left, r.top, SRCCOPY);  // keeps the alpha bytes
        GdiFlush();
        out->assign(static_cast<uint8_t*>(bits), static_cast<uint8_t*>(bits) + static_cast<size_t>(cw) * ch * 4);
        SelectObject(dc, old);
        DeleteDC(dc);
        DeleteObject(copy);
        *w = cw;
        *h = ch;
        *screen = {lastOrigin_.x + r.left, lastOrigin_.y + r.top, lastOrigin_.x + r.right, lastOrigin_.y + r.bottom};
        return true;
    }

   private:
    HWND hwnd_ = nullptr;
    HDC memDc_ = nullptr;
    HBITMAP dib_ = nullptr;
    HBITMAP oldBitmap_ = nullptr;
    D2D1_POINT_2F bindOffset_{};  // Bunbun: top-left of the region bound this frame (see Render)
    int bitmapWidth_ = 0;     // logical size of the frame being drawn
    int bitmapHeight_ = 0;
    int bitmapCapW_ = 0;      // allocated size of the DIB (>= logical size)
    int bitmapCapH_ = 0;

    // Fixed-stage bookkeeping (see Render)
    POINT lastOrigin_ = {0, 0};
    RECT lastBounds_ = {0, 0, 0, 0};
    BYTE lastAlpha_ = 0;
    bool stageValid_ = false;
    double lastFullPaint_ = -1e9;

    // Pills drawn this frame, in client px, for the DWM backdrop windows (#59).
    D2D1_RECT_F pillRects_[DwmBackdrop::kMaxPills] = {};
    float pillRadii_[DwmBackdrop::kMaxPills] = {};
    int pillCount_ = 0;

    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<ID2D1DCRenderTarget> target_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IDWriteTextFormat> textFormat_;
    ComPtr<IDWriteTextFormat> smallTextFormat_;
    ComPtr<IDWriteTextFormat> boldTextFormat_;
    ComPtr<IDWriteTextFormat> hugeTextFormat_;
    ComPtr<IDWriteTextFormat> clockFormat_;
    ComPtr<IDWriteTextFormat> iconFormat_;
    ComPtr<IDWriteTextFormat> mediaPlayIconFormat_;
    ComPtr<IDWriteTextFormat> mediaNavIconFormat_;
    bool usingFluentIcons_ = true;
    ComPtr<IDWriteTextFormat> idleTextFormat_;
    ComPtr<IDWriteTextFormat> calDayLargeFormat_;
    ComPtr<IDWriteTextFormat> calGridFormat_;
    ComPtr<IDWriteTextFormat> timeDashboardFormat_;
    ComPtr<IDWriteTextFormat> dateDashboardFormat_;
    ComPtr<ID2D1SolidColorBrush> accentBrush_;
    ComPtr<ID2D1SolidColorBrush> redBrush_;
    ComPtr<ID2D1SolidColorBrush> textBrush_;
    ComPtr<ID2D1SolidColorBrush> mutedBrush_;
    ComPtr<ID2D1SolidColorBrush> tintBrush_;
    ComPtr<ID2D1SolidColorBrush> shadowBrush_;
    ComPtr<ID2D1SolidColorBrush> micDotBrush_;
    ComPtr<ID2D1SolidColorBrush> micGlowBrush_;
    ComPtr<ID2D1SolidColorBrush> camDotBrush_;
    ComPtr<ID2D1SolidColorBrush> camGlowBrush_;
    ComPtr<ID2D1SolidColorBrush> scratchColorBrush_;
    MarqueeLayoutCache marqueeTitleCache_;
    MarqueeLayoutCache marqueeArtistCache_;
    MarqueeLayoutCache marqueeAlbumCache_;
    MarqueeLayoutCache marqueeClipboardCache_;
    MarqueeLayoutCache marqueeNotificationCache_;
    MarqueeLayoutCache marqueeAgentCache_;
    ComPtr<ID2D1SolidColorBrush> agentInkBrush_;  // created on first use by AgentInk
    ComPtr<ID2D1Bitmap> artBitmap_;
    ComPtr<ID2D1Bitmap> notificationIconBitmap_;
    ComPtr<ID2D1Bitmap> mediaSourceIconBitmap_;
    ComPtr<ID2D1Bitmap> clipboardIconBitmap_;
    ComPtr<ID2D1Bitmap> clipboardImageBitmap_;
    uint64_t artGeneration_ = 0;
    SYSTEMTIME calendarCachedDate_{};
    std::wstring calendarCachedMonthName_;
    std::wstring calendarCachedWeekdayName_;
    std::array<std::wstring, 7> calendarCachedInitials_;  // indexed 0 = Sunday
    int calendarCachedFirstDay_ = 0;                       // resolved, 0 = Sunday
    int calendarCachedFirstDaySetting_ = -2;               // -2 forces the first fill
    ComPtr<IDWriteTextFormat> weatherDescFormat_;
    float weatherDescFormatSize_ = -1.0f;
    uint64_t notificationIconGeneration_ = 0;
    uint64_t mediaSourceIconGeneration_ = 0;
    uint64_t clipboardIconGeneration_ = 0;
    uint64_t clipboardImageGeneration_ = 0;
    ComPtr<ID2D1Bitmap> fileTrayIconBitmap_;
    uint64_t fileTrayIconGeneration_ = 0;

    // ── Lyrics tab state ─────────────────────────────────────────────────────
    KaraokeLayoutCache karaokeCache_;

    // ── Collapsed lyrics state ───────────────────────────────────────────────
    CollapsedLyricLine collapsedLyricCur_;
    CollapsedLyricLine collapsedLyricPrev_;
    int collapsedLyricIdx_ = -2;            // -2 = nothing built yet (forces a snap)
    std::wstring collapsedLyricTitle_;
    std::wstring collapsedLyricWidthTitle_, collapsedLyricWidthArtist_;
    double collapsedLineSince_ = 0.0;  // Bunbun: when the current line appeared (title marquee start)  // Bunbun: song the pill was sized for
    std::wstring collapsedLyricArtist_;
    double collapsedLyricAnimStart_ = -10.0;

    // ── Media-tab lyrics panel state ─────────────────────────────────────────
    CollapsedLyricLine sideLyricCur_;
    CollapsedLyricLine sideLyricPrev_;
    int sideLyricIdx_ = -2;                 // -2 = nothing built yet (forces a snap)
    std::wstring sideLyricTitle_;
    std::wstring sideLyricArtist_;
    std::wstring sideLyricFamily_;
    float sideLyricScale_ = -1.0f;
    double sideLyricAnimStart_ = -10.0;
    double sideLyricLastDrawn_ = -1.0;

    // ── Immersive lyrics state ───────────────────────────────────────────────
    float immersiveMixLin_ = 0.0f;
    double immersiveMixLast_ = -1.0;
    double immLastDrawn_ = -1.0;
    float immScroll_ = 0.0f;
    float immVel_ = 0.0f;
    float immGlow_ = 0.0f;
    std::wstring immTrackKey_;
    std::unordered_map<int, CollapsedLyricLine> immCache_;
    ComPtr<IDWriteTextFormat> immFormat_;
    float immFmtScale_ = -1.0f;
    std::wstring immFmtFamily_;
    MarqueeLayoutCache marqueeImmersiveCache_;
    LinearGradientSlot immKaraokeGrad_;
    float lyricsScrollPos_ = 0.0f;
    float lyricsScrollAnimStartPos_ = 0.0f;
    float lyricsScrollAnimTargetPos_ = 0.0f;
    double lyricsScrollAnimStartTime_ = -1.0;
    std::wstring lyricsScrollTrackKey_;
    int lyricsHighlightIndex_ = -1;
    int lyricsHighlightPrevIndex_ = -1;
    double lyricsHighlightFadeStartTime_ = -1.0;
    ComPtr<IDWriteTextFormat> lyricsActiveTextFormat_;
    float lyricsActiveTextFormatSize_ = -1.0f;
    ComPtr<IDWriteTextFormat> lyricsActiveFitTextFormat_;
    float lyricsActiveFitTextFormatSize_ = -1.0f;
    float lyricsWaveformAlpha_ = 0.0f;
    double lyricsWaveformFadeTime_ = -1.0;
    double lyricsLastDrawn_ = -1.0;  // last frame the lyrics tab was painted
    float  hoverScale_ = 1.0f;       // pill scale set by Render(); gives the resting expanded size
    float  islandSizeLastW_ = 0.0f;  // island resize tracking (see DrawMedia)
    float  islandSizeLastH_ = 0.0f;
    double islandSizeMovedAt_ = -1e9;
    bool   islandSettled_ = true;    // false while the island is still springing to a new size
    float  lyricsFxMix_ = 1.0f;      // 0 while resizing, eases to 1 after; scales lyric effects
    double lyricsFxLast_ = -1.0;
    float settingsOpacity_ = 0.96f;
    D2D1_COLOR_F pillBgColor_ = D2D1::ColorF(0.031f, 0.031f, 0.039f, 1.0f);
    // Design tokens for the current frame, rebuilt by EnsureBrushes.
    MaterialTokens material_{};
    // Accent color lerp: smoothly transition between successive sampled accents
    // so track changes don't produce a jarring instant color pop.
    D2D1_COLOR_F currentAccent_ = D2D1::ColorF(0x4cc9f0);
    double       lastAccentTime_ = -1.0;  // -1 = not yet set (will snap on first frame)
    float        mediaBtnPress_[3] = {0.0f, 0.0f, 0.0f};
    double       lastMediaBtnTime_ = -1.0;
    ComPtr<ID2D1PathGeometry> skipTriFull_;   // skip icons: rounded triangle, left edge at x=0
    ComPtr<ID2D1PathGeometry> skipTriNotch_;  // same, with the front triangle's gap cut out
    unsigned     skipSeenTrigger_[2] = {0, 0};
    double       skipAnimStart_[2] = {-1.0, -1.0};
    float        skipProgress_[2] = {-1.0f, -1.0f};  // index 0 = previous, 1 = next; <0 = idle, else 0..1
    double       lastSkipClickAt_ = -1.0;
    float        lastSkipDir_ = 1.0f;
    float        playMorph_ = 0.0f;  // play/pause morph: 0 = pause bars, 1 = play triangle
    float        playMorphFrom_ = 0.0f;
    float        playMorphTarget_ = 0.0f;
    double       playMorphStart_ = -1.0;
    double       playMorphLastDrawn_ = -1.0;
    bool         playMorphInit_ = false;
    ComPtr<ID2D1StrokeStyle> roundJoinStyle_;
    std::vector<TitleGlyph> titleOld_;  // title change: shared letters slide, the rest dissolve
    std::vector<TitleGlyph> titleNew_;
    std::wstring titleShown_;
    bool         titleShownPlaceholder_ = false;
    bool         titleAnimActive_ = false;
    double       titleAnimStart_ = 0.0;
    double       titleLastDrawn_ = -1.0;
    float        titleAnimDur_ = 1.0f;
    float        titleAvail_ = 0.0f;
    TitleScroll  titleOldScroll_;
    TitleScroll  titleNewScroll_;
    SwapTextState swapArtist_;
    SwapTextState swapAlbum_;
    ComPtr<ID2D1Bitmap> artFlipOld_;  // album art flip
    double       artFlipStart_ = -1.0;
    double       artFlipLastDrawn_ = -1.0;
    uint64_t     artFlipHash_ = 0;
    uint64_t     artHashGen_ = 0;
    uint64_t     artHashValue_ = 0;
    bool         artFlipInit_ = false;
    ComPtr<ID2D1Bitmap> pillArtOld_;    // pill cover change: outgoing disc during a drop
    ComPtr<ID2D1Bitmap> pillArtLast_;   // bitmap drawn in the previous pill frame
    double       pillArtStart_ = -1.0;  // -1 = idle
    double       pillArtLastDrawn_ = -1.0;
    double       pillArtFullAt_ = -1.0; // when the last full drop started
    double       artOuterLast_ = -1.0;  // shared art pass: last frame it ran
    float        artOuterAlpha_ = 1.0f; // shared art pass: eased opacity
    bool         upkeepPending_ = false;  // z-order upkeep requested, throttled in Render()
    double       lastUpkeepAt_ = -1e9;    // last time ApplyOverlayZOrder actually ran
    bool         islandHiding_ = false; // target size is 0 (auto-hide / fullscreen): art shrinks with the pill
    float        artMorphLin_ = 0.0f;   // art circle->square progress (linear, before easing)
    double       artMorphLast_ = -1.0;
    float        artBadgeAlpha_ = 1.0f; // source badge opacity on the expanded art
    double       pillTitlePendingAt_ = -1.0;
    uint64_t     pillArtHash_ = 0;
    std::wstring pillTitle_;
    float        pillArtDir_ = 1.0f;    // +1 clockwise (next), -1 counter-clockwise (previous)
    bool         pillArtPulse_ = false; // same cover, new track
    bool         pillArtInit_ = false;
    bool         expandedAnim_ = false;   // Settings::expandedMediaAnim, latched per frame
    bool         pillCoverAnim_ = false;  // Settings::pillCoverAnim, latched per frame
    bool         specInit_ = false;     // audio spectrum state
    double       specLastDrawn_ = -1.0;
    double       specStart_ = -1.0;     // -1 = no track-change animation running
    size_t       specKey_ = 0;
    float        specT_ = -1.0f;        // 0..1 animation progress, <0 when idle
    float        specDir_ = 1.0f;
    float        specPhase_ = 0.0f;
    float        specBass_ = 0.0f;
    float        specBassSlow_ = 0.0f;
    float        specKick_ = 0.0f;
    float        specPeak_[kSpectrumBands] = {};
    float        specPeakVel_[kSpectrumBands] = {};
    float        progressAmp_ = 1.0f;   // wavy/squiggle bar: wave strength, eases to 0 while paused
    double       progressLastTime_ = -1.0;

    // ── Source switch + dock ────────────────────────────────────────────────
    int          swapPhase_ = 0;        // kSwapNone
    double       swapStart_ = 0.0;
    double       swapLastUpdate_ = -1.0;
    float        swapDir_ = 1.0f;
    unsigned     swapSeenSeq_ = 0;
    std::wstring swapTarget_;
    ComPtr<ID2D1Layer> swapGroupLayer_;
    D2D1_MATRIX_3X2_F swapGroupOldTransform_ = D2D1::Matrix3x2F::Identity();
    bool         dockInit_ = false;
    int          dockSlotCount_ = 0;
    double       dockLastDrawn_ = -1.0;
    float        dockIndPos_ = 0.0f;
    float        dockIndFrom_ = 0.0f;
    float        dockIndTarget_ = 0.0f;
    double       dockIndStart_ = -1.0;
    float        dockHover_[MediaLayout::kDockMaxSlots] = {};
    float        dockPress_[MediaLayout::kDockMaxSlots] = {};
    ComPtr<ID2D1Bitmap> dockIconBitmap_[MediaLayout::kDockMaxSlots];
    uint64_t     dockIconGen_[MediaLayout::kDockMaxSlots] = {};
    MarqueeLayoutCache marqueeSourceCache_;

    // ── Tab transition state ────────────────────────────────────────────────
    int          tabContext_ = -1;      // 0 = media view, 1 = idle view (ids mean different things)
    int          tabCount_ = 0;
    bool         tabInit_ = false;      // false = next draw snaps to the selected tab
    bool         tabAnimating_ = false;
    float        tabPos_ = 0.0f;        // continuous position the spring moves
    float        tabTarget_ = 0.0f;
    float        tabVel_ = 0.0f;
    float        tabDir_ = 1.0f;        // +1 = last move went forward, -1 = backward
    double       tabLastDrawn_ = -1.0;
    ComPtr<ID2D1Layer> tabLayer_;       // reused layer for the tab fade

    // ── Cached resources (see "Cached Direct2D resources") ──────────────────
    std::vector<ComPtr<ID2D1SolidColorBrush>> brushPool_;
    size_t brushPoolUsed_ = 0;
    std::vector<ComPtr<ID2D1Layer>> layerPool_;
    size_t layerPoolUsed_ = 0;
    ComPtr<ID2D1SolidColorBrush> raisedBrush_;        // DrawCard fill
    ComPtr<ID2D1SolidColorBrush> raisedStrongBrush_;  // active card, badges, tracks
    LinearGradientSlot depthGrad_;
    LinearGradientSlot trackGrad_;
    RadialGradientSlot bloomGrad_;
    ShapeEntry shapeCache_[kShapeCacheSize];
    uint64_t shapeClock_ = 0;
};

Activity ActivityForKind(IslandKind kind, const Settings& settings, const SharedState& state);
#include "modules/rest_island_activities.inc"  // Bunbun: The rest island and activity choice

#include "modules/input_hooks.inc"  // Bunbun: Low-level keyboard and mouse hook threads

#include "modules/file_tray_store.inc"  // Bunbun: File Tray persistence

#include "modules/file_tray_dnd.inc"  // Bunbun: File Tray drag and drop (target, source, drag start)

#include "modules/overlay_wndproc.inc"  // Bunbun: OverlayWndProc: the island window procedure

#include "modules/render_thread.inc"  // Bunbun: The render thread: frame loop and window messages

void StopThreads() {
    UnsubscribeDndNotification();
    BrainShutdown();  // Bunbun: the local AI helper
    if (g_keyboardThreadId != 0) {
        PostThreadMessageW(g_keyboardThreadId, WM_QUIT, 0, 0);
    }
    if (g_mouseThreadId != 0) {
        PostThreadMessageW(g_mouseThreadId, WM_QUIT, 0, 0);
    }
    if (g_stopEvent) {
        SetEvent(g_stopEvent);
    }
    screentime::Save();  // Bunbun: today's Screen Time

    HANDLE handles[] = {g_renderThread, g_mediaThread, g_audioThread, g_weatherThread, g_lyricsThread, g_notificationThread, g_keyboardThread, g_mouseThread, g_bluetoothThread, g_connectivityThread,
                       g_calendarThread, g_downloadsThread, g_privacyThread, g_todoThread, g_waterThread};
    for (HANDLE handle : handles) {
        if (handle) {
            WaitForSingleObject(handle, 3000);
            CloseHandle(handle);
        }
    }

    g_renderThread = nullptr;
    g_mediaThread = nullptr;
    g_audioThread = nullptr;
    g_weatherThread = nullptr;
    g_lyricsThread = nullptr;
    g_notificationThread = nullptr;
    g_keyboardThread = nullptr;
    g_keyboardThreadId = 0;
    g_mouseThread = nullptr;
    g_mouseThreadId = 0;
    g_bluetoothThread = nullptr;
    g_connectivityThread = nullptr;
    g_calendarThread = nullptr;
    g_downloadsThread = nullptr;
    g_privacyThread = nullptr;
    g_todoThread = nullptr;
    if (g_todoWake) {
        CloseHandle(g_todoWake);
        g_todoWake = nullptr;
    }
    g_waterThread = nullptr;  // Bunbun
    if (g_waterWake) {
        CloseHandle(g_waterWake);
        g_waterWake = nullptr;
    }
    g_workThread = nullptr;  // Bunbun: Work mode
    if (g_workWake) {
        CloseHandle(g_workWake);
        g_workWake = nullptr;
    }
    if (g_connectivityWake) {
        CloseHandle(g_connectivityWake);
        g_connectivityWake = nullptr;
    }

    if (g_stopEvent) {
        CloseHandle(g_stopEvent);
        g_stopEvent = nullptr;
    }
    if (g_settingsChangedEvent) {
        CloseHandle(g_settingsChangedEvent);
        g_settingsChangedEvent = nullptr;
    }
    if (g_mediaRefreshEvent) {
        CloseHandle(g_mediaRefreshEvent);
        g_mediaRefreshEvent = nullptr;
    }

    g_running = false;
}



}  // namespace

BOOL IslandStart() {
    LoadSettings();

    if (!StartThreads()) {
        StopThreads();
        return FALSE;
    }

    g_layoutDirty = true;
    Wh_Log(L"Dynamic Island for Windows initialized.");
    return TRUE;
}

void IslandReloadSettings() {
    LoadSettings();
}

void IslandStop() {
    if (g_hwnd) {
        PostMessageW(g_hwnd, WM_CLOSE, 0, 0);
    }
    StopThreads();
    Wh_Log(L"Dynamic Island for Windows unloaded.");
}


// ── Bunbun hooks into the island ────────────────────────────────────────────

double IslandNow() {
    return NowSeconds();
}

// Bunbun: "a new version is available", on the island. Clicking it opens the
// release page (see the notification click in OverlayWndProc).
void IslandAnnounceUpdate(const wchar_t* version, const wchar_t* url) {
    {
        std::lock_guard lock(g_stateMutex);
        g_updatePageUrl = url ? url : L"";
        g_state.notification.active = true;
        g_state.notification.app = L"Bunbun";
        g_state.notification.title = std::wstring(L"Bunbun ") + (version ? version : L"") + L" is ready";
        g_state.notification.body = L"An update is available \x00B7 click to open the download page";
        g_state.notification.icon = {};
        g_state.notification.expiresAt = NowSeconds() + 8.0;
        g_state.notification.shownAt = NowSeconds();  // Bunbun: the pill's animation counts from here
    }
    g_layoutDirty = true;
    if (HWND hwnd = g_hwnd) PostMessageW(hwnd, WM_APP_NEW_EVENT, 0, 0);
}

void IslandSetAgentView(const bb::AgentView& view, bool nudge) {
    bool changed = false;
    {
        std::lock_guard lock(g_stateMutex);
        const bb::AgentView& cur = g_state.agent;
        changed = cur.phase != view.phase || cur.requestId != view.requestId || cur.step != view.step ||
                  cur.detail != view.detail || cur.project != view.project || cur.agent != view.agent ||
                  cur.added != view.added || cur.removed != view.removed || cur.queued != view.queued ||
                  cur.sessions != view.sessions || cur.options != view.options || cur.tool != view.tool ||
                  // Bunbun: the Studio agent page and usage ring
                  cur.stage != view.stage || cur.command != view.command || cur.output != view.output ||
                  cur.commandFailed != view.commandFailed || cur.turnStart != view.turnStart ||
                  cur.prompt != view.prompt || cur.diffOld != view.diffOld || cur.diffNew != view.diffNew ||
                  cur.jobsToday != view.jobsToday || cur.history.size() != view.history.size() ||
                  (!view.history.empty() && cur.history.front().finishedAt != view.history.front().finishedAt) ||
                  cur.usage5h != view.usage5h || cur.usage7d != view.usage7d || cur.contextPct != view.contextPct ||
                  cur.usage5hResets != view.usage5hResets || cur.usage7dResets != view.usage7dResets ||
                  cur.plan != view.plan || cur.questions.size() != view.questions.size() ||
                  cur.replyId != view.replyId;
        if (changed) g_state.agent = view;
    }
    NoteAgentActivity(view);  // Bunbun: the agent tile appears once an agent has been active
    {
        // Bunbun: a chime when an agent finishes, needs you, or stops with an error.
        static bb::AgentPhase s_lastPhase = bb::AgentPhase::None;
        static uint64_t s_lastRequest = 0;
        const bool newRequest = view.requestId != 0 && view.requestId != s_lastRequest;
        if (view.phase == bb::AgentPhase::Finished && s_lastPhase != bb::AgentPhase::Finished) PlayChime(Chime::Done);
        else if (view.phase == bb::AgentPhase::Error && s_lastPhase != bb::AgentPhase::Error) PlayChime(Chime::Error);
        else if ((view.phase == bb::AgentPhase::Approval || view.phase == bb::AgentPhase::Question) && newRequest)
            PlayChime(Chime::Question);
        s_lastPhase = view.phase;
        if (view.requestId) s_lastRequest = view.requestId;
    }
    if (!changed) return;
    g_layoutDirty = true;
    if (nudge) {
        TriggerNudge();
    } else if (HWND hwnd = g_hwnd) {
        PostMessageW(hwnd, WM_NULL, 0, 0);  // wakes a parked render loop
    }
}

void IslandOpenAsk() {
    // Same path as the Quick Lookup hotkey, so it runs on the window thread.
    if (HWND hwnd = g_hwnd) PostMessageW(hwnd, WM_HOTKEY, ID_LOOKUP_HOTKEY, 0);
}

void IslandSubmitAsk(const std::wstring& query) {
    if (HWND hwnd = g_hwnd) {
        PostMessageW(hwnd, WM_APP_SUBMIT_ASK, 0, reinterpret_cast<LPARAM>(_wcsdup(query.c_str())));
    }
}

bool IslandAskHotkeyActive() {
    return g_lookupHotkeyRegistered;
}

// Bunbun: the island as it is on screen right now, as PNG bytes with its screen
// rect, plus the rect and work area of the display it is on. Used by the
// settings app's live preview (via the agent pipe). Any thread.
// Bunbun: where display `index` (1-based) shows the island: its mirror's place, so
// the settings app can preview each display. False for the island's own display.
bool IslandDisplayPlace(int index, RECT* screen, RECT* monitor, RECT* work) {
    HMONITOR mon = nullptr;
    RECT shown{};
    if (index <= 0 || !Mirrors::PlaceOf(index, &shown, &mon)) return false;
    MONITORINFO mi = {sizeof(mi)};
    if (!GetMonitorInfoW(mon, &mi)) return false;
    // The snapshot is the island plus a margin; keep its size, centred on the mirror's place.
    const LONG w = screen->right - screen->left, h = screen->bottom - screen->top;
    const LONG cx = (shown.left + shown.right) / 2, top = shown.top;
    const LONG dy = top - screen->top;
    *screen = RECT{cx - w / 2, screen->top + dy, cx - w / 2 + w, screen->top + dy + h};
    *monitor = mi.rcMonitor;
    *work = mi.rcWork;
    return true;
}

bool IslandSnapshotPng(std::string* png, RECT* screen, RECT* monitor, RECT* work) {
    std::vector<uint8_t> bgra;
    int w = 0, h = 0;
    {
        std::lock_guard frameLock(g_frameMutex);
        if (!g_snapshotRenderer || !g_snapshotRenderer->Snapshot(&bgra, &w, &h, screen)) return false;
    }
    MONITORINFO mi = {sizeof(mi)};
    GetMonitorInfoW(MonitorFromRect(screen, MONITOR_DEFAULTTONEAREST), &mi);
    *monitor = mi.rcMonitor;
    *work = mi.rcWork;

    ComPtr<IWICImagingFactory> wic;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&wic)))) return false;
    ComPtr<IWICBitmap> bmp;
    if (FAILED(wic->CreateBitmapFromMemory(w, h, GUID_WICPixelFormat32bppPBGRA, w * 4, static_cast<UINT>(bgra.size()),
                                           bgra.data(), &bmp)))
        return false;
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(nullptr, 0));
    ComPtr<IWICBitmapEncoder> enc;
    ComPtr<IWICBitmapFrameEncode> frame;
    if (!stream || FAILED(wic->CreateEncoder(GUID_ContainerFormatPng, nullptr, &enc)) ||
        FAILED(enc->Initialize(stream.Get(), WICBitmapEncoderNoCache)) || FAILED(enc->CreateNewFrame(&frame, nullptr)) ||
        FAILED(frame->Initialize(nullptr)) || FAILED(frame->WriteSource(bmp.Get(), nullptr)) || FAILED(frame->Commit()) ||
        FAILED(enc->Commit()))
        return false;
    STATSTG st{};
    stream->Stat(&st, STATFLAG_NONAME);
    png->resize(static_cast<size_t>(st.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    stream->Seek(zero, STREAM_SEEK_SET, nullptr);
    ULONG read = 0;
    stream->Read(png->data(), static_cast<ULONG>(png->size()), &read);
    png->resize(read);
    return true;
}

// Bunbun: pictures of every page, taken only when asked (the `capture_pages` pipe
// command), to make the pictures bundled with the settings app. The app no
// longer asks for them, and the island never takes them by itself.
// Holds the island open ("Keep open"), shows each tab in turn, snapshots it and
// puts the tab and Keep open back. About half a second per page, on its own
// thread so neither the pipe nor the render loop waits.
namespace {
const wchar_t* const kPageKeys[] = {L"home", L"music", L"calendar", L"weather", L"system", L"toggles", L"tray", L"todo",
                                    L"lyrics", L"agent", L"clipboard", L"note", L"focus", L"apps", L"mirror", L"water", L"screentime", L"work"};

std::wstring PagePicturesDir() {
    std::wstring dir = bb::StatePath();
    dir = dir.substr(0, dir.find_last_of(L'\\')) + L"\\previews";
    CreateDirectoryW(dir.c_str(), nullptr);
    return dir;
}

bool SavePagePicture(const std::wstring& dir, StudioPage page) {
    const int i = static_cast<int>(page);
    if (i < 0 || i >= static_cast<int>(std::size(kPageKeys))) return false;
    std::string png;
    RECT screen{}, monitor{}, work{};
    if (!IslandSnapshotPng(&png, &screen, &monitor, &work) || png.empty()) return false;
    // Written to a temp name and renamed, so the settings app never reads half a file.
    const std::wstring path = dir + L"\\" + kPageKeys[i] + L".png", temp = path + L".tmp";
    HANDLE f = CreateFileW(temp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    const bool ok = WriteFile(f, png.data(), static_cast<DWORD>(png.size()), &written, nullptr) && written == png.size();
    CloseHandle(f);
    return ok && MoveFileExW(temp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING);
}

}  // namespace

int IslandTestOpen(int tab) {
    if (tab < 0) {
        g_pinnedExpanded = false;
        g_clickExpanded = false;
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP_TEST_CLOSE, 0, 0);
    } else {
        g_pinnedExpanded = true;
        g_idleTab = tab;
        // As the tab bar does: the ring's stop for that page too (else the ring puts the old page back).
        g_activityIndex.store(g_ringActivityCount.load(std::memory_order_relaxed) + tab, std::memory_order_relaxed);
    }
    g_layoutDirty = true;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
    const Settings s = GetSettingsCopy();
    return static_cast<int>(DashboardTabIds(g_dashboardContext.load(std::memory_order_relaxed), s).size());
}

std::string IslandTestHits() {
    std::string out = "[]";
    if (HWND hwnd = g_hwnd) {
        DWORD_PTR r = 0;
        SendMessageTimeoutW(hwnd, WM_APP_TEST_HITS, 0, reinterpret_cast<LPARAM>(&out), SMTO_ABORTIFHUNG, 2000, &r);
    }
    return out;
}

bool IslandTestState(const std::string& kind, const std::wstring& a, const std::wstring& b, int n, int seconds,
                     const std::wstring& lyrics) {
    const double now = NowSeconds();
    const double until = now + std::max(1, seconds);
    {
        std::lock_guard lock(g_stateMutex);
        if (kind == "clear") {  // every made-up event goes; the pollers write the real state again
            g_testMediaUntil = 0.0;
            g_testPrivacyUntil = 0.0;
            if (g_state.media.sourceName == L"Radio") g_state.media = MediaSnapshot{};
            if (g_state.lyrics.synced && g_state.media.title.empty()) g_state.lyrics = LyricsSnapshot{};
            g_state.download = DownloadSnapshot{};
            g_state.progress = ProgressSnapshot{};
            g_state.volume.active = g_state.brightness.active = g_state.capsLock.active = false;
            g_state.notification.active = g_state.privacyAlert.active = g_state.bluetoothDevice.active = false;
            g_state.battery.active = false;
        } else if (kind == "media") {
            g_testMediaUntil = until;
            MediaSnapshot m;
            m.available = true;
            m.playing = n != 0;
            m.title = a.empty() ? L"Test Track" : a;
            m.artist = b.empty() ? L"Test Artist" : b;
            m.albumTitle = L"Test Album";
            m.sourceName = L"Radio";
            m.endTicks = 2400000000LL;  // 4 min, playing from 1:00
            m.positionTicks = 600000000LL;
            m.lastUpdatedTicks = static_cast<int64_t>(GetTickCount64());
            m.titleChangedAt = now;
            g_state.media = std::move(m);
            std::vector<LyricsLine> lines;
            if (lyrics.empty()) {
                for (int i = 0; i < 12; ++i) lines.push_back({i * 5000LL + 50000, L"Test lyric line " + std::to_wstring(i + 1)});
            } else {  // one line every 5 s, the third one being sung at 1:00
                size_t at = 0;
                for (int i = 0; at <= lyrics.size(); ++i) {
                    const size_t bar = lyrics.find(L'|', at);
                    lines.push_back({i * 5000LL + 50000, lyrics.substr(at, bar == std::wstring::npos ? std::wstring::npos : bar - at)});
                    if (bar == std::wstring::npos) break;
                    at = bar + 1;
                }
            }
            LyricsSnapshot ly;
            ly.hasData = true;
            ly.synced = true;
            ly.matchedTitle = g_state.media.title;
            ly.matchedArtist = g_state.media.artist;
            ly.lines = LyricsLines(std::move(lines));
            g_state.lyrics = std::move(ly);
        } else if (kind == "cheer") {  // the mascot's Happy cheer alone (Battery moods), no pill
            g_batteryCheerAt.store(now, std::memory_order_relaxed);
        } else if (kind == "volume") {
            g_state.volume.active = true;
            g_state.volume.percent = n;
            g_state.volume.muted = n == 0;
            g_state.volume.deviceName = L"Speakers";
            g_state.volume.expiresAt = until;
        } else if (kind == "brightness") {
            g_state.brightness = {true, n, until};
        } else if (kind == "caps") {
            g_state.capsLock.active = true;
            g_state.capsLock.capsOn = n != 0;
            g_state.capsLock.isNumEvent = false;
            g_state.capsLock.expiresAt = until;
        } else if (kind == "battery") {
            g_state.battery.active = true;
            g_state.battery.low = n <= 20;
            g_state.battery.percent = n;
            g_state.battery.hasBattery = true;
            g_state.battery.full = n >= 100;
            g_state.battery.expiresAt = until;
            if (n >= 100) g_batteryCheerAt.store(now, std::memory_order_relaxed);  // the "full" cheer
        } else if (kind == "notification") {
            g_state.notification.active = true;
            g_state.notification.app = L"Bunbun";
            g_state.notification.title = a.empty() ? L"Test notification" : a;
            g_state.notification.body = b.empty() ? L"A message for the test" : b;
            g_state.notification.icon = {};
            g_state.notification.shownAt = now;
            g_state.notification.expiresAt = until;
        } else if (kind == "progress") {
            g_state.progress = {n >= 0, std::max(0, n)};
        } else if (kind == "privacy") {
            g_state.privacyAlert.active = true;
            g_state.privacyAlert.text = a.empty() ? L"Test is using your microphone" : a;
            g_state.privacyAlert.kind = 0;
            g_state.privacyAlert.expiresAt = until;
        } else if (kind == "mic" || kind == "camera") {
            g_testPrivacyUntil = until;
            (kind == "mic" ? g_state.system.micActive : g_state.system.cameraActive) = n != 0;
        } else if (kind == "download") {
            g_state.download.active = true;
            g_state.download.finished = n < 0;
            g_state.download.name = a.empty() ? L"test-file.zip" : a;
            g_state.download.bytes = static_cast<ULONG64>(std::max(0, n)) * 1024ULL * 1024ULL;
            g_state.download.bytesPerSecond = 2.5 * 1024 * 1024;
            g_state.download.expiresAt = until;
        } else if (kind == "bluetooth") {
            g_state.bluetoothDevice.active = true;
            g_state.bluetoothDevice.connected = true;
            g_state.bluetoothDevice.deviceName = a.empty() ? L"Test Buds" : a;
            g_state.bluetoothDevice.batteryPercent = n;
            g_state.bluetoothDevice.expiresAt = until;
        } else {
            return false;
        }
    }
    g_layoutDirty = true;
    if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
    return true;
}

bool IslandTestClick(int x, int y) {
    HWND hwnd = g_hwnd;
    if (!hwnd) return false;
    const LPARAM at = MAKELPARAM(x, y);
    SendMessageW(hwnd, WM_MOUSEMOVE, 0, at);
    SendMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, at);
    SendMessageW(hwnd, WM_LBUTTONUP, 0, at);
    g_layoutDirty = true;
    return true;
}

int IslandCapturePages() {
    if (g_capturingPages.exchange(true)) return 0;
    const Settings s = GetSettingsCopy();
    const int count = static_cast<int>(DashboardTabIds(g_dashboardContext.load(std::memory_order_relaxed), s).size());
    std::thread([s] {
        const std::wstring dir = PagePicturesDir();
        auto repaint = [] {
            g_layoutDirty = true;
            if (g_hwnd) PostMessageW(g_hwnd, WM_APP_NEW_EVENT, 0, 0);
        };
        // An idle island draws only when nudged, so keep nudging while a page settles.
        auto settle = [&](int ms) {
            for (int t = 0; t < ms; t += 40) {
                repaint();
                Sleep(40);
            }
        };
        const int oldTab = g_idleTab.load();
        const bool oldPinned = g_pinnedExpanded.exchange(true);
        settle(800);  // the island opens
        // Bunbun: the page list of the view it opened in (media or idle), read only now:
        // read before, a media view's extra Music page put every later picture one file off.
        const int context = g_dashboardContext.load(std::memory_order_relaxed);
        const std::vector<int> ids = DashboardTabIds(context, s);
        for (size_t i = 0; i < ids.size(); ++i) {
            g_idleTab = static_cast<int>(i);
            settle(650);  // the tab slides in and settles
            SavePagePicture(dir, StudioPageFor(context, ids[i]));
        }
        g_idleTab = oldTab;
        g_pinnedExpanded = oldPinned;
        repaint();
        g_capturingPages = false;
    }).detach();
    return count;
}
