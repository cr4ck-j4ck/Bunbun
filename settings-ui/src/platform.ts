// Which system the settings app runs on, and what the Linux island has so far.
// The UI is the same build on both; on Linux it marks what isn't there yet
// instead of offering switches that do nothing (see docs/linux.md).
import { useEffect, useState } from "react";
import { call } from "./bridge";

let platform: Promise<string> | null = null;

export function getPlatform(): Promise<string> {
  platform ??= call<{ platform?: string }>("getSystem")
    .then((r) => r.platform ?? "windows")
    .catch(() => "windows");
  return platform;
}

export function useIsLinux(): boolean {
  const [linux, setLinux] = useState(false);
  useEffect(() => { getPlatform().then((p) => setLinux(p === "linux")); }, []);
  return linux;
}

// Settings the Linux island, its agent bridge and hook read. Keep in step with
// src/platform/linux (island_linux.cpp, studio_linux.cpp) and src/agents.
export const LINUX_SETTINGS = new Set([
  "Appearance.SizeScale", "Appearance.MatchScreenSize", "Appearance.OffsetY", "Themes.PillOpacity", "Themes.AccentColorMode", "Themes.CustomAccentHex",
  // edgelight_linux.cpp, timers_linux.cpp, screentime_linux.cpp
  "Bunbun.EdgeLight", "Bunbun.EdgeLightOn", "Bunbun.EdgeLightColor", "Bunbun.EdgeLightCustomColor",
  "Bunbun.PomodoroFocus", "Bunbun.PomodoroShortBreak", "Bunbun.PomodoroLongBreak", "Bunbun.PomodoroCycles", "Bunbun.PomodoroAutoStart",
  "Bunbun.TimerPresets", "Bunbun.ScreenTime", "Bunbun.ScreenTimeLimit",
  "Appearance.DisplayLink", "Appearance.DisplayShapes", "Appearance.DisplaySizes",  // island_linux.cpp: Linked / Separate, each display's look
  // look_linux.cpp: shape, theme, colours, text, sounds, privacy dots
  "Appearance.ShapeStyle", "Themes.ThemePreset", "Themes.PillBgColor", "Themes.TextPrimaryColor", "Themes.ContourBorderMode",
  "Themes.ContourBorderHex", "Themes.DropShadow", "Themes.FontFamily", "Themes.TextScale", "Themes.IslandFill", "Themes.SolidColor",
  "Bunbun.Sounds", "Bunbun.SoundOpenClose", "Bunbun.SoundVolume",
  "Indicators.PrivacyDots", "Indicators.PrivacyDotsMic", "Indicators.PrivacyDotsCam", "Indicators.PrivacyDotsMicHex",
  "Indicators.PrivacyDotsCamHex", "Indicators.PrivacyDotsScreen", "Indicators.PrivacyDotsScreenHex",
  "Bunbun.StreamingMood", "Bunbun.LiveMood", "Modules.MediaTabLyrics",
  // features_linux.cpp: placement, hiding, motion, the music pill, alerts, the date, the eyes, lyrics
  "Appearance.Position", "Appearance.TargetMonitor", "Appearance.Displays", "Appearance.MirrorPosition", "Appearance.DisplayPositions", "Appearance.OffsetX", "Appearance.SeparateExpandedOffsetY",
  "Appearance.OffsetYExpanded", "Appearance.BorderMergedMode", "Appearance.ProgressStyle", "Appearance.SpectrumStyle",
  "Behavior.AlwaysOnTop", "Behavior.AutoHideIdleSeconds", "Behavior.FullscreenMode", "Behavior.FullscreenShowApps", "Behavior.FullscreenTuckApps",
  "Behavior.FullscreenHideApps", "Bunbun.TuckRevealMs", "Bunbun.DndFollowSystem", "Bunbun.AfkMinutes", "Bunbun.SleepMinutes",
  "Bunbun.WaterPage", "Bunbun.WaterReminders", "Bunbun.WaterGoalMl", "Bunbun.WaterGlassMl", "Bunbun.WaterBottleMl", "Bunbun.WaterStartHour", "Bunbun.WaterEndHour",
  "Bunbun.MusicApps", "Bunbun.VideoApps",
  "Behavior.UnhideOnHover", "Shortcuts.HideShowHotkeyEnabled", "Shortcuts.HideShowModifiers", "Shortcuts.HideShowKey",
  "Bunbun.TuckHotkey",
  "Animations.AnimationStyle", "Animations.AnimationSpeed", "Animations.TargetFPS", "Animations.PowerSaver",
  "Animations.PillCoverSpin", "Animations.ExpandedMediaTransitions", "Bunbun.AmbientFps",
  "Themes.TextSecondaryColor", "Themes.MaterialDepth", "Themes.AccentBloom", "Themes.TintIntensity",
  "Modules.CollapsedTitle", "Modules.MediaPillClock", "Modules.MediaAutoExpand", "Modules.MediaExpandBlocklist",
  "Bunbun.MusicSwipe", "Bunbun.MusicApp",
  "Modules.Battery", "Bunbun.BatteryMoods", "Bunbun.ChargingReminderMinutes", "Modules.CapsLock", "Modules.Brightness",
  "Modules.Volume", "Modules.BluetoothIndicator", "Modules.BluetoothShowBattery", "Bunbun.BluetoothLowWarning",
  "Modules.DoNotDisturbIndicator", "Modules.Clipboard", "Modules.StatusCountdownProgress", "Modules.TimerModule",
  "Modules.ReminderSound", "Modules.QuietReminders", "Bunbun.FocusGuard", "Bunbun.FocusGuardApps",
  "Modules.DateFormat", "Modules.FirstDayOfWeek",
  "Bunbun.IdleFace", "Bunbun.EyeColor", "Bunbun.EyeStatusColors", "Bunbun.IdleStripItems", "Bunbun.IdleName", "Bunbun.UseCase", "Work.Days", "Work.Start", "Work.End", "Work.LunchStart", "Work.LunchMinutes", "Work.BreakEvery", "Work.BreakMinutes", "Work.GoalHours", "Work.AutoStart", "Work.Reminders", "Study.Timetable", "Study.GoalHours", "Study.Pomodoro", "Bunbun.StatusRing", "Bunbun.MascotDnd",
  "Modules.KaraokeLyrics", "Modules.LyricsVisualizer", "Modules.KaraokeLetterGlow", "Modules.LyricsNeighborLinesVisible", "Modules.LyricsOffsetBluetoothMs",
  "Modules.ClockFormat", "Modules.ShowSeconds", "Modules.Media", "Behavior.ExpandOnHover",
  "Modules.Weather", "Modules.WeatherCity", "Modules.WeatherFahrenheit", "Modules.HardwareMonitorModule",
  "Modules.ConnectivityModule", "Modules.TodoModule",
  "Bunbun.ClipboardHistory", "Bunbun.QuickNote", "Bunbun.FocusPage", "Bunbun.AppsPage",
  "Modules.Lyrics", "Bunbun.PillLyrics", "Modules.LyricsNetEase", "Modules.LyricsCache", "Modules.LyricsCacheMaxMB",
  "Modules.LyricsOffsetMs",
  "Bunbun.Layout", "Bunbun.PageOrder", "Bunbun.HomeTile1", "Bunbun.HomeTile2", "Bunbun.HomeTile3", "Bunbun.HomeLinks",
  "Bunbun.Mascot", "Bunbun.MascotOnIsland", "Bunbun.Character", "Bunbun.MascotColor", "Bunbun.MascotMotion",
  "Bunbun.MascotHop", "Bunbun.MascotBlinkEvery", "Bunbun.MascotEyesFollow", "Bunbun.MascotCelebrate",
  "Bunbun.Agents", "Bunbun.AgentTab", "Bunbun.AgentPill", "Bunbun.UsageLimits", "Bunbun.ShellApprovals", "Bunbun.MainAgent",
  "Bunbun.ShowClaude", "Bunbun.ShowAntigravity", "Bunbun.ShowCursor", "Bunbun.ShowCopilot", "Bunbun.ShowCodex",
  "Bunbun.ShowOtherAgents", "Bunbun.AntigravityQuota", "Modules.CalendarIcsUrl", "Modules.MeetingAlertMinutes",
  "Modules.FileTrayModule", "Modules.FileTrayMaxItems", "Modules.FileTrayPersist", "Bunbun.MirrorPage",
]);

