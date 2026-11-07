import { motion } from "framer-motion";
import { Bell, Bot, Briefcase, Brush, Home, LayoutGrid, LayoutPanelTop, ListChecks, Monitor, Music2, NotebookPen, Search, Settings2, Smile, Sparkles, Wrench } from "lucide-react";
import { ReactNode, useEffect, useState } from "react";
import { inHost } from "./bridge";
import { useIsLinux } from "./platform";
import { StoreContext, useSettingsStore } from "./store";
import { useAppTheme } from "./theme";
import { MascotImage, useMascotStyle } from "./ui/mascot";
import { Overview } from "./pages/Overview";
import { Appearance } from "./pages/Appearance";
import { Displays } from "./pages/Displays";
import { Mascot } from "./pages/Mascot";
import { FeatureGroupsPage, Features } from "./pages/Features";
import { IslandPages } from "./pages/IslandPages";
import { Assistant } from "./pages/Assistant";
import { Todos } from "./pages/Todos";
import { Notes } from "./pages/Notes";
import { General, SearchResults } from "./pages/General";
import { modeNav, UseCaseWelcome, Work } from "./pages/Work";

// The sidebar, in groups: the island itself, its features (one page per area),
// Bun and the agents, your own lists, then the app.
// Each page has its own tile colour in the sidebar, so the list reads at a glance.
const PAGES: { id: string; label: string; icon: ReactNode; tint: string; group?: string }[] = [
  { id: "overview", label: "Overview", icon: <Home size={15} strokeWidth={2.2} />, tint: "#636366" },
  // Named after your mode (Work day, Study, Mode): see modeNav in pages/Work.tsx.
  { id: "work", label: "Mode", icon: <Briefcase size={15} strokeWidth={2.2} />, tint: "#0a84ff" },
  { id: "pages", label: "Island pages", icon: <LayoutPanelTop size={15} strokeWidth={2.2} />, tint: "#0a84ff", group: "Island" },
  { id: "appearance", label: "Appearance", icon: <Brush size={15} strokeWidth={2.2} />, tint: "#bf5af2" },
  { id: "displays", label: "Position & motion", icon: <Monitor size={15} strokeWidth={2.2} />, tint: "#32ade6" },
  { id: "features", label: "All features", icon: <LayoutGrid size={15} strokeWidth={2.2} />, tint: "#5e5ce6", group: "Features" },
  { id: "music", label: "Music & lyrics", icon: <Music2 size={15} strokeWidth={2.2} />, tint: "#ff375f" },
  { id: "alerts", label: "Notifications & alerts", icon: <Bell size={15} strokeWidth={2.2} />, tint: "#ff453a" },
  { id: "productivity", label: "Productivity", icon: <Sparkles size={15} strokeWidth={2.2} />, tint: "#30d158" },
  { id: "tools", label: "System & tools", icon: <Wrench size={15} strokeWidth={2.2} />, tint: "#64748b" },
  { id: "mascot", label: "Mascot studio", icon: <Smile size={15} strokeWidth={2.2} />, tint: "#ff6482", group: "Bun & agents" },
  { id: "assistant", label: "Assistant & agents", icon: <Bot size={15} strokeWidth={2.2} />, tint: "#d97757" },
  { id: "todos", label: "To-dos", icon: <ListChecks size={15} strokeWidth={2.2} />, tint: "#00c7be", group: "Your stuff" },
  { id: "notes", label: "Notes", icon: <NotebookPen size={15} strokeWidth={2.2} />, tint: "#ac8e68" },
  { id: "general", label: "General", icon: <Settings2 size={15} strokeWidth={2.2} />, tint: "#8e8e93", group: "App" },
];

function Brand() {
  const style = useMascotStyle();
  const linux = useIsLinux();
  return (
    <div className="brand">
      <MascotImage style={style} animate size={44} />
      <div>
        <h1>Bunbun</h1>
        <p>Dynamic Island for {linux ? "Linux" : "Windows"}</p>
      </div>
    </div>
  );
}

