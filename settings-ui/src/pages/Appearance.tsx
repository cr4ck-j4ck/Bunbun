import { EdgeLightSettings } from "../ui/EdgeLight";
import { IdleFaceSettings } from "../ui/IdleFace";
import { Check } from "lucide-react";
import { useStore } from "../store";
import { PageHead, Section, Setting } from "../ui/controls";
import { LivePreview } from "../ui/live";
import { AccentPicker, ThemePresets } from "../ui/ThemeColors";

const SHAPES = [
  { value: "notch", label: "Notch", desc: "MacBook style, hangs from the top edge (default)", radius: "0 0 14px 14px", top: 0 },
  { value: "default", label: "Pill", desc: "The classic Dynamic Island capsule", radius: "999px", top: 10 },
  { value: "w11", label: "Windows 11", desc: "Soft rectangle that matches Fluent", radius: "7px", top: 10 },
];

// Fill: see-through (the theme and the Material settings) or one solid colour.
const FILLS = [
  { value: "transparent", label: "Transparent", desc: "Theme colour; the desktop can show through",
    swatch: () => ({ background: "rgba(8,8,10,.55)", backdropFilter: "blur(6px)", border: "1px solid rgba(255,255,255,.18)" }) },
  { value: "opaque", label: "Opaque", desc: "One steady solid colour",
    swatch: (hex: string) => ({ background: hex, border: "1px solid rgba(255,255,255,.15)" }) },
];

// Solid colours to pick from; Black first. Any hex works in the field below.
const SOLIDS: [string, string][] = [
  ["#000000", "Black"],
  ["#08080A", "Obsidian"],
  ["#1C1C1E", "Graphite"],
  ["#101828", "Navy"],
  ["#0F1A15", "Forest"],
  ["#1A1020", "Plum"],
  ["#2A1F1A", "Espresso"],
  ["#F4F3F1", "White"],
];

export function Appearance() {
  const s = useStore();
  const shape = s.get("Appearance.ShapeStyle");
  const theme = s.get("Themes.ThemePreset");
  const fill = s.get("Themes.IslandFill") || "transparent";
  const solid = s.get("Themes.SolidColor") || "#000000";
  return (
    <div className="page">
      <PageHead title="Appearance" subtitle="Shape, theme and material. The preview below is your real island on your real screen, live." />
      <LivePreview />

      <section className="section">
        <h3>Shape</h3>
        <div className="choices" style={{ gridTemplateColumns: "repeat(3, 1fr)" }}>
          {SHAPES.map((sh) => (
            <button key={sh.value} className={`choice ${shape === sh.value ? "on" : ""}`} onClick={() => s.set("Appearance.ShapeStyle", sh.value)}>
              {shape === sh.value && <span className="check"><Check size={13} strokeWidth={3} /></span>}
              <div style={{ position: "relative", height: 56, borderRadius: 10, background: "linear-gradient(160deg,#2a2340,#1a1626)", overflow: "hidden" }}>
                <div style={{ position: "absolute", left: "50%", top: sh.top, transform: "translateX(-50%)", width: 88, height: 22, background: "#08080a", borderRadius: sh.radius, border: "1px solid rgba(255,255,255,.15)" }} />
              </div>
              <b>{sh.label}</b>
              <small>{sh.desc}</small>
            </button>
          ))}
        </div>
      </section>

      <EdgeLightSettings />

      <section className="section">
        <h3>Theme</h3>
        <ThemePresets />
      </section>

      <Section title="Layout">
        <Setting k="Bunbun.Layout" />
        <Setting k="Bunbun.TuckHotkey" />
      </Section>

      <IdleFaceSettings />

      <Section title="Size & text">
        <Setting k="Appearance.SizeScale" />
        <Setting k="Appearance.MatchScreenSize" />
        <Setting k="Appearance.AutoDpiScale" />
        <Setting k="Themes.TextScale" />
        <Setting k="Themes.FontFamily" description="Any installed font name. Empty uses Segoe UI Variable." />
      </Section>

      <section className="section">
        <h3>Fill</h3>
        <div className="choices" style={{ gridTemplateColumns: "repeat(2, 1fr)" }}>
          {FILLS.map((f) => (
            <button key={f.value} className={`choice ${fill === f.value ? "on" : ""}`} onClick={() => s.set("Themes.IslandFill", f.value)}>
              {fill === f.value && <span className="check"><Check size={13} strokeWidth={3} /></span>}
              <div style={{ position: "relative", height: 56, borderRadius: 10, background: "linear-gradient(120deg,#5b3fa0,#d9667a 55%,#f2b35a)", overflow: "hidden" }}>
                <div style={{ position: "absolute", left: "50%", top: 0, transform: "translateX(-50%)", width: 88, height: 22, borderRadius: "0 0 14px 14px", ...f.swatch(solid) }} />
              </div>
              <b>{f.label}</b>
              <small>{f.desc}</small>
            </button>
          ))}
        </div>
        {fill === "opaque" && (
          <>
            <div className="choices" style={{ gridTemplateColumns: "repeat(4, 1fr)", marginTop: 12 }}>
              {SOLIDS.map(([hex, name]) => (
                <button key={hex} className={`choice ${solid.toUpperCase() === hex ? "on" : ""}`} onClick={() => s.set("Themes.SolidColor", hex)}>
                  {solid.toUpperCase() === hex && <span className="check"><Check size={13} strokeWidth={3} /></span>}
                  <div style={{ height: 28, borderRadius: 8, background: hex, border: "1px solid rgba(255,255,255,.15)" }} />
                  <b>{name}</b>
                </button>
              ))}
            </div>
            <Setting k="Themes.SolidColor" />
          </>
        )}
      </section>

      <Section title="Material">
        {fill !== "opaque" && (
          <>
            <Setting k="Themes.PillOpacity" />
            <Setting k="Themes.BackdropMaterial" />
            <Setting k="Themes.BackdropFillOpacity" />
            <Setting k="Themes.BackdropTint" />
            <Setting k="Themes.TintIntensity" />
            <Setting k="Themes.MaterialDepth" />
          </>
        )}
        <Setting k="Themes.DropShadow" />
        {fill !== "opaque" && <Setting k="Themes.AccentBloom" />}
      </Section>

      <Section title="Colours">
        <AccentPicker />
        <Setting k="Themes.CalendarAccent" />
        <Setting k="Themes.ClockAccentGlow" />
        {theme === "custom" && (
          <>
            <Setting k="Themes.PillBgColor" />
            <Setting k="Themes.TextPrimaryColor" />
            <Setting k="Themes.TextSecondaryColor" />
          </>
        )}
        <Setting k="Themes.ContourBorderMode" />
        <Setting k="Themes.ContourBorderHex" />
      </Section>

      <Section title="Visualizers">
        <Setting k="Appearance.SpectrumStyle" />
        <Setting k="Appearance.ProgressStyle" />
      </Section>
    </div>
  );
}
