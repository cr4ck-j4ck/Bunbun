import { Check } from "lucide-react";
import { useEffect, useRef } from "react";
import { useStore } from "../store";
import { LinuxBadge, Section, Setting } from "../ui/controls";

// The island's own multipliers per style (island_core.cpp, render loop).
const STYLES = [
  { value: "smooth", label: "Smooth", desc: "Calm, no overshoot", k: 1.0, d: 1.35 },
  { value: "default", label: "Default", desc: "Balanced and lively", k: 1.0, d: 1.0 },
  { value: "bouncy", label: "Bouncy", desc: "Playful overshoot", k: 1.1, d: 0.7 },
  { value: "snappy", label: "Snappy", desc: "Quick and precise", k: 1.5, d: 1.25 },
];

const SPEEDS: Record<string, number> = {
  "very-slow": 0.5, slow: 0.75, normal: 1, fast: 1.35, "very-fast": 1.65, "ultra-fast": 2,
};

// Collapsed and expanded island sizes for the demo, in px.
const SMALL = { w: 56, h: 20 };
const BIG = { w: 150, h: 44 };
const TRACE_W = 150;
const TRACE_H = 26;

// One spring axis, stepped exactly like the island's Spring::Step: semi-implicit
// Euler in fixed 0.5 ms steps. Growing uses k 380 / d 26 and shrinking k 200 /
// d 28, both scaled by the style, and time is scaled by Animation speed.
type Axis = { v: number; vel: number };
function step(a: Axis, target: number, dt: number, km: number, dm: number) {
  const grow = target > a.v;
  const k = (grow ? 380 : target < a.v ? 200 : 280) * km;
  const d = (grow ? 26 : target < a.v ? 28 : 24) * dm;
  for (let left = dt; left > 0; left -= 0.0005) {
    const h = Math.min(left, 0.0005);
    a.vel += (-k * (a.v - target) - d * a.vel) * h;
    a.v += a.vel * h;
  }
}

// Every card runs off the same clock, so the four styles can be compared side by side.
function Demo({ km, dm, speed }: { km: number; dm: number; speed: number }) {
  const pill = useRef<HTMLDivElement>(null);
  const trace = useRef<SVGPolylineElement>(null);
  useEffect(() => {
    const w: Axis = { v: SMALL.w, vel: 0 };
    const h: Axis = { v: SMALL.h, vel: 0 };
    const history: number[] = [];
    // Long enough for the slowest style to settle at the chosen speed.
    const period = 1.7 / Math.min(speed, 1);
    let last = performance.now();
    let raf = 0;
    const frame = (now: number) => {
      raf = requestAnimationFrame(frame);
      if (document.visibilityState !== "visible") { last = now; return; }
      const dt = Math.min((now - last) / 1000, 0.05);
      last = now;
      const expanded = Math.floor(now / 1000 / period) % 2 === 1;
      step(w, expanded ? BIG.w : SMALL.w, dt * speed, km, dm);
      step(h, expanded ? BIG.h : SMALL.h, dt * speed, km, dm);
      if (pill.current) {
        pill.current.style.width = `${w.v}px`;
        pill.current.style.height = `${h.v}px`;
        pill.current.style.borderRadius = `${h.v / 2}px`;
      }
      // Trace of the width over the last couple of seconds; overshoot pokes
      // above the dashed target lines.
      history.push(w.v);
      if (history.length > TRACE_W) history.shift();
      if (trace.current) {
        const span = BIG.w - SMALL.w;
        trace.current.setAttribute("points", history
          .map((v, i) => `${i + TRACE_W - history.length},${(TRACE_H - 4 - ((v - SMALL.w) / span) * (TRACE_H - 10)).toFixed(1)}`)
          .join(" "));
      }
    };
    raf = requestAnimationFrame(frame);
    return () => cancelAnimationFrame(raf);
  }, [km, dm, speed]);
  return (
    <div className="demo">
      <div className="demo-lane"><div ref={pill} className="demo-pill" /></div>
      <svg className="demo-trace" viewBox={`0 0 ${TRACE_W} ${TRACE_H}`} preserveAspectRatio="none" aria-hidden>
        <line x1="0" x2={TRACE_W} y1={6} y2={6} />
        <line x1="0" x2={TRACE_W} y1={TRACE_H - 4} y2={TRACE_H - 4} />
        <polyline ref={trace} />
      </svg>
    </div>
  );
}

// How the island moves and when it appears. Shown on the Position & motion page,
// under the screen and position settings (Displays.tsx).
export function MotionSections() {
  const s = useStore();
  const style = s.get("Animations.AnimationStyle");
  const speed = SPEEDS[String(s.get("Animations.AnimationSpeed"))] ?? 1;
  return (
    <>
      <section className="section">
        <h3>Motion style<LinuxBadge keys={["Animations.AnimationStyle"]} /></h3>
        <p className="section-note">Each preview runs the island's real spring at your animation speed. The line underneath
          traces the size: anything poking past the dashed lines is overshoot.</p>
        <div className="choices" style={{ gridTemplateColumns: "repeat(auto-fill, minmax(170px, 1fr))" }}>
          {STYLES.map((st) => (
            <button key={st.value} aria-pressed={style === st.value} className={`choice ${style === st.value ? "on" : ""}`} onClick={() => s.set("Animations.AnimationStyle", st.value)}>
              {style === st.value && <span className="check"><Check size={13} strokeWidth={3} /></span>}
              <Demo km={st.k} dm={st.d} speed={speed} />
              <b>{st.label}</b>
              <small>{st.desc}</small>
            </button>
          ))}
        </div>
      </section>
      <Section title="Speed & smoothness">
        <Setting k="Animations.AnimationSpeed" />
        <Setting k="Animations.PowerSaver" />
        <Setting k="Animations.TargetFPS" />
        <Setting k="Bunbun.AmbientFps" label="Ambient frame rate" />
      </Section>
      <Section title="Transitions">
        <Setting k="Animations.ExpandedMediaTransitions" />
        <Setting k="Animations.PillCoverSpin" />
      </Section>
      <Section title="When the island appears">
        <Setting k="Behavior.ExpandOnHover" />
        <Setting k="Behavior.AutoHideIdleSeconds" />
        <Setting k="Behavior.FullscreenMode" />
        <Setting k="Behavior.FullscreenShowApps" />
        <Setting k="Behavior.FullscreenTuckApps" />
        <Setting k="Behavior.FullscreenHideApps" />
        <Setting k="Bunbun.TuckRevealMs" />
        <Setting k="Behavior.UnhideOnHover" />
        <Setting k="Behavior.AlwaysOnTop" />
      </Section>
    </>
  );
}
