import { motion } from "framer-motion";
import { useEffect, useRef, useState } from "react";
import { call } from "../bridge";
import { useStore } from "../store";

export type MascotStyle = {
  character: string;
  color: string;
  motion: number;
  hop: number;
  blink: number;
  eyesFollow: boolean;
  celebrate: boolean;
};

// Bunbun.MascotColor keeps one colour per character: "bunny=#F4B6C8;cat=#5B5868".
// A plain hex from older settings applies to every character.
export function colorFor(setting: string, character: string): string {
  if (character === "spiderman" || character === "doom" || character.startsWith("custom:")) return "";
  if (!setting.includes("=")) return setting;
  for (const part of setting.split(";")) {
    const [k, v] = part.split("=");
    if (k?.trim().toLowerCase() === character) return (v ?? "").trim();
  }
  return "";
}

export function withColor(setting: string, character: string, hex: string): string {
  const map: Record<string, string> = {};
  if (setting.includes("=")) {
    for (const part of setting.split(";")) {
      const [k, v] = part.split("=");
      if (k && v) map[k.trim().toLowerCase()] = v.trim();
    }
  }
  if (hex) map[character] = hex; else delete map[character];
  return Object.entries(map).map(([k, v]) => `${k}=${v}`).join(";");
}

export function useMascotStyle(): MascotStyle {
  const s = useStore();
  return {
    character: s.get("Bunbun.Character"),
    color: colorFor(s.get("Bunbun.MascotColor"), s.get("Bunbun.Character")),
    motion: s.num("Bunbun.MascotMotion", 100),
    hop: s.num("Bunbun.MascotHop", 100),
    blink: s.num("Bunbun.MascotBlinkEvery", 4),
    eyesFollow: s.bool("Bunbun.MascotEyesFollow"),
    celebrate: s.bool("Bunbun.MascotCelebrate"),
  };
}

/**
 * Frames come from the host, drawn by the island's own mascot code, so the
 * preview is pixel-exact. Animated previews pull ~16 frames per second and
 * never queue more than one request.
 */
export function useMascotFrame(style: MascotStyle, mood: string, animate: boolean, size = 220, look?: { x: number; y: number }) {
  const [src, setSrc] = useState("");
  const key = JSON.stringify(style);
  const start = useRef(performance.now());
  const moodStart = useRef(0);
  const lookRef = useRef(look);
  lookRef.current = look;

  useEffect(() => { moodStart.current = (performance.now() - start.current) / 1000; }, [mood]);

  useEffect(() => {
    let alive = true;
    let timer = 0;
    const frame = async () => {
      const t = animate ? (performance.now() - start.current) / 1000 : 0.4;
      try {
        const url = await call<string>("renderMascot", {
          ...style, mood, t, since: animate ? moodStart.current : -10, size,
          lookX: lookRef.current?.x ?? 0, lookY: lookRef.current?.y ?? 0,
        });
        if (alive && url) setSrc(url);
      } catch { /* host busy; try next tick */ }
      if (alive && animate) timer = window.setTimeout(frame, 60);
    };
    frame();
    return () => { alive = false; window.clearTimeout(timer); };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [key, mood, animate, size]);
  return src;
}

export function MascotImage({ style, mood = "idle", animate = false, size = 72, className }: {
  style: MascotStyle; mood?: string; animate?: boolean; size?: number; className?: string;
}) {
  const src = useMascotFrame(style, mood, animate, size * 2);
  return src ? <img src={src} width={size} height={size} className={className} alt="" draggable={false} />
             : <div className={`ph ${className ?? ""}`} style={{ width: size, height: size }} />;
}

/** The big mascot that watches the cursor (the eyes come from the host). */
export function LiveMascot({ style, mood, size = 220 }: { style: MascotStyle; mood: string; size?: number }) {
  const ref = useRef<HTMLDivElement>(null);
  const [look, setLook] = useState({ x: 0, y: 0 });
  useEffect(() => {
    const move = (e: MouseEvent) => {
      const r = ref.current?.getBoundingClientRect();
      if (!r) return;
      setLook({ x: Math.max(-1, Math.min(1, (e.clientX - (r.left + r.width / 2)) / 240)),
                y: Math.max(-1, Math.min(1, (e.clientY - (r.top + r.height / 2)) / 200)) });
    };
    window.addEventListener("mousemove", move);
    return () => window.removeEventListener("mousemove", move);
  }, []);
  const src = useMascotFrame(style, mood, true, size * 2, look);
  return (
    <div ref={ref} style={{ width: size, height: size, display: "grid", placeItems: "center" }}>
      {src ? <motion.img src={src} width={size} height={size} alt="" draggable={false} initial={{ opacity: 0, scale: 0.92 }} animate={{ opacity: 1, scale: 1 }} />
           : <div className="ph" style={{ width: size * 0.6, height: size * 0.6 }} />}
    </div>
  );
}
