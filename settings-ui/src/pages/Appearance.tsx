import { Check } from "lucide-react";
import { useStore } from "../store";
import { PageHead, Section, Setting } from "../ui/controls";
import { LivePreview } from "../ui/live";
import { THEMES } from "../theme";

const SHAPES = [
  { value: "default", label: "Pill", desc: "The classic Dynamic Island capsule", radius: "999px", top: 10 },
  { value: "notch", label: "Notch", desc: "MacBook style, hangs from the top edge", radius: "0 0 14px 14px", top: 0 },
  { value: "w11", label: "Windows 11", desc: "Soft rectangle that matches Fluent", radius: "7px", top: 10 },
];


export function Appearance() {
  const s = useStore();
  const shape = s.get("Appearance.ShapeStyle");
  const theme = s.get("Themes.ThemePreset");
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

      <section className="section">
        <h3>Theme</h3>
        <div className="choices" style={{ gridTemplateColumns: "repeat(auto-fill, minmax(140px, 1fr))" }}>
          {THEMES.map(([id, name, sw]) => (
            <button key={id} className={`choice ${theme === id ? "on" : ""}`} onClick={() => s.set("Themes.ThemePreset", id)}>
              {theme === id && <span className="check"><Check size={13} strokeWidth={3} /></span>}
              <div className="theme-sw">{sw.map((c) => <i key={c} style={{ background: c }} />)}</div>
              <b>{name}</b>
            </button>
          ))}
        </div>
      </section>

      <Section title="Size & text">
        <Setting k="Appearance.SizeScale" />
        <Setting k="Appearance.AutoDpiScale" />
        <Setting k="Themes.TextScale" />
        <Setting k="Themes.FontFamily" description="Any installed font name. Empty uses Segoe UI Variable." />
      </Section>

      <Section title="Material">
        <Setting k="Themes.PillOpacity" />
        <Setting k="Themes.BackdropMaterial" />
        <Setting k="Themes.BackdropFillOpacity" />
        <Setting k="Themes.BackdropTint" />
        <Setting k="Themes.TintIntensity" />
        <Setting k="Themes.MaterialDepth" />
        <Setting k="Themes.DropShadow" />
        <Setting k="Themes.AccentBloom" />
      </Section>

      <Section title="Colours">
        <Setting k="Themes.AccentColorMode" />
        {s.get("Themes.AccentColorMode") === "custom" && <Setting k="Themes.CustomAccentHex" />}
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
