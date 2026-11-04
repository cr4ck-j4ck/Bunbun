// Settings state shared by every page. Writes are optimistic and debounced
// per key, so dragging a slider previews live on the island without
// flooding settings.ini.
import { createContext, useCallback, useContext, useEffect, useMemo, useRef, useState } from "react";
import { call } from "./bridge";
import schemaJson from "./schema.json";

export type SchemaEntry = { default: string; name: string; description: string; options: { value: string; label: string }[] };
export const schema = schemaJson as Record<string, SchemaEntry>;

type SettingsReply = {
  values: Record<string, string>;
  defaults: Record<string, string>;
  custom: Record<string, boolean>;
  path: string;
};

export type Store = {
  ready: boolean;
  values: Record<string, string>;
  defaults: Record<string, string>;
  get(key: string): string;
  bool(key: string): boolean;
  num(key: string, fallback?: number): number;
  set(key: string, value: string | number | boolean): void;
  setMany(values: Record<string, string>): void;
  reset(keys: string[]): Promise<void>;
  isDefault(key: string): boolean;
  saving: boolean;
  loadError: string;   // non-empty when settings couldn't be read
  saveError: string;   // non-empty while the last write failed
  retry(): void;
};

export const StoreContext = createContext<Store | null>(null);

export function useStore(): Store {
  const s = useContext(StoreContext);
  if (!s) throw new Error("StoreContext missing");
  return s;
}

export function useSettingsStore(): Store {
  const [values, setValues] = useState<Record<string, string>>({});
  const [defaults, setDefaults] = useState<Record<string, string>>({});
  const [ready, setReady] = useState(false);
  const [saving, setSaving] = useState(false);
  const [loadError, setLoadError] = useState("");
  const [saveError, setSaveError] = useState("");
  const [attempt, setAttempt] = useState(0);
  const queue = useRef<Record<string, string>>({});
  const timer = useRef<number | undefined>(undefined);
  const defaultsRef = useRef<Record<string, string>>({});

  useEffect(() => {
    setLoadError("");
    call<SettingsReply>("getSettings")
      .then((r) => {
        setValues(r.values);
        setDefaults(r.defaults);
        defaultsRef.current = r.defaults;
        setReady(true);
      })
      .catch((e) => setLoadError(String(e?.message ?? e)));
  }, [attempt]);

  const flush = useCallback(() => {
    window.clearTimeout(timer.current);
    const batch = queue.current;
    queue.current = {};
    if (!Object.keys(batch).length) return;
    // A value equal to the default is written as null, which removes the key.
    // An explicit default would freeze it, and later default changes would never apply.
    const values: Record<string, string | null> = {};
    for (const [k, v] of Object.entries(batch)) {
      const def = defaultsRef.current[k] ?? schema[k]?.default;
      // Numbers count as the default by value: a slider's "1" is Size scale's "1.0".
      const sameNumber = def !== undefined && def.trim() !== "" && v.trim() !== "" && !isNaN(+def) && !isNaN(+v) && +v === +def;
      values[k] = def !== undefined && (v === def || sameNumber) ? null : v;
    }
    setSaving(true);
    call<boolean | { skipped?: string[] }>("setSettings", { values })
      .then((r) => {
        // The host writes every key it knows and lists the ones it couldn't.
        const skipped = r && typeof r === "object" ? r.skipped ?? [] : [];
        setSaveError(skipped.length ? `Some settings couldn't be saved: ${skipped.join(", ")}` : "");
      })
      .catch((e) => {
        // Keep the failed edits so the next change (or Retry) writes them again.
        queue.current = { ...batch, ...queue.current };
        setSaveError(String(e?.message ?? e));
      })
      .finally(() => setTimeout(() => setSaving(false), 300));
  }, []);

  // Don't lose edits made in the last moments before the window closes.
  useEffect(() => {
    const onHide = () => { if (document.visibilityState === "hidden") flush(); };
    window.addEventListener("pagehide", flush);
    document.addEventListener("visibilitychange", onHide);
    return () => {
      window.removeEventListener("pagehide", flush);
      document.removeEventListener("visibilitychange", onHide);
    };
  }, [flush]);

  const setMany = useCallback(
    (next: Record<string, string>) => {
      setValues((v) => ({ ...v, ...next }));
      Object.assign(queue.current, next);
      window.clearTimeout(timer.current);
      timer.current = window.setTimeout(flush, 140);
    },
    [flush],
  );

  const set = useCallback(
    (key: string, value: string | number | boolean) => setMany({ [key]: typeof value === "boolean" ? (value ? "true" : "false") : String(value) }),
    [setMany],
  );

  const reset = useCallback(async (keys: string[]) => {
    // Drop pending writes for these keys, or the debounced flush would put the
    // old value back right after the reset.
    for (const k of keys) delete queue.current[k];
    await call("resetSettings", { keys });
    setValues((v) => {
      const next = { ...v };
      for (const k of keys) next[k] = defaults[k] ?? schema[k]?.default ?? "";
      return next;
    });
  }, [defaults]);

  return useMemo<Store>(() => {
    const get = (k: string) => values[k] ?? defaults[k] ?? schema[k]?.default ?? "";
    return {
      ready,
      values,
      defaults,
      get,
      bool: (k) => get(k).toLowerCase() === "true",
      num: (k, f = 0) => {
        const n = parseFloat(get(k));
        return Number.isFinite(n) ? n : f;
      },
      set,
      setMany,
      reset,
      isDefault: (k) => get(k) === (defaults[k] ?? schema[k]?.default),
      saving,
      loadError,
      saveError,
      retry: () => (ready ? flush() : setAttempt((a) => a + 1)),
    };
  }, [values, defaults, ready, set, setMany, reset, saving, loadError, saveError, flush]);
}
