import { motion } from "framer-motion";
import { Info, MousePointer2 } from "lucide-react";
import { useEffect, useMemo, useState } from "react";
import { call } from "../bridge";
import { useStore } from "../store";
import { PageHead, Row, Section, Segmented, Setting } from "../ui/controls";
import { LivePreview } from "../ui/live";

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

export function Displays() {
  const s = useStore();
  const [monitors, setMonitors] = useState<Monitor[]>([]);
  useEffect(() => { call<Monitor[]>("listMonitors").then(setMonitors); }, []);

  const target = s.get("Appearance.TargetMonitor");
  const follow = target === "follow";
  const all = s.get("Appearance.Displays") === "all";
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

  // Lay the displays out to scale inside the map.
  const layout = useMemo(() => {
    if (!monitors.length) return [];
    const minX = Math.min(...monitors.map((m) => m.bounds.x)), minY = Math.min(...monitors.map((m) => m.bounds.y));
    const maxX = Math.max(...monitors.map((m) => m.bounds.x + m.bounds.w)), maxY = Math.max(...monitors.map((m) => m.bounds.y + m.bounds.h));
    const W = 860, H = 260, pad = 20;
    const k = Math.min((W - pad * 2) / (maxX - minX), (H - pad * 2) / (maxY - minY));
    const ox = (W - (maxX - minX) * k) / 2, oy = (H - (maxY - minY) * k) / 2;
    return monitors.map((m) => ({ m, left: ox + (m.bounds.x - minX) * k + 3, top: oy + (m.bounds.y - minY) * k + 3, w: m.bounds.w * k - 6, h: m.bounds.h * k - 6 }));
  }, [monitors]);

  return (
    <div className="page">
      <PageHead title="Position & displays" subtitle="Pick the screen, where the island sits on it, and whether your other screens get one too." />
      <LivePreview showActualSize={false} />
      <div style={{ height: 18 }} />

      <div className="monitors" style={{ maxWidth: 900 }}>
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
                <b>{m.name}</b>
                {m.bounds.w}×{m.bounds.h}
                <div style={{ display: "flex", gap: 6, marginTop: 4 }}>
                  {m.primary && <span className="badge">Primary</span>}
                  {isMain && !follow && <span className="badge green">Main island</span>}
                  {!isMain && all && <span className="badge">Mirror</span>}
                </div>
              </div>
              <div className="num">{m.index}</div>
            </motion.div>
          );
        })}
      </div>

      <Section title="Displays">
        <Row label="Show the island on" description="Mirrors repeat the main island on other screens. They show everything; hover and clicks work on the main one." settingKeys={["Appearance.Displays"]}>
          <Segmented value={all ? "all" : "single"} options={[{ value: "single", label: "One display" }, { value: "all", label: "All displays" }]}
            onChange={(v) => s.set("Appearance.Displays", v)} />
        </Row>
        <Row label={<span style={{ display: "flex", gap: 8, alignItems: "center" }}><MousePointer2 size={15} /> Follow the mouse</span>}
          description="The island moves to whichever display your cursor is on." settingKeys={["Appearance.TargetMonitor"]}>
          <Segmented value={follow ? "follow" : "fixed"} options={[{ value: "fixed", label: "Fixed display" }, { value: "follow", label: "Follow mouse" }]}
            onChange={(v) => s.set("Appearance.TargetMonitor", v === "follow" ? "follow" : "primary")} />
        </Row>
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
    </div>
  );
}
