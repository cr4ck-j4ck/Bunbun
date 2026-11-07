import { ArchiveRestore, Download, ExternalLink, FileText, RefreshCw, RotateCcw, Save } from "lucide-react";
import { useEffect, useState } from "react";
import { call } from "../bridge";
import { schema, useStore } from "../store";
import { PageHead, Row, Section, Setting, Toggle } from "../ui/controls";
import { AccentPicker, ThemePresets } from "../ui/ThemeColors";

type UpdateInfo = {
  current: string;
  checked: boolean;
  latest?: string;
  newer?: boolean;
  url?: string;
  error?: string;
  canInstall?: boolean;  // an installed copy, and the release has a setup to install
};

// "Check for updates": asks GitHub for the latest release (the host does the
// request on a worker thread). Update hands over to the island's updater, which
// closes Bunbun, installs the new version and starts it; a copy that wasn't
// installed with setup opens the release page instead.
type BackupResult = { cancelled?: boolean; path?: string; files?: number; bytes?: number; failed?: number; safety?: string;
                      fromVersion?: string; fromPlatform?: string };

// Bunbun: one .zip with your settings and data (water, Screen Time, notes, to-dos,
// timers, skin packs). Restoring keeps a copy of what was there first and
// restarts the island. The same file works on Windows and Linux.
function Backups() {
  const [busy, setBusy] = useState("");
  const [note, setNote] = useState("");
  const run = (cmd: "backupCreate" | "backupRestore") => {
    if (cmd === "backupRestore" &&
        !window.confirm("Restore from a backup? Your settings and data are replaced by the backup's. A copy of what you have now is saved first, and the island restarts."))
      return;
    setBusy(cmd);
    setNote("");
    call<BackupResult>(cmd)
      .then((r) => {
        if (!r || r.cancelled) return;
        if (cmd === "backupCreate") {
          const kb = Math.max(1, Math.round((r.bytes ?? 0) / 1024));
          setNote(`Saved ${r.files} files (${kb >= 1024 ? (kb / 1024).toFixed(1) + " MB" : kb + " KB"}) to ${r.path}.`);
        } else {
          const from = r.fromVersion ? ` from Bunbun ${r.fromVersion}${r.fromPlatform ? ` on ${r.fromPlatform === "linux" ? "Linux" : "Windows"}` : ""}` : "";
          setNote(`Restored ${r.files} files${from}.${r.failed ? ` ${r.failed} couldn't be written.` : ""} What you had before is in ${r.safety}.`);
          setTimeout(() => window.location.reload(), 1200);  // read the restored settings
        }
      })
      .catch((e) => setNote(`Couldn't ${cmd === "backupCreate" ? "back up" : "restore"}: ${String(e?.message ?? e)}`))
      .finally(() => setBusy(""));
  };
  return (
    <>
      <Row label="Back up" description="Saves your settings and Bunbun's data in one .zip: water, Screen Time, notes, to-dos, timers and your mascot packs. Not the AI model. Works on Windows and Linux alike.">
        <button className="btn small" disabled={!!busy} onClick={() => run("backupCreate")}>
          <Save size={14} /> {busy === "backupCreate" ? "Saving…" : "Create backup…"}
        </button>
      </Row>
      <Row label="Restore" description="Loads a backup in place of your current settings and data. A copy of what you have now is kept first, and the island restarts.">
        <button className="btn small" disabled={!!busy} onClick={() => run("backupRestore")}>
          <ArchiveRestore size={14} /> {busy === "backupRestore" ? "Restoring…" : "Restore from backup…"}
        </button>
      </Row>
      {note && <div className="note" style={{ margin: "0 20px 14px" }}>{note}</div>}
    </>
  );
}

function Updates() {
  const [busy, setBusy] = useState(false);
  const [info, setInfo] = useState<UpdateInfo | null>(null);
  const [failed, setFailed] = useState("");
  const check = () => {
    setBusy(true);
    setFailed("");
    call<UpdateInfo>("checkUpdate")
      .then(setInfo)
      .catch((e) => setFailed(String(e?.message ?? e)))
      .finally(() => setBusy(false));
  };
  let status = "Bunbun looks for new releases on GitHub.";
  if (failed) status = `Couldn't check: ${failed}`;
  else if (info && !info.checked) status = `Couldn't check: ${info.error ?? "unknown error"}`;
  else if (info?.newer) status = `Bunbun ${info.latest} is available. You have ${info.current}.`;
  else if (info) status = `You're up to date (${info.current}).`;
  return (
    <>
      <Row label="Check for updates" description={status}>
        {info?.newer && info.canInstall && (
          <button className="btn small primary" onClick={() => call("installUpdate").catch((e) => setFailed(String(e?.message ?? e)))}>
            <Download size={14} /> Update to {info.latest}
          </button>
        )}
        {info?.newer && !info.canInstall && info.url && (
          <button className="btn small primary" onClick={() => call("openUrl", { url: info.url })}>
            <Download size={14} /> Download {info.latest}
          </button>
        )}
        <button className="btn small" onClick={check} disabled={busy}>
          <RefreshCw size={14} className={busy ? "spin" : ""} /> {busy ? "Checking…" : "Check now"}
        </button>
      </Row>
      <Setting k="Bunbun.UpdateCheck" label="Check automatically"
        description="Once a day, with a notification when there's a new version. Nothing is installed until you choose Update." />
    </>
  );
}

export function General() {
  const s = useStore();
  // null until the host answers, so the toggle doesn't flash "off" first.
  const [autostart, setAutostart] = useState<boolean | null>(null);
  const [version, setVersion] = useState("");
  const [autostartError, setAutostartError] = useState("");
  const [linux, setLinux] = useState(false);
  useEffect(() => {
    call<{ autostart: boolean; version?: string; platform?: string }>("getSystem")
      .then((r) => { setAutostart(r.autostart); setVersion(r.version ?? ""); setLinux(r.platform === "linux"); })
      .catch(() => setAutostart(false));
  }, []);

  return (
    <div className="page">
      <PageHead title="General" subtitle="Startup, language, clock and the hide/show hotkey." />
      <section className="section">
        <h3>Theme</h3>
        <ThemePresets />
      </section>
      <Section title="Colours">
        <AccentPicker />
      </Section>
      <Section title="Startup">
        <Row label={linux ? "Start when you log in" : "Start with Windows"} description="Launch Bunbun when you sign in.">
          <Toggle on={!!autostart} label={linux ? "Start when you log in" : "Start with Windows"} disabled={autostart === null}
            onChange={(v) => call<boolean>("setAutostart", { on: v })
              .then((r) => { setAutostart(r); setAutostartError(""); })
              .catch((e) => setAutostartError(String(e?.message ?? e)))} />
          {autostartError && <small className="err-text">Couldn't change it: {autostartError}</small>}
        </Row>
      </Section>
      <Section title="Sounds">
        <Setting k="Bunbun.Sounds" />
        <Setting k="Bunbun.SoundVolume" />
        <Setting k="Bunbun.SoundOpenClose" />
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
      <Section title="Backup & restore">
        <Backups />
      </Section>
      <Section title="Updates">
        <Updates />
      </Section>
      <Section title="About">
        <Row label="Bunbun" description={`A native Dynamic Island for ${linux ? "Linux" : "Windows"} with coding agent companions and mascot vibes.`}>
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
