import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";
import { readFileSync } from "node:fs";

// The mock bridge (npm run dev) reports this as the app's version, so screenshots show the real one.
const version = readFileSync("../VERSION", "utf8").trim();

// Built into ../build/settings-ui, which bunbun-settings.exe serves as https://bunbun.ui/.
export default defineConfig({
  plugins: [react()],
  define: { __APP_VERSION__: JSON.stringify(version) },
  base: "./",
  build: { outDir: "../build/settings-ui", emptyOutDir: true, target: "es2022" },
});
