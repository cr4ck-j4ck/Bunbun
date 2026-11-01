// The settings app wears the island's colours: its accent follows the theme
// preset, a custom accent, or Windows' accent, exactly like the island.
import { useEffect, useState } from "react";
import { call } from "./bridge";
import type { Store } from "./store";

// id, display name, [background, surface, accent] — approximates each island preset.
export const THEMES: [string, string, string[]][] = [
  ["obsidian", "Obsidian", ["#050507", "#16161c", "#4cc9f0"]],
  ["graphite", "Graphite", ["#202020", "#2c2c2c", "#60cdff"]],
  ["slate", "Slate", ["#141a24", "#1f2836", "#7aa2f7"]],
  ["nord", "Nord", ["#2e3440", "#3b4252", "#88c0d0"]],
  ["evergreen", "Evergreen", ["#0f1a15", "#18271f", "#5bd6a0"]],
  ["espresso", "Espresso", ["#1b1411", "#2a1f1a", "#e0a370"]],
  ["plum", "Plum", ["#1a1020", "#281830", "#d68cff"]],
  ["porcelain", "Porcelain", ["#f4f3f1", "#ffffff", "#3478f6"]],
  ["custom", "Custom", ["#08080a", "#1e1e22", "#b69cff"]],
];

function hexToRgb(hex: string): [number, number, number] | null {
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

  useEffect(() => {
    const theme = THEMES.find(([id]) => id === preset) ?? THEMES[0];
    const accentHex = mode === "custom" && hexToRgb(custom) ? custom
                    : mode === "system" && systemAccent ? systemAccent
                    : theme[2][2];
    const rgb = hexToRgb(accentHex) ?? [182, 156, 255];
    const bg = hexToRgb(theme[2][0]) ?? [14, 13, 20];
    const root = document.documentElement.style;
    root.setProperty("--accent", `rgb(${rgb.join(",")})`);
    root.setProperty("--accent-2", companion(rgb));
    root.setProperty("--accent-grad", `linear-gradient(135deg, rgb(${rgb.join(",")}) 0%, ${companion(rgb)} 100%)`);
    root.setProperty("--accent-soft", `rgba(${rgb.join(",")}, 0.12)`);
    root.setProperty("--accent-line", `rgba(${rgb.join(",")}, 0.6)`);
    // Text on accent fills: dark on light accents, white on dark ones (e.g. Windows blue).
    const luma = (0.2126 * rgb[0] + 0.7152 * rgb[1] + 0.0722 * rgb[2]) / 255;
    root.setProperty("--on-accent", luma > 0.55 ? "#1a1222" : "#ffffff");
    // The window's tint leans toward the theme's background (light presets stay dark here).
    const dark = bg[0] + bg[1] + bg[2] < 300;
    root.setProperty("--bg", dark ? `rgba(${bg.join(",")}, 0.80)` : "rgba(14, 13, 20, 0.78)");
  }, [preset, mode, custom, systemAccent]);
}
