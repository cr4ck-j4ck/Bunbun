import { Check, FolderOpen, Info, RotateCcw } from "lucide-react";
import { useEffect, useState } from "react";
import { call } from "../bridge";
import { IdleFaceSettings } from "../ui/IdleFace";
import { SheetImport } from "../ui/SheetImport";
import { useStore } from "../store";
import { ColorField, PageHead, Row, Section, Setting, Toggle } from "../ui/controls";
import { colorFor, LiveMascot, MascotImage, MascotStyle, useMascotStyle, withColor } from "../ui/mascot";

const CHARACTERS = [
  ["bunny", "Bun", "The original. Ears for days."],
  ["cat", "Miso", "Pointy ears, whiskers, opinions."],
  ["dog", "Biscuit", "Floppy ears, endless loyalty."],
  ["fox", "Ember", "Clever fox with a white muzzle."],
  ["panda", "Bao", "Sleepy panda, black eye patches."],
  ["robot", "Bolt", "LED eyes and an antenna that blinks."],
  ["spiderman", "Spidey", "Masked web-slinger with expressive lenses."],
  ["luffy", "Luffy", "Straw hat, big grin, and Gear 5 when there's work to do."],
  ["dazai", "Dazai", "Bandaged, bored-looking detective who reads while you work."],
] as const;

const MOODS = [
  ["idle", "Idle"], ["working", "Working"], ["thinking", "Thinking"], ["alert", "Needs you"],
  ["happy", "Celebrate"], ["music", "Music"], ["snack", "AFK"], ["dnd", "Do not disturb"], ["live", "Live"], ["sad", "Error"], ["sleepy", "Sleepy"],
  ["listening", "Listening"], ["talking", "Talking"], ["confused", "Confused"], ["streaming", "Streaming Shows"],
] as const;

// Preset colours per character; "" is the character's own colour. Luffy, Dazai and
// skin packs keep their own look and get no colour row; Spidey's colours are Venom suits.
const PALETTES: Record<string, string[]> = {
  bunny: ["", "#F4B6C8", "#CFC6BD", "#DDAE74", "#B9A7F2", "#9CC3F5", "#5B5868"],
  cat: ["", "#F08A3C", "#2D2B33", "#F7F1E8", "#9AA6B2", "#DDAE74", "#B9A7F2"],
  dog: ["", "#F7F1E8", "#8C5B3E", "#2D2B33", "#C9C2B8", "#F2C48D", "#9CC3F5"],
  fox: ["", "#E86A33", "#F7F1E8", "#B5532E", "#9AA6B2", "#F4B6C8", "#5B5868"],
  panda: ["", "#F4D9C6", "#F4B6C8", "#9AD7C2", "#CFC6BD", "#B9A7F2", "#9CC3F5"],
  robot: ["", "#9AD7C2", "#F4B6C8", "#F2C48D", "#B9A7F2", "#5B5868", "#E8E8EC"],
  // Any colour turns Spidey's suit into a Venom suit in that shade, with grey webbing.
  spiderman: ["", "#17161C", "#22263A", "#2B1B33"],
};
const SWATCH_NAMES: Record<string, string> = { "#17161C": "Venom", "#22263A": "Venom (midnight)", "#2B1B33": "Venom (plum)" };

