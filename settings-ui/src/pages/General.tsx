import { ExternalLink, FileText, RotateCcw } from "lucide-react";
import { useEffect, useState } from "react";
import { call } from "../bridge";
import { schema, useStore } from "../store";
import { PageHead, Row, Section, Setting, Toggle } from "../ui/controls";

export function General() {
  const s = useStore();
  // null until the host answers, so the toggle doesn't flash "off" first.
  const [autostart, setAutostart] = useState<boolean | null>(null);
  const [version, setVersion] = useState("");
  const [autostartError, setAutostartError] = useState("");
  useEffect(() => {
    call<{ autostart: boolean; version?: string }>("getSystem")
      .then((r) => { setAutostart(r.autostart); setVersion(r.version ?? ""); })
      .catch(() => setAutostart(false));
  }, []);

  return (
    <div className="page">
      <PageHead title="General" subtitle="Startup, language, clock and the hide/show hotkey." />
      <Section title="Startup">
        <Row label="Start with Windows" description="Launch Bunbun when you sign in.">
          <Toggle on={!!autostart} label="Start with Windows" disabled={autostart === null}
            onChange={(v) => call<boolean>("setAutostart", { on: v })
              .then((r) => { setAutostart(r); setAutostartError(""); })
              .catch((e) => setAutostartError(String(e?.message ?? e)))} />
          {autostartError && <small className="err-text">Couldn't change it: {autostartError}</small>}
        </Row>
      </Section>
      <Section title="Language & time">
        <Setting k="Modules.Language" />
        <Setting k="Modules.ClockFormat" />
        <Setting k="Modules.ShowSeconds" />
        <Setting k="Modules.DateFormat" />
        <Setting k="Modules.DateFirst" />
        <Setting k="Modules.FirstDayOfWeek" />
      </Section>
      <Section title="Hide / show hotkey">
        <Setting k="Shortcuts.HideShowHotkeyEnabled" />
        <Setting k="Shortcuts.HideShowModifiers" />
        <Setting k="Shortcuts.HideShowKey" />
      </Section>
      <Section title="Advanced">
        <Row label="Settings file" description="Everything on these pages lives in settings.ini. Edit it by hand if you like; the island reloads on save.">
          <button className="btn small" onClick={() => call("openSettingsFile")}><FileText size={14} /> Open settings.ini</button>
        </Row>
        <Row label="Reset everything" description="Put every setting back to its default. Your mascot packs and hooks are kept.">
          <button className="btn small danger" onClick={() => window.confirm("Reset every Bunbun setting to its default?") && s.reset(Object.keys(schema))}>
            <RotateCcw size={14} /> Reset all
          </button>
        </Row>
      </Section>
      <Section title="About">
        <Row label="Bunbun" description="A native Dynamic Island for Windows with coding agent companions and mascot vibes.">
          <span className="badge">{version ? `v${version}` : "…"}</span>
        </Row>
      </Section>
    </div>
  );
}

/** Every setting whose name or description matches the query. */
export function SearchResults({ query }: { query: string }) {
  const q = query.trim().toLowerCase();
  const keys = Object.entries(schema)
    .filter(([k, v]) => k.toLowerCase().includes(q) || v.name.toLowerCase().includes(q) || v.description.toLowerCase().includes(q))
    .map(([k]) => k);
  return (
    <div className="page">
      <PageHead title={`Results for “${query}”`} subtitle={`${keys.length} setting${keys.length === 1 ? "" : "s"}`} />
      {keys.length ? <Section>{keys.map((k) => <Setting key={k} k={k} />)}</Section> : <div className="empty">Nothing matches. Try “lyrics”, “position” or “opacity”.</div>}
    </div>
  );
}
