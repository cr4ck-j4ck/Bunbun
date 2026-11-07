// The island's theme and accent colour, shared by Appearance and General → Look.
// The settings app wears the same colours (theme.ts), so there is one choice, not two.
import { Check } from "lucide-react";
import { useStore } from "../store";
import { ACCENT_PALETTE, THEMES, hexToRgb } from "../theme";
import { Row, Segmented } from "./controls";

/** Theme presets: background, surface and accent of each. */
export function ThemePresets() {
  const s = useStore();
  const theme = s.get("Themes.ThemePreset");
  return (
    <div className="choices" style={{ gridTemplateColumns: "repeat(auto-fill, minmax(140px, 1fr))" }}>
      {THEMES.map(([id, name, sw]) => (
        <button key={id} className={`choice ${theme === id ? "on" : ""}`} onClick={() => s.set("Themes.ThemePreset", id)}>
          {theme === id && <span className="check"><Check size={13} strokeWidth={3} /></span>}
          <div className="theme-sw">{sw.map((c) => <i key={c} style={{ background: c }} />)}</div>
          <b>{name}</b>
        </button>
      ))}
    </div>
  );
}

/** Accent: Dynamic (from what's playing), Solid (one colour) or System (the device accent). */
export function AccentPicker() {
  const s = useStore();
  const raw = s.get("Themes.AccentColorMode");
  const mode = raw === "custom" ? "solid" : raw === "system" ? "system" : "dynamic";
  const hex = s.get("Themes.CustomAccentHex") || "#4cc9f0";
  const known = ACCENT_PALETTE.some(([h]) => h.toLowerCase() === hex.toLowerCase());
  const pick = (h: string) => {
    s.set("Themes.CustomAccentHex", h);
    if (raw !== "custom") s.set("Themes.AccentColorMode", "custom");
  };
  const description = mode === "solid" ? "One fixed colour, whatever is playing."
                    : mode === "system" ? "Your device's accent colour."
                    : "Picked from the album art of what's playing; the theme's colour when nothing is.";
  return (
    <>
      <Row label="Accent colour" description={description} settingKeys={["Themes.AccentColorMode", "Themes.CustomAccentHex"]}>
        <Segmented value={mode}
          onChange={(v) => s.set("Themes.AccentColorMode", v === "solid" ? "custom" : v === "system" ? "system" : "auto")}
          options={[{ value: "dynamic", label: "Dynamic" }, { value: "solid", label: "Solid" }, { value: "system", label: "System" }]} />
      </Row>
      {mode === "solid" && (
        <div className="row palette-row">
          <div className="palette">
            {ACCENT_PALETTE.map(([h, name]) => (
              <button key={h} title={name} aria-label={name} className={`swatch ${h.toLowerCase() === hex.toLowerCase() ? "on" : ""}`}
                style={{ background: h }} onClick={() => pick(h)} />
            ))}
            <label className={`swatch custom ${known ? "" : "on"}`} title="Your own colour" style={known ? undefined : { background: hex }}>
              <input type="color" value={hexToRgb(hex) ? hex : "#4cc9f0"} onChange={(e) => pick(e.target.value)} />
            </label>
          </div>
        </div>
      )}
    </>
  );
}
