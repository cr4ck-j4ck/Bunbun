import { Bot, Briefcase, Check, Coffee, FileImage, FileText, Flame, GraduationCap, Layers, Plus, SlidersHorizontal, Trash2, X } from "lucide-react";
import { useCallback, useEffect, useState } from "react";
import { call } from "../bridge";
import { useStore } from "../store";
import { PageHead, Row, Section, Segmented, Setting, Toggle } from "../ui/controls";

// ── What Bunbun is for (Bunbun.UseCase) ─────────────────────────────────────
// The sidebar names this page after the mode: Work day, Study, or Mode for agents.
export function modeNav(use: string): { label: string; icon: JSX.Element; tint: string } {
  if (use === "study") return { label: "Study", icon: <GraduationCap size={15} strokeWidth={2.2} />, tint: "#bf5af2" };
  if (use === "work" || use === "both") return { label: "Work day", icon: <Briefcase size={15} strokeWidth={2.2} />, tint: "#0a84ff" };
  return { label: "Mode", icon: <SlidersHorizontal size={15} strokeWidth={2.2} />, tint: "#30b0c7" };
}
export const USE_CASES = [
  { value: "agents", title: "AI agents", icon: <Bot size={18} />, tint: "#d97757", text: "Coding agents on the island: progress, approvals and limits.",
    gets: ["Agent page", "Approvals", "Limits"] },
  { value: "work", title: "Office work", icon: <Briefcase size={18} />, tint: "#0a84ff", text: "Your working day: hours, lunch, breaks and a report. No agent pieces.",
    gets: ["Work day", "Breaks", "Report"] },
  { value: "both", title: "Both", icon: <Layers size={18} />, tint: "#5e5ce6", text: "Coding agents and your working day, side by side.",
    gets: ["Agents", "Work day", "Report"] },
  { value: "study", title: "Study", icon: <GraduationCap size={18} />, tint: "#bf5af2", text: "Your timetable, Pomodoro sessions, a daily goal and a streak.",
    gets: ["Timetable", "Streak", "By subject"] },
];

export function UseCaseChoice() {
  const s = useStore();
  const value = s.get("Bunbun.UseCase") || "agents";
  return (
    <div className="mode-grid">
      {USE_CASES.map((u) => {
        const on = value === u.value;
        return (
          <button key={u.value} className={`mode-card ${on ? "on" : ""}`} style={{ "--mode": u.tint } as React.CSSProperties}
                  aria-pressed={on} onClick={() => s.set("Bunbun.UseCase", u.value)}>
            <span className="mode-ico">{u.icon}</span>
            {on && <span className="mode-check"><Check size={13} strokeWidth={3} /></span>}
            <b>{u.title}</b>
            <small>{u.text}</small>
            <span className="mode-gets">{u.gets.map((g) => <i key={g}>{g}</i>)}</span>
          </button>
        );
      })}
    </div>
  );
}

// Asked once, the first time Settings opens (and never again once answered).
const ASKED = "bunbun.useCaseAsked";
export function UseCaseWelcome() {
  const s = useStore();
  const [open, setOpen] = useState(() => {
    try { return localStorage.getItem(ASKED) !== "1"; } catch { return false; }
  });
  if (!open || !s.ready) return null;
  if (s.get("Bunbun.UseCase") && s.get("Bunbun.UseCase") !== "agents") return null;  // already chosen
  const pick = (v: string) => {
    s.set("Bunbun.UseCase", v);
    try { localStorage.setItem(ASKED, "1"); } catch { /* a private view: asked again next time */ }
    setOpen(false);
  };
  return (
    <div className="modal-back" role="dialog" aria-modal="true" aria-label="What is Bunbun for?">
      <div className="modal welcome">
        <h2>What will you use Bunbun for?</h2>
        <p>This sets what the island shows. You can change it any time on the Mode page.</p>
        <div className="mode-grid two">
          {USE_CASES.map((u) => (
            <button key={u.value} className="mode-card" style={{ "--mode": u.tint } as React.CSSProperties} onClick={() => pick(u.value)}>
              <span className="mode-ico">{u.icon}</span>
              <b>{u.title}</b>
              <small>{u.text}</small>
              <span className="mode-gets">{u.gets.map((g) => <i key={g}>{g}</i>)}</span>
            </button>
          ))}
        </div>
      </div>
    </div>
  );
}

// ── The schedule ─────────────────────────────────────────────────────────────
const DAYS = ["mon", "tue", "wed", "thu", "fri", "sat", "sun"];
const DAY_NAMES = ["Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"];

function Days() {
  const s = useStore();
  const raw = (s.get("Work.Days") || "").toLowerCase();
  const on = new Set(raw.includes("every") ? DAYS : DAYS.filter((d) => raw.includes(d) || (raw.includes("weekday") && DAYS.indexOf(d) < 5)));
  const toggle = (d: string) => {
    const next = new Set(on);
    if (next.has(d)) next.delete(d); else next.add(d);
    s.set("Work.Days", DAYS.filter((x) => next.has(x)).join(","));
  };
  return (
    <div className="day-chips">
      {DAYS.map((d, i) => (
        <button key={d} className={`chip ${on.has(d) ? "on" : ""}`} onClick={() => toggle(d)}>{DAY_NAMES[i]}</button>
      ))}
    </div>
  );
}

function TimeInput({ k }: { k: string }) {
  const s = useStore();
  return <input type="time" className="field" style={{ width: 130, minWidth: 0 }} value={s.get(k) || ""} onChange={(e) => e.target.value && s.set(k, e.target.value)} />;
}

function NumberInput({ k, min, max, unit }: { k: string; min: number; max: number; unit: string }) {
  const s = useStore();
  return (
    <span className="num-unit">
      <input type="number" className="field" style={{ width: 80, minWidth: 0 }} min={min} max={max} value={s.get(k)}
             onChange={(e) => { const v = Math.max(min, Math.min(max, Number(e.target.value) || 0)); s.set(k, v); }} />
      <span>{unit}</span>
    </span>
  );
}

