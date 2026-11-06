import { motion } from "framer-motion";
import { Info, MousePointer2 } from "lucide-react";
import { useEffect, useMemo, useRef, useState } from "react";
import { call } from "../bridge";
import { useStore } from "../store";
import { PageHead, Row, Section, Segmented, Setting } from "../ui/controls";
import { LivePreview } from "../ui/live";
import { MotionSections } from "./Animations";

type Rect = { x: number; y: number; w: number; h: number };
type Monitor = { index: number; name: string; primary: boolean; bounds: Rect; work: Rect };

const POSITIONS = ["top-left", "top-center", "top-right", "bottom-left", "bottom-center", "bottom-right"];

function PositionGrid({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  return (
    <div className="posgrid" onClick={(e) => e.stopPropagation()}>
      {POSITIONS.map((p) => (
        <button key={p} title={p.replace("-", " ")} className={value === p ? "on" : ""} onClick={() => onChange(p)}
          style={{ alignItems: p.startsWith("top") ? "start" : "end", paddingTop: 4, paddingBottom: 4 }}>
          <i />
        </button>
      ))}
    </div>
  );
}

function parseMap(text: string): Record<number, string> {
  const out: Record<number, string> = {};
  for (const part of text.split(/[;,]/)) {
    const [k, v] = part.split("=");
    if (k && v && POSITIONS.includes(v.trim())) out[parseInt(k, 10)] = v.trim();
  }
  return out;
}

function mapText(map: Record<number, string>) {
  return Object.entries(map).map(([k, v]) => `${k}=${v}`).join(";");
}

// "2=pill;3=1.2" style lists by display number, any value (Appearance.DisplayShapes / DisplaySizes).
function parseAny(text: string): Record<number, string> {
  const out: Record<number, string> = {};
  for (const part of text.split(/[;,]/)) {
    const [k, v] = part.split("=");
    if (k && v && v.trim()) out[parseInt(k, 10)] = v.trim();
  }
  return out;
}

const SHAPES = [{ value: "", label: "Default" }, { value: "pill", label: "Pill" }, { value: "notch", label: "Notch" }, { value: "w11", label: "Windows 11" }];
const SIZES = [{ value: "", label: "Default" }, { value: "0.8", label: "80%" }, { value: "0.9", label: "90%" }, { value: "1.1", label: "110%" },
  { value: "1.25", label: "125%" }, { value: "1.5", label: "150%" }];

export function Displays() {
  const s = useStore();
  const [monitors, setMonitors] = useState<Monitor[]>([]);
  const [previewDisplay, setPreviewDisplay] = useState(0);  // 0 = the main island's display
  useEffect(() => { call<Monitor[]>("listMonitors").then(setMonitors); }, []);

  const target = s.get("Appearance.TargetMonitor");
  const follow = target === "follow";
  const all = s.get("Appearance.Displays") === "all";
  const separate = s.get("Appearance.DisplayLink") === "separate";
  const mainIndex = follow ? -1 : target === "primary" ? (monitors.find((m) => m.primary)?.index ?? 1) : parseInt(target, 10) || 1;
  const mainPos = s.get("Appearance.Position");
  const mirrorDefault = s.get("Appearance.MirrorPosition");
  const perDisplay = parseMap(s.get("Appearance.DisplayPositions"));

  const positionOf = (m: Monitor) => {
    if (m.index === mainIndex || follow) return mainPos;
    return perDisplay[m.index] ?? (POSITIONS.includes(mirrorDefault) ? mirrorDefault : mainPos);
  };
  const setPositionOf = (m: Monitor, p: string) => {
    if (m.index === mainIndex) s.set("Appearance.Position", p);
    else s.set("Appearance.DisplayPositions", mapText({ ...perDisplay, [m.index]: p }));
  };

  // Lay the displays out to scale inside the map, sized to the map's real width
  // (measured, so they never run past its edge) and centred; the map is only as
  // tall as the displays need, up to 300 px.
  const mapRef = useRef<HTMLDivElement>(null);
  const [mapW, setMapW] = useState(0);
  useEffect(() => {
    const el = mapRef.current;
    if (!el) return;
    const ro = new ResizeObserver(() => setMapW(el.clientWidth));
    ro.observe(el);
    setMapW(el.clientWidth);
    return () => ro.disconnect();
  }, []);
  const { layout, mapH } = useMemo(() => {
    if (!monitors.length || mapW <= 0) return { layout: [], mapH: 240 };
    const minX = Math.min(...monitors.map((m) => m.bounds.x)), minY = Math.min(...monitors.map((m) => m.bounds.y));
    const maxX = Math.max(...monitors.map((m) => m.bounds.x + m.bounds.w)), maxY = Math.max(...monitors.map((m) => m.bounds.y + m.bounds.h));
    const pad = 22, maxH = 300;
    const k = Math.min((mapW - pad * 2) / (maxX - minX), (maxH - pad * 2) / (maxY - minY));
    const H = Math.max(180, (maxY - minY) * k + pad * 2);
    const ox = (mapW - (maxX - minX) * k) / 2, oy = (H - (maxY - minY) * k) / 2;
    const gap = 5;  // between displays that touch
    return {
      mapH: H,
      layout: monitors.map((m) => ({ m, left: ox + (m.bounds.x - minX) * k + gap, top: oy + (m.bounds.y - minY) * k + gap,
        w: m.bounds.w * k - gap * 2, h: m.bounds.h * k - gap * 2 })),
    };
  }, [monitors, mapW]);

  return (
    <div className="page">
      <PageHead title="Position & motion" subtitle="Pick the screen and where the island sits on it, then how it moves and when it shows up." />
      {/* With an island on every display, each can be previewed on its own screen. */}
      {all && monitors.length > 1 && (
        <div className="display-pick">
          <span>Preview</span>
          <Segmented value={String(previewDisplay)} onChange={(v) => setPreviewDisplay(parseInt(v, 10))}
            options={[{ value: "0", label: "Main island" }, ...monitors.filter((m) => m.index !== mainIndex).map((m) => ({ value: String(m.index), label: `${m.index} \u00B7 ${m.name}` }))]} />
        </div>
      )}
      <LivePreview showActualSize={false} display={all ? previewDisplay : 0} />
      <div style={{ height: 18 }} />

      <div className="monitors" ref={mapRef} style={{ height: mapH }}>
        {layout.map(({ m, left, top, w, h }) => {
          const isMain = m.index === mainIndex;
          const shown = isMain || all || follow;
          const pos = positionOf(m);
          const pillTop = pos.startsWith("top");
          const align = pos.endsWith("left") ? "8%" : pos.endsWith("right") ? "66%" : "37%";
          return (
            <motion.div key={m.index} className={`monitor ${isMain ? "main" : ""} ${shown ? "" : "off"}`} style={{ left, top, width: w, height: h }}
              whileHover={{ scale: 1.01 }} onClick={() => !follow && s.set("Appearance.TargetMonitor", String(m.index))}
              title={follow ? "" : "Make this the main display"}>
              {shown && <motion.div layout className="mini-pill" style={{ left: align, top: pillTop ? 8 : undefined, bottom: pillTop ? undefined : 10 }} />}
              <div style={{ position: "absolute", top: pillTop ? undefined : 10, bottom: pillTop ? 10 : undefined, right: 10 }}>
                {shown && <PositionGrid value={pos} onChange={(p) => setPositionOf(m, p)} />}
              </div>
              <div className="meta">
                <b><span className="mon-num">{m.index}</span>{m.name}</b>
                {m.bounds.w}×{m.bounds.h}
                <div style={{ display: "flex", gap: 6, marginTop: 4, flexWrap: "wrap" }}>
                  {m.primary && <span className="badge">Primary</span>}
                  {isMain && !follow && <span className="badge green">Main island</span>}
                  {/* Following the mouse, no display is the main one: the island is wherever the cursor is. */}
                  {!isMain && all && !follow && <span className="badge">{separate ? "Own island" : "Linked"}</span>}
                </div>
              </div>
            </motion.div>
          );
        })}
      </div>
      {follow && (
        <p className="section-note" style={{ marginTop: 8 }}>
          The island follows your mouse to whichever display it's on{all ? "; the others show a mirror of it" : ""}.
        </p>
      )}

      <Section title="Displays">
        <Row label="Show the island on" description="One display, or an island on every screen (how they work together is below)." settingKeys={["Appearance.Displays"]}>
          <Segmented value={all ? "all" : "single"} options={[{ value: "single", label: "One display" }, { value: "all", label: "All displays" }]}
            onChange={(v) => s.set("Appearance.Displays", v)} />
        </Row>
        {all && (
          <Row label="How they work" description={separate
              ? "Separate: the island you point at opens on its own, at that display's position. The others stay at rest."
              : "Linked: point at any island and they all open together; clicks and scrolling work on each one."}
            settingKeys={["Appearance.DisplayLink"]}>
            <Segmented value={separate ? "separate" : "linked"} options={[{ value: "linked", label: "Linked" }, { value: "separate", label: "Separate" }]}
              onChange={(v) => s.set("Appearance.DisplayLink", v)} />
          </Row>
        )}
        {!(all && separate) && (  // Separate islands already follow the pointer
        <Row label={<span style={{ display: "flex", gap: 8, alignItems: "center" }}><MousePointer2 size={15} /> Follow the mouse</span>}
          description="The island moves to whichever display your cursor is on." settingKeys={["Appearance.TargetMonitor"]}>
          <Segmented value={follow ? "follow" : "fixed"} options={[{ value: "fixed", label: "Fixed display" }, { value: "follow", label: "Follow mouse" }]}
            onChange={(v) => s.set("Appearance.TargetMonitor", v === "follow" ? "follow" : "primary")} />
        </Row>
        )}
        {all && separate && monitors.map((m) => {
          // Each display's own look (Separate mode): its shape and size, else the island's.
          const shapes = parseAny(s.get("Appearance.DisplayShapes"));
          const sizes = parseAny(s.get("Appearance.DisplaySizes"));
          const setIn = (key: string, map: Record<number, string>, v: string) => {
            const next = { ...map };
            if (v) next[m.index] = v; else delete next[m.index];
            s.set(key, mapText(next));
          };
          return (
            <Row key={`look-${m.index}`} label={`Display ${m.index} \u00B7 ${m.name}`}
              description="Its island's shape and size, at rest and when it opens there. Default follows Appearance."
              settingKeys={["Appearance.DisplayShapes", "Appearance.DisplaySizes"]}>
              <Segmented value={shapes[m.index] ?? ""} options={SHAPES} onChange={(v) => setIn("Appearance.DisplayShapes", shapes, v)} />
              <select className="field" style={{ minWidth: 0, width: 110 }} value={sizes[m.index] ?? ""}
                onChange={(e) => setIn("Appearance.DisplaySizes", sizes, e.target.value)}>
                {SIZES.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
              </select>
            </Row>
          );
        })}
        {all && (
          <Row label="Mirror position" description="Default spot on the other displays. Use each display's grid above to override it." settingKeys={["Appearance.MirrorPosition", "Appearance.DisplayPositions"]}>
            <select className="field" value={POSITIONS.includes(mirrorDefault) ? mirrorDefault : "same"} onChange={(e) => s.set("Appearance.MirrorPosition", e.target.value)}>
              <option value="same">Same as main island</option>
              {POSITIONS.map((p) => <option key={p} value={p}>{p.replace("-", " ").replace(/^\w/, (c) => c.toUpperCase())}</option>)}
            </select>
          </Row>
        )}
      </Section>

      <Section title="Fine-tuning">
        <Setting k="Appearance.Position" />
        <Setting k="Appearance.OffsetX" />
        <Setting k="Appearance.OffsetY" />
        <Setting k="Appearance.SeparateExpandedOffsetY" />
        {s.bool("Appearance.SeparateExpandedOffsetY") && <Setting k="Appearance.OffsetYExpanded" />}
        <Setting k="Appearance.BorderMergedMode" />
      </Section>

      <div className="note" style={{ marginTop: 18 }}>
        <Info size={16} />
        The Notch shape is designed for the top edge. At the bottom of a screen the Pill or Windows 11 shape looks best.
      </div>

      <MotionSections />
    </div>
  );
}
