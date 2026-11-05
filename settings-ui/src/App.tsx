import { motion } from "framer-motion";
import { Bell, Bot, Brush, Gauge, Home, LayoutGrid, LayoutPanelTop, ListChecks, Monitor, Music2, NotebookPen, Search, Settings2, Smile, Sparkles, Wrench } from "lucide-react";
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
import { Animations } from "./pages/Animations";
import { FeatureGroupsPage, Features } from "./pages/Features";
import { IslandPages } from "./pages/IslandPages";
import { Assistant } from "./pages/Assistant";
import { Todos } from "./pages/Todos";
import { Notes } from "./pages/Notes";
import { General, SearchResults } from "./pages/General";

// The sidebar, in groups: the island itself, its features (one page per area),
// Bun and the agents, your own lists, then the app.
const PAGES: { id: string; label: string; icon: ReactNode; group?: string }[] = [
  { id: "overview", label: "Overview", icon: <Home size={17} /> },
  { id: "pages", label: "Island pages", icon: <LayoutPanelTop size={17} />, group: "Island" },
  { id: "appearance", label: "Appearance", icon: <Brush size={17} /> },
  { id: "displays", label: "Position & displays", icon: <Monitor size={17} /> },
  { id: "animations", label: "Motion & behaviour", icon: <Gauge size={17} /> },
  { id: "features", label: "All features", icon: <LayoutGrid size={17} />, group: "Features" },
  { id: "music", label: "Music & lyrics", icon: <Music2 size={17} /> },
  { id: "alerts", label: "Notifications & alerts", icon: <Bell size={17} /> },
  { id: "productivity", label: "Productivity", icon: <Sparkles size={17} /> },
  { id: "tools", label: "System & tools", icon: <Wrench size={17} /> },
  { id: "mascot", label: "Mascot studio", icon: <Smile size={17} />, group: "Bun & agents" },
  { id: "assistant", label: "Assistant & agents", icon: <Bot size={17} /> },
  { id: "todos", label: "To-dos", icon: <ListChecks size={17} />, group: "Your stuff" },
  { id: "notes", label: "Notes", icon: <NotebookPen size={17} /> },
  { id: "general", label: "General", icon: <Settings2 size={17} />, group: "App" },
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
  const [page, setPage] = useState(() => (PAGES.some((p) => `#${p.id}` === location.hash) ? location.hash.slice(1) : "overview"));
  // The island opens a page by its hash (bunbun-settings --page notes), also while the window is already open.
  useEffect(() => {
    const onHash = () => { const id = location.hash.slice(1); if (PAGES.some((p) => p.id === id)) setPage(id); };
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
  else if (page === "animations") content = <Animations />;
  else if (page === "features") content = <Features go={setPage} />;
  else if (page === "music") content = <FeatureGroupsPage title="Music & lyrics" subtitle="What's playing, its controls and cover art, and synced lyrics." ids={["media", "lyrics"]} />;
  else if (page === "alerts") content = <FeatureGroupsPage title="Notifications & alerts" subtitle="Your apps' notifications, the system's pop-ups and the privacy dots." ids={["notify", "system", "privacy"]} />;
  else if (page === "productivity") content = <FeatureGroupsPage title="Productivity" subtitle="Water, reminders, meetings and downloads. Your to-dos and note are under Your stuff." ids={["water", "calendar"]} />;
  else if (page === "tools") content = <FeatureGroupsPage title="System & tools" subtitle="Page options, Quick Lookup and the game overlay." ids={["pages", "lookup", "game"]} />;
  else if (page === "pages") content = <IslandPages />;
  else if (page === "todos") content = <Todos />;
  else if (page === "notes") content = <Notes />;
  else if (page === "assistant") content = <Assistant />;
  else if (page === "general") content = <General />;
  else content = <Overview go={setPage} />;

  return (
    <StoreContext.Provider value={store}>
      <div className="shell">
        <aside className="sidebar">
          <Brand />
          <div className="search">
            <Search size={15} />
            <input placeholder="Search settings" value={query} onChange={(e) => setQuery(e.target.value)} />
          </div>
          <nav className="nav">
            {PAGES.map((p) => {
              const active = !searching && page === p.id;
              return [
                p.group && <div key={`g-${p.group}`} className="nav-group">{p.group}</div>,
                <button key={p.id} className={active ? "active" : ""} onClick={() => { setPage(p.id); setQuery(""); }}>
                  {active && <motion.span layoutId="nav-bg" className="pill-bg" transition={{ type: "spring", stiffness: 500, damping: 38 }} />}
                  {active && <motion.span layoutId="nav-bar" className="pill-bar" transition={{ type: "spring", stiffness: 500, damping: 38 }} />}
                  {p.icon}
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
