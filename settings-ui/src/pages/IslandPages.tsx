import { Reorder, useDragControls } from "framer-motion";
import {
  Battery, Bot, Briefcase, CalendarDays, ChartColumnBig, ChevronDown, ChevronUp, Clipboard, CloudSun, Cpu, Droplet, FolderDown, GripVertical, Home, Camera,
  LayoutGrid, Link2, ListChecks, Music2, Plus, Quote, RefreshCw, Sparkles, StickyNote, Timer, ToggleRight, X,
} from "lucide-react";
import { createContext, ReactNode, useCallback, useContext, useEffect, useState } from "react";
import { call } from "../bridge";
import { LINUX_CARD_STAND_IN, LINUX_PAGES, useIsLinux } from "../platform";
import { schema, useStore } from "../store";
import { PageHead, Section, Segmented, TextField, Toggle } from "../ui/controls";
import { PageKey, PageThumb } from "../ui/PageThumb";

// Pages in the island's default order (DashboardTabIds). `key` is what
// Bunbun.PageOrder stores; `setting` turns the page on or off.
type PageInfo = { key: PageKey; name: string; desc: string; icon: ReactNode; setting?: string; when?: string };
const PAGES: PageInfo[] = [
  { key: "calendar", name: "Calendar", desc: "The date, your next meeting and today's to-dos.", icon: <CalendarDays size={16} /> },
  { key: "weather", name: "Weather", desc: "Now, the next hours and five days, with humidity, wind and UV. Switch °C / °F and the city from the page.", icon: <CloudSun size={16} />, setting: "Modules.Weather" },
  { key: "system", name: "System", desc: "Battery, network, CPU, memory and GPU, volume and Bluetooth. In Studio it also holds the Wi-Fi & Bluetooth toggles.", icon: <Cpu size={16} />, setting: "Modules.HardwareMonitorModule" },
  // Classic only: in Studio these toggles are part of the System page.
  { key: "toggles", name: "Wi-Fi & Bluetooth", desc: "Quick toggles and your battery.", icon: <ToggleRight size={16} />, setting: "Modules.ConnectivityModule" },
  { key: "tray", name: "File shelf", desc: "Drop files on the island, drag them out later.", icon: <FolderDown size={16} />, setting: "Modules.FileTrayModule" },
  { key: "todo", name: "To-dos", desc: "Your list and reminders.", icon: <ListChecks size={16} />, setting: "Modules.TodoModule" },
  { key: "lyrics", name: "Lyrics", desc: "Synced lyrics for the song that's playing.", icon: <Quote size={16} />, setting: "Modules.Lyrics", when: "While music plays" },
  { key: "agent", name: "Agent", desc: "What your coding agent is doing, its plan and usage.", icon: <Bot size={16} />, setting: "Bunbun.AgentTab", when: "With an agent connected" },
  { key: "clipboard", name: "Clipboard", desc: "Your last 8 copies; click one to copy it again.", icon: <Clipboard size={16} />, setting: "Bunbun.ClipboardHistory" },
  { key: "note", name: "Quick note", desc: "One note with checkboxes.", icon: <StickyNote size={16} />, setting: "Bunbun.QuickNote" },
  // No switch of its own: it comes with the mode (Settings, Mode: Office work, Both or Study).
  { key: "work", name: "Work day", desc: "Today's work against your goal, the state (working, lunch, away) with Pause, Break and Done, a timeline of the day and the report. In Study mode: your classes and subjects.", icon: <Briefcase size={16} />, when: "In Work or Study mode" },
  { key: "focus", name: "Timers", desc: "Pomodoro, a countdown you set on a ring, a stopwatch with laps, and Water.", icon: <Timer size={16} />, setting: "Bunbun.FocusPage" },
  // Not listed (merged): Water is a mode of the Timers page. Kept so Bunbun.PageOrder still parses it.
  { key: "water", name: "Water", desc: "Today's water, a + glass button and the week. Reminders are in Features.", icon: <Droplet size={16} />, setting: "Bunbun.WaterPage" },
  { key: "mirror", name: "Mirror", desc: "A camera preview before a call.", icon: <Camera size={16} />, setting: "Bunbun.MirrorPage" },
  { key: "screentime", name: "Screen Time", desc: "Today's screen time and the last week, by app, with each app's own name and icon. Kept on this PC only.", icon: <ChartColumnBig size={16} />, setting: "Bunbun.ScreenTime" },
  { key: "apps", name: "All apps", desc: "Every page, the colour picker and Settings in one grid.", icon: <LayoutGrid size={16} />, setting: "Bunbun.AppsPage" },
];
const DEFAULT_ORDER = PAGES.map((p) => p.key);
const byKey = Object.fromEntries(PAGES.map((p) => [p.key, p])) as Record<string, PageInfo>;