// A progress ring (0..1) with a label in the middle.
function Ring({ value, colour, size = 76, children }: { value: number; colour: string; size?: number; children?: React.ReactNode }) {
  const r = size / 2 - 6, c = 2 * Math.PI * r;
  return (
    <div className="ring" style={{ width: size, height: size }}>
      <svg width={size} height={size}>
        <circle cx={size / 2} cy={size / 2} r={r} stroke="var(--line)" strokeWidth={6} fill="none" />
        <circle cx={size / 2} cy={size / 2} r={r} stroke={colour} strokeWidth={6} fill="none" strokeLinecap="round"
                strokeDasharray={`${c * Math.min(1, value)} ${c}`} transform={`rotate(-90 ${size / 2} ${size / 2})`} />
      </svg>
      <span>{children}</span>
    </div>
  );
}

// Today so far, read from the log the island keeps (updated every minute).
function TodayCard({ study, onReport }: { study: boolean; onReport: () => void }) {
  const s = useStore();
  const [days, setDays] = useState<WorkDay[]>([]);
  useEffect(() => {
    const load = () => {
      const today = new Date();
      call<WorkDay[]>("workDays", { from: ymd(new Date(today.getTime() - 60 * 864e5)), to: ymd(today) }).then(setDays).catch(() => setDays([]));
    };
    load();
    const t = window.setInterval(load, 30000);
    return () => window.clearInterval(t);
  }, []);
  const today = ymd(new Date());
  const d = days.find((x) => x.date === today);
  const worked = d ? (d.seconds.work || 0) + (d.seconds.overtime || 0) : 0;
  const away = d ? (d.seconds.away || 0) + (d.seconds.asleep || 0) : 0;
  const startMin = minutesOf(s.get("Work.Start"), 540), endMin = minutesOf(s.get("Work.End"), 1080);
  const shift = ((endMin - startMin) % 1440 + 1440) % 1440;
  const goalH = Number(s.get(study ? "Study.GoalHours" : "Work.GoalHours")) || 0;
  const goal = goalH > 0 ? goalH * 3600 : study ? 0 : Math.max(0, shift - (Number(s.get("Work.LunchMinutes")) || 0)) * 60;
  // The streak: days in a row (to yesterday, and today once it's met) that reached the goal.
  let streak = 0;
  if (goal > 0) {
    const met = (n: number) => { const x = days.find((y) => y.date === n); return !!x && (x.seconds.work || 0) + (x.seconds.overtime || 0) >= goal; };
    let day = new Date();
    if (met(ymd(day))) streak++;
    for (let i = 0; i < 60; i++) { day = new Date(day.getTime() - 864e5); if (met(ymd(day))) streak++; else break; }
  }
  const colour = study ? "#bf5af2" : "#0a84ff";
  const subjects = Object.entries(d?.subjects || {}).sort((a, b) => b[1] - a[1]).slice(0, 3);
  return (
    <div className="today-card" style={{ "--mode": colour } as React.CSSProperties}>
      <Ring value={goal > 0 ? worked / goal : 0} colour={colour}>{goal > 0 ? `${Math.round((100 * worked) / goal)}%` : "\u2014"}</Ring>
      <div className="today-main">
        <span className="today-label">Today</span>
        <b>{dur(worked)}</b>
        <span className="muted">{study ? "studied" : "worked"}{goal > 0 ? ` of ${dur(goal)}` : ""} · {dur(away)} away · {d?.breaks ?? 0} breaks</span>
        {study && subjects.length > 0 && <span className="today-subjects">{subjects.map(([n, v]) => <i key={n}>{n} {dur(v)}</i>)}</span>}
      </div>
      <div className="today-side">
        {study && goal > 0 && <span className={`streak ${streak ? "hot" : ""}`}><Flame size={14} /> {streak} {streak === 1 ? "day" : "days"}</span>}
        <button className="btn small primary" onClick={onReport}><FileText size={14} /> {study ? "Study report" : "Work report"}</button>
      </div>
      <div className="today-bar">
        {(d?.segments || []).map(([a, b, k], i) => {
          const from = Math.min(d!.first, startMin * 60), to = Math.max(d!.last, (endMin <= startMin ? endMin + 1440 : endMin) * 60);
          return <i key={i} style={{ left: `${(100 * (a - from)) / (to - from)}%`, width: `${Math.max(0.4, (100 * (b - a)) / (to - from))}%`, background: colourOf(k) }} />;
        })}
        {!d && <span className="muted">Nothing recorded yet today.</span>}
      </div>
    </div>
  );
}

// The working day at a glance: the hours, with lunch shaded.
function DayPreview() {
  const s = useStore();
  const start = minutesOf(s.get("Work.Start"), 540), end0 = minutesOf(s.get("Work.End"), 1080);
  const end = end0 <= start ? end0 + 1440 : end0;
  const lunch = minutesOf(s.get("Work.LunchStart"), 780), lm = Number(s.get("Work.LunchMinutes")) || 0;
  const span = Math.max(60, end - start);
  const pct = (m: number) => `${Math.max(0, Math.min(100, (100 * (m - start)) / span))}%`;
  const hours = Math.round(span / 6) / 10;
  const fmt = (m: number) => `${String(Math.floor((m % 1440) / 60)).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`;
  return (
    <div className="day-preview">
      <div className="day-track">
        <i className="work" style={{ left: 0, width: "100%" }} />
        {lm > 0 && lunch >= start && lunch < end && <i className="lunch" style={{ left: pct(lunch), width: `${(100 * lm) / span}%` }} title="Lunch"><Coffee size={11} /></i>}
      </div>
      <div className="day-labels"><span>{fmt(start)}</span><span>{hours}h{lm ? `, ${lm} min lunch` : ""}</span><span>{fmt(end)}</span></div>
    </div>
  );
}

// ── Study: the timetable (Study.Timetable) ──────────────────────────────────
// "mon,wed 10:00-11:00 Maths; tue 14:00-16:00 Physics", edited as rows.
type Block = { days: string[]; start: string; end: string; subject: string };

