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
  "Appearance.SizeScale", "Appearance.OffsetY", "Themes.PillOpacity", "Themes.AccentColorMode", "Themes.CustomAccentHex",
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
  "Bunbun.ShowOtherAgents",
]);

/** True when none of a row's settings do anything on Linux yet. */
export const notOnLinux = (keys: string[] | undefined) => !!keys?.length && keys.every((k) => !LINUX_SETTINGS.has(k));

// Island pages Linux has (Home and Music are always there). Not yet: the file
// shelf and the camera mirror.
export const LINUX_PAGES = new Set(["calendar", "weather", "system", "toggles", "todo", "lyrics", "agent", "clipboard", "note", "focus", "apps"]);
// Home cards Linux shows something else for, until those pages exist (none left).
export const LINUX_CARD_STAND_IN: Record<string, string> = {};