function parseOrder(value: string): PageKey[] {
  const named = value.split(",").map((k) => k.trim().toLowerCase()).filter((k, i, a) => byKey[k] && a.indexOf(k) === i) as PageKey[];
  return [...named, ...DEFAULT_ORDER.filter((k) => !named.includes(k))];
}

// ── Pictures of the pages ───────────────────────────────────────────────────
// Example pictures ship with the app (PICTURES below) and show by default. Nothing
// is captured by itself: only "Take pictures of my island" asks the island to show
// each page for a moment and snapshot it. After that, the user can switch between
// the examples and their own pictures (remembered in this browser view only).
type Shot = { src: string; at: number };
const ShotsContext = createContext<Record<string, Shot>>({});
const useShot = (key: string): Shot | undefined => useContext(ShotsContext)[key];
const PREF = "bunbun.pagePictures";

function useOwnPictures() {
  const [shots, setShots] = useState<Record<string, Shot>>({});
  const [busy, setBusy] = useState(false);
  const [note, setNote] = useState("");
  const [mine, setMineState] = useState(() => { try { return localStorage.getItem(PREF) === "mine"; } catch { return false; } });
  const setMine = (v: boolean) => {
    setMineState(v);
    try { localStorage.setItem(PREF, v ? "mine" : "examples"); } catch { /* private view: not remembered */ }
  };
  // Reads the pictures already taken (files on this PC); it takes none.
  const load = useCallback(() => call<Record<string, Shot> | boolean>("getPagePreviews", { since: 0 })
    .then((r) => { if (r && typeof r === "object") setShots(r); }).catch(() => {}), []);
  useEffect(() => { load(); }, [load]);

  const take = useCallback(async () => {
    setBusy(true);
    setNote("");
    try {
      const r = await call<{ running: boolean; count: number }>("capturePages");
      if (!r || typeof r !== "object" || !r.running) { setNote("Start the island first, then take the pictures."); return; }
      // The island shows each page for about 0.7 s.
      await new Promise((res) => setTimeout(res, 1400 + (r.count || 0) * 700));
      await load();
      setMine(true);
    } finally {
      setBusy(false);
    }
  }, [load]);

  const has = Object.keys(shots).length > 0;
  return { shots: mine && has ? shots : {}, has, mine: mine && has, setMine, busy, note, take };
}

export function IslandPages() {
  const s = useStore();
  const studio = s.get("Bunbun.Layout") !== "classic";
  const pics = useOwnPictures();
  return (
    <ShotsContext.Provider value={pics.shots}>
    <div className="page">
      <PageHead title="Island pages" subtitle="Choose which pages your island has and drag them into the order you like. Scroll or click the tab bar on the island to move between them." />

      <Section title="Layout">
        <div className="layout-pick">
          {[["studio", "Studio", "A frame with a header, an icon tab bar and big Home cards."], ["classic", "Classic", "The original island: no frame, dots to page."]].map(([v, name, desc]) => (
            <button key={v} className={`choice ${s.get("Bunbun.Layout") === v ? "on" : ""}`} onClick={() => s.set("Bunbun.Layout", v)}>
              <div className={`layout-art ${v}`}>
                {v === "studio" ? <><i className="hd" /><i className="t" /><i className="t" /><i className="t" /><i className="bar" /></> : <><i className="t wide" /><i className="dots" /></>}
              </div>
              <b>{name}</b><small>{desc}</small>
            </button>
          ))}
        </div>
      </Section>

      <div className="note shots-note">
        <RefreshCw size={16} className={pics.busy ? "spin" : ""} />
        <span>
          {pics.busy ? "Taking pictures: the island shows each page for a moment\u2026"
            : pics.note || (pics.mine ? "Pictures of your island." : "Example pictures of each page.")}
        </span>
        {pics.has && !pics.busy && (
          <Segmented value={pics.mine ? "mine" : "examples"} onChange={(v) => pics.setMine(v === "mine")}
            options={[{ value: "examples", label: "Examples" }, { value: "mine", label: "My island" }]} />
        )}
        <button className="btn small" disabled={pics.busy} onClick={pics.take}>
          {pics.busy ? "Taking\u2026" : pics.has ? "Retake" : "Take pictures of my island"}
        </button>
      </div>

      <HomeCards studio={studio} />
      <PageList />
      <HomeLinks />
    </div>
    </ShotsContext.Provider>
  );
}