function parseTimetable(text: string): Block[] {
  const out: Block[] = [];
  for (const entry of (text || "").split(/[;\n]/)) {
    const m = /^\s*([a-z,]+)\s+(\d{1,2}(?::\d{2})?)\s*-\s*(\d{1,2}(?::\d{2})?)\s*(.*)$/i.exec(entry);
    if (!m) continue;
    const pad = (s: string) => { const [h, mm = "00"] = s.split(":"); return `${h.padStart(2, "0")}:${mm}`; };
    out.push({ days: m[1].toLowerCase().split(",").filter((d) => DAYS.includes(d)), start: pad(m[2]), end: pad(m[3]), subject: m[4].trim() });
  }
  return out;
}
const formatTimetable = (rows: Block[]) =>
  rows.filter((r) => r.days.length).map((r) => `${r.days.join(",")} ${r.start}-${r.end} ${r.subject || "Study"}`).join("; ");

function WeekView({ rows }: { rows: Block[] }) {
  if (!rows.length) return null;
  const palette = ["#0a84ff", "#bf5af2", "#30d158", "#ff9f0a", "#64d2ff", "#ff375f", "#5e5ce6"];
  const subjects = [...new Set(rows.map((r) => r.subject || "Study"))];
  return (
    <div className="week-view">
      {DAYS.map((d, i) => (
        <div key={d} className="week-col">
          <span className="week-day">{DAY_NAMES[i]}</span>
          {rows.filter((r) => r.days.includes(d)).sort((a, b) => a.start.localeCompare(b.start)).map((r, k) => (
            <span key={k} className="week-block" style={{ "--c": palette[subjects.indexOf(r.subject || "Study") % palette.length] } as React.CSSProperties}>
              <b>{r.subject || "Study"}</b>{r.start}–{r.end}
            </span>
          ))}
        </div>
      ))}
    </div>
  );
}

function Timetable() {
  const s = useStore();
  const rows = parseTimetable(s.get("Study.Timetable"));
  const save = (next: Block[]) => s.set("Study.Timetable", formatTimetable(next));
  const update = (i: number, patch: Partial<Block>) => save(rows.map((r, j) => (j === i ? { ...r, ...patch } : r)));
  return (
    <div className="timetable">
      <WeekView rows={rows} />
      {rows.length === 0 && <p className="muted">No classes yet. Add your first one: its days, times and subject.</p>}
      {rows.map((r, i) => (
        <div className="tt-row" key={i}>
          <div className="day-chips">
            {DAYS.map((d, k) => (
              <button key={d} className={`chip small ${r.days.includes(d) ? "on" : ""}`}
                      onClick={() => update(i, { days: r.days.includes(d) ? r.days.filter((x) => x !== d) : DAYS.filter((x) => x === d || r.days.includes(x)) })}>
                {DAY_NAMES[k].slice(0, 2)}
              </button>
            ))}
          </div>
          <input type="time" className="field" style={{ width: 120, minWidth: 0 }} value={r.start} onChange={(e) => e.target.value && update(i, { start: e.target.value })} />
          <span className="muted">to</span>
          <input type="time" className="field" style={{ width: 120, minWidth: 0 }} value={r.end} onChange={(e) => e.target.value && update(i, { end: e.target.value })} />
          <input className="field" style={{ flex: 1, minWidth: 110 }} placeholder="Subject" value={r.subject}
                 onChange={(e) => update(i, { subject: e.target.value.replace(/[;\n]/g, " ") })} />
          <button className="btn small icon" aria-label="Remove" onClick={() => save(rows.filter((_, j) => j !== i))}><Trash2 size={14} /></button>
        </div>
      ))}
      <button className="btn small" onClick={() => save([...rows, { days: ["mon"], start: "09:00", end: "10:00", subject: "" }])}>
        <Plus size={14} /> Add a class
      </button>
    </div>
  );
}

// ── The report ───────────────────────────────────────────────────────────────
type WorkDay = { date: number; seconds: Record<string, number>; breaks: number; first: number; last: number; segments: [number, number, string][];
                 subjects?: Record<string, number> };
const KINDS: { key: string; label: string; colour: string }[] = [
  { key: "work", label: "Worked", colour: "#0a84ff" },
  { key: "overtime", label: "Overtime", colour: "#5e5ce6" },
  { key: "away", label: "Away", colour: "#ff9f0a" },
  { key: "asleep", label: "Asleep", colour: "#8e8e93" },
  { key: "lunch", label: "Lunch", colour: "#30d158" },
  { key: "break", label: "Breaks", colour: "#64d2ff" },
];
const colourOf = (k: string) => KINDS.find((x) => x.key === k)?.colour ?? "#636366";
// Study's words for the same kinds of time.
const labelOf = (k: { key: string; label: string }, study: boolean) =>
  study && k.key === "work" ? "Studied" : study && k.key === "overtime" ? "Extra" : k.label;

const dur = (s: number) => {
  const m = Math.floor(Math.max(0, s) / 60);
  return m < 60 ? `${m}m` : `${Math.floor(m / 60)}h ${m % 60}m`;
};
const ymd = (d: Date) => d.getFullYear() * 10000 + (d.getMonth() + 1) * 100 + d.getDate();
const fromYmd = (n: number) => new Date(Math.floor(n / 10000), Math.floor(n / 100) % 100 - 1, n % 100);
const clock = (sec: number) => {
  const m = Math.floor(((sec % 86400) + 86400) % 86400 / 60);
  return `${String(Math.floor(m / 60)).padStart(2, "0")}:${String(m % 60).padStart(2, "0")}`;
};
const minutesOf = (t: string, fallback: number) => {
  const m = /^(\d{1,2})(?::(\d{2}))?/.exec(t || "");
  return m ? Number(m[1]) * 60 + Number(m[2] || 0) : fallback;
};

type Range = "today" | "yesterday" | "week";

