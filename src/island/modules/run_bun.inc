// Bunbun: RunBun: executing Ask Bun commands.
// Split out of island_core.cpp; included inside its anonymous namespace at the
// original position, so declaration order is unchanged. Not a standalone file.

// ── Bunbun: Bun's local assistant ───────────────────────────────────────────
// Parses with bb::assistant and carries the intent out. Runs on the window
// thread; returns the card the Ask Bun box shows. Commands that ran set
// `command`, so the card only flashes and the island goes back home.
QuickLookup::Result RunBun(const std::wstring& question) {
    namespace as = bb::assistant;
    QuickLookup::Result r;
    r.source = L"Bun";
    r.subtitle = L"Bun";
    r.title = question;

    std::wstring engine = ToLowerCopy(GetStringSettingCopy(L"Bunbun.SearchEngine"));
    const as::Intent in = as::Parse(question, engine);
    auto open = [](const std::wstring& target) {
        return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", target.c_str(), nullptr, nullptr,
                                                       SW_SHOWNORMAL)) > 32;
    };
    auto setVolume = [](float level) {
        if (!g_endpointVolume) return false;
        g_endpointVolume->SetMasterVolumeLevelScalar(Clamp(level, 0.0f, 1.0f), nullptr);
        g_endpointVolume->SetMute(FALSE, nullptr);
        return true;
    };
    float current = 0.0f;
    if (g_endpointVolume) g_endpointVolume->GetMasterVolumeLevelScalar(&current);
    wchar_t buf[256];

    switch (in.action) {
        case as::Action::None:
        case as::Action::Reply:
            r.body = in.reply;
            r.unknown = in.unknown;  // Bunbun: the local AI may answer instead
            break;
        case as::Action::Lookup:
            r.lookupFetch = true;
            r.lookupTopic = in.target;
            break;
        case as::Action::Open: {
            const std::wstring target = in.target.empty() ? as::FindStartMenuApp(in.label) : in.target;
            if (target.empty()) r.body = L"I couldn’t find “" + in.label + L"”. Try the app’s exact name.";
            else if (open(target)) { r.body = L"Opening " + in.label + L" ✨"; r.command = true; }
            else r.body = L"Windows couldn’t open " + in.label + L".";
            break;
        }
        case as::Action::WebSearch:
            r.command = open(in.target);
            r.body = r.command ? L"Searching the web for “" + in.label + L"”."
                               : std::wstring(L"I couldn’t open your browser.");
            break;
        case as::Action::MediaPrevious: SendMediaTransportCommand(0); r.body = L"Previous track."; r.command = true; break;
        case as::Action::MediaToggle: SendMediaTransportCommand(1); r.body = L"Play / pause."; r.command = true; break;
        case as::Action::MediaNext: SendMediaTransportCommand(2); r.body = L"Next track."; r.command = true; break;
        case as::Action::VolumeSet:
        case as::Action::VolumeUp:
        case as::Action::VolumeDown: {
            float target = in.number / 100.0f;
            if (in.action == as::Action::VolumeUp) target = current + 0.10f;
            if (in.action == as::Action::VolumeDown) target = current - 0.10f;
            BOOL wasMuted = FALSE;
            if (g_endpointVolume) g_endpointVolume->GetMute(&wasMuted);
            if (setVolume(target)) {
                swprintf(buf, 256, L"Volume %d%%.", static_cast<int>(std::lround(Clamp(target, 0.0f, 1.0f) * 100)));
                r.body = buf;
                r.command = true;
                SetBunUndo([previous = current, wasMuted]() {  // Bunbun
                    if (!g_endpointVolume) return std::wstring(L"I can’t reach your speakers right now.");
                    g_endpointVolume->SetMasterVolumeLevelScalar(previous, nullptr);
                    g_endpointVolume->SetMute(wasMuted, nullptr);
                    return std::wstring(L"Volume back to ") + std::to_wstring(static_cast<int>(std::lround(previous * 100))) + L"%.";
                });
            } else {
                r.body = L"I can’t reach your speakers right now.";
            }
            break;
        }
        case as::Action::Mute:
        case as::Action::Unmute:
            if (g_endpointVolume) {
                BOOL wasMuted = FALSE;
                g_endpointVolume->GetMute(&wasMuted);
                g_endpointVolume->SetMute(in.action == as::Action::Mute, nullptr);
                r.body = in.action == as::Action::Mute ? L"Muted. Shh \U0001F92B" : L"Sound is back on.";
                r.command = true;
                SetBunUndo([wasMuted]() {  // Bunbun
                    if (g_endpointVolume) g_endpointVolume->SetMute(wasMuted, nullptr);
                    return std::wstring(wasMuted ? L"Muted again." : L"Sound is back on.");
                });
            } else {
                r.body = L"I can’t reach your speakers right now.";
            }
            break;
        case as::Action::Lock:
            LockWorkStation();
            r.body = L"Locked. See you soon!";
            r.command = true;
            break;
        case as::Action::Screenshot:
            r.command = open(L"ms-screenclip:");
            r.body = r.command ? L"Snip away." : L"Snipping Tool isn’t available.";
            break;
        case as::Action::TimerStart:
            if (!GetSettingsCopy().timerEnabled) { r.body = L"The timer module is turned off in settings."; break; }
            // Bunbun: a plain timer (its pill says "Timer"), not a focus session for Focus Guard.
            StartTimerSeconds(in.number * 60, false);
            {
                std::lock_guard lock(g_stateMutex);
                g_state.timer.label = L"Timer";
            }
            swprintf(buf, 256, L"Timer set for %d minute%ls. I’ll hop when it’s done.", in.number,
                     in.number == 1 ? L"" : L"s");
            r.body = buf;
            r.command = true;
            SetBunUndo([]() { StopFocusTimer(); return std::wstring(L"Timer cancelled."); });  // Bunbun
            break;
        case as::Action::TimerStop:
            StopFocusTimer();
            r.body = L"Timer stopped.";
            r.command = true;
            break;
        // Bunbun: water, as the Water page's + glass (the mascot drinks along).
        case as::Action::WaterAdd: {
            int bottle = 0;
            if (in.number == as::kWaterBottle) {
                std::lock_guard lock(g_stateMutex);
                bottle = g_state.water.bottleMl > 0 ? g_state.water.bottleMl : 750;
            }
            const int ml = bottle ? bottle : in.number > 0 ? in.number : 0;
            const int count = !bottle && in.number < 0 ? -in.number : 1;
            WaterDrinkAmount(ml, count);
            WaterSnapshot w;
            {
                std::lock_guard lock(g_stateMutex);
                w = g_state.water;
            }
            const int added = (ml > 0 ? ml : w.glassMl) * count;
            auto liters = [](int v) {
                wchar_t b[32];
                if (v >= 1000) swprintf(b, 32, L"%.1f L", v / 1000.0);
                else swprintf(b, 32, L"%d ml", v);
                return std::wstring(b);
            };
            r.body = L"Logged " + liters(added) + L". " + liters(w.takenMl) + L" of " + liters(w.goalMl) + L" today" +
                     (w.goalMl > 0 && w.takenMl >= w.goalMl ? L", goal reached! 🎉" : L".");
            r.command = true;
            SetBunUndo([ml, count]() { WaterTakeBack(ml, count); return std::wstring(L"Took that water back."); });
            break;
        }
        case as::Action::WaterUndo:
            WaterUndo();
            r.body = L"Took the last glass back.";
            r.command = true;
            break;
        // Bunbun: Work / Study mode's buttons.
        case as::Action::WorkStart:
        case as::Action::WorkPause:
        case as::Action::WorkResume:
        case as::Action::WorkBreak:
        case as::Action::WorkDone: {
            bool on = false, study = false;
            {
                std::lock_guard lock(g_stateMutex);
                on = g_state.work.on;
                study = g_state.work.study;
            }
            if (!on) {
                r.body = L"Work mode is off. Pick Office work, Both or Study in Settings, Mode.";
                break;
            }
            const std::wstring what = study ? L"Study" : L"Work";
            switch (in.action) {
                case as::Action::WorkStart: WorkCommand(WorkButton::Start); r.body = what + L" started. Have a good one!"; break;
                case as::Action::WorkPause: WorkCommand(WorkButton::Pause); r.body = what + L" paused. Say “back to work” to go on."; break;
                case as::Action::WorkResume: WorkCommand(WorkButton::Resume); r.body = L"Back at it."; break;
                case as::Action::WorkBreak: {
                    WorkCommand(WorkButton::Break);
                    swprintf(buf, 256, L"Break time: %d min. Stretch, drink some water.", std::clamp(Wh_GetIntSetting(L"Work.BreakMinutes"), 1, 120));
                    r.body = buf;
                    break;
                }
                default: WorkCommand(WorkButton::Done); r.body = L"Done for today. Nice work!"; break;
            }
            r.command = true;
            break;
        }
        case as::Action::MascotDndOn:
        case as::Action::MascotDndOff: {
            // Bunbun: written to settings.ini like the settings app does; "off" removes
            // the key so the default applies. The file watcher reloads the island.
            const bool on = in.action == as::Action::MascotDndOn;
            const bool wasOn = GetSettingsCopy().mascotDnd;
            SetBunUndo([wasOn]() {  // Bunbun
                WritePrivateProfileStringW(L"Bunbun", L"MascotDnd", wasOn ? L"true" : nullptr, bb::SettingsPath());
                LoadSettings();
                g_layoutDirty = true;
                return std::wstring(wasOn ? L"Snack time again." : L"I’m back! Bouncing again.");
            });
            WritePrivateProfileStringW(L"Bunbun", L"MascotDnd", on ? L"true" : nullptr, bb::SettingsPath());
            LoadSettings();
            g_layoutDirty = true;
            r.body = on ? L"Snack time. I’ll stay quiet until you say “dnd off”." : L"I’m back! Bouncing again.";
            r.command = true;
            break;
        }
        case as::Action::WifiOn:
        case as::Action::WifiOff:
        case as::Action::BluetoothOn:
        case as::Action::BluetoothOff: {
            // Bunbun: the same switch as the Connectivity cards (connectivity_module.inc).
            const bool bt = in.action == as::Action::BluetoothOn || in.action == as::Action::BluetoothOff;
            const bool on = in.action == as::Action::WifiOn || in.action == as::Action::BluetoothOn;
            SetRadioAsync(bt, on);
            r.body = std::wstring(bt ? L"Bluetooth " : L"Wi-Fi ") + (on ? L"on." : L"off.");
            r.command = true;
            SetBunUndo([bt, on]() {
                SetRadioAsync(bt, !on);
                return std::wstring(bt ? L"Bluetooth " : L"Wi-Fi ") + (on ? L"off again." : L"back on.");
            });
            break;
        }
        case as::Action::BrightnessSet:
        case as::Action::BrightnessUp:
        case as::Action::BrightnessDown: {
            // Bunbun: the built-in screen (bun_actions.inc).
            const int was = CurrentPanelBrightness();
            if (was < 0) {
                r.body = L"I can only change a laptop’s built-in screen. Opening display settings.";
                open(L"ms-settings:display");
                break;
            }
            int target = in.number;
            if (in.action == as::Action::BrightnessUp) target = std::min(100, was + 10);
            if (in.action == as::Action::BrightnessDown) target = std::max(0, was - 10);
            if (SetPanelBrightness(target)) {
                swprintf(buf, 256, L"Brightness %d%%.", target);
                r.body = buf;
                r.command = true;
                SetBunUndo([was]() {
                    SetPanelBrightness(was);
                    return std::wstring(L"Brightness back to ") + std::to_wstring(was) + L"%.";
                });
            } else {
                r.body = L"Windows didn’t let me change the brightness. Opening display settings.";
                open(L"ms-settings:display");
            }
            break;
        }
        case as::Action::Undo:
            r.body = RunBunUndo();
            r.command = r.body != L"Nothing to undo.";
            break;
        case as::Action::FocusStart: {
            // Bunbun: Focus mode (focus_module.inc).
            StartFocus(in.number);
            swprintf(buf, 256, L"Focus for %d min. Notifications wait, the mascot's on do-not-disturb.", in.number);
            r.body = buf;
            r.command = true;
            SetBunUndo([]() {
                g_focusActive.store(false);  // no summary for a session that never really happened
                StopFocusTimer();
                return std::wstring(L"Focus cancelled.");
            });
            break;
        }
        case as::Action::FocusStop:
            if (FocusModeActive()) {
                EndFocus(false, true);  // its summary replaces this card
                return r;
            }
            r.body = L"Focus mode isn’t on.";
            break;
        case as::Action::TodoAdd:
        case as::Action::TodoList:
        case as::Action::TodoDone:
        case as::Action::TodoClearDone: {
            // Bunbun: to-dos and reminders (todos_module.inc). Undo puts the whole
            // list back as it was.
            std::vector<TodoItem> before;
            {
                std::lock_guard lock(g_stateMutex);
                before = g_state.todos;
            }
            bool ran = false;
            r.body = RunTodoIntent(in, &ran);
            r.command = ran;
            if (ran) {
                SetBunUndo([before]() {
                    {
                        std::lock_guard lock(g_stateMutex);
                        g_state.todos = before;
                    }
                    Todos::Commit();
                    return std::wstring(L"Put your list back the way it was.");
                });
            }
            break;
        }
        case as::Action::Info: {
            const bool fahrenheit = GetSettingsCopy().weatherFahrenheit;
            std::lock_guard lock(g_stateMutex);
            const SharedState& s = g_state;
            SYSTEMTIME t;
            GetLocalTime(&t);
            switch (in.info) {
                case as::Info::Time:
                    GetTimeFormatEx(LOCALE_NAME_USER_DEFAULT, TIME_NOSECONDS, &t, nullptr, buf, 256);
                    r.body = std::wstring(L"It’s ") + buf + L".";
                    break;
                case as::Info::Date:
                    GetDateFormatEx(LOCALE_NAME_USER_DEFAULT, DATE_LONGDATE, &t, nullptr, buf, 256, nullptr);
                    r.body = std::wstring(L"Today is ") + buf + L".";
                    break;
                case as::Info::Battery: {
                    SYSTEM_POWER_STATUS ps{};
                    if (GetSystemPowerStatus(&ps) && ps.BatteryLifePercent <= 100) {
                        swprintf(buf, 256, L"Battery at %d%%%ls.", ps.BatteryLifePercent,
                                 ps.ACLineStatus == 1 ? L", plugged in" : L"");
                        r.body = buf;
                        if (ps.BatteryLifeTime != (DWORD)-1 && ps.ACLineStatus != 1) {
                            swprintf(buf, 256, L" About %lu h %lu min left.", ps.BatteryLifeTime / 3600,
                                     (ps.BatteryLifeTime % 3600) / 60);
                            r.body += buf;
                        }
                    } else {
                        r.body = L"This PC doesn’t report a battery.";
                    }
                    break;
                }
                case as::Info::Weather:
                    if (!in.target.empty() || in.number > 0) {
                        // Bunbun: another place, or tomorrow: SubmitLookup fetches it.
                        r.weatherFetch = true;
                        r.weatherPlace = in.target;
                        r.weatherDays = in.number;
                    } else if (s.weather.hasData) {
                        const float temp = fahrenheit ? s.weather.temperature * 9.0f / 5.0f + 32.0f : s.weather.temperature;
                        swprintf(buf, 256, L"%.0f°%ls and %ls in %ls. Feels like %ls°, humidity %ls.", temp,
                                 fahrenheit ? L"F" : L"C", s.weather.weatherDesc.c_str(), s.weather.city.c_str(),
                                 s.weather.feelsLike.c_str(), s.weather.humidity.c_str());
                        r.body = buf;
                    } else {
                        r.body = L"No weather yet. Is the Weather module on and the network up?";
                    }
                    break;
                case as::Info::NowPlaying:
                    if (s.media.available && !s.media.title.empty()) {
                        r.body = std::wstring(s.media.playing ? L"Now playing: " : L"Paused: ") + s.media.title;
                        if (!s.media.artist.empty()) r.body += L" by " + s.media.artist;
                        if (!s.media.sourceName.empty()) r.body += L" (" + s.media.sourceName + L")";
                        r.body += L".";
                    } else {
                        r.body = L"Nothing is playing right now.";
                    }
                    break;
                case as::Info::System:
                    swprintf(buf, 256, L"CPU %d%%, memory %d%% (%.1f of %.1f GB), %d%% disk free.", s.system.cpuPercent,
                             s.system.memoryPercent, s.system.memoryUsedGB, s.system.memoryTotalGB,
                             s.system.diskFreePercent);
                    r.body = buf;
                    break;
                case as::Info::Water: {  // Bunbun
                    const WaterSnapshot& w = s.water;
                    const int pct = w.goalMl > 0 ? std::min(100, w.takenMl * 100 / w.goalMl) : 0;
                    swprintf(buf, 256, L"%d ml of %.1f L today (%d%%), %d glass%ls.", w.takenMl, w.goalMl / 1000.0, pct,
                             w.glasses, w.glasses == 1 ? L"" : L"es");
                    r.body = buf;
                    if (w.nextMinute >= 0) {
                        swprintf(buf, 256, L" Next reminder at %02d:%02d.", w.nextMinute / 60, w.nextMinute % 60);
                        r.body += buf;
                    }
                    break;
                }
                case as::Info::Work: {  // Bunbun
                    const WorkSnapshot& w = s.work;
                    if (!w.on) { r.body = L"Work mode is off. Pick Office work, Both or Study in Settings, Mode."; break; }
                    r.body = std::wstring(L"You’ve ") + (w.study ? L"studied " : L"worked ") + bb::work::Duration(w.Worked());
                    if (w.goal > 0) r.body += L" of " + bb::work::Duration(w.goal);
                    r.body += L" today";
                    const int away = w.seconds[static_cast<int>(bb::work::Kind::Away)] + w.seconds[static_cast<int>(bb::work::Kind::Asleep)];
                    if (away >= 60) r.body += L", away " + bb::work::Duration(away);
                    r.body += L". ";
                    r.body += w.study ? bb::work::StudyLabel(static_cast<bb::work::Phase>(w.phase))
                                      : bb::work::PhaseLabel(static_cast<bb::work::Phase>(w.phase));
                    r.body += L".";
                    break;
                }
                default:
                    r.body = as::HelpText();
            }
            break;
        }
    }
    return r;
}

