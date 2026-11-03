import { BatteryMedium, Bell, CalendarDays, Gamepad2, LayoutGrid, Mic, Music2, Quote, Search, SlidersHorizontal } from "lucide-react";
import { ReactNode, useEffect, useRef, useState } from "react";
import { notOnLinux, useIsLinux } from "../platform";
import { useStore } from "../store";
import { PageHead, Setting, Toggle } from "../ui/controls";

type Group = { id: string; title: string; desc: string; icon: ReactNode; main?: string; keys: string[] };

const GROUPS: Group[] = [
  { id: "media", title: "Music", desc: "Now playing, controls and cover art, and which app the Music card opens.", icon: <Music2 size={18} />, main: "Modules.Media",
    keys: ["Bunbun.MusicApp", "Bunbun.StreamingMood", "Modules.MediaAutoExpand", "Modules.CollapsedTitle", "Modules.MediaPillClock", "Bunbun.MusicSwipe", "Modules.MediaExpandBlocklist"] },
  { id: "lyrics", title: "Lyrics", desc: "Synced lyrics, karaoke style, and timing for Bluetooth headphones.", icon: <Quote size={18} />, main: "Modules.Lyrics",
    keys: ["Bunbun.PillLyrics", "Modules.MediaTabLyrics", "Modules.KaraokeLyrics", "Modules.KaraokeLetterGlow", "Modules.LyricsNeighborLinesVisible", "Modules.LyricsVisualizer",
      "Modules.LyricsOffsetMs", "Modules.LyricsOffsetBluetoothMs", "Modules.LyricsNetEase", "Modules.LyricsCache", "Modules.LyricsCacheMaxMB"] },
  { id: "system", title: "System pop-ups", desc: "Volume, brightness, battery, Bluetooth, Caps Lock and Do Not Disturb.", icon: <BatteryMedium size={18} />,
    keys: ["Modules.Volume", "Modules.Brightness", "Modules.Battery", "Modules.BluetoothIndicator", "Modules.BluetoothShowBattery", "Bunbun.BluetoothLowWarning",
      "Modules.CapsLock", "Modules.DoNotDisturbIndicator", "Modules.Progress", "Modules.StatusCountdownProgress",
      "Bunbun.BatteryMoods", "Bunbun.ChargingReminderMinutes"] },
  { id: "notify", title: "Notifications", desc: "Toasts from your apps, a peek at what you copy, and timers.", icon: <Bell size={18} />,
    keys: ["Modules.Clipboard", "Modules.NotificationRespectDnD", "Modules.TimerModule"] },
  { id: "calendar", title: "Reminders & meetings", desc: "Reminder sounds, your calendar link with a Join button, and browser downloads.", icon: <CalendarDays size={18} />,
    keys: ["Modules.CalendarIcsUrl", "Modules.MeetingAlertMinutes", "Modules.ReminderSound", "Modules.QuietReminders", "Modules.Downloads"] },
  { id: "pages", title: "Page options", desc: "Weather city and units, system numbers, the file shelf and the focus guard.", icon: <SlidersHorizontal size={18} />,
    keys: ["Modules.WeatherCity", "Modules.WeatherFahrenheit", "Modules.ShowMetricText", "Modules.FileTrayMaxItems", "Modules.FileTrayPersist",
      "Bunbun.FocusGuard", "Bunbun.FocusGuardApps"] },
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
