// Bunbun: helpers for Ask Bun's commands that aren't parsing: undo, and setting
// the screen brightness.
//
// Undo: each command that changes something leaves behind how to put it back
// (SetBunUndo); "undo" (or Ctrl+Z in the empty box) runs it once. Only the last
// command can be undone. RunBun runs on the window thread, and so does this.
//
// Included in the core's anonymous namespace before RunBun.

std::function<std::wstring()> g_bunUndo;  // returns the reply for "undo"

void SetBunUndo(std::function<std::wstring()> undo) {
    g_bunUndo = std::move(undo);
}

std::wstring RunBunUndo() {
    if (!g_bunUndo) return L"Nothing to undo.";
    auto undo = std::move(g_bunUndo);
    g_bunUndo = nullptr;
    return undo();
}

// Built-in panel brightness, 0-100, or -1 on a desktop monitor. Windows keeps a
// level per power source; this reads and sets the one in use.
bool OnAcPower() {
    SYSTEM_POWER_STATUS ps{};
    return !GetSystemPowerStatus(&ps) || ps.ACLineStatus != 0;
}

int CurrentPanelBrightness() {
    return ReadPanelBrightness(OnAcPower());
}

// Sets the built-in panel's brightness through the display driver, the same
// way Windows' slider does. External monitors (DDC/CI) aren't covered.
bool SetPanelBrightness(int level) {
    constexpr DWORD kIoctlSetDisplayBrightness = 0x23049C;  // IOCTL_VIDEO_SET_DISPLAY_BRIGHTNESS
    struct DisplayBrightness {  // DISPLAY_BRIGHTNESS from ntddvdeo.h
        UCHAR policy;           // 1 = AC, 2 = DC, 3 = both
        UCHAR acBrightness;
        UCHAR dcBrightness;
    };
    HANDLE lcd = CreateFileW(L"\\\\.\\LCD", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, 0, nullptr);
    if (lcd == INVALID_HANDLE_VALUE) return false;
    const UCHAR v = static_cast<UCHAR>(std::clamp(level, 0, 100));
    DisplayBrightness db{static_cast<UCHAR>(OnAcPower() ? 1 : 2), v, v};
    DWORD returned = 0;
    const bool ok = DeviceIoControl(lcd, kIoctlSetDisplayBrightness, &db, sizeof(db), nullptr, 0, &returned, nullptr) != FALSE;
    CloseHandle(lcd);
    return ok;
}
