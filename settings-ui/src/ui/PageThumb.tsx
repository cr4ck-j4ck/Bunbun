// Small drawings of each island page, for the Island pages screen. They are
// sketches, not screenshots: just enough shape to recognise the page at a glance.
import { CSSProperties, ReactNode } from "react";

export type PageKey =
  | "home" | "music" | "calendar" | "weather" | "system" | "toggles" | "tray" | "todo"
  | "lyrics" | "agent" | "clipboard" | "note" | "work" | "focus" | "mirror" | "apps" | "water" | "screentime";

const box = (s: CSSProperties): CSSProperties => ({ position: "absolute", ...s });
const line = (left: number, top: number, width: number, o = 0.35, h = 5): ReactNode => (
  <i style={box({ left, top, width, height: h, borderRadius: h, background: `rgba(255,255,255,${o})` })} />
);

function Body({ page, accent }: { page: PageKey; accent: string }) {
  const a = accent;
  switch (page) {
    case "home":
      return <>{[0, 1, 2].map((i) => (
        <i key={i} style={box({ left: 8 + i * 50, top: 8, width: 44, height: 50, borderRadius: 9,
          background: i === 0 ? `color-mix(in srgb, ${a} 35%, #1c1c1e)` : "#1c1c1e" })} />
      ))}</>;
    case "music":
      return <>
        <i style={box({ left: 10, top: 10, width: 44, height: 44, borderRadius: 9, background: `linear-gradient(135deg, ${a}, #222)` })} />
        {line(62, 16, 70, 0.8)}{line(62, 27, 46)}
        {[0, 1, 2, 3, 4].map((i) => <i key={i} style={box({ left: 62 + i * 7, top: 40 + (i % 2) * 4, width: 4, height: 12 - (i % 2) * 4, borderRadius: 2, background: a })} />)}
      </>;
    case "calendar":
      return <>
        <b style={box({ left: 12, top: 8, fontSize: 24, fontWeight: 700, color: "#fff" })}>14</b>
        {line(12, 44, 30, 0.35)}
        <i style={box({ left: 68, top: 12, width: 3, height: 14, borderRadius: 2, background: a })} />{line(76, 16, 60, 0.6)}
        <i style={box({ left: 68, top: 34, width: 3, height: 14, borderRadius: 2, background: "rgba(255,255,255,.3)" })} />{line(76, 38, 44)}
      </>;
    case "weather":
      return <>
        <i style={box({ left: 14, top: 12, width: 30, height: 30, borderRadius: "50%", background: a, boxShadow: `0 0 14px ${a}` })} />
        <b style={box({ left: 60, top: 10, fontSize: 22, fontWeight: 650, color: "#fff" })}>24°</b>
        {line(60, 42, 54)}
      </>;
    case "system":
      return <>{[0, 1, 2].map((i) => (
        <i key={i} style={box({ left: 12 + i * 48, top: 12, width: 34, height: 34, borderRadius: "50%",
          background: `conic-gradient(${a} ${[62, 38, 80][i]}%, rgba(255,255,255,.1) 0)`,
          WebkitMask: "radial-gradient(circle, transparent 11px, #000 12px)", mask: "radial-gradient(circle, transparent 11px, #000 12px)" })} />
      ))}</>;
    case "toggles":
      return <>{[0, 1, 2, 3].map((i) => (
        <i key={i} style={box({ left: 12 + i * 37, top: 14, width: 28, height: 28, borderRadius: "50%",
          background: i < 2 ? a : "rgba(255,255,255,.12)" })} />
      ))}</>;
    case "tray":
      return <>{[0, 1, 2].map((i) => (
        <i key={i} style={box({ left: 18 + i * 44, top: 12, width: 30, height: 36, borderRadius: 6,
          background: "rgba(255,255,255,.14)", borderTop: `6px solid ${i === 0 ? a : "rgba(255,255,255,.25)"}` })} />
      ))}</>;
    case "todo":
    case "note":
      return <>{[0, 1, 2].map((i) => (
        <span key={i}>
          <i style={box({ left: 12, top: 10 + i * 15, width: 9, height: 9, borderRadius: page === "todo" ? "50%" : 3,
            background: i === 0 ? a : "transparent", border: i === 0 ? "none" : "1.5px solid rgba(255,255,255,.45)" })} />
          {line(28, 12 + i * 15, [80, 60, 96][i], i === 0 ? 0.25 : 0.6)}
        </span>
      ))}</>;
    case "lyrics":
      return <>
        {line(30, 12, 100, 0.25)}
        <i style={box({ left: 20, top: 26, width: 120, height: 8, borderRadius: 8, background: `linear-gradient(90deg, #fff 55%, ${a} 55%)` })} />
        {line(36, 44, 88, 0.25)}
      </>;
    case "agent":
      return <>
        <i style={box({ left: 12, top: 12, width: 10, height: 10, borderRadius: "50%", background: a })} />
        {line(28, 14, 60, 0.8)}{line(12, 30, 120, 0.35)}
        <i style={box({ left: 12, top: 44, width: 136, height: 5, borderRadius: 5, background: "rgba(255,255,255,.1)" })} />
        <i style={box({ left: 12, top: 44, width: 84, height: 5, borderRadius: 5, background: a })} />
      </>;
    case "clipboard":
      return <>{[0, 1, 2].map((i) => (
        <i key={i} style={box({ left: 10, top: 8 + i * 16, width: 140, height: 13, borderRadius: 5,
          background: i === 0 ? `color-mix(in srgb, ${a} 30%, transparent)` : "rgba(255,255,255,.07)" })} />
      ))}</>;
    case "focus":
      return <>
        <i style={box({ left: 52, top: 4, width: 52, height: 52, borderRadius: "50%",
          background: `conic-gradient(${a} 70%, rgba(255,255,255,.1) 0)`,
          WebkitMask: "radial-gradient(circle, transparent 20px, #000 21px)", mask: "radial-gradient(circle, transparent 20px, #000 21px)" })} />
        <b style={box({ left: 0, right: 0, top: 22, textAlign: "center", fontSize: 11, fontWeight: 650, color: "#fff" })}>25:00</b>
      </>;
    case "work":  // today's time and goal ring on the left, the day's timeline on the right
      return <>
        <i style={box({ left: 8, top: 6, width: 62, height: 52, borderRadius: 8, background: "rgba(255,255,255,.07)" })} />
        <b style={box({ left: 14, top: 18, fontSize: 13, fontWeight: 700, color: "#fff" })}>3h 2m</b>
        {line(14, 40, 40, 0.3, 4)}
        <i style={box({ left: 76, top: 6, width: 76, height: 52, borderRadius: 8, background: "rgba(255,255,255,.07)" })} />
        <i style={box({ left: 82, top: 16, width: 64, height: 8, borderRadius: 4, background: "rgba(255,255,255,.12)" })} />
        <i style={box({ left: 90, top: 16, width: 26, height: 8, borderRadius: 4, background: a })} />
        <i style={box({ left: 120, top: 16, width: 8, height: 8, borderRadius: 4, background: "#30d158" })} />
        {[0, 1, 2].map((i) => <span key={i}>{line(82, 32 + i * 8, 40 - i * 8, 0.3, 4)}</span>)}
      </>;
    case "screentime":
      return <>
        <b style={box({ left: 10, top: 6, fontSize: 15, fontWeight: 700, color: "#fff" })}>2h 5m</b>
        {[0, 1, 2, 3, 4, 5, 6].map((i) => (
          <i key={i} style={box({ left: 12 + i * 9, top: 34 + (i * 5) % 14, width: 5, height: 22 - (i * 5) % 14, borderRadius: 3,
            background: i === 6 ? a : "rgba(255,255,255,.25)" })} />
        ))}
        {[0, 1, 2].map((i) => <i key={`r${i}`} style={box({ left: 84, top: 8 + i * 17, width: 11, height: 11, borderRadius: 3,
          background: ["#4285f4", "#d97757", "#34c759"][i] })} />)}
        {[0, 1, 2].map((i) => <span key={`l${i}`}>{line(100, 11 + i * 17, 46 - i * 12, i === 0 ? 0.8 : 0.35)}</span>)}
      </>;
    case "water":
      return <>
        <i style={box({ left: 14, top: 6, width: 52, height: 52, borderRadius: "50%",
          background: "conic-gradient(#5ac8fa 45%, rgba(255,255,255,.1) 0)",
          WebkitMask: "radial-gradient(circle, transparent 20px, #000 21px)", mask: "radial-gradient(circle, transparent 20px, #000 21px)" })} />
        <i style={box({ left: 80, top: 8, width: 64, height: 16, borderRadius: 8, background: "#5ac8fa" })} />
        {[0, 1, 2, 3, 4, 5, 6].map((i) => (
          <i key={i} style={box({ left: 82 + i * 9, top: 30 + (i * 7) % 18, width: 5, height: 26 - (i * 7) % 18, borderRadius: 3,
            background: i === 6 ? "#5ac8fa" : "rgba(90,200,250,.45)" })} />
        ))}
      </>;
    case "mirror":
      return <>
        <i style={box({ left: 30, top: 6, width: 100, height: 52, borderRadius: 10, background: "linear-gradient(160deg, #2b3440, #151a20)" })} />
        <i style={box({ left: 70, top: 16, width: 20, height: 20, borderRadius: "50%", background: "rgba(255,255,255,.35)" })} />
        <i style={box({ left: 60, top: 38, width: 40, height: 20, borderRadius: "20px 20px 0 0", background: "rgba(255,255,255,.25)" })} />
      </>;
    case "apps":
      return <>{[0, 1, 2, 3, 4, 5, 6, 7].map((i) => (
        <i key={i} style={box({ left: 14 + (i % 4) * 34, top: 8 + Math.floor(i / 4) * 26, width: 24, height: 20, borderRadius: 6,
          background: i === 2 ? a : "rgba(255,255,255,.12)" })} />
      ))}</>;
  }
}

/** A tiny island page: a dark rounded frame, a header line and the page's sketch. */
export function PageThumb({ page, accent = "var(--accent)", off }: { page: PageKey; accent?: string; off?: boolean }) {
  return (
    <div className={`thumb ${off ? "off" : ""}`} aria-hidden>
      <div className="thumb-head"><i style={{ background: accent }} /><i /></div>
      <div className="thumb-body"><Body page={page} accent={accent} /></div>
    </div>
  );
}