// Pictures of each page that ship with the app: assets/pages/<page key>.png
// (home.png, music.png, screentime.png...), added by hand. A page without one
// shows its sketch.
const PICTURES: Record<string, string> = Object.fromEntries(
  Object.entries(import.meta.glob("../assets/pages/*.png", { eager: true, query: "?url", import: "default" }) as Record<string, string>)
    .map(([path, url]) => [path.replace(/^.*\/|\.png$/g, ""), url]),
);

/** The user's own picture (if they chose My island), else the bundled one, else the sketch. */
function Thumb({ page, off }: { page: PageKey; off?: boolean }) {
  const src = useShot(page)?.src ?? PICTURES[page];
  if (!src) return <PageThumb page={page} off={off} />;
  return (
    <div className={`thumb real ${off ? "off" : ""}`}>
      <img src={src} alt="" />
    </div>
  );
}

// ── Pages: on/off and order ─────────────────────────────────────────────────

function PageList() {
  const s = useStore();
  const studio = s.get("Bunbun.Layout") !== "classic";
  // Pages merged into another one aren't listed: in Studio, Wi-Fi & Bluetooth is part
  // of System; Water is a mode of Timers (its switch is in Productivity, Timers).
  const merged = (k: PageKey) => (k === "toggles" && studio) || k === "water";
  const [order, setOrder] = useState<PageKey[]>(() => parseOrder(s.get("Bunbun.PageOrder")));
  useEffect(() => setOrder(parseOrder(s.get("Bunbun.PageOrder"))), [s.get("Bunbun.PageOrder")]);
  const visible = order.filter((k) => !merged(k));
  // The listed pages in their new order; the hidden ones keep going at the end.
  const withHidden = (next: PageKey[]) => [...next, ...order.filter(merged)];

  const save = (next: PageKey[]) => {
    const full = withHidden(next.filter((k) => !merged(k)));
    setOrder(full);
    const isDefault = next.filter((k) => !merged(k)).join(",") === DEFAULT_ORDER.filter((k) => !merged(k)).join(",");
    s.set("Bunbun.PageOrder", isDefault ? "" : full.join(","));
  };
  const move = (key: PageKey, by: number) => {
    const i = visible.indexOf(key), j = i + by;
    if (j < 0 || j >= visible.length) return;
    const next = [...visible];
    [next[i], next[j]] = [next[j], next[i]];
    save(next);
  };
  const listed = PAGES.filter((p) => !merged(p.key));
  const shown = listed.filter((p) => !p.setting || s.bool(p.setting)).length;

  return (
    <section className="section">
      <div className="section-title">
        <h3>Pages</h3>
        <span>{shown + 1} of {listed.length + 1} on</span>
        {s.get("Bunbun.PageOrder") && <button className="link" onClick={() => save(DEFAULT_ORDER)}>Reset order</button>}
      </div>
      <div className="page-first">
        <Thumb page="home" />
        <div className="label"><b><Home size={15} /> Home <span className="badge">Always first</span></b>
          <span>The three cards below, or the player while music plays in Classic. In Studio, Home stays first and the player comes next.</span></div>
      </div>
      <Reorder.Group axis="y" values={visible} onReorder={(next) => setOrder(withHidden(next))} className="page-list" as="div">
        {visible.map((k, i) => (
          <PageRow key={k} page={byKey[k]} first={i === 0} last={i === visible.length - 1}
            onMove={(by) => move(k, by)} onDrop={() => save(visible)} />
        ))}
      </Reorder.Group>
    </section>
  );
}

