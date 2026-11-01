// Talks to bunbun-settings.exe over WebView2's postMessage channel.
// In a plain browser (npm run dev) a mock answers instead, so the UI can be
// designed without the host.
import schema from "./schema.json";

type Reply = { id: number; ok: boolean; result?: unknown; error?: string };
type WebView = {
  postMessage(msg: unknown): void;
  addEventListener(type: "message", fn: (e: { data: Reply }) => void): void;
};

const webview: WebView | undefined = (window as unknown as { chrome?: { webview?: WebView } }).chrome?.webview;
const pending = new Map<number, { resolve: (v: unknown) => void; reject: (e: Error) => void }>();
let nextId = 1;

webview?.addEventListener("message", (e) => {
  const reply = e.data;
  const p = pending.get(reply.id);
  if (!p) return;
  pending.delete(reply.id);
  if (reply.ok) p.resolve(reply.result);
  else p.reject(new Error(reply.error ?? "request failed"));
});

export const inHost = !!webview;

export function call<T = unknown>(cmd: string, args: Record<string, unknown> = {}): Promise<T> {
  if (!webview) return mock(cmd, args) as Promise<T>;
  const id = nextId++;
  return new Promise<T>((resolve, reject) => {
    pending.set(id, { resolve: resolve as (v: unknown) => void, reject });
    webview.postMessage({ id, cmd, args });
  });
}

// ── Browser mock ───────────────────────────────────────────────────────────
const mockValues: Record<string, string> = {};
async function mock(cmd: string, args: Record<string, unknown>): Promise<unknown> {
  const defaults = Object.fromEntries(Object.entries(schema).map(([k, v]) => [k, (v as { default: string }).default]));
  switch (cmd) {
    case "getSettings":
      return {
        values: { ...defaults, ...mockValues },
        defaults,
        custom: Object.fromEntries(Object.keys(defaults).map((k) => [k, k in mockValues])),
        path: "%APPDATA%\\Bunbun\\settings.ini",
      };
    case "setSettings":
      Object.assign(mockValues, args.values as Record<string, string>);
      return true;
    case "resetSettings":
      for (const k of args.keys as string[]) delete mockValues[k];
      return true;
    case "listMonitors":
      return [
        { index: 1, name: "Built-in display", primary: true, bounds: { x: 0, y: 0, w: 1920, h: 1080 }, work: { x: 0, y: 0, w: 1920, h: 1032 } },
        { index: 2, name: "DELL U2720Q", primary: false, bounds: { x: 1920, y: -300, w: 2560, h: 1440 }, work: { x: 1920, y: -300, w: 2560, h: 1400 } },
      ];
    case "listPacks":
      return [];
    case "agentStatus":
      return { claudeConnected: false, claudeSettingsPath: "~/.claude/settings.json" };
    case "getSystem":
      return { autostart: false, islandRunning: true, version: "0.1.0", packsFolder: "%APPDATA%\\Bunbun\\mascots" };
    case "renderMascot":
      return "";
    default:
      return true;
  }
}
