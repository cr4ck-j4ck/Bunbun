import { BatteryMedium, CalendarClock, CalendarDays, Cpu, FolderDown, Gamepad2, Mic, Music2, Shield } from "lucide-react";
import { ReactNode, useState } from "react";
import { useStore } from "../store";
import { PageHead, Setting, Toggle } from "../ui/controls";

type Group = { id: string; title: string; desc: string; icon: ReactNode; main?: string; keys: string[] };

const GROUPS: Group[] = [
  { id: "media", title: "Music & media", desc: "Now playing, controls, cover art, waveform and synced lyrics.", icon: <Music2 size={18} />, main: "Modules.Media",
    keys: ["Modules.MediaAutoExpand", "Modules.CollapsedTitle", "Modules.MediaPillClock", "Modules.Lyrics", "Modules.MediaTabLyrics", "Modules.LyricsVisualizer",
      "Modules.KaraokeLyrics", "Modules.KaraokeLetterGlow", "Modules.LyricsNeighborLinesVisible", "Modules.LyricsCache", "Modules.LyricsCacheMaxMB",
      "Modules.LyricsOffsetMs", "Modules.LyricsOffsetBluetoothMs", "Modules.MediaExpandBlocklist"] },
  { id: "system", title: "System flyouts", desc: "Volume, brightness, battery, Bluetooth, Caps Lock, Do Not Disturb.", icon: <BatteryMedium size={18} />,
    keys: ["Modules.Volume", "Modules.Brightness", "Modules.Battery", "Modules.BluetoothIndicator", "Modules.BluetoothShowBattery",
      "Modules.CapsLock", "Modules.DoNotDisturbIndicator", "Modules.Progress", "Modules.StatusCountdownProgress",
      "Bunbun.BatteryMoods", "Bunbun.ChargingReminderMinutes"] },
  { id: "notify", title: "Notifications & clipboard", desc: "Toasts from your apps and a peek at what you copy.", icon: <CalendarClock size={18} />,
    keys: ["Modules.Clipboard", "Modules.NotificationRespectDnD", "Modules.TimerModule"] },
  { id: "calendar", title: "To-dos, meetings & downloads", desc: "Reminders and a to-do list, your next meeting with a Join button, and browser downloads as they arrive.", icon: <CalendarDays size={18} />,
    keys: ["Modules.TodoModule", "Modules.ReminderSound", "Modules.QuietReminders", "Modules.CalendarIcsUrl", "Modules.MeetingAlertMinutes", "Modules.Downloads"] },
  { id: "dash", title: "Dashboards", desc: "Weather and the hardware monitor when you hover the island.", icon: <Cpu size={18} />,
    keys: ["Modules.Weather", "Modules.WeatherCity", "Modules.WeatherFahrenheit", "Modules.HardwareMonitorModule", "Modules.ConnectivityModule", "Modules.ShowMetricText"] },
  { id: "game", title: "Game overlay", desc: "A compact metrics strip for fullscreen play.", icon: <Gamepad2 size={18} />, main: "Modules.GameOverlay",
    keys: ["Modules.GameOverlayCompact", "Modules.GameOverlayShowFps", "Modules.GameOverlayShowCpu", "Modules.GameOverlayShowGpu", "Modules.GameOverlayShowRam", "Modules.GameOverlayShowDisk"] },
  { id: "files", title: "File shelf", desc: "Drop files on the island, drag them out later.", icon: <FolderDown size={18} />, main: "Modules.FileTrayModule",
    keys: ["Modules.FileTrayMaxItems", "Modules.FileTrayPersist"] },
  { id: "privacy", title: "Privacy dots", desc: "Orange when the microphone is in use, green for the camera, blue while the screen is recorded.", icon: <Mic size={18} />, main: "Indicators.PrivacyDots",
    keys: ["Indicators.PrivacyDotsMic", "Indicators.PrivacyDotsMicHex", "Indicators.PrivacyDotsCam", "Indicators.PrivacyDotsCamHex",
      "Indicators.PrivacyDotsScreen", "Indicators.PrivacyDotsScreenHex"] },
  { id: "lookup", title: "Quick Lookup & Ask Bun", desc: "The Ctrl+Alt+B box: Bun's commands, plus dictionary and Wikipedia.", icon: <Shield size={18} />, main: "Modules.QuickLookup",
    keys: ["Bunbun.Assistant", "Bunbun.SearchEngine"] },
];

function GroupCard({ g }: { g: Group }) {
  const s = useStore();
  const [open, setOpen] = useState(false);
  const enabled = g.main ? s.bool(g.main) : true;
  return (
    <section className="section">
      <div className="card">
        <div className="row" style={{ cursor: "pointer" }} onClick={() => setOpen((o) => !o)}>
          <div style={{ display: "flex", gap: 14, alignItems: "center" }}>
            <div style={{ width: 38, height: 38, borderRadius: 11, display: "grid", placeItems: "center", background: "rgba(182,156,255,.12)", color: "var(--accent)" }}>{g.icon}</div>
            <div className="label"><b>{g.title}</b><span>{g.desc}</span></div>
          </div>
          <div className="control" onClick={(e) => e.stopPropagation()}>
            <span style={{ color: "var(--muted)", fontSize: 12, cursor: "pointer" }} onClick={() => setOpen((o) => !o)}>{open ? "Hide options" : `${g.keys.length} options`}</span>
            {g.main && <Toggle on={enabled} onChange={(v) => s.set(g.main!, v)} />}
          </div>
        </div>
        {open && (
          <div className="fade-in" style={{ opacity: enabled ? 1 : 0.5 }}>
            {g.keys.map((k) => <Setting key={k} k={k} />)}
          </div>
        )}
      </div>
    </section>
  );
}

export function Features() {
  return (
    <div className="page">
      <PageHead title="Features" subtitle="Turn on what you use, turn off what you don’t. Disabled features stop their background work completely." />
      {GROUPS.map((g) => <GroupCard key={g.id} g={g} />)}
    </div>
  );
}