// Draws the report on a canvas (white, A4 proportions): the same picture becomes the PNG and the PDF.
// Sections, top to bottom: the header, the headline (goal donut, total, focus, longest stretch, breaks),
// a tile per kind of time with its share, how the time split, daily totals (several days) or the
// day's timeline, when in the day you worked, by subject (study), insights and the footer.
function drawReport(days: WorkDay[], range: Range,
                    sched: { start: number; end: number; goal: number; name: string; study: boolean; dates: number[] }): HTMLCanvasElement {
  const W = 1240, M = 64, CW = W - 2 * M;
  const study = sched.study;
  const accent = study ? "#bf5af2" : "#0a84ff", accent2 = study ? "#5e5ce6" : "#5e5ce6";
  const ink = "#111114", muted = "#6e6e73", faint = "#a1a1a6", line = "#e8e8ed", soft = "#f5f5f7";
  const font = (px: number, weight = 400) => `${weight} ${px}px "Segoe UI", "Inter", system-ui, sans-serif`;
  const verb = study ? "studied" : "worked";
  const isWork = (k: string) => k === "work" || k === "overtime";

  // ── The numbers ──
  const tot: Record<string, number> = {};
  let breaks = 0;
  for (const d of days) { for (const k of KINDS) tot[k.key] = (tot[k.key] || 0) + (d.seconds[k.key] || 0); breaks += d.breaks; }
  const worked = (tot.work || 0) + (tot.overtime || 0);
  const away = (tot.away || 0) + (tot.asleep || 0);
  const tracked = KINDS.reduce((a, k) => a + (tot[k.key] || 0), 0);
  const nDays = range === "week" ? sched.dates.length : 1;
  const activeDays = days.filter((d) => (d.seconds.work || 0) + (d.seconds.overtime || 0) > 0).length;
  const goal = sched.goal * nDays;
  const pctGoal = goal > 0 ? worked / goal : 0;
  const focus = worked + away > 0 ? worked / (worked + away) : 0;
  // The longest stretch of work without a gap (work and overtime joined, gaps of a minute or less).
  let longest = 0;
  for (const d of days) {
    let run = 0, end = -1e9;
    for (const [a, b, k] of [...d.segments].sort((x, y) => x[0] - y[0])) {
      if (!isWork(k)) { run = 0; end = -1e9; continue; }
      run = a - end <= 60 ? run + (b - a) : b - a;
      end = b;
      longest = Math.max(longest, run);
    }
  }
  // When in the day: work seconds per hour of the clock.
  const hours = new Array(24).fill(0);
  for (const d of days)
    for (const [a, b, k] of d.segments) {
      if (!isWork(k)) continue;
      for (let s = a; s < b;) {
        const next = Math.min(b, (Math.floor(s / 3600) + 1) * 3600);
        hours[Math.floor(s / 3600) % 24] += next - s;
        s = next;
      }
    }
  const busiest = hours.indexOf(Math.max(...hours));
  const perDay = sched.dates.map((date) => ({ date, d: days.find((x) => x.date === date) }));
  const best = perDay.reduce((acc, x) => {
    const w = x.d ? (x.d.seconds.work || 0) + (x.d.seconds.overtime || 0) : 0;
    return w > acc.w ? { date: x.date, w } : acc;
  }, { date: 0, w: 0 });
  const subj: Record<string, number> = {};
  for (const d of days) for (const [k, v] of Object.entries(d.subjects || {})) subj[k] = (subj[k] || 0) + v;
  const subjects = Object.entries(subj).sort((a, b) => b[1] - a[1]).slice(0, 8);
  const subjTotal = subjects.reduce((a, [, v]) => a + v, 0);
  const palette = ["#0a84ff", "#bf5af2", "#30d158", "#ff9f0a", "#64d2ff", "#ff375f", "#5e5ce6", "#ffd60a"];

  // ── The layout: each section's height, so the canvas is exactly as tall as it needs ──
  const timelineRows = range === "week" ? perDay.length : 1;
  const H_HEAD = 170, H_HERO = 250, H_TILES = 150, H_SPLIT = 110;
  const H_CHART = range === "week" ? 300 : 0;
  const H_TIMELINE = 80 + timelineRows * 58;
  const H_HOURS = 230;
  const H_SUBJ = study && subjects.length ? 80 + subjects.length * 42 : 0;
  const H_INSIGHTS = 240, H_FOOT = 70;  // room for five insights
  const H = H_HEAD + H_HERO + H_TILES + H_SPLIT + H_CHART + H_TIMELINE + H_HOURS + H_SUBJ + H_INSIGHTS + H_FOOT;
  const c = document.createElement("canvas");
  c.width = W; c.height = H;
  const g = c.getContext("2d")!;
  g.fillStyle = "#ffffff"; g.fillRect(0, 0, W, H);
  const text = (s: string, x: number, y: number, px: number, weight: number, colour: string, align: CanvasTextAlign = "left") => {
    g.font = font(px, weight); g.fillStyle = colour; g.textAlign = align; g.fillText(s, x, y); g.textAlign = "left";
  };
  const sectionTitle = (s: string, y: number, note?: string) => {
    text(s, M, y, 22, 700, ink);
    if (note) text(note, M + CW, y, 15, 400, faint, "right");
  };

  // ── Header ──
  const grad = g.createLinearGradient(0, 0, W, H_HEAD);
  grad.addColorStop(0, accent); grad.addColorStop(1, accent2);
  g.fillStyle = grad; g.fillRect(0, 0, W, H_HEAD);
  g.globalAlpha = 0.12; g.fillStyle = "#fff";
  g.beginPath(); g.arc(W - 120, 30, 160, 0, Math.PI * 2); g.fill();
  g.beginPath(); g.arc(W - 300, H_HEAD + 40, 110, 0, Math.PI * 2); g.fill();
  g.globalAlpha = 1;
  const first = fromYmd(sched.dates[0]), last = fromYmd(sched.dates[sched.dates.length - 1]);
  const fmt = (d: Date) => d.toLocaleDateString(undefined, { weekday: "long", day: "numeric", month: "long", year: "numeric" });
  text(study ? "Study report" : "Work report", M, 82, 46, 700, "#ffffff");
  text(range === "week" ? `${first.toLocaleDateString(undefined, { day: "numeric", month: "short" })} – ${fmt(last)}` : fmt(first), M, 122, 21, 400, "rgba(255,255,255,.9)");
  text("Bunbun", W - M, 70, 22, 700, "#ffffff", "right");
  text(`Made ${new Date().toLocaleString(undefined, { day: "numeric", month: "short", hour: "2-digit", minute: "2-digit" })}`, W - M, 98, 15, 400, "rgba(255,255,255,.85)", "right");
  let y = H_HEAD;

  // ── Headline ──
  {
    const top = y + 36, cx = M + 100, cy = top + 92, r = 82;
    g.lineWidth = 18; g.lineCap = "round";
    g.strokeStyle = soft; g.beginPath(); g.arc(cx, cy, r, 0, Math.PI * 2); g.stroke();
    if (pctGoal > 0) {
      const rg = g.createLinearGradient(cx - r, cy - r, cx + r, cy + r);
      rg.addColorStop(0, accent); rg.addColorStop(1, accent2);
      g.strokeStyle = rg; g.beginPath(); g.arc(cx, cy, r, -Math.PI / 2, -Math.PI / 2 + Math.PI * 2 * Math.min(1, pctGoal)); g.stroke();
    }
    g.lineCap = "butt";
    text(goal > 0 ? `${Math.round(pctGoal * 100)}%` : "—", cx, cy + 8, 34, 700, ink, "center");
    text(goal > 0 ? "of goal" : "no goal set", cx, cy + 32, 14, 400, muted, "center");
    const x0 = M + 240;
    text(study ? "Time studied" : "Time worked", x0, top + 26, 16, 600, muted);
    text(dur(worked), x0, top + 96, 72, 700, ink);
    const sub = [goal > 0 ? `of a ${dur(goal)} goal` : "", nDays > 1 ? `${dur(activeDays ? worked / activeDays : 0)} a day on ${activeDays} ${activeDays === 1 ? "day" : "days"}` : ""]
      .filter(Boolean).join("  ·  ");
    text(sub, x0, top + 132, 18, 400, muted);
    // Three stat chips down the right.
    const chips: [string, string, string][] = [
      ["Focus", tracked ? `${Math.round(focus * 100)}%` : "—", `${verb} vs away`],
      ["Longest stretch", longest ? dur(longest) : "—", "without a break"],
      ["Breaks", String(breaks), `${dur(tot.break || 0)} in all`],
    ];
    const cxw = 250, chx = M + CW - cxw;
    chips.forEach(([label, value, note], i) => {
      const cy0 = top + i * 62;
      g.fillStyle = soft; roundRect(g, chx, cy0, cxw, 54, 14); g.fill();
      text(label, chx + 18, cy0 + 22, 14, 600, muted);
      text(note, chx + 18, cy0 + 42, 13, 400, faint);
      text(value, chx + cxw - 18, cy0 + 36, 24, 700, ink, "right");
    });
  }
  y += H_HERO;

  // ── A tile per kind ──
  {
    const n = KINDS.length, gap = 14, tw = (CW - gap * (n - 1)) / n;
    KINDS.forEach((k, i) => {
      const x = M + i * (tw + gap), v = tot[k.key] || 0;
      g.fillStyle = soft; roundRect(g, x, y, tw, 118, 16); g.fill();
      g.fillStyle = k.colour; roundRect(g, x, y, tw, 6, 3); g.fill();
      g.fillStyle = k.colour; g.beginPath(); g.arc(x + 22, y + 34, 6, 0, Math.PI * 2); g.fill();
      text(k.key === "break" ? `Breaks (${breaks})` : labelOf(k, study), x + 36, y + 39, 15, 600, muted);
      text(dur(v), x + 18, y + 80, 28, 700, ink);
      text(tracked ? `${Math.round((100 * v) / tracked)}% of tracked` : "", x + 18, y + 104, 13, 400, faint);
    });
  }
  y += H_TILES;

  // ── How the time split ──
  {
    sectionTitle("How the time split", y + 10, tracked ? `${dur(tracked)} tracked` : undefined);
    const by = y + 32;
    g.fillStyle = soft; roundRect(g, M, by, CW, 26, 13); g.fill();
    g.save(); roundRect(g, M, by, CW, 26, 13); g.clip();
    let x = M;
    for (const k of KINDS) {
      const w = tracked ? (CW * (tot[k.key] || 0)) / tracked : 0;
      if (w > 0) { g.fillStyle = k.colour; g.fillRect(x, by, w, 26); x += w; }
    }
    g.restore();
    let lx = M;
    for (const k of KINDS) {
      const v = tot[k.key] || 0;
      if (!v) continue;
      const label = `${labelOf(k, study)} ${Math.round((100 * v) / Math.max(1, tracked))}%`;
      g.fillStyle = k.colour; roundRect(g, lx, by + 42, 12, 12, 3); g.fill();
      g.font = font(14, 500); g.fillStyle = muted; g.fillText(label, lx + 18, by + 53);
      lx += 18 + g.measureText(label).width + 22;
    }
  }
  y += H_SPLIT;

  // ── Daily totals (several days): stacked bars and the goal line ──
  if (H_CHART) {
    sectionTitle("Day by day", y + 10, best.w ? `Best: ${fromYmd(best.date).toLocaleDateString(undefined, { weekday: "long" })}, ${dur(best.w)}` : undefined);
    const top = y + 40, ch = 200, n = perDay.length;
    const max = Math.max(sched.goal, ...perDay.map((x) => x.d ? KINDS.reduce((a, k) => a + (x.d!.seconds[k.key] || 0), 0) : 0), 3600);
    // Gridlines every two hours.
    g.strokeStyle = line; g.lineWidth = 1;
    for (let h = 0; h <= max / 3600; h += 2) {
      const gy = top + ch - (ch * h * 3600) / max;
      g.beginPath(); g.moveTo(M + 40, gy); g.lineTo(M + CW, gy); g.stroke();
      text(`${h}h`, M + 30, gy + 5, 12, 400, faint, "right");
    }
    const slot = (CW - 50) / n, bw = Math.min(70, slot * 0.55);
    perDay.forEach(({ date, d }, i) => {
      const x = M + 50 + i * slot + (slot - bw) / 2;
      let yy = top + ch;
      for (const k of KINDS) {
        const v = d ? d.seconds[k.key] || 0 : 0;
        if (!v) continue;
        const h = (ch * v) / max;
        g.fillStyle = k.colour; g.fillRect(x, yy - h, bw, h); yy -= h;
      }
      if (yy < top + ch) { g.save(); g.globalCompositeOperation = "destination-over"; g.restore(); }
      const w = d ? (d.seconds.work || 0) + (d.seconds.overtime || 0) : 0;
      text(w ? dur(w) : "—", x + bw / 2, Math.min(yy, top + ch) - 8, 13, 600, w ? ink : faint, "center");
      text(fromYmd(date).toLocaleDateString(undefined, { weekday: "short" }), x + bw / 2, top + ch + 22, 13, 600, muted, "center");
      text(String(fromYmd(date).getDate()), x + bw / 2, top + ch + 40, 12, 400, faint, "center");
    });
    if (sched.goal > 0) {
      const gy = top + ch - (ch * sched.goal) / max;
      g.setLineDash([8, 6]); g.strokeStyle = accent; g.lineWidth = 2;
      g.beginPath(); g.moveTo(M + 40, gy); g.lineTo(M + CW, gy); g.stroke(); g.setLineDash([]);
      text(`Goal ${dur(sched.goal)}`, M + CW, gy - 8, 12, 600, accent, "right");
    }
  }
  y += H_CHART;

  // ── Timelines: one per day, with hour gridlines ──
  {
    let from = sched.start * 60, to = (sched.end <= sched.start ? sched.end + 1440 : sched.end) * 60;
    for (const d of days) { if (d.first >= 0) from = Math.min(from, d.first); if (d.last >= 0) to = Math.max(to, d.last); }
    from = Math.floor(from / 3600) * 3600; to = Math.ceil(to / 3600) * 3600;
    if (to - from < 3 * 3600) to = from + 3 * 3600;
    sectionTitle(range === "week" ? "Timelines" : "Your day", y + 10, `${clock(from)} – ${clock(to)}`);
    const tx = M + 150, tw = CW - 150, top = y + 44;
    const step = (to - from) / 3600 > 12 ? 2 : 1;
    for (let s = from; s <= to; s += 3600 * step) {
      const x = tx + (tw * (s - from)) / (to - from);
      g.strokeStyle = line; g.lineWidth = 1; g.beginPath(); g.moveTo(x, top - 6); g.lineTo(x, top + timelineRows * 58 - 18); g.stroke();
      text(clock(s), x, top - 12, 11, 400, faint, "center");
    }
    const rowsList = range === "week" ? perDay : [{ date: sched.dates[0], d: days[0] }];
    rowsList.forEach(({ date, d }, i) => {
      const ry = top + 8 + i * 58;
      text(fromYmd(date).toLocaleDateString(undefined, { weekday: "short", day: "numeric", month: "short" }), M, ry + 18, 15, 600, ink);
      g.fillStyle = soft; roundRect(g, tx, ry, tw, 26, 8); g.fill();
      g.save(); roundRect(g, tx, ry, tw, 26, 8); g.clip();
      for (const [a, b, k] of d?.segments || []) {
        const x0 = tx + (tw * (a - from)) / (to - from), x1 = tx + (tw * (b - from)) / (to - from);
        g.fillStyle = colourOf(k); g.fillRect(x0, ry, Math.max(1.5, x1 - x0), 26);
      }
      g.restore();
      const w = d ? (d.seconds.work || 0) + (d.seconds.overtime || 0) : 0;
      const a = d ? (d.seconds.away || 0) + (d.seconds.asleep || 0) : 0;
      text(d ? `${dur(w)} ${verb} · ${dur(a)} away${d.first >= 0 ? ` · ${clock(d.first)} to ${clock(d.last)}` : ""}` : "Nothing recorded",
           tx, ry + 44, 12, 400, muted);
    });
  }
  y += H_TIMELINE;

  // ── When in the day ──
  {
    const max = Math.max(...hours, 1);
    sectionTitle(study ? "When you studied" : "When you worked", y + 10, max > 1 ? `Busiest hour: ${clock(busiest * 3600)}` : undefined);
    const top = y + 40, ch = 130, bw = CW / 24;
    for (let h = 0; h < 24; h++) {
      const v = hours[h], bh = (ch * v) / max;
      g.fillStyle = soft; roundRect(g, M + h * bw + 3, top, bw - 6, ch, 6); g.fill();
      if (v > 0) { g.fillStyle = h === busiest ? accent : `${accent}88`; roundRect(g, M + h * bw + 3, top + ch - bh, bw - 6, bh, 6); g.fill(); }
      if (h % 3 === 0) text(String(h).padStart(2, "0"), M + h * bw + bw / 2, top + ch + 20, 12, 400, faint, "center");
    }
  }
  y += H_HOURS;

  // ── By subject ──
  if (H_SUBJ) {
    sectionTitle("By subject", y + 10, `${subjects.length} ${subjects.length === 1 ? "subject" : "subjects"}`);
    const most = Math.max(1, ...subjects.map((x) => x[1]));
    subjects.forEach(([name, sec], i) => {
      const ry = y + 40 + i * 42, bx = M + 200, bw = CW - 200 - 150;
      g.fillStyle = palette[i % palette.length]; g.beginPath(); g.arc(M + 8, ry + 13, 6, 0, Math.PI * 2); g.fill();
      text(name, M + 24, ry + 19, 17, 600, ink);
      g.fillStyle = soft; roundRect(g, bx, ry + 4, bw, 18, 9); g.fill();
      g.fillStyle = palette[i % palette.length]; roundRect(g, bx, ry + 4, Math.max(18, (bw * sec) / most), 18, 9); g.fill();
      text(dur(sec), M + CW - 60, ry + 19, 17, 700, ink, "right");
      text(`${Math.round((100 * sec) / Math.max(1, subjTotal))}%`, M + CW, ry + 19, 14, 500, faint, "right");
    });
  }
  y += H_SUBJ;

  // ── Insights ──
  {
    g.fillStyle = soft; roundRect(g, M, y + 10, CW, H_INSIGHTS - 30, 18); g.fill();
    g.fillStyle = accent; roundRect(g, M, y + 10, 6, H_INSIGHTS - 30, 3); g.fill();
    text("Insights", M + 28, y + 46, 20, 700, ink);
    const notes: string[] = [];
    if (!tracked) notes.push(`Nothing was recorded for this ${range === "week" ? "week" : "day"} yet.`);
    if (goal > 0 && tracked) notes.push(pctGoal >= 1 ? `Goal reached: ${dur(worked)} against ${dur(goal)}.` : `${dur(Math.max(0, goal - worked))} short of the ${dur(goal)} goal.`);
    if (longest) notes.push(`Longest stretch without a break: ${dur(longest)}.`);
    if (tracked && away) notes.push(`Away from the keyboard for ${dur(away)} (${Math.round((100 * away) / tracked)}% of the tracked time).`);
    if (max1(hours)) notes.push(`You ${study ? "study" : "work"} most around ${clock(busiest * 3600)}.`);
    if (range === "week" && best.w) notes.push(`Best day: ${fromYmd(best.date).toLocaleDateString(undefined, { weekday: "long" })}, with ${dur(best.w)}.`);
    if (study && subjects.length) notes.push(`Most time on ${subjects[0][0]} (${Math.round((100 * subjects[0][1]) / Math.max(1, subjTotal))}%).`);
    notes.slice(0, 5).forEach((n, i) => {
      g.fillStyle = accent; g.beginPath(); g.arc(M + 34, y + 78 + i * 28, 4, 0, Math.PI * 2); g.fill();
      text(n, M + 48, y + 83 + i * 28, 16, 400, ink);
    });
  }
  y += H_INSIGHTS;

  // ── Footer ──
  g.strokeStyle = line; g.lineWidth = 1; g.beginPath(); g.moveTo(M, y + 10); g.lineTo(M + CW, y + 10); g.stroke();
  text("Made by Bunbun on this PC. Your work log never leaves it.", M, y + 40, 13, 400, faint);
  text(new Date().toLocaleDateString(undefined, { day: "numeric", month: "long", year: "numeric" }), M + CW, y + 40, 13, 400, faint, "right");
  return c;
}

