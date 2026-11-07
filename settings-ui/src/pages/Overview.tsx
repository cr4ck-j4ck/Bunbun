import { motion } from "framer-motion";
import { Bot, Music2, Play, Sparkles, Wand2 } from "lucide-react";
import { useEffect, useState } from "react";
import { call } from "../bridge";
import { useStore } from "../store";
import { Row, Section, Toggle } from "../ui/controls";
import { LiveMascot, useMascotStyle } from "../ui/mascot";
import { IslandPreview } from "../ui/preview";
import { modeNav, USE_CASES } from "./Work";

type System = { autostart: boolean; islandRunning: boolean; version: string; platform?: string };

export function Overview({ go }: { go: (page: string) => void }) {
  const s = useStore();
  const style = useMascotStyle();
  const [sys, setSys] = useState<System | null>(null);
  const [mood, setMood] = useState("idle");

  useEffect(() => {
    const load = () => call<System>("getSystem").then(setSys);
    load();
    const t = window.setInterval(load, 3000);
    return () => window.clearInterval(t);
  }, []);

  // Bun reacts while you look around the page.
  useEffect(() => {
    const moods = ["idle", "music", "working", "thinking", "happy"];
    let i = 0;
    const t = window.setInterval(() => setMood(moods[++i % moods.length]), 3200);
    return () => window.clearInterval(t);
  }, []);

  const quick: [string, string, string, typeof Bot][] = [
    ["Bunbun.Agents", "Coding agents", "Antigravity, Claude Code, Cursor, VS Code sessions and approvals", Bot],
    ["Bunbun.Assistant", "Ask Bun", "Your local Windows assistant on Ctrl+Alt+B", Wand2],
    ["Modules.Media", "Music & media", "Now playing, controls, waveform", Music2],
    ["Behavior.ExpandOnHover", "Expand on hover", "Hover the island to open its dashboard", Sparkles],
  ];

  return (
    <div className="page">
      <motion.div className="hero" initial={{ opacity: 0, y: 10 }} animate={{ opacity: 1, y: 0 }}>
        <div>
          <h2>Hi, I’m <em>Bun</em>.</h2>
          <p>
            Your island lives at the top of the screen: music, weather, timers, files, and {({ work: "your work day", study: "your studies", both: "your coding agents and work day" } as Record<string, string>)[s.get("Bunbun.UseCase")] ?? "your coding agents"},
            all in one place. Everything here applies instantly.
          </p>
          <div className="actions">
            {sys && !sys.islandRunning && (
              <button className="btn primary" onClick={() => call("startIsland").then(() => setTimeout(() => call<System>("getSystem").then(setSys), 1500))}>
                <Play size={16} /> Start the island
              </button>
            )}
            <button className="btn" onClick={() => go("mascot")}>Customize Bun</button>
            <button className="btn" onClick={() => go("pages")}>Choose pages</button>
            <button className="btn" onClick={() => go("appearance")}>Island style</button>
          </div>
        </div>
        <div style={{ display: "grid", gridTemplateColumns: "1fr auto", alignItems: "center", gap: 6 }}>
          <IslandPreview />
          <LiveMascot style={style} mood={mood} size={150} />
        </div>
      </motion.div>

      <Section title="Your mode">
        {(() => {
          const use = s.get("Bunbun.UseCase") || "agents";
          const m = USE_CASES.find((u) => u.value === use) ?? USE_CASES[0];
          const nav = modeNav(use);
          return (
            <div className="mode-now" style={{ "--mode": m.tint } as React.CSSProperties}>
              <span className="mode-now-ico">{m.icon}</span>
              <div className="mode-now-text">
                <small>Bunbun is for</small>
                <b>{m.title}</b>
                <span>{m.text}</span>
              </div>
              <button className="btn small" onClick={() => go("work")}>{use === "agents" ? "Change" : `Open ${nav.label}`}</button>
            </div>
          );
        })()}
      </Section>

      <Section title="Quick switches">
        {quick.filter(([k]) => k !== "Bunbun.Agents" || ["agents", "both", ""].includes(s.get("Bunbun.UseCase"))).map(([k, label, desc, Icon]) => (
          <Row key={k} label={<span style={{ display: "flex", alignItems: "center", gap: 10 }}><Icon size={16} color="var(--accent)" />{label}</span>} description={desc} settingKeys={[k]}>
            <Toggle on={s.bool(k)} onChange={(v) => s.set(k, v)} />
          </Row>
        ))}
      </Section>

      <Section title="Status">
        <Row label="Island" description={sys?.islandRunning ? "Running. Changes preview live." : "Not running. Start it to see your changes."}>
          <span className={`badge ${sys?.islandRunning ? "green" : ""}`}>{sys?.islandRunning ? "Running" : "Stopped"}</span>
        </Row>
        <Row label="Version" description={sys?.platform === "linux"
          ? "Bunbun is native C++ with GTK and Cairo; this window uses the system WebKitGTK only while it is open."
          : "Bunbun is native C++ and Direct2D; this window uses the system WebView2 only while it is open."}>
          <span className="badge">v{sys?.version ?? "…"}</span>
        </Row>
      </Section>
    </div>
  );
}
