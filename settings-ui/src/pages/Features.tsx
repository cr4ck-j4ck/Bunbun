import { BatteryMedium, Bell, CalendarDays, Droplet, Gamepad2, LayoutGrid, Mic, Music2, Quote, Search, SlidersHorizontal, Timer } from "lucide-react";
import { ReactNode, useEffect, useRef, useState } from "react";
import { neverOnLinux, notOnLinux, useIsLinux } from "../platform";
import { useStore } from "../store";
import { PageHead, Setting, Toggle } from "../ui/controls";

type Group = { id: string; title: string; desc: string; icon: ReactNode; main?: string; keys: string[] };

export const GROUPS: Group[] = [
  { id: "media", title: "Music", desc: "Now playing, controls and cover art, and which app the Music card opens.", icon: <Music2 size={18} />, main: "Modules.Media",
    keys: ["Bunbun.MusicApp", "Bunbun.StreamingMood", "Modules.MediaAutoExpand", "Modules.CollapsedTitle", "Modules.MediaPillClock", "Bunbun.MusicSwipe", "Modules.MediaExpandBlocklist"] },
  { id: "lyrics", title: "Lyrics", desc: "Synced lyrics, karaoke style, and timing for Bluetooth headphones.", icon: <Quote size={18} />, main: "Modules.Lyrics",
    keys: ["Bunbun.PillLyrics", "Modules.MediaTabLyrics", "Modules.KaraokeLyrics", "Modules.KaraokeLetterGlow", "Modules.LyricsNeighborLinesVisible", "Modules.LyricsVisualizer",
      "Modules.LyricsOffsetMs", "Modules.LyricsOffsetBluetoothMs", "Modules.LyricsNetEase", "Modules.LyricsCache", "Modules.LyricsCacheMaxMB"] },
  { id: "system", title: "System pop-ups", desc: "Volume, brightness, battery, Bluetooth, Caps Lock and Do Not Disturb.", icon: <BatteryMedium size={18} />,
    keys: ["Modules.Volume", "Modules.Brightness", "Modules.Battery", "Modules.BluetoothIndicator", "Modules.BluetoothShowBattery", "Bunbun.BluetoothLowWarning",
      "Modules.CapsLock", "Modules.DoNotDisturbIndicator", "Modules.Progress", "Modules.StatusCountdownProgress",
      "Bunbun.BatteryMoods", "Bunbun.ChargingReminderMinutes"] },
  { id: "notify", title: "Notifications", desc: "Your apps' notifications (also during Do Not Disturb, if you like), a peek at what you copy, and timers.", icon: <Bell size={18} />,
    keys: ["Modules.NotificationRespectDnD", "Modules.Clipboard", "Modules.TimerModule"] },
  { id: "timers", title: "Timers", desc: "Pomodoro lengths, starting the next phase by itself, the countdown's presets, and Water as a mode.", icon: <Timer size={18} />,
    keys: ["Bunbun.WaterPage", "Bunbun.PomodoroFocus", "Bunbun.PomodoroShortBreak", "Bunbun.PomodoroLongBreak", "Bunbun.PomodoroCycles", "Bunbun.PomodoroAutoStart", "Bunbun.TimerPresets"] },
  { id: "water", title: "Water", desc: "Reminders to drink, paced across your active hours, and the bottle to refill.", icon: <Droplet size={18} />, main: "Bunbun.WaterReminders",
    keys: ["Bunbun.WaterGoalMl", "Bunbun.WaterGlassMl", "Bunbun.WaterBottleMl", "Bunbun.WaterStartHour", "Bunbun.WaterEndHour"] },
  { id: "calendar", title: "Reminders & meetings", desc: "Reminder sounds, your calendar link with a Join button, and browser downloads.", icon: <CalendarDays size={18} />,
    keys: ["Modules.CalendarIcsUrl", "Modules.MeetingAlertMinutes", "Modules.ReminderSound", "Modules.QuietReminders", "Modules.Downloads"] },
  { id: "pages", title: "Page options", desc: "Weather city and units, system numbers, the file shelf, the focus guard and a daily screen time limit.", icon: <SlidersHorizontal size={18} />,
    keys: ["Modules.WeatherCity", "Modules.WeatherFahrenheit", "Modules.ShowMetricText", "Modules.FileTrayMaxItems", "Modules.FileTrayPersist",
      "Bunbun.FocusGuard", "Bunbun.FocusGuardApps", "Bunbun.ScreenTimeLimit"] },
  { id: "game", title: "Game overlay", desc: "A compact metrics strip for fullscreen play.", icon: <Gamepad2 size={18} />, main: "Modules.GameOverlay",
    keys: ["Modules.GameOverlayCompact", "Modules.GameOverlayShowFps", "Modules.GameOverlayShowCpu", "Modules.GameOverlayShowGpu", "Modules.GameOverlayShowRam", "Modules.GameOverlayShowDisk"] },
  { id: "privacy", title: "Privacy dots", desc: "Orange for the microphone, green for the camera, blue while the screen is recorded.", icon: <Mic size={18} />, main: "Indicators.PrivacyDots",
    keys: ["Indicators.PrivacyDotsMic", "Indicators.PrivacyDotsMicHex", "Indicators.PrivacyDotsCam", "Indicators.PrivacyDotsCamHex",
      "Indicators.PrivacyDotsScreen", "Indicators.PrivacyDotsScreenHex"] },
  { id: "lookup", title: "Quick Lookup & Ask Bun", desc: "The Ctrl+Alt+B box: Bun's commands, plus dictionary and Wikipedia.", icon: <Search size={18} />, main: "Modules.QuickLookup",
    keys: ["Bunbun.Assistant", "Bunbun.SearchEngine"] },
];

