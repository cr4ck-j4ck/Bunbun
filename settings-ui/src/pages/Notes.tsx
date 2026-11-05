// The Quick note (note.txt next to settings.ini): the same note the island's Note
// page shows, edited here instead of in Notepad. Saved as you type; the island
// notices the file change by itself.
import { useEffect, useRef, useState } from "react";
import { call } from "../bridge";
import { PageHead, Section } from "../ui/controls";

export function Notes() {
  const [text, setText] = useState<string | null>(null);
  const [status, setStatus] = useState("");
  const timer = useRef<number>();
  const latest = useRef("");
  const onDisk = useRef<string | null>(null);  // the file as last read or written

  useEffect(() => {
    call<string>("readNote")
      .then((t) => { setText(t); latest.current = t; onDisk.current = t; })
      .catch((e) => setStatus(String(e?.message ?? e)));
  }, []);

  // The island rewrites the note when you tick a box on its Note page. Pick that up
  // while nothing typed here is waiting to be saved, so the next save keeps the tick.
  useEffect(() => {
    const poll = window.setInterval(() => {
      if (timer.current || onDisk.current === null || latest.current !== onDisk.current) return;
      call<string>("readNote").then((t) => {
        if (timer.current || latest.current !== onDisk.current || t === onDisk.current) return;
        onDisk.current = t;
        latest.current = t;
        setText(t);
      }).catch(() => {});
    }, 1500);
    return () => window.clearInterval(poll);
  }, []);

  const save = (value: string) => {
    setStatus("Saving…");
    call("writeNote", { text: value })
      .then(() => { onDisk.current = value; setStatus("Saved"); })
      .catch((e) => setStatus(`Couldn't save: ${String(e?.message ?? e)}`));
  };

  const change = (value: string) => {
    setText(value);
    latest.current = value;
    window.clearTimeout(timer.current);
    timer.current = window.setTimeout(() => { timer.current = undefined; save(value); }, 600);
  };

  // Don't lose the last few keystrokes when the window closes.
  useEffect(() => {
    const flush = () => { if (timer.current) { window.clearTimeout(timer.current); timer.current = undefined; save(latest.current); } };
    window.addEventListener("pagehide", flush);
    return () => { window.removeEventListener("pagehide", flush); flush(); };
  }, []);

  const lines = (text ?? "").split("\n");
  const tasks = lines.filter((l) => /^\s*\[( |x|X)\]/.test(l));
  const done = tasks.filter((l) => /^\s*\[(x|X)\]/.test(l)).length;

  return (
    <div className="page">
      <PageHead title="Notes" subtitle="Your quick note, the one on the island's Note page. Changes show on the island straight away." />
      <Section title="Quick note">
        <div className="note-editor">
          <textarea
            value={text ?? ""}
            disabled={text === null}
            placeholder={"Quick note\n[ ] Something to do\n- A thought"}
            spellCheck
            onChange={(e) => change(e.target.value)}
          />
          <div className="note-foot">
            <span>{tasks.length ? `${done} of ${tasks.length} ticked` : "Start a line with [ ] for a checkbox, - for a bullet."}</span>
            <span>{status}</span>
          </div>
        </div>
      </Section>
    </div>
  );
}