export function Mascot() {
  const s = useStore();
  const style = useMascotStyle();
  const [mood, setMood] = useState("idle");
  const [packs, setPacks] = useState<string[]>([]);
  const [folder, setFolder] = useState("");
  useEffect(() => {
    call<string[]>("listPacks").then(setPacks);
    call<{ packsFolder: string }>("getSystem").then((r) => setFolder(r.packsFolder));
  }, []);

  // Replay the hop whenever Celebrate is picked.
  const [moodKey, setMoodKey] = useState(0);
  const pick = (m: string) => { setMood(m); setMoodKey((k) => k + 1); };
  const colors = s.get("Bunbun.MascotColor");
  const characterStyle = (id: string): MascotStyle => ({ ...style, character: id, color: colorFor(colors, id) });
  const palette = PALETTES[style.character];
  const setColor = (hex: string) => s.set("Bunbun.MascotColor", withColor(colors, style.character, hex));
  const current = CHARACTERS.find(([id]) => id === style.character)?.[1] ?? "this mascot";

  return (
    <div className="page">
      <PageHead title="Mascot studio" subtitle="Choose who lives on your island, how they look, and how much they move." />

      <IdleFaceSettings />

      <div className="studio">
        <div>
          <div className="stage">
            <div className="shadow" />
            <LiveMascot key={moodKey} style={style} mood={mood} size={220} />
          </div>
          <div className="moods">
            {MOODS.map(([id, label]) => (
              <button key={id} className={`chip ${mood === id ? "on" : ""}`} onClick={() => pick(id)}>{label}</button>
            ))}
          </div>
        </div>

        <div>
          <div className="choices" style={{ gridTemplateColumns: "repeat(3, 1fr)" }}>
            {CHARACTERS.map(([id, name, desc]) => (
              <button key={id} className={`choice char ${style.character === id ? "on" : ""}`} onClick={() => s.set("Bunbun.Character", id)}>
                {style.character === id && <span className="check"><Check size={13} strokeWidth={3} /></span>}
                <MascotImage style={characterStyle(id)} size={72} />
                <b>{name}</b>
                <small>{desc}</small>
              </button>
            ))}
            {packs.map((p) => {
              const id = `custom:${p}`;
              return (
                <button key={id} className={`choice char ${style.character === id ? "on" : ""}`} onClick={() => s.set("Bunbun.Character", id)}>
                  {style.character === id && <span className="check"><Check size={13} strokeWidth={3} /></span>}
                  <MascotImage style={characterStyle(id)} size={72} />
                  <b>{p}</b>
                  <small>Custom skin pack</small>
                </button>
              );
            })}
          </div>
        </div>
      </div>

      <Section title="Look">
        {palette ? (
          <Row label={`${current}'s colour`} description="Each mascot remembers its own colour. The first swatch is the original." settingKeys={["Bunbun.MascotColor"]}>
            <div className="swatches">
              {palette.map((c) => (
                <button key={c || "auto"} title={SWATCH_NAMES[c] ?? (c || "Original colour")} aria-label={SWATCH_NAMES[c] ?? (c || "Original colour")} aria-pressed={style.color.toUpperCase() === c}
                  className={style.color.toUpperCase() === c ? "on" : ""}
                  style={{ background: c || "conic-gradient(#f7f1e8, #f08a3c, #b9a7f2, #9ad7c2, #f7f1e8)" }}
                  onClick={() => setColor(c)} />
              ))}
            </div>
            <ColorField value={style.color} onChange={setColor} placeholder="Original" />
          </Row>
        ) : (
          <Row label="Colour" description={`${current} keeps its own colours.`}><span /></Row>
        )}
        <Row label="Show the mascot" description="On agent pills, approval cards and the Ask Bun box." settingKeys={["Bunbun.Mascot"]}>
          <Toggle on={s.bool("Bunbun.Mascot")} onChange={(v) => s.set("Bunbun.Mascot", v)} />
        </Row>
        <Setting k="Bunbun.MascotOnIsland" label="On the island" description="Beside the clock and the music pill. When music plays, headphones go on and the vibing starts." />
        <Setting k="Bunbun.MascotDnd" label="Do not disturb" description={'Headphones on, eyes closed and a Do Not Disturb badge; the mascot stops reacting. Separate from Windows\' Do Not Disturb. Ask Bun "dnd on" or "dnd off" to switch it; Focus mode does the same.'} />
        <Setting k="Bunbun.AfkMinutes" label="AFK after (minutes)" />
        <Setting k="Bunbun.LiveMood" label="Live while the screen is shared" />
      </Section>

      <Section title="Personality">
        <Setting k="Bunbun.MascotMotion" label="Liveliness" description="How much the mascot bobs, sways and wiggles." />
        <Setting k="Bunbun.MascotHop" label="Celebration hop" description="Height of the hop when a task finishes." />
        <Setting k="Bunbun.MascotBlinkEvery" label="Blink every" description="0 turns blinking off." />
        <Setting k="Bunbun.MascotEyesFollow" label="Eyes follow your cursor" description="" />
        <Setting k="Bunbun.MascotCelebrate" label="Hop when a task finishes" description="" />
        <Setting k="Bunbun.PokeReactions" />
      </Section>

      <Section title="Custom skins">
        <div className="row" style={{ display: "block" }}>
          <div className="label" style={{ marginBottom: 10 }}>
            <b>Make a pack from a character sheet</b>
            <span>One row per mood, a few frames per row: they play as an animation. A row of look directions makes your character look at the cursor.</span>
          </div>
          <SheetImport onSaved={(pack) => {
            call<string[]>("listPacks").then(setPacks);
            s.set("Bunbun.Character", `custom:${pack}`);
          }} />
        </div>
        <Row label="Skin packs" description={<>Or make a folder in <code>{folder || "%APPDATA%\\Bunbun\\mascots"}</code> with <code>idle.png</code> and, optionally, <code>working</code>, <code>thinking</code>, <code>alert</code>, <code>happy</code>, <code>sad</code>, <code>sleepy</code>, <code>music</code>, <code>snack</code>, <code>listening</code>, <code>talking</code>, <code>confused</code>, <code>streaming</code> (Streaming Shows), <code>dnd</code> and <code>live</code> PNGs. Square, transparent, 256 px or more.</>}>
          <button className="btn small" onClick={() => call("openPacksFolder")}><FolderOpen size={15} /> Open folder</button>
          <button className="btn small" onClick={() => call<string[]>("listPacks").then(setPacks)}><RotateCcw size={14} /> Refresh</button>
        </Row>
      </Section>
      <div className="note" style={{ marginTop: 12 }}>
        <Info size={16} />
        Skin packs are for artwork you own or have permission to use: your own drawings, commissions and licensed art.
      </div>
    </div>
  );
}
