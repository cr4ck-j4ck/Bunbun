import { CalendarClock, Check, Plus, Repeat, Trash2 } from "lucide-react";
import React, { useCallback, useEffect, useState } from "react";
import { call } from "../bridge";
import { PageHead } from "../ui/controls";

// The island's to-do list (todos.tsv next to settings.ini). Every change is one
// small edit written straight to the file; the island notices and reloads.
type Todo = { id: number; text: string; due: number; done: boolean; repeat: string };

const DAYS = ["Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"];

// `due` (Unix seconds) <-> the value of an <input type="datetime-local">, in local time.
function toLocalInput(due: number): string {
  if (!due) return "";
  const d = new Date(due * 1000);
  const pad = (n: number) => String(n).padStart(2, "0");
  return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())}T${pad(d.getHours())}:${pad(d.getMinutes())}`;
}
function fromLocalInput(v: string): number {
  return v ? Math.round(new Date(v).getTime() / 1000) : 0;
}

function describe(t: Todo): string {
  if (!t.due) return "No reminder";
  const when = new Date(t.due * 1000);
  const time = when.toLocaleTimeString([], { hour: "numeric", minute: "2-digit" });
  if (t.repeat === "daily") return `Every day at ${time}`;
  if (t.repeat === "weekdays") return `Weekdays at ${time}`;
  if (t.repeat.startsWith("weekly:")) {
    const days = t.repeat.slice(7).split(",").map((d) => DAYS[Number(d)]).join(", ");
    return `Every ${days} at ${time}`;
  }
  const late = !t.done && t.due * 1000 < Date.now();
  const date = when.toLocaleDateString([], { weekday: "short", day: "numeric", month: "short" });
  return `${late ? "Overdue · " : ""}${date}, ${time}`;
}

// Repeat picker: none / every day / weekdays / chosen days.
function RepeatPicker({ value, onChange, disabled }: { value: string; onChange: (v: string) => void; disabled?: boolean }) {
  const mode = value === "daily" || value === "weekdays" ? value : value.startsWith("weekly:") ? "weekly" : "";
  const days = value.startsWith("weekly:") ? value.slice(7).split(",").filter(Boolean).map(Number) : [];
  const setDays = (d: number[]) => onChange(d.length ? `weekly:${[...d].sort().join(",")}` : "");
  return (
    <div className="todo-repeat">
      <select className="field" value={mode} disabled={disabled} aria-label="Repeat"
        onChange={(e) => onChange(e.target.value === "weekly" ? `weekly:${new Date().getDay()}` : e.target.value)}>
        <option value="">Doesn't repeat</option>
        <option value="daily">Every day</option>
        <option value="weekdays">Weekdays</option>
        <option value="weekly">On chosen days</option>
      </select>
      {mode === "weekly" && (
        <div className="todo-days">
          {DAYS.map((d, i) => (
            <button key={d} type="button" className={`todo-day ${days.includes(i) ? "on" : ""}`}
              onClick={() => setDays(days.includes(i) ? days.filter((x) => x !== i) : [...days, i])}>{d[0]}</button>
          ))}
        </div>
      )}
    </div>
  );
}

function TodoRow({ todo, onSave, onDelete }: { todo: Todo; onSave: (t: Todo) => void; onDelete: () => void }) {
  const [text, setText] = useState(todo.text);
  const [editing, setEditing] = useState(false);
  useEffect(() => setText(todo.text), [todo.text]);
  const commitText = () => {
    setEditing(false);
    const v = text.trim();
    if (v && v !== todo.text) onSave({ ...todo, text: v });
    else setText(todo.text);
  };
  return (
    <div className={`todo-row ${todo.done ? "done" : ""}`}>
      <button type="button" className={`todo-check ${todo.done ? "on" : ""}`} aria-label={todo.done ? "Mark not done" : "Mark done"}
        onClick={() => onSave({ ...todo, done: !todo.done })}>{todo.done && <Check size={12} strokeWidth={3} />}</button>
      <div className="todo-main">
        {editing ? (
          <input className="field" autoFocus value={text} onChange={(e) => setText(e.target.value)} onBlur={commitText}
            onKeyDown={(e) => { if (e.key === "Enter") commitText(); if (e.key === "Escape") { setText(todo.text); setEditing(false); } }} />
        ) : (
          <b onClick={() => setEditing(true)} title="Click to edit">{todo.text}</b>
        )}
        <span className="todo-when">{todo.repeat ? <Repeat size={11} /> : <CalendarClock size={11} />} {describe(todo)}</span>
        <div className="todo-edit">
          <input className="field" type="datetime-local" aria-label="Remind me at" value={toLocalInput(todo.due)}
            onChange={(e) => onSave({ ...todo, due: fromLocalInput(e.target.value), repeat: e.target.value ? todo.repeat : "" })} />
          <RepeatPicker value={todo.repeat} disabled={!todo.due} onChange={(repeat) => onSave({ ...todo, repeat })} />
        </div>
      </div>
      <button type="button" className="btn small danger" aria-label="Delete" onClick={onDelete}><Trash2 size={14} /></button>
    </div>
  );
}

// A titled block without the card Section adds (this page lays out its own).
function Block({ title, children }: { title?: string; children: React.ReactNode }) {
  return (
    <section className="section">
      {title && <h3>{title}</h3>}
      {children}
    </section>
  );
}

export function Todos() {
  const [todos, setTodos] = useState<Todo[] | null>(null);
  const [error, setError] = useState("");
  const [draft, setDraft] = useState("");
  const [draftDue, setDraftDue] = useState("");

  const load = useCallback(() => {
    call<Todo[]>("listTodos").then(setTodos).catch((e) => setError(String(e.message ?? e)));
  }, []);
  useEffect(() => {
    load();
    const id = window.setInterval(load, 5000);  // reminders firing and Ask Bun change it too
    return () => window.clearInterval(id);
  }, [load]);

  const save = (t: Todo) =>
    call<Todo>("saveTodo", t).then(load).catch((e) => setError(String(e.message ?? e)));
  const add = () => {
    const text = draft.trim();
    if (!text) return;
    save({ id: 0, text, due: fromLocalInput(draftDue), done: false, repeat: "" });
    setDraft("");
    setDraftDue("");
  };

  const open = (todos ?? []).filter((t) => !t.done).sort((a, b) => (a.due || 9e15) - (b.due || 9e15));  // no reminder: last (Infinity - Infinity is NaN)
  const finished = (todos ?? []).filter((t) => t.done);

  return (
    <div className="page">
      <PageHead title="To-dos" subtitle="Your list and reminders. Ask Bun adds to it too: “add buy milk”, “remind me every day at 9 to stand up”." />
      {error && <div className="hint bad">{error}</div>}

      <Block>
        <div className="todo-add">
          <input className="field" placeholder="Add a to-do…" value={draft} onChange={(e) => setDraft(e.target.value)}
            onKeyDown={(e) => { if (e.key === "Enter") add(); }} />
          <input className="field" type="datetime-local" aria-label="Remind me at (optional)" value={draftDue}
            onChange={(e) => setDraftDue(e.target.value)} />
          <button type="button" className="btn primary" onClick={add} disabled={!draft.trim()}><Plus size={15} /> Add</button>
        </div>
      </Block>

      <Block title={`To do${open.length ? ` · ${open.length}` : ""}`}>
        <div className="card">
          {todos === null && <div className="todo-empty">Loading…</div>}
          {todos !== null && open.length === 0 && <div className="todo-empty">Nothing to do. Enjoy it.</div>}
          {open.map((t) => (
            <TodoRow key={t.id} todo={t} onSave={save} onDelete={() => call("deleteTodo", { id: t.id }).then(load)} />
          ))}
        </div>
      </Block>

      {finished.length > 0 && (
        <Block title={`Finished · ${finished.length}`}>
          <div className="card">
            {finished.map((t) => (
              <TodoRow key={t.id} todo={t} onSave={save} onDelete={() => call("deleteTodo", { id: t.id }).then(load)} />
            ))}
          </div>
          <div style={{ marginTop: 10 }}>
            <button type="button" className="btn small" onClick={() => call("clearDoneTodos").then(load)}>Clear finished</button>
          </div>
        </Block>
      )}
    </div>
  );
}