// Settings that won't come to Linux: Windows' own blur and DPI scaling, the update
// check (the package manager updates Bunbun there), and the game overlay, the
// downloads and progress alerts, notifications and poke reactions, which the
// owner chose to leave Windows-only. The Linux settings app hides them.
export const LINUX_NEVER = new Set([
  "Appearance.AutoDpiScale", "Themes.BackdropMaterial", "Themes.BackdropFillOpacity", "Themes.BackdropTint",
  "Bunbun.UpdateCheck", "Modules.GameOverlay", "Modules.GameOverlayCompact", "Modules.GameOverlayShowFps",
  "Modules.GameOverlayShowCpu", "Modules.GameOverlayShowGpu", "Modules.GameOverlayShowRam", "Modules.GameOverlayShowDisk",
  "Modules.ShowMetricText", "Modules.Downloads", "Modules.Progress", "Modules.NotificationRespectDnD", "Bunbun.PokeReactions",
]);

/** True when none of a row's settings do anything on Linux yet. */
export const notOnLinux = (keys: string[] | undefined) => !!keys?.length && keys.every((k) => !LINUX_SETTINGS.has(k));
/** True when every one of a row's settings is Windows-only for good (hidden on Linux). */
export const neverOnLinux = (keys: string[] | undefined) => !!keys?.length && keys.every((k) => LINUX_NEVER.has(k));

// Island pages Linux has (Home and Music are always there): all of them.
export const LINUX_PAGES = new Set(["calendar", "weather", "system", "toggles", "tray", "todo", "lyrics", "agent", "clipboard", "note", "focus", "apps", "mirror", "water", "screentime", "work"]);
// Home cards Linux shows something else for, until those pages exist (none left).
export const LINUX_CARD_STAND_IN: Record<string, string> = {};