function PageRow({ page, first, last, onMove, onDrop }: { page: PageInfo; first: boolean; last: boolean; onMove: (by: number) => void; onDrop: () => void }) {
  const s = useStore();
  const drag = useDragControls();
  // Work follows the mode (Bunbun.UseCase): on for Office work, Both and Study; called Study in Study mode.
  const use = s.get("Bunbun.UseCase") || "agents";
  const isWork = page.key === "work";
  const on = page.setting ? s.bool(page.setting) : isWork ? use !== "agents" : true;
  const name = isWork && use === "study" ? "Study" : page.name;
  const linuxMissing = useIsLinux() && !LINUX_PAGES.has(page.key);
  return (
    <Reorder.Item value={page.key} as="div" className={`page-row ${on ? "" : "off"}`} dragListener={false} dragControls={drag} onDragEnd={onDrop}>
      <button className="grip" aria-label={`Drag ${page.name}`} onPointerDown={(e) => drag.start(e)}><GripVertical size={16} /></button>
      <Thumb page={page.key} off={!on} />
      <div className="label">
        <b>{page.icon} {name}{page.when && <span className="badge muted">{page.when}</span>}
          {linuxMissing && <em className="badge muted linux">Not on Linux yet</em>}</b>
        <span>{page.desc}</span>
      </div>
      <div className="control">
        <div className="move">
          <button disabled={first} aria-label={`Move ${page.name} up`} onClick={() => onMove(-1)}><ChevronUp size={15} /></button>
          <button disabled={last} aria-label={`Move ${page.name} down`} onClick={() => onMove(1)}><ChevronDown size={15} /></button>
        </div>
        {page.setting ? <Toggle on={on} onChange={(v) => s.set(page.setting!, v)} label={page.name} />
          : <span className="always" title={isWork ? "Comes with your mode: on for Office work, Both and Study (Settings, Mode)." : undefined}>
            {isWork ? (on ? "With mode" : "Off") : "Always on"}</span>}
      </div>
    </Reorder.Item>
  );
}

// ── Home cards ─────────────────────────────────────────────────────────────

const TILE_ICON: Record<string, ReactNode> = {
  auto: <Sparkles size={18} />, agent: <Bot size={18} />, music: <Music2 size={18} />, focus: <Timer size={18} />,
  weather: <CloudSun size={18} />, calendar: <CalendarDays size={18} />, battery: <Battery size={18} />, system: <Cpu size={18} />,
};
const AUTO_HINT = ["Your main agent, or the weather", "Music", "Focus"];
const AUTO_KIND = ["agent", "music", "focus"];
// Each Home card has its own colour on the island; only Music follows the album art.
const TILE_COLOR: Record<string, string> = {
  agent: "#d97757", music: "var(--accent)", focus: "#bf5af2", weather: "#64d2ff",
  calendar: "#ff453a", battery: "#30d158", system: "#30d158", work: "#0a84ff",
};

function HomeCards({ studio }: { studio: boolean }) {
  const s = useStore();
  const [slot, setSlot] = useState(0);
  const keys = ["Bunbun.HomeTile1", "Bunbun.HomeTile2", "Bunbun.HomeTile3"];
  const options = schema[keys[0]]?.options ?? [];
  const label = (v: string) => options.find((o) => o.value === v)?.label ?? v;
  const linux = useIsLinux();
  const standIn = linux ? LINUX_CARD_STAND_IN[s.get(keys[slot]) || ""] : undefined;
  return (
    <section className="section">
      <div className="section-title">
        <h3>Home cards</h3>
        {!studio && <span>Shown in the Studio layout</span>}
      </div>
      <div className="card home-cards">
        <HomeShot />
        <div className="home-frame">
          {keys.map((k, i) => {
            const v = s.get(k) || "auto";
            const tint = TILE_COLOR[v === "auto" ? AUTO_KIND[i] : v];
            return (
              <button key={k} className={`home-tile ${slot === i ? "on" : ""}`} onClick={() => setSlot(i)}
                style={{ ["--tile" as string]: tint }}>
                <span className="n">{i + 1}</span>
                {TILE_ICON[v]}
                <b>{v === "auto" ? "Automatic" : label(v)}</b>
                <small>{v === "auto" ? AUTO_HINT[i] : "Tap to change"}</small>
              </button>
            );
          })}
        </div>
        <div className="home-options">
          <span>Card {slot + 1} shows</span>
          <div className="chips">
            {options.map((o) => (
              <button key={o.value} className={`chip ${(s.get(keys[slot]) || "auto") === o.value ? "on" : ""}`} onClick={() => s.set(keys[slot], o.value)}>
                {TILE_ICON[o.value]} {o.label}
              </button>
            ))}
          </div>
          {standIn && <small className="linux-note">On Linux this card shows {standIn} for now.</small>}
        </div>
      </div>
    </section>
  );
}

