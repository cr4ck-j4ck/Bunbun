import { ImagePlus, Save } from "lucide-react";
import { useRef, useState } from "react";
import { call } from "../bridge";

// What a row of the sheet can become. "look" is a row of look directions in the
// order left, up, centre, down, right; the character turns to the cursor with them.
const TARGETS = [
  ["", "Skip this row"], ["idle", "Idle"], ["working", "Working"], ["thinking", "Thinking"], ["alert", "Needs you"],
  ["happy", "Celebrate"], ["sad", "Error"], ["sleepy", "Sleepy"], ["music", "Music"], ["snack", "Do not disturb"],
  ["listening", "Listening"], ["talking", "Talking"], ["confused", "Confused"], ["look", "Look directions (← ↑ • ↓ →)"],
] as const;

type Row = { frames: string[] };

// Turns a character sheet (one row per mood, several frames per row) into a skin
// pack. The settings window finds the rows and frames and cuts them out; you say
// which row is which mood. Import more sheets into the same pack to add moods.
export function SheetImport({ onSaved }: { onSaved: (pack: string) => void }) {
  const input = useRef<HTMLInputElement>(null);
  const [rows, setRows] = useState<Row[] | null>(null);
  const [moods, setMoods] = useState<string[]>([]);
  const [name, setName] = useState("My mascot");
  const [busy, setBusy] = useState(false);
  const [message, setMessage] = useState("");

  const pick = (file: File) => {
    setBusy(true);
    setMessage("");
    const reader = new FileReader();
    reader.onload = async () => {
      try {
        const r = await call<Row[]>("sliceSheet", { data: String(reader.result) });
        setRows(r);
        setMoods(r.map(() => ""));
      } catch (e) {
        setRows(null);
        setMessage(String((e as Error)?.message ?? e));
      } finally {
        setBusy(false);
      }
    };
    reader.readAsDataURL(file);
  };

  const save = async () => {
    if (!rows) return;
    setBusy(true);
    try {
      const pack = await call<string>("savePack", { name, rows: moods.map((mood, row) => ({ row, mood })).filter((r) => r.mood) });
      setMessage(`Saved to “${pack}”. Import another sheet into the same name to add more moods.`);
      onSaved(pack);
    } catch (e) {
      setMessage(String((e as Error)?.message ?? e));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div className="sheet-import">
      <div style={{ display: "flex", gap: 8, alignItems: "center", flexWrap: "wrap" }}>
        <button className="btn small primary" disabled={busy} onClick={() => input.current?.click()}>
          <ImagePlus size={15} /> {rows ? "Pick another sheet" : "Pick a character sheet"}
        </button>
        <span style={{ color: "var(--muted)", fontSize: 12 }}>PNG, WebP or JPEG; transparent or plain background. Label boxes and arrows are ignored.</span>
        <input ref={input} type="file" accept="image/png,image/webp,image/jpeg,image/bmp" hidden
          onChange={(e) => { const f = e.target.files?.[0]; if (f) pick(f); e.target.value = ""; }} />
      </div>
      {busy && <p style={{ color: "var(--muted)" }}>Working…</p>}
      {rows && (
        <>
          <div className="sheet-rows">
            {rows.map((r, i) => (
              <div key={i} className={`sheet-row ${moods[i] ? "on" : ""}`}>
                <select className="field" value={moods[i]} onChange={(e) => setMoods(moods.map((m, j) => (j === i ? e.target.value : m)))}>
                  {TARGETS.map(([id, label]) => <option key={id} value={id}>{label}</option>)}
                </select>
                <div className="sheet-frames">
                  {r.frames.map((src, j) => <img key={j} src={src} alt={`Row ${i + 1}, frame ${j + 1}`} />)}
                </div>
              </div>
            ))}
          </div>
          <div style={{ display: "flex", gap: 8, alignItems: "center", marginTop: 10 }}>
            <input className="field" aria-label="Pack name" value={name} maxLength={40} onChange={(e) => setName(e.target.value)} />
            <button className="btn small primary" disabled={busy || !moods.some(Boolean)} onClick={save}><Save size={14} /> Save pack</button>
          </div>
        </>
      )}
      {message && <p style={{ marginTop: 8 }}>{message}</p>}
    </div>
  );
}