function max1(a: number[]) { return a.some((v) => v > 0); }

function roundRect(g: CanvasRenderingContext2D, x: number, y: number, w: number, h: number, r: number) {
  g.beginPath();
  g.moveTo(x + r, y);
  g.arcTo(x + w, y, x + w, y + h, r);
  g.arcTo(x + w, y + h, x, y + h, r);
  g.arcTo(x, y + h, x, y, r);
  g.arcTo(x, y, x + w, y, r);
  g.closePath();
}

// A one-page PDF holding the report as a JPEG (no library needed).
function pdfFromCanvas(c: HTMLCanvasElement): string {
  const jpeg = atob(c.toDataURL("image/jpeg", 0.92).split(",")[1]);
  const pw = 595, ph = Math.round((595 * c.height) / c.width);
  const objs: string[] = [];
  objs.push("<< /Type /Catalog /Pages 2 0 R >>");
  objs.push("<< /Type /Pages /Kids [3 0 R] /Count 1 >>");
  objs.push(`<< /Type /Page /Parent 2 0 R /MediaBox [0 0 ${pw} ${ph}] /Resources << /XObject << /Im0 4 0 R >> >> /Contents 5 0 R >>`);
  objs.push(`<< /Type /XObject /Subtype /Image /Width ${c.width} /Height ${c.height} /ColorSpace /DeviceRGB /BitsPerComponent 8 /Filter /DCTDecode /Length ${jpeg.length} >>\nstream\n${jpeg}\nendstream`);
  const content = `q ${pw} 0 0 ${ph} 0 0 cm /Im0 Do Q`;
  objs.push(`<< /Length ${content.length} >>\nstream\n${content}\nendstream`);
  let out = "%PDF-1.4\n%âãÏÓ\n";
  const offsets: number[] = [];
  objs.forEach((o, i) => { offsets.push(out.length); out += `${i + 1} 0 obj\n${o}\nendobj\n`; });
  const xref = out.length;
  out += `xref\n0 ${objs.length + 1}\n0000000000 65535 f \n`;
  for (const off of offsets) out += `${String(off).padStart(10, "0")} 00000 n \n`;
  out += `trailer\n<< /Size ${objs.length + 1} /Root 1 0 R >>\nstartxref\n${xref}\n%%EOF\n`;
  return btoa(out);  // every character is a byte (the JPEG came from atob)
}

