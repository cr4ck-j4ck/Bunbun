import { useStore } from "../store";
import { Row, Section, Segmented, Setting, Toggle } from "./controls";

// The eye colours the island draws (kEyeColors in studio_draw.inc), in Bunbun.EyeColor order.
const EYE_COLORS: [string, string, string][] = [
  ["mint", "Mint", "#e3ffd4"],
  ["white", "White", "#ffffff"],
  ["blue", "Blue", "#8fd3ff"],
  ["pink", "Pink", "#ffb3d1"],
  ["purple", "Purple", "#d4b3ff"],
  ["yellow", "Yellow", "#fff2a8"],
];

// A small idle island with the two eyes, so the choice is visible before saving.
function EyePreview({ color }: { color: string }) {
  return (
    <svg width="92" height="30" viewBox="0 0 92 30" aria-hidden="true" style={{ flex: "none" }}>
      <path d="M0 0 H92 Q82 0 82 8 V16 Q82 30 68 30 H24 Q10 30 10 16 V8 Q10 0 0 0 Z" fill="#000" />
      <ellipse cx="40.7" cy="14" rx="3.1" ry="5" fill={color} style={{ filter: `drop-shadow(0 0 3px ${color})` }} />
      <ellipse cx="51.3" cy="14" rx="3.1" ry="5" fill={color} style={{ filter: `drop-shadow(0 0 3px ${color})` }} />
    </svg>
  );
}

// Island face: the mascot or two eyes beside the clock, their colour, and what sits next to them.
// Shown at the top of Mascot studio and on Appearance, so it's easy to find.
export function IdleFaceSettings({ title = "Island face" }: { title?: string }) {
  const s = useStore();
  const eyes = s.get("Bunbun.IdleFace") === "eyes";
  const color = String(s.get("Bunbun.EyeColor") || "mint");
  const hex = EYE_COLORS.find(([id]) => id === color)?.[2] ?? EYE_COLORS[0][2];
  return (
    <Section title={title}>
      <Row label="Face on the idle island" description="Your mascot, or just two eyes that follow your pointer and blink, like a little creature living in the notch."
           settingKeys={["Bunbun.IdleFace"]}>
        {eyes && <EyePreview color={hex} />}
        <Segmented value={eyes ? "eyes" : "mascot"} onChange={(v) => s.set("Bunbun.IdleFace", v)}
                   options={[{ value: "mascot", label: "Mascot" }, { value: "eyes", label: "Eyes" }]} />
      </Row>
      {eyes && (
        <>
          <Row label="Eye colour" description="Their resting colour." settingKeys={["Bunbun.EyeColor"]}>
            <div className="swatches">
              {EYE_COLORS.map(([id, name, c]) => (
                <button key={id} title={name} aria-label={name} aria-pressed={color === id}
                        className={color === id ? "on" : ""} style={{ background: c }}
                        onClick={() => s.set("Bunbun.EyeColor", id)} />
              ))}
            </div>
          </Row>
          <Row label="Show what your agents are doing" description="The eyes pulse orange while a coding agent works, turn green when it's done and red on an error. Off keeps them your colour."
               settingKeys={["Bunbun.EyeStatusColors"]}>
            <Toggle on={s.bool("Bunbun.EyeStatusColors")} onChange={(v) => s.set("Bunbun.EyeStatusColors", v)} />
          </Row>
        </>
      )}
      <Setting k="Bunbun.IdleStripItems" />
    </Section>
  );
}