export default function App() {
  const store = useSettingsStore();
  useAppTheme(store);
  // "#work-report" (the island's Report button) is the Work page with the report open.
  const [report, setReport] = useState(() => location.hash === "#work-report");
  const [page, setPage] = useState(() => location.hash === "#work-report" ? "work"
    : PAGES.some((p) => `#${p.id}` === location.hash) ? location.hash.slice(1) : "overview");
  // The island opens a page by its hash (bunbun-settings --page notes), also while the window is already open.
  useEffect(() => {
    const onHash = () => {
      const id = location.hash.slice(1);
      if (id === "work-report") { setPage("work"); setReport(true); return; }
      if (PAGES.some((p) => p.id === id)) setPage(id);
    };
    window.addEventListener("hashchange", onHash);
    return () => window.removeEventListener("hashchange", onHash);
  }, []);
  const [query, setQuery] = useState("");
  const searching = query.trim().length > 1;

  let content: ReactNode;
  if (searching) content = <SearchResults query={query} />;
  else if (page === "appearance") content = <Appearance />;
  else if (page === "displays") content = <Displays />;
  else if (page === "mascot") content = <Mascot />;
  else if (page === "features") content = <Features go={setPage} />;
  else if (page === "music") content = <FeatureGroupsPage title="Music & lyrics" subtitle="What's playing, its controls and cover art, and synced lyrics." ids={["media", "lyrics"]} />;
  else if (page === "alerts") content = <FeatureGroupsPage title="Notifications & alerts" subtitle="Your apps' notifications, the system's pop-ups and the privacy dots." ids={["notify", "system", "privacy"]} />;
  else if (page === "productivity") content = <FeatureGroupsPage title="Productivity" subtitle="Timers, water, reminders, meetings and downloads. Your to-dos and note are under Your stuff." ids={["timers", "water", "calendar"]} />;
  else if (page === "tools") content = <FeatureGroupsPage title="System & tools" subtitle="Page options, Quick Lookup and the game overlay." ids={["pages", "lookup", "game"]} />;
  else if (page === "pages") content = <IslandPages />;
  else if (page === "todos") content = <Todos />;
  else if (page === "notes") content = <Notes />;
  else if (page === "assistant") content = <Assistant />;
  else if (page === "general") content = <General />;
  else if (page === "work") content = <Work openReport={report} />;
  else content = <Overview go={setPage} />;

  return (
    <StoreContext.Provider value={store}>
      <UseCaseWelcome />
      <div className="shell">
        <aside className="sidebar">
          <Brand />
          <div className="search">
            <Search size={15} />
            <input placeholder="Search settings" value={query} onChange={(e) => setQuery(e.target.value)} />
          </div>
          <nav className="nav">
            {PAGES.map((p0) => {
              // The mode page follows Bunbun.UseCase; with agents off, their page is just Ask Bun.
              const use = store.get("Bunbun.UseCase") || "agents";
              const p = p0.id === "work" ? { ...p0, ...modeNav(use) }
                : p0.id === "assistant" && (use === "work" || use === "study") ? { ...p0, label: "Ask Bun" } : p0;
              const active = !searching && page === p.id;
              return [
                p.group && <div key={`g-${p.group}`} className="nav-group">{p.group}</div>,
                <button key={p.id} className={active ? "active" : ""} onClick={() => { setPage(p.id); setQuery(""); }}>
                  {active && <motion.span layoutId="nav-bg" className="pill-bg" transition={{ type: "spring", stiffness: 500, damping: 38 }} />}
                  <span className="nav-ico" style={{ background: p.tint }}>{p.icon}</span>
                  <span>{p.label}</span>
                </button>,
              ];
            })}
          </nav>
          <div className="grow" />
          {!inHost && (
            <div className="mock-note" role="note">
              <b>Design preview</b>
              Changes here are pretend and never reach the island. Use Bunbun Settings from the tray to change your island.
            </div>
          )}
          <div className="status" role="status">
            <span className={`dot ${store.ready && !store.saveError ? "on" : store.saveError ? "err" : ""}`} />
            {store.saveError ? (
              <>Couldn't save <button className="link" onClick={store.retry}>Retry</button></>
            ) : store.saving ? "Saving…" : store.ready ? "All changes saved" : store.loadError ? "Couldn't load" : "Loading…"}
          </div>
        </aside>
        <main className="main">
          {!store.ready && store.loadError && (
            <div className="page fade-in">
              <div className="note">
                Bunbun couldn't read your settings ({store.loadError}).{" "}
                <button className="btn small" onClick={store.retry}>Try again</button>
              </div>
            </div>
          )}
          {store.saveError && (
            <div className="unsaved fade-in" role="alert">
              <b>Changes not saved</b>
              <span>Your last changes haven't reached the island yet. {store.saveError}</span>
              <button className="btn small primary" onClick={store.retry}>Save again</button>
            </div>
          )}
          {store.ready && (
            <div key={searching ? "search" : page} className="fade-in">
              {content}
            </div>
          )}
        </main>
      </div>
    </StoreContext.Provider>
  );
}
