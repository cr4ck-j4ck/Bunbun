// The Edge light section on the Appearance page: its style (with a small animated
// preview of each), what plays it (any mix of triggers), and its colour.
import { Check } from "lucide-react";
import { useStore } from "../store";
import { ColorField, Row, Section, Segmented } from "./controls";

const STYLES = [
  { value: "twin", label: "Twin comets", desc: "From both top corners, meeting at the bottom" },
  { value: "comet", label: "Comet", desc: "One light, ear to ear and back" },
  { value: "breathe", label: "Breathe", desc: "The whole outline glows softly" },
  { value: "off", label: "Off", desc: "No light" },
];

// Bunbun.EdgeLightOn words, in the order they show.
const TRIGGERS = [
  { value: "hover", label: "Hover" },
  { value: "open", label: "Opening" },
  { value: "close", label: "Closing" },
  { value: "track", label: "Track change" },
  { value: "notification", label: "Notifications" },
  { value: "agent", label: "Agents" },
  { value: "system", label: "System pop-ups" },
  { value: "always", label: "Always" },
];
const DEFAULT_ON = "open,notification,agent";

const PALETTE = ["#5AC8FA", "#0A84FF", "#BF5AF2", "#FF375F", "#FF9F0A", "#FFD60A", "#30D158", "#64D2FF", "#FFFFFF"];

// The notch outline, open at the top (that edge is the screen's), ear to ear.
const NOTCH = "M4 3 Q18 3 18 13 L18 22 Q18 33 30 33 L90 33 Q102 33 102 22 L102 13 Q102 3 116 3";
const NOTCH_REVERSED = "M116 3 Q102 3 102 13 L102 22 Q102 33 90 33 L30 33 Q18 33 18 22 L18 13 Q18 3 4 3";

function Preview({ style, color }: { style: string; color: string }) {
  return (
    <svg className={`edge-demo ${style}`} viewBox="0 0 120 40" aria-hidden="true">
      <path d="M0 3 L120 3" stroke="rgba(255,255,255,.08)" strokeWidth="1" fill="none" />
      <path d={NOTCH} fill="#08080a" stroke="rgba(255,255,255,.14)" strokeWidth="1" />
      {style === "twin" && <>
        <path className="comet half" d={NOTCH} pathLength={100} stroke={color} />
        <path className="comet half" d={NOTCH_REVERSED} pathLength={100} stroke={color} />
      </>}
      {style === "comet" && <path className="comet full" d={NOTCH} pathLength={100} stroke={color} />}
      {style === "breathe" && <path className="glow" d={NOTCH} stroke={color} />}
    </svg>
  );
}

export function EdgeLightSettings() {
  const s = useStore();
  const style = s.get("Bunbun.EdgeLight") || "twin";
  const colorMode = s.get("Bunbun.EdgeLightColor") || "default";
  const custom = s.get("Bunbun.EdgeLightCustomColor") || "#5AC8FA";
  const raw = s.get("Bunbun.EdgeLightOn");
  const on = new Set((raw.trim() ? raw : DEFAULT_ON).split(",").map((w) => w.trim().toLowerCase()).filter(Boolean));
  const previewColor = colorMode === "custom" ? custom : colorMode === "dynamic" ? "var(--accent)" : "#ffffff";

  const toggle = (word: string) => {
    const next = new Set(on);
    if (next.has(word)) next.delete(word); else next.add(word);
    const list = TRIGGERS.map((t) => t.value).filter((w) => next.has(w)).join(",");
    // The default is written as unset, so a later new default still applies.
    s.set("Bunbun.EdgeLightOn", list === DEFAULT_ON ? "" : list || "none");
  };

  return (
    <>
      <section className="section">
        <h3>Edge light</h3>
        <div className="choices" style={{ gridTemplateColumns: "repeat(auto-fill, minmax(150px, 1fr))" }}>
          {STYLES.map((st) => (
            <button key={st.value} className={`choice ${style === st.value ? "on" : ""}`} onClick={() => s.set("Bunbun.EdgeLight", st.value)}>
              {style === st.value && <span className="check"><Check size={13} strokeWidth={3} /></span>}
              <Preview style={st.value} color={previewColor} />
              <b>{st.label}</b>
              <small>{st.desc}</small>
            </button>
          ))}
        </div>
      </section>
      {style !== "off" && (
        <Section>
          <Row label="When it plays" description="Pick any mix. Each one plays the light for a few seconds; Always keeps it running (a little more power)."
            settingKeys={["Bunbun.EdgeLightOn"]}>
            <div className="chips edge-on">
              {TRIGGERS.map((t) => (
                <button key={t.value} className={`chip ${on.has(t.value) ? "on" : ""}`} aria-pressed={on.has(t.value)} onClick={() => toggle(t.value)}>
                  {t.label}
                </button>
              ))}
            </div>
          </Row>
          <Row label="Colour" description="Default is white. Dynamic follows the island's accent: the album art's while music plays, else yours."
            settingKeys={["Bunbun.EdgeLightColor", "Bunbun.EdgeLightCustomColor"]}>
            <Segmented value={colorMode === "accent" ? "dynamic" : colorMode} onChange={(v) => s.set("Bunbun.EdgeLightColor", v)}
              options={[{ value: "default", label: "Default" }, { value: "dynamic", label: "Dynamic" }, { value: "custom", label: "Custom" }]} />
          </Row>
          {colorMode === "custom" && (
            <Row label="Custom colour" settingKeys={["Bunbun.EdgeLightCustomColor"]}>
              <div className="swatches">
                {PALETTE.map((c) => (
                  <button key={c} title={c} className={custom.toUpperCase() === c ? "on" : ""} style={{ background: c }}
                    onClick={() => s.set("Bunbun.EdgeLightCustomColor", c)} />
                ))}
              </div>
              <ColorField value={custom} onChange={(v) => s.set("Bunbun.EdgeLightCustomColor", v)} />
            </Row>
          )}
        </Section>
      )}
    </>
  );
}
