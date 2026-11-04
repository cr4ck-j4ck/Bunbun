import { motion } from "framer-motion";
import { RotateCcw } from "lucide-react";
import { ReactNode, useEffect, useId, useState } from "react";
import { neverOnLinux, notOnLinux, useIsLinux } from "../platform";
import { schema, useStore } from "../store";

const spring = { type: "spring", stiffness: 520, damping: 34 } as const;

export function Toggle({ on, onChange, label, disabled }: { on: boolean; onChange: (v: boolean) => void; label?: string; disabled?: boolean }) {
  return (
    <button className={`switch ${on ? "on" : ""}`} role="switch" aria-checked={on} aria-label={label} disabled={disabled}
      onClick={() => onChange(!on)}>
      <motion.span className="knob" animate={{ x: on ? 20 : 0 }} transition={spring} />
    </button>
  );
}

export function Segmented({ value, options, onChange }: { value: string; options: { value: string; label: ReactNode }[]; onChange: (v: string) => void }) {
  const id = useId();
  return (
    <div className="seg" role="radiogroup">
      {options.map((o) => (
        <button key={o.value} className={o.value === value ? "on" : ""} role="radio" aria-checked={o.value === value} onClick={() => onChange(o.value)}>
          {o.value === value && <motion.span layoutId={`seg-${id}`} className="seg-bg" transition={spring} />}
          <span>{o.label}</span>
        </button>
      ))}
    </div>
  );
}

export function Slider({ value, min, max, step = 1, unit = "", onChange, format }: {
  value: number; min: number; max: number; step?: number; unit?: string; onChange: (v: number) => void; format?: (v: number) => string;
}) {
  const p = ((value - min) / (max - min)) * 100;
  return (
    <div className="slider">
      <input type="range" min={min} max={max} step={step} value={value} style={{ ["--p" as string]: `${p}%` }}
        onChange={(e) => onChange(parseFloat(e.target.value))} />
      <output>{format ? format(value) : `${value}${unit}`}</output>
    </div>
  );
}

export function ColorField({ value, onChange, placeholder = "#RRGGBB" }: { value: string; onChange: (v: string) => void; placeholder?: string }) {
  const [text, setText] = useState(value);
  useEffect(() => setText(value), [value]);
  // 3, 4, 6 or 8 hex digits, like the island accepts.
  const valid = /^#?([0-9a-fA-F]{3,4}|[0-9a-fA-F]{6}|[0-9a-fA-F]{8})$/.test(text);
  const hex = text.replace(/^#/, "");
  const full = hex.length <= 4 ? hex.split("").map((c) => c + c).join("") : hex;
  const shown = valid ? `#${full}` : "#000000";
  return (
    <div className="color">
      <label className="swatch" style={{ background: value ? shown : "repeating-conic-gradient(#555 0 25%, #333 0 50%) 0 0/10px 10px" }}>
        <input type="color" value={shown.slice(0, 7)} onChange={(e) => onChange(e.target.value.toUpperCase())} />
      </label>
      <input className="field" value={text} placeholder={placeholder} spellCheck={false}
        onChange={(e) => setText(e.target.value)}
        onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }}
        onBlur={() => { if (text === "" || valid) onChange(text === "" ? "" : (text.startsWith("#") ? text : `#${text}`).toUpperCase()); else setText(value); }} />
    </div>
  );
}

export function TextField({ value, onChange, placeholder, width }: { value: string; onChange: (v: string) => void; placeholder?: string; width?: number }) {
  const [text, setText] = useState(value);
  useEffect(() => setText(value), [value]);
  return (
    <input className="field" style={width ? { width, minWidth: width } : undefined} value={text} placeholder={placeholder}
      spellCheck={false} onChange={(e) => setText(e.target.value)} onBlur={() => text !== value && onChange(text)}
      onKeyDown={(e) => e.key === "Enter" && (e.target as HTMLInputElement).blur()} />
  );
}

