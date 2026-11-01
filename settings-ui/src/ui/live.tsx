import { Radio } from "lucide-react";
import { useEffect, useRef, useState } from "react";
import { call, inHost } from "../bridge";
import { IslandPreview } from "./preview";

type Rect = { x: number; y: number; w: number; h: number };
type Preview = {
  running: boolean;
  shown?: boolean;
  screen?: string;   // your wallpaper + the island, at your monitor's proportions
  island?: string;   // the island's own pixels, 1:1
  monitor?: Rect;
  islandRect?: Rect;
};

/**
 * The real island on your real desktop. Frames come from the running island
 * itself (its own pixels, at its real position and size) composited onto your
 * wallpaper by the settings host, so what you see here is exactly what is on
 * screen. Falls back to a mock-up when the island isn't running.
 */
export function LivePreview({ showActualSize = true }: { showActualSize?: boolean }) {
  const [data, setData] = useState<Preview | null>(null);
  const [actualW, setActualW] = useState(0);
  const alive = useRef(true);

  useEffect(() => {
    alive.current = true;
    let timer = 0;
    const tick = async () => {
      if (document.visibilityState === "visible") {
        try {
          const p = await call<Preview>("capturePreview", { width: 1280 });
          if (alive.current) setData(p);
        } catch { /* host busy; try again next tick */ }
      }
      if (alive.current) timer = window.setTimeout(tick, 300);
    };
    tick();
    return () => { alive.current = false; window.clearTimeout(timer); };
  }, []);

  if (!inHost || (data && !data.running)) {
    return (
      <div>
        <IslandPreview />
        <div className="live-caption">{inHost ? "Start the island to see the live preview of your screen." : "Mock-up (browser dev mode)."}</div>
      </div>
    );
  }

  return (
    <div className="live">
      <div className="live-screen">
        {data?.screen ? <img src={data.screen} alt="Your screen with the island" draggable={false} /> : <div className="live-wait" />}
        <span className="live-badge"><Radio size={12} /> Live from your screen{data?.monitor ? ` · ${data.monitor.w}×${data.monitor.h}` : ""}</span>
        {data && data.shown === false && <span className="live-hidden">The island is hidden right now</span>}
      </div>
      {showActualSize && data?.island && (
        <div className="live-actual">
          <span>Actual size</span>
          <img src={data.island} alt="The island at actual size" draggable={false}
            style={actualW ? { width: actualW } : undefined}
            onLoad={(e) => setActualW(e.currentTarget.naturalWidth / window.devicePixelRatio)} />
        </div>
      )}
    </div>
  );
}