export function WorkReport({ onClose }: { onClose: () => void }) {
  const s = useStore();
  const [range, setRange] = useState<Range>("today");
  const [img, setImg] = useState<HTMLCanvasElement | null>(null);
  const [note, setNote] = useState("");
  const make = useCallback(async () => {
    const today = new Date();
    const end = range === "yesterday" ? new Date(today.getTime() - 864e5) : today;
    const start = range === "week" ? new Date(today.getTime() - 6 * 864e5) : end;
    let days: WorkDay[] = [];
    try { days = await call<WorkDay[]>("workDays", { from: ymd(start), to: ymd(end) }); } catch { days = []; }
    const study = s.get("Bunbun.UseCase") === "study";
    let startMin = minutesOf(s.get("Work.Start"), 540), endMin = minutesOf(s.get("Work.End"), 1080);
    const lunch = Number(s.get("Work.LunchMinutes")) || 0;
    const goalH = Number(s.get(study ? "Study.GoalHours" : "Work.GoalHours")) || 0;
    const shift = ((endMin - startMin) % 1440 + 1440) % 1440;
    const goal = goalH > 0 ? goalH * 3600 : study ? 0 : Math.max(0, shift - lunch) * 60;
    if (study) {  // the timetable's earliest start and latest end
      const rows = parseTimetable(s.get("Study.Timetable"));
      if (rows.length) { startMin = Math.min(...rows.map((r) => minutesOf(r.start, 540))); endMin = Math.max(...rows.map((r) => minutesOf(r.end, 1080))); }
    }
    // Every date of the range, so a day with nothing recorded still has its place.
    const dates: number[] = [];
    for (let d = new Date(start); ymd(d) <= ymd(end); d = new Date(d.getTime() + 864e5)) dates.push(ymd(d));
    setImg(drawReport(days, range, { start: startMin, end: endMin, goal, name: "", study, dates }));
  }, [range, s]);
  useEffect(() => { make(); }, [make]);
  const save = async (ext: "png" | "pdf") => {
    if (!img) return;
    const stamp = new Date().toISOString().slice(0, 10);
    const base64 = ext === "png" ? img.toDataURL("image/png").split(",")[1] : pdfFromCanvas(img);
    try {
      const title = s.get("Bunbun.UseCase") === "study" ? "Study report" : "Work report";
      const r = await call<{ cancelled?: boolean; path?: string }>("saveFile", { name: `${title} ${stamp}.${ext}`, ext, base64 });
      if (r && !r.cancelled) setNote(`Saved to ${r.path}`);
    } catch (e) { setNote(`Couldn't save: ${String((e as Error)?.message ?? e)}`); }
  };
  return (
    <div className="modal-back" role="dialog" aria-modal="true" aria-label="Work report" onClick={onClose}>
      <div className="modal report" onClick={(e) => e.stopPropagation()}>
        <div className="report-bar">
          <Segmented value={range} onChange={(v) => setRange(v as Range)}
                     options={[{ value: "today", label: "Today" }, { value: "yesterday", label: "Yesterday" }, { value: "week", label: "Last 7 days" }]} />
          <span className="grow" />
          <button className="btn small" disabled={!img} onClick={() => save("png")}><FileImage size={14} /> Save image</button>
          <button className="btn small primary" disabled={!img} onClick={() => save("pdf")}><FileText size={14} /> Save PDF</button>
          <button className="btn small icon" aria-label="Close" onClick={onClose}><X size={15} /></button>
        </div>
        <div className="report-view">{img ? <img src={img.toDataURL("image/png")} alt="Work report" /> : "Making the report…"}</div>
        {note && <div className="note" style={{ margin: "10px 0 0" }}>{note}</div>}
      </div>
    </div>
  );
}

