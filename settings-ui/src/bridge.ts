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
type MockTodo = { id: number; text: string; due: number; done: boolean; repeat: string };
let mockNote = "Quick note\n[ ] Book the dentist\n[x] Ship 0.3.6\n- Try the Water page";
let mockTodos: MockTodo[] = [
  { id: 1, text: "Send the report", due: Math.round(Date.now() / 1000) + 3600, done: false, repeat: "" },
  { id: 2, text: "Stand up and stretch", due: Math.round(Date.now() / 1000) + 7200, done: false, repeat: "weekdays" },
  { id: 3, text: "Buy milk", due: 0, done: false, repeat: "" },
  { id: 4, text: "Call Sam", due: 0, done: true, repeat: "" },
];
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
    case "readNote":
      return mockNote;
    case "writeNote":
      mockNote = String(args.text ?? "");
      return true;
    case "listTodos":
      return mockTodos.map((t) => ({ ...t }));  // a fresh list each time, like the host
    case "saveTodo": {
      const t = args as unknown as MockTodo;
      const existing = mockTodos.find((x) => x.id === t.id);
      if (existing) Object.assign(existing, t);
      else mockTodos.push({ ...t, id: Math.max(0, ...mockTodos.map((x) => x.id)) + 1 });
      return t;
    }
    case "deleteTodo":
      mockTodos = mockTodos.filter((x) => x.id !== (args.id as number));
      return true;
    case "clearDoneTodos":
      mockTodos = mockTodos.filter((x) => !x.done);
      return true;
    case "listPacks":
      return [];
    case "sliceSheet": {
      await new Promise((r) => setTimeout(r, 400));
      const frame = "data:image/svg+xml;utf8," + encodeURIComponent("<svg xmlns='http://www.w3.org/2000/svg' width='72' height='48'><circle cx='36' cy='24' r='16' fill='%23F2C48D'/></svg>");
      return [{ frames: [frame, frame, frame, frame, frame] }, { frames: [frame, frame, frame] }];
    }
    case "savePack":
      return String(args.name ?? "My mascot");
    case "localAiStatus":
      return { helper: true, modelsFolder: "%LOCALAPPDATA%\\Bunbun\\models",
               models: [{ name: "qwen2.5-1.5b-instruct-q4_k_m.gguf", path: "C:\\models\\qwen.gguf", size: 1117320736 }] };
    case "agentStatus":
      return { claudeConnected: false, claudeSettingsPath: "~/.claude/settings.json" };
    case "getSystem":
      // ?platform=linux shows what the settings app marks as not on Linux yet.
      return { autostart: false, islandRunning: true, version: "0.1.0", packsFolder: "%APPDATA%\\Bunbun\\mascots",
               platform: new URLSearchParams(location.search).get("platform") ?? "windows" };
    case "renderMascot":
      return "";
    case "checkUpdate":
      await new Promise((r) => setTimeout(r, 600));
      return { current: "0.1.0", checked: true, latest: "0.2.0", newer: true,
               url: "https://github.com/iaryasharma/Bunbun/releases/latest" };
    default:
      return true;
  }
}
