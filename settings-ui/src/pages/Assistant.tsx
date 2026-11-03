import { BookOpen, Bot, Check, FolderOpen, Info, Keyboard, Link2, Unlink2 } from "lucide-react";
import { useEffect, useState } from "react";
import { call } from "../bridge";
import { useIsLinux } from "../platform";
import { useStore } from "../store";
import { PageHead, Row, Section, Setting, Toggle } from "../ui/controls";

const COMMANDS = [
  "open notepad", "wifi settings", "bluetooth", "play / pause", "next song", "volume 40", "mute", "timer 25 min",
  "pomodoro", "stop timer", "search cute bunnies", "what's playing", "weather", "battery", "cpu", "what time is it",
  "12*7 + 3", "screenshot", "lock", "dnd on", "dnd off", "define serendipity", "wiki Direct2D", "help",
];

type AgentStatus = Record<string, string | boolean>;
type AiStatus = { helper: boolean; modelsFolder: string; models: { name: string; path: string; size: number }[] };

const GUIDE = "https://github.com/iaryasharma/Bunbun/blob/main/docs/local-ai.md";

function aiSummary(ai: AiStatus) {
  const gb = (n: number) => `${(n / 1e9).toFixed(1)} GB`;
  const model = ai.models.length === 0
    ? `No model yet: put a .gguf file in ${ai.modelsFolder}.`
    : `${ai.models.length === 1 ? `Model: ${ai.models[0].name} (${gb(ai.models[0].size)}).` : `${ai.models.length} models found.`}`;
  return `${ai.helper ? "Helper installed." : "bunbun-brain.exe isn't built yet (see the guide)."} ${model} Runs offline; nothing leaves this PC.`;
}

// Every agent connects the same way and gets the same island experience:
// session, live steps, diffs, approval cards, and a hop when it finishes.
const AGENTS = [
  { id: "claude", name: "Claude Code", cmd: "Claude", show: "Bunbun.ShowClaude", approvals: "Cards whenever Claude Code would ask for permission, and its questions." },
  { id: "antigravity", name: "Antigravity (IDE, app & CLI)", cmd: "Antigravity", show: "Bunbun.ShowAntigravity", approvals: "Cards for run_command when Ask before shell commands is on." },
  { id: "cursor", name: "Cursor (Agent & Composer)", cmd: "Cursor", show: "Bunbun.ShowCursor", approvals: "Cards for shell commands when Ask before shell commands is on." },
  { id: "copilot", name: "VS Code / GitHub Copilot", cmd: "Copilot", show: "Bunbun.ShowCopilot", approvals: "Cards whenever Copilot would ask for permission." },
  { id: "codex", name: "Codex", cmd: "Codex", show: "Bunbun.ShowCodex", approvals: "Progress on the island; Codex asks for permission in its own window." },
] as const;

// Commits on blur / Enter, and only a single letter, digit or "SPACE". Saving on
// every keystroke briefly wrote "" while retyping, and the island then fell back
// to Space, which grabs Ctrl+Alt+Space from the Claude desktop app.
function HotkeyKey({ value, onChange }: { value: string; onChange: (v: string) => void }) {
  const [text, setText] = useState(value);
  useEffect(() => setText(value), [value]);
  const commit = () => {
    const v = text.trim().toUpperCase();
    if (/^[A-Z0-9]$/.test(v) || v === "SPACE") onChange(v);
    else setText(value);
  };
  return (
    <input className="field" aria-label="Hotkey key" style={{ width: 90, minWidth: 90, textAlign: "center", textTransform: "uppercase" }}
      maxLength={5} value={text} onChange={(e) => setText(e.target.value)} onBlur={commit}
      onKeyDown={(e) => { if (e.key === "Enter") (e.target as HTMLInputElement).blur(); }} />
  );
}