// ── The page ─────────────────────────────────────────────────────────────────
export function Work({ openReport = false }: { openReport?: boolean }) {
  const s = useStore();
  const [report, setReport] = useState(openReport);
  useEffect(() => { if (openReport) setReport(true); }, [openReport]);
  const use = s.get("Bunbun.UseCase") || "agents";
  const workOn = use !== "agents";
  const study = use === "study";
  const title = study ? "Study" : workOn ? "Work day" : "Mode";
  const subtitle = study
    ? "Your timetable and study sessions. Bunbun follows the timetable, counts Pomodoro sessions, keeps time per subject and builds your streak."
    : workOn
      ? "Your working hours. Bunbun follows them, pauses for lunch, nudges you to take breaks, counts time away and keeps a report of your day."
      : "What you use Bunbun for. Pick Office work or Study to add a working day or a timetable to the island.";
  return (
    <div className="page">
      <PageHead title={title} subtitle={subtitle} />
      {workOn && <TodayCard study={study} onReport={() => setReport(true)} />}
      <Section title="Bunbun is for">
        <UseCaseChoice />
      </Section>
      {study && (
        <>
          <Section title="Timetable">
            <Timetable />
          </Section>
          <Section title="Goal & breaks">
            <Row label="Daily study goal" description="Fills the ring on the island. Days that reach it build your streak." settingKeys={["Study.GoalHours"]}>
              <NumberInput k="Study.GoalHours" min={0} max={24} unit="h" />
            </Row>
            <Setting k="Study.Pomodoro" />
            <Row label="Breaks" description="A reminder after this much study in a row, and how long a break is." settingKeys={["Work.BreakEvery", "Work.BreakMinutes"]}>
              <NumberInput k="Work.BreakEvery" min={0} max={480} unit="min" /><NumberInput k="Work.BreakMinutes" min={1} max={120} unit="min" />
            </Row>
            <Setting k="Bunbun.FocusGuard" />
          </Section>
        </>
      )}
      {workOn && !study && (
        <>
          <Section title="Working hours">
            <div className="day-card">
              <DayPreview />
            </div>
            <Row label="Working days" settingKeys={["Work.Days"]}><Days /></Row>
            <Row label="Hours" description="An end before the start runs past midnight, for night shifts." settingKeys={["Work.Start", "Work.End"]}>
              <TimeInput k="Work.Start" /><span className="muted">to</span><TimeInput k="Work.End" />
            </Row>
            <Row label="Lunch" description="Work mode pauses for it, and the mascot has a snack. 0 minutes for no lunch break." settingKeys={["Work.LunchStart", "Work.LunchMinutes"]}>
              <TimeInput k="Work.LunchStart" /><NumberInput k="Work.LunchMinutes" min={0} max={240} unit="min" />
            </Row>
            <Row label="Start with my hours" description="Work mode starts and stops with your hours. Off, it only runs when you press Start on the island." settingKeys={["Work.AutoStart"]}>
              <Toggle on={s.bool("Work.AutoStart")} onChange={(v) => s.set("Work.AutoStart", v)} />
            </Row>
          </Section>
          <Section title="Goal & breaks">
            <Row label="Daily goal" description="Fills the ring on the island. 0 uses your hours less lunch." settingKeys={["Work.GoalHours"]}>
              <NumberInput k="Work.GoalHours" min={0} max={24} unit="h" />
            </Row>
            <Row label="Breaks" description="A reminder after this much work in a row, and how long a break is. Being away that long counts as one." settingKeys={["Work.BreakEvery", "Work.BreakMinutes"]}>
              <NumberInput k="Work.BreakEvery" min={0} max={480} unit="min" /><NumberInput k="Work.BreakMinutes" min={1} max={120} unit="min" />
            </Row>
          </Section>
        </>
      )}
      {workOn && (
        <Section title="Reminders & away">
          {study
            ? <Setting k="Work.Reminders" label="Class and break reminders" description="A note when a class begins, a break is due and you reach your goal." />
            : <Setting k="Work.Reminders" />}
          <Setting k="Bunbun.AfkMinutes" />
          <Setting k="Bunbun.SleepMinutes" />
        </Section>
      )}
      {report && <WorkReport onClose={() => setReport(false)} />}
    </div>
  );
}
