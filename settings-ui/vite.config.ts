import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// Built into ../build/settings-ui, which bunbun-settings.exe serves as https://bunbun.ui/.
export default defineConfig({
  plugins: [react()],
  base: "./",
  build: { outDir: "../build/settings-ui", emptyOutDir: true, target: "es2022" },
});