export function Assistant() {
  const s = useStore();
  const linux = useIsLinux();
  const [status, setStatus] = useState<AgentStatus | null>(null);
  const [busy, setBusy] = useState("");
  const [report, setReport] = useState("");
  const [copied, setCopied] = useState(false);
  const [statusError, setStatusError] = useState(false);
  const refresh = () => call<AgentStatus>("agentStatus")
    .then((r) => { setStatus(r); setStatusError(false); })
    .catch(() => setStatusError(true));
  useEffect(() => { refresh(); }, []);
  const [ai, setAi] = useState<AiStatus | null>(null);
  const refreshAi = () => call<AiStatus>("localAiStatus").then(setAi).catch(() => setAi(null));
  useEffect(() => {
    refreshAi();
    // Pick up a model dropped into the folder while this page is open.
    const t = setInterval(refreshAi, 5000);
    return () => clearInterval(t);
  }, []);

  const toggle = async (a: (typeof AGENTS)[number]) => {
    if (!status) return;
    const connected = !!status[`${a.id}Connected`];
    const path = String(status[`${a.id}SettingsPath`] ?? "");
    const ok = window.confirm(connected
      ? `Remove Bunbun's hooks for ${a.name} from
${path}?

A backup is made first.`
      : `Add Bunbun's hook entries for ${a.name} to
${path}

Your other settings stay as they are, and a backup is made first.`);
    if (!ok) return;
    setBusy(a.id);
    setReport("");
    try {
      const r = await call<{ ok: boolean; report: string }>(`${connected ? "disconnect" : "connect"}${a.cmd}`);
      setReport(r.ok ? r.report : `Couldn't update ${a.name}: ${r.report}`);
    } catch (e) {
      setReport(`Couldn't update ${a.name}: ${String((e as Error)?.message ?? e)}`);
    } finally {
      setBusy("");
      refresh();
    }
  };

  return (
    <div className="page">
      <PageHead title="Assistant & agents" subtitle={linux ? "Bun keeps an eye on your coding agents. Ask Bun, the local assistant, isn't on Linux yet."
        : "Bun is a local Windows assistant, and keeps an eye on your coding agents."} />

      <Section title="Ask Bun">
        <Setting k="Bunbun.Assistant" label="Ask Bun" description="Press the hotkey, type a command, press Enter. Everything runs on this PC; nothing is sent to any AI service." />
        <Row label={<span style={{ display: "flex", gap: 8, alignItems: "center" }}><Keyboard size={15} /> Hotkey</span>}
          description="Default Ctrl + Alt + B. (Ctrl + Alt + Space is used by the Claude desktop app.)" settingKeys={["Shortcuts.LookupModifiers", "Shortcuts.LookupKey"]}>
          <select className="field" style={{ minWidth: 150 }} value={s.get("Shortcuts.LookupModifiers")} onChange={(e) => s.set("Shortcuts.LookupModifiers", e.target.value)}>
            <option value="ctrl_alt">Ctrl + Alt</option>
            <option value="ctrl_shift">Ctrl + Shift</option>
            <option value="alt_shift">Alt + Shift</option>
            <option value="win_alt">Win + Alt</option>
            <option value="ctrl_alt_shift">Ctrl + Alt + Shift</option>
          </select>
          <span style={{ color: "var(--muted)" }}>+</span>
          <HotkeyKey value={s.get("Shortcuts.LookupKey")} onChange={(v) => s.set("Shortcuts.LookupKey", v)} />
        </Row>
        <Setting k="Bunbun.SearchEngine" />
      </Section>
      <Section title="Local AI">
        <Setting k="Bunbun.LocalAi" />
        <Row label="Status" description={ai ? aiSummary(ai) : "Checking…"}>
          {ai?.helper && ai.models.length > 0 && <span className="badge green"><Check size={12} /> Ready</span>}
          <button className="btn small" onClick={() => call("openModelsFolder").then(refreshAi)}><FolderOpen size={14} /> Models folder</button>
          <button className="btn small" onClick={() => call("openUrl", { url: GUIDE })}><BookOpen size={14} /> Guide</button>
        </Row>
        {ai && ai.models.length > 1 && (
          <Row label="Model" description="Which downloaded model Bun uses." settingKeys={["Bunbun.LocalAiModel"]}>
            <select className="field" style={{ minWidth: 220 }} value={s.get("Bunbun.LocalAiModel")}
              onChange={(e) => s.set("Bunbun.LocalAiModel", e.target.value)}>
              <option value="">First in the folder</option>
              {ai.models.map((m) => <option key={m.path} value={m.path}>{m.name}</option>)}
            </select>
          </Row>
        )}
        <Setting k="Bunbun.LocalAiIdleMinutes" />
      </Section>
      <section className="section">
        <h3>Things you can say</h3>
        <div className="card cmds">{COMMANDS.map((c) => <span key={c}>{c}</span>)}</div>
      </section>

      <Section title="Coding agents">
        <Row label={<span style={{ display: "flex", gap: 8, alignItems: "center" }}><Bot size={16} color="var(--accent)" /> Show agent sessions</span>}
          description="Live progress, Allow / Deny cards for permission requests, and answers to questions, right on the island." settingKeys={["Bunbun.Agents"]}>
          <Toggle on={s.bool("Bunbun.Agents")} onChange={(v) => s.set("Bunbun.Agents", v)} />
        </Row>
        {AGENTS.map((a) => {
          const connected = !!status?.[`${a.id}Connected`];
          const path = String(status?.[`${a.id}SettingsPath`] ?? "");
          return (
            <Row key={a.id} label={a.name}
              description={status ? `${connected ? `Connected through ${path}` : "Not connected yet."} ${a.approvals}` : "Checking…"}>
              {connected && <span className="badge green"><Check size={12} /> Connected</span>}
              <button className={`btn small ${connected ? "" : "primary"}`} disabled={!!busy || !status} onClick={() => toggle(a)}>
                {connected ? <><Unlink2 size={14} /> Disconnect</> : <><Link2 size={14} /> Connect</>}
              </button>
              <span title="Show this agent on the island" style={{ display: "inline-flex", alignItems: "center", gap: 6, color: "var(--muted)", fontSize: 12 }}>
                On island <Toggle on={s.bool(a.show)} onChange={(v) => s.set(a.show, v)} />
              </span>
            </Row>
          );
        })}
        <Setting k="Bunbun.ShowOtherAgents" />
        <Setting k="Bunbun.AgentPill" />
        <Setting k="Bunbun.MainAgent" />
        <Setting k="Bunbun.ShellApprovals" />
        <Setting k="Bunbun.UsageLimits" />
        <Setting k="Bunbun.AntigravityQuota" />
        <Row label="Other agents and scripts" description="Any tool with command hooks can pipe Claude Code-style JSON events into Bunbun. Use --agent <name> to label it.">
          <button className="btn small" onClick={() => {
            navigator.clipboard.writeText(String(status?.hookCmdCli ?? "bunbun-hook.exe --agent cli"));
            setCopied(true);
            setTimeout(() => setCopied(false), 2000);
          }}>{copied ? "Copied!" : "Copy hook command"}</button>
        </Row>
      </Section>
      {report && <div className="note" style={{ marginTop: 12, whiteSpace: "pre-wrap" }}><Info size={16} />{report}</div>}
      <div className="note" style={{ marginTop: 12 }}>
        <Info size={16} />
        Bunbun never approves anything by itself. If it is closed, or you don't click, every agent falls back to its own prompt exactly as if Bunbun weren't installed.
      </div>
    </div>
  );
}