export function Select({ value, options, onChange }: { value: string; options: { value: string; label: string }[]; onChange: (v: string) => void }) {
  return (
    <select className="field" value={value} onChange={(e) => onChange(e.target.value)}>
      {options.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
    </select>
  );
}

export function Section({ title, children }: { title?: string; children: ReactNode }) {
  return (
    <section className="section">
      {title && <h3>{title}</h3>}
      <div className="card">{children}</div>
    </section>
  );
}

/** "Not on Linux yet" next to a custom control's heading, when none of its settings work there. */
export function LinuxBadge({ keys }: { keys: string[] }) {
  return useIsLinux() && notOnLinux(keys) ? <em className="badge muted linux">Not on Linux yet</em> : null;
}

export function Row({ label, description, children, settingKeys }: { label: ReactNode; description?: ReactNode; children: ReactNode; settingKeys?: string[] }) {
  const store = useStore();
  const changed = settingKeys?.some((k) => !store.isDefault(k));
  const linux = useIsLinux();
  const linuxMissing = linux && notOnLinux(settingKeys);
  if (linux && neverOnLinux(settingKeys)) return null;  // Windows-only for good
  return (
    <div className="row">
      <div className="label">
        <b>{label}{linuxMissing && <em className="badge muted linux">Not on Linux yet</em>}</b>
        {description && <span>{description}</span>}
      </div>
      <div className="control">
        {settingKeys && (
          <button className={`reset ${changed ? "show" : ""}`} title="Reset to default" onClick={() => store.reset(settingKeys)}>
            <RotateCcw size={14} />
          </button>
        )}
        {children}
      </div>
    </div>
  );
}

const SLIDER_RANGES: Record<string, [number, number, number, string]> = {
  "Themes.PillOpacity": [35, 100, 1, "%"],
  "Themes.TintIntensity": [0, 100, 1, "%"],
  "Themes.BackdropFillOpacity": [0, 100, 1, "%"],
  "Themes.BackdropTint": [0, 100, 1, "%"],
  "Themes.AccentBloom": [0, 200, 5, "%"],
  "Themes.TextScale": [70, 160, 5, "%"],
  "Appearance.OffsetX": [-400, 400, 1, " px"],
  "Appearance.OffsetY": [-60, 200, 1, " px"],
  "Appearance.OffsetYExpanded": [-60, 200, 1, " px"],
  "Modules.LyricsOffsetMs": [-3000, 3000, 50, " ms"],
  "Modules.LyricsOffsetBluetoothMs": [-3000, 3000, 50, " ms"],
  "Modules.FileTrayMaxItems": [1, 30, 1, ""],
  "Bunbun.AmbientFps": [15, 120, 5, " fps"],
  "Bunbun.MascotMotion": [0, 200, 5, "%"],
  "Bunbun.MascotHop": [0, 200, 5, "%"],
  "Bunbun.MascotBlinkEvery": [0, 12, 1, " s"],
  "Bunbun.AfkMinutes": [0, 30, 1, " min"],
  "Appearance.SizeScale": [0.6, 2.5, 0.05, "x"],
  "Bunbun.SleepMinutes": [0, 120, 5, " min"],
  "Bunbun.TuckRevealMs": [0, 2000, 50, " ms"],
};

/** A row for any setting, with the control chosen from its schema entry. */
export function Setting({ k, label, description }: { k: string; label?: string; description?: string }) {
  const store = useStore();
  const s = schema[k];
  if (!s) return null;
  const value = store.get(k);
  const isBool = s.default === "true" || s.default === "false";
  const range = SLIDER_RANGES[k];
  let control: ReactNode;
  if (isBool) control = <Toggle on={store.bool(k)} onChange={(v) => store.set(k, v)} label={s.name} />;
  else if (s.options.length && s.options.length <= 4 && s.options.every((o) => o.label.length < 18))
    control = <Segmented value={value} options={s.options} onChange={(v) => store.set(k, v)} />;
  else if (s.options.length) control = <Select value={value} options={s.options} onChange={(v) => store.set(k, v)} />;
  else if (range) control = <Slider value={store.num(k)} min={range[0]} max={range[1]} step={range[2]} unit={range[3]} onChange={(v) => store.set(k, v)} />;
  else if (/(Hex|Color)$/.test(k)) control = <ColorField value={value} onChange={(v) => store.set(k, v)} />;
  else control = <TextField value={value} onChange={(v) => store.set(k, v)} />;
  return (
    <Row label={label ?? s.name} description={description ?? s.description} settingKeys={[k]}>
      {control}
    </Row>
  );
}

export function PageHead({ title, subtitle, children }: { title: string; subtitle?: string; children?: ReactNode }) {
  return (
    <div className="page-head">
      <div>
        <h2>{title}</h2>
        {subtitle && <p>{subtitle}</p>}
      </div>
      {children}
    </div>
  );
}