function HomeShot() {
  const src = useShot("home")?.src ?? PICTURES.home;
  if (!src) return null;
  return (
    <div className="home-shot">
      <img src={src} alt="The Home page" />
      <span>The Home page. Pick what each card shows below.</span>
    </div>
  );
}

// ── Links under the Home cards ───────────────────────────────────────────────

type Link = { name: string; target: string };
const parseLinks = (v: string): Link[] => v.split(";").map((p) => p.trim()).filter(Boolean).map((p) => {
  const bar = p.indexOf("|");
  return bar < 0 ? { name: p, target: p } : { name: p.slice(0, bar).trim(), target: p.slice(bar + 1).trim() };
});
const joinLinks = (links: Link[]) => links.filter((l) => l.name || l.target).map((l) => `${l.name || l.target}|${l.target}`).join("; ");

function HomeLinks() {
  const s = useStore();
  const links = parseLinks(s.get("Bunbun.HomeLinks"));
  const [adding, setAdding] = useState(false);
  const update = (next: Link[]) => s.set("Bunbun.HomeLinks", joinLinks(next));
  return (
    <section className="section">
      <div className="section-title">
        <h3>Links on Home</h3>
        <span>Up to four shortcuts under the cards</span>
      </div>
      <div className="card">
        {links.map((l, i) => (
          <div className="row link-row" key={`${i}-${l.name}`}>
            <Link2 size={16} color="var(--accent)" />
            <TextField value={l.name} placeholder="Name" width={160} onChange={(v) => update(links.map((x, j) => (j === i ? { ...x, name: v } : x)))} />
            <TextField value={l.target} placeholder="https://… or C:\…" onChange={(v) => update(links.map((x, j) => (j === i ? { ...x, target: v } : x)))} />
            <button className="icon-btn" aria-label={`Remove ${l.name}`} onClick={() => update(links.filter((_, j) => j !== i))}><X size={15} /></button>
          </div>
        ))}
        {adding && links.length < 4 && <NewLink onAdd={(l) => { update([...links, l]); setAdding(false); }} onCancel={() => setAdding(false)} />}
        {!adding && links.length < 4 && (
          <div className="row"><div className="label"><span>{links.length ? "" : "Websites, files, folders or programs."}</span></div>
            <button className="btn small" onClick={() => setAdding(true)}><Plus size={14} /> Add a link</button></div>
        )}
      </div>
    </section>
  );
}

function NewLink({ onAdd, onCancel }: { onAdd: (l: Link) => void; onCancel: () => void }) {
  const [name, setName] = useState("");
  const [target, setTarget] = useState("");
  return (
    <div className="row link-row fade-in">
      <Link2 size={16} color="var(--accent)" />
      <input className="field" style={{ width: 160, minWidth: 160 }} placeholder="Name" value={name} autoFocus onChange={(e) => setName(e.target.value)} />
      <input className="field" placeholder="https://… or C:\…" value={target} onChange={(e) => setTarget(e.target.value)}
        onKeyDown={(e) => e.key === "Enter" && target.trim() && onAdd({ name: name.trim(), target: target.trim() })} />
      <button className="btn small primary" disabled={!target.trim()} onClick={() => onAdd({ name: name.trim(), target: target.trim() })}>Add</button>
      <button className="icon-btn" aria-label="Cancel" onClick={onCancel}><X size={15} /></button>
    </div>
  );
}