export function Features({ go }: { go?: (page: string) => void }) {
  const s = useStore();
  const [open, setOpen] = useState<string | null>(null);
  const g = GROUPS.find((x) => x.id === open);
  const linux = useIsLinux();
  const optionsRef = useRef<HTMLElement>(null);
  // The options open under the grid, so bring them into view.
  useEffect(() => { if (open) optionsRef.current?.scrollIntoView({ behavior: "smooth", block: "nearest" }); }, [open]);
  return (
    <div className="page">
      <PageHead title="Features" subtitle="Switch features on or off, and click one to see its options. Features you turn off stop their background work completely." />
      {go && (
        <div className="note" style={{ marginBottom: 16, alignItems: "center" }}>
          <LayoutGrid size={16} />
          <span style={{ flex: 1 }}>The island's pages (Weather, System, Focus, Clipboard…), their order and the Home cards are in Island pages.</span>
          <button className="btn small" onClick={() => go("pages")}>Open Island pages</button>
        </div>
      )}
      <div className="feature-grid">
        {GROUPS.map((x) => {
          const on = x.main ? s.bool(x.main) : true;
          if (linux && neverOnLinux(x.main ? [x.main, ...x.keys] : x.keys)) return null;  // Windows-only for good
          return (
            <div key={x.id} role="button" tabIndex={0} className={`feature ${open === x.id ? "open" : ""} ${on ? "" : "off"}`}
              onClick={() => setOpen(open === x.id ? null : x.id)} onKeyDown={(e) => e.key === "Enter" && setOpen(open === x.id ? null : x.id)}>
              <div className="top">
                <div className="ico">{x.icon}</div>
                <b>{x.title}</b>
                {x.main && <span onClick={(e) => e.stopPropagation()}><Toggle on={on} onChange={(v) => s.set(x.main!, v)} label={x.title} /></span>}
              </div>
              <p>{x.desc}</p>
              {linux && notOnLinux(x.main ? [x.main, ...x.keys] : x.keys) && <em className="badge muted linux">Not on Linux yet</em>}
              <span className="more">{open === x.id ? "Hide options" : `${x.keys.length} options`}</span>
            </div>
          );
        })}
      </div>
      {g && (
        <section className="section fade-in" key={g.id} ref={optionsRef}>
          <div className="section-title"><h3>{g.title}</h3>{g.main && !s.bool(g.main) && <span>Turned off</span>}</div>
          <div className="card" style={{ opacity: g.main && !s.bool(g.main) ? 0.5 : 1 }}>
            {g.keys.map((k) => <Setting key={k} k={k} />)}
          </div>
        </section>
      )}
    </div>
  );
}

// A feature area as a page of its own (Music & lyrics, Notifications & alerts…):
// each group with its switch, what it does, and all its options, nothing folded away.
export function FeatureGroupsPage({ title, subtitle, ids }: { title: string; subtitle: string; ids: string[] }) {
  const s = useStore();
  const linux = useIsLinux();
  const groups = ids.map((id) => GROUPS.find((g) => g.id === id)).filter((g): g is Group => !!g);
  return (
    <div className="page">
      <PageHead title={title} subtitle={subtitle} />
      {groups.map((g) => {
        if (linux && neverOnLinux(g.main ? [g.main, ...g.keys] : g.keys)) return null;  // Windows-only for good
        const on = g.main ? s.bool(g.main) : true;
        return (
          <section className="section fade-in" key={g.id}>
            <div className="feature-head">
              <div className="ico">{g.icon}</div>
              <div className="what">
                <h3>{g.title}</h3>
                <p>{g.desc}</p>
              </div>
              {linux && notOnLinux(g.main ? [g.main, ...g.keys] : g.keys) && <em className="badge muted linux">Not on Linux yet</em>}
              {g.main && <Toggle on={on} onChange={(v) => s.set(g.main!, v)} label={g.title} />}
            </div>
            <div className="card" style={{ opacity: on ? 1 : 0.5 }}>
              {g.keys.map((k) => <Setting key={k} k={k} />)}
            </div>
          </section>
        );
      })}
    </div>
  );
}
