import { motion } from "framer-motion";
import { Bot, Brush, Gauge, Home, LayoutGrid, Monitor, Search, Settings2, Smile } from "lucide-react";
import { ReactNode, useState } from "react";
import { StoreContext, useSettingsStore } from "./store";
import { useAppTheme } from "./theme";
import { MascotImage, useMascotStyle } from "./ui/mascot";
import { Overview } from "./pages/Overview";
import { Appearance } from "./pages/Appearance";
import { Displays } from "./pages/Displays";
import { Mascot } from "./pages/Mascot";
import { Animations } from "./pages/Animations";
import { Features } from "./pages/Features";
import { Assistant } from "./pages/Assistant";
import { General, SearchResults } from "./pages/General";

const PAGES: { id: string; label: string; icon: ReactNode }[] = [
  { id: "overview", label: "Overview", icon: <Home size={17} /> },
  { id: "appearance", label: "Appearance", icon: <Brush size={17} /> },
  { id: "displays", label: "Position & displays", icon: <Monitor size={17} /> },
  { id: "mascot", label: "Mascot studio", icon: <Smile size={17} /> },
  { id: "animations", label: "Animations", icon: <Gauge size={17} /> },
  { id: "features", label: "Features", icon: <LayoutGrid size={17} /> },
  { id: "assistant", label: "Assistant & agents", icon: <Bot size={17} /> },
  { id: "general", label: "General", icon: <Settings2 size={17} /> },
];

function Brand() {
  const style = useMascotStyle();
  return (
    <div className="brand">
      <MascotImage style={style} animate size={44} />
      <div>
        <h1>Bunbun</h1>
        <p>Dynamic Island for Windows</p>
      </div>
    </div>
  );
}

export default function App() {
  const store = useSettingsStore();
  useAppTheme(store);
  const [page, setPage] = useState(() => (PAGES.some((p) => `#${p.id}` === location.hash) ? location.hash.slice(1) : "overview"));
  const [query, setQuery] = useState("");
  const searching = query.trim().length > 1;

  let content: ReactNode;
  if (searching) content = <SearchResults query={query} />;
  else if (page === "appearance") content = <Appearance />;
  else if (page === "displays") content = <Displays />;
  else if (page === "mascot") content = <Mascot />;
  else if (page === "animations") content = <Animations />;
  else if (page === "features") content = <Features />;
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
              return (
                <button key={p.id} className={active ? "active" : ""} onClick={() => { setPage(p.id); setQuery(""); }}>
                  {active && <motion.span layoutId="nav-bg" className="pill-bg" transition={{ type: "spring", stiffness: 500, damping: 38 }} />}
                  {active && <motion.span layoutId="nav-bar" className="pill-bar" transition={{ type: "spring", stiffness: 500, damping: 38 }} />}
                  {p.icon}
                  <span>{p.label}</span>
                </button>
              );
            })}
          </nav>
          <div className="grow" />
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
