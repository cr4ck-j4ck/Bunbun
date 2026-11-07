// The settings app wears the island's colours: its accent follows the theme
// preset, a solid (custom) accent, or the device accent, exactly like the island.
import { useEffect, useState } from "react";
import { call } from "./bridge";
import type { Store } from "./store";

// id, display name, [background, border, secondary text]: the island's own presets
// (kThemePalettes in island_core.cpp). The accent isn't part of a preset: it comes
// from Themes.AccentColorMode, as on the island.
export const THEMES: [string, string, string[]][] = [
  ["obsidian", "Obsidian", ["#08080A", "#1E1E22", "#9B9BA5"]],
  ["graphite", "Graphite", ["#1C1C1E", "#323236", "#A8A8AE"]],
  ["slate", "Slate", ["#111721", "#232C3A", "#92A2B8"]],
  ["nord", "Nord", ["#2E3440", "#3B4252", "#A7B0C0"]],
  ["evergreen", "Evergreen", ["#0C1512", "#1A2A23", "#8CAE9D"]],
  ["espresso", "Espresso", ["#1A1512", "#2E2420", "#B7A395"]],
  ["plum", "Plum", ["#16111C", "#281F33", "#AC9BB9"]],
  ["porcelain", "Porcelain", ["#F5F6F8", "#D8DBE1", "#5B6270"]],
  ["custom", "Custom", ["#08080A", "#1E1E22", "#9B9BA5"]],
];

// Solid accent colours (Themes.CustomAccentHex): tuned to read well on the dark
// island and window, with enough spread that neighbours don't look alike.
export const ACCENT_PALETTE: [string, string][] = [
  ["#8b7cf6", "Iris"], ["#b69cff", "Lavender"], ["#d68cff", "Orchid"], ["#ff7eb6", "Rose"],
  ["#ff6b6b", "Coral"], ["#ff9f5a", "Tangerine"], ["#f5c451", "Amber"], ["#a3d977", "Lime"],
  ["#4ade80", "Mint"], ["#2dd4bf", "Teal"], ["#4cc9f0", "Sky"], ["#5b9cff", "Ocean"],
  ["#94a3b8", "Slate"], ["#e5e5ea", "Silver"],
];

export function hexToRgb(hex: string): [number, number, number] | null {
  const m = /^#?([0-9a-f]{6})/i.exec(hex.trim());
  if (!m) return null;
  const n = parseInt(m[1], 16);
  return [(n >> 16) & 255, (n >> 8) & 255, n & 255];
}

// A second gradient stop: the accent rotated toward pink and lifted a little.
function companion([r, g, b]: [number, number, number]): string {
  const mix = (a: number, t: number, k: number) => Math.round(a + (t - a) * k);
  return `rgb(${mix(r, 255, 0.35)}, ${mix(g, 143, 0.35)}, ${mix(b, 194, 0.35)})`;
}

/** Applies the island's accent (and a whisper of its theme) to the app. */
export function useAppTheme(store: Store) {
  const [systemAccent, setSystemAccent] = useState("");
  useEffect(() => { call<{ systemAccent?: string }>("getSystem").then((r) => setSystemAccent(r.systemAccent ?? "")); }, []);

  const preset = store.get("Themes.ThemePreset");
  const mode = store.get("Themes.AccentColorMode");
  const custom = store.get("Themes.CustomAccentHex");
  const customBg = store.get("Themes.PillBgColor");

  // Dynamic picks the accent from the album art, which only the island sees: it
  // publishes the colour it shows (getAccent), and the app follows within a moment.
  const [live, setLive] = useState("");
  useEffect(() => {
    if (mode !== "auto" && mode !== "") return;
    let stop = false;
    const poll = () => call<string>("getAccent").then((h) => { if (!stop) setLive(typeof h === "string" ? h : ""); }).catch(() => {});
    poll();
    const id = setInterval(poll, 2000);
    return () => { stop = true; clearInterval(id); };
  }, [mode]);

  useEffect(() => {
    const theme = THEMES.find(([id]) => id === preset) ?? THEMES[0];
    // As the island: custom, system, or (Dynamic) the album art's colour, falling back to the custom hex.
    const accentHex = mode === "custom" && hexToRgb(custom) ? custom
                    : mode === "system" && systemAccent ? systemAccent
                    : mode !== "custom" && mode !== "system" && hexToRgb(live) ? live
                    : hexToRgb(custom) ? custom : "#4cc9f0";
    const rgb = hexToRgb(accentHex) ?? [182, 156, 255];
    const bg = hexToRgb(preset === "custom" && hexToRgb(customBg) ? customBg : theme[2][0]) ?? [14, 13, 20];
    const root = document.documentElement.style;
    root.setProperty("--accent", `rgb(${rgb.join(",")})`);
    root.setProperty("--accent-2", companion(rgb));
    // Flat accent, no two-colour gradient: calmer, like the island itself.
    root.setProperty("--accent-grad", `rgb(${rgb.join(",")})`);
    root.setProperty("--accent-soft", `rgba(${rgb.join(",")}, 0.12)`);
    root.setProperty("--accent-line", `rgba(${rgb.join(",")}, 0.6)`);
    // Text tinted by the accent (badges, notes): mostly white, a little accent.
    const lift = rgb.map((c) => Math.round(c + (255 - c) * 0.62));
    root.setProperty("--accent-text", `rgb(${lift.join(",")})`);
    // Text on accent fills: dark on light accents, white on dark ones (e.g. Windows blue).
    const luma = (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) / 255;
    root.setProperty("--on-accent", luma > 0.55 ? "#1a1222" : "#ffffff");
    // The window's tint leans toward the theme's background (light presets stay dark here).
    const dark = bg[0] + bg[1] + bg[2] < 300;
    root.setProperty("--bg", dark ? `rgba(${bg.join(",")}, 0.80)` : "rgba(14, 13, 20, 0.78)");
  }, [preset, mode, custom, customBg, systemAccent, live]);
}
