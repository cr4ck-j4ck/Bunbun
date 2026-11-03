import { motion } from "framer-motion";
import { useStore } from "../store";
import { MascotImage, useMascotStyle } from "./mascot";

export function shapeRadius(shape: string, h: number) {
  if (shape === "notch") return `0 0 ${h * 0.42}px ${h * 0.42}px`;
  if (shape === "w11") return "8px";
  return `${h}px`;
}

/** A small desktop with the island on it, styled from the live settings. */
export function IslandPreview({ content = "agent", height = 210 }: { content?: "agent" | "idle"; height?: number }) {
  const s = useStore();
  const style = useMascotStyle();
  const shape = s.get("Appearance.ShapeStyle");
  const pos = s.get("Appearance.Position");
  const bottom = pos.startsWith("bottom");
  const flush = shape === "notch" || s.bool("Appearance.BorderMergedMode");
  const align = pos.endsWith("left") ? "left" : pos.endsWith("right") ? "right" : "center";
  const opaque = s.get("Themes.IslandFill") === "opaque";
  const bg = opaque ? s.get("Themes.SolidColor") || "#000000" : s.get("Themes.PillBgColor") || "#08080A";
  const opacity = opaque ? 1 : s.num("Themes.PillOpacity", 96) / 100;
  const h = 46;
  const w = content === "agent" ? "86%" : 120;

  const x = align === "left" ? 14 : align === "right" ? undefined : "50%";
  return (
    <div className="screen" style={{ height }}>
      <motion.div
        className="island"
        layout
        transition={{ type: "spring", stiffness: 380, damping: 28 }}
        style={{
          height: h, width: w, background: bg, opacity,
          borderRadius: flush && !bottom ? shapeRadius("notch", h) : shapeRadius(shape, h),
          top: bottom ? undefined : flush ? 0 : 12,
          bottom: bottom ? 38 : undefined,
          left: x, right: align === "right" ? 14 : undefined,
          translateX: align === "center" ? "-50%" : 0,
          border: s.get("Themes.ContourBorderMode") === "borderless" ? "none" : `1px solid ${s.get("Themes.ContourBorderHex") || "#1E1E22"}`,
        }}
      >
        {content === "agent" ? (
          <>
            {s.bool("Bunbun.Mascot") && <MascotImage style={style} mood="working" animate size={34} />}
            <div style={{ minWidth: 0 }}>
              <div className="t1">Claude Code · Bunbun</div>
              <div className="t2">Edits · island.cpp</div>
            </div>
            <div className="dots"><i /><i /><i /></div>
          </>
        ) : (
          <div className="t2" style={{ margin: "0 auto" }}>9:41 · 24°</div>
        )}
      </motion.div>
      <div className="taskbar" />
    </div>
  );
}
