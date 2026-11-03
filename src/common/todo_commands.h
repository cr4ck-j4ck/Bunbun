// Bunbun — the settings app's To-dos commands (listTodos, saveTodo, deleteTodo,
// clearDoneTodos), shared by the Windows and Linux settings hosts. The list lives
// in todos.tsv (todos.h).
#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "json.h"
#include "todos.h"

namespace bb::todos {

inline json::Value ToJson(const Item& t) {
    json::Value o = json::Value::Object();
    o["id"] = json::Value::Number(t.id);
    o["text"] = json::Value::String(ToUtf8(t.text));
    o["due"] = json::Value::Number(t.due);
    o["done"] = json::Value::Bool(t.done);
    o["repeat"] = json::Value::String(ToUtf8(t.repeat));
    return o;
}

// Ticks (or unticks) a to-do. A repeating reminder never finishes: ticking it
// skips to its next occurrence instead.
inline void SetDone(Item* item, bool done) {
    item->done = done;
    if (item->done && !item->repeat.empty()) {
        item->done = false;
        item->due = NextOccurrence(item->repeat, item->due, std::max(Now(), item->due));
        item->notified = false;
    }
}

// Runs one command against the list at `path`. On failure returns a null Value
// and sets *err.
inline json::Value Command(const std::string& cmd, const json::Value& args, const std::wstring& path, std::string* err) {
    std::vector<Item> items = Load(path);
    if (cmd == "listTodos") {
        json::Value list = json::Value::Array();
        for (const Item& t : items) list.push(ToJson(t));
        return list;
    }
    if (cmd == "saveTodo") {  // id 0 adds a new one
        const int id = static_cast<int>(args.getNumber("id"));
        const std::wstring text = FromUtf8(args.getString("text"));
        if (text.empty()) { *err = "text is empty"; return {}; }
        Item* item = nullptr;
        for (Item& t : items) if (t.id == id) item = &t;
        if (!item) {
            Item fresh;
            fresh.id = NextId(items);
            items.push_back(fresh);
            item = &items.back();
        }
        const double due = args.getNumber("due");
        const std::wstring repeat = due > 0 ? FromUtf8(args.getString("repeat")) : std::wstring();
        if (due != item->due || repeat != item->repeat) item->notified = false;  // rescheduled: ring again
        item->text = text;
        item->due = due;
        item->repeat = repeat;
        SetDone(item, args.getBool("done"));
        if (!Save(path, items)) { *err = "couldn't write todos.tsv"; return {}; }
        return ToJson(*item);
    }
    if (cmd == "deleteTodo") {
        const int id = static_cast<int>(args.getNumber("id"));
        items.erase(std::remove_if(items.begin(), items.end(), [&](const Item& t) { return t.id == id; }), items.end());
        if (!Save(path, items)) { *err = "couldn't write todos.tsv"; return {}; }
        return json::Value::Bool(true);
    }
    if (cmd == "clearDoneTodos") {
        items.erase(std::remove_if(items.begin(), items.end(), [](const Item& t) { return t.done; }), items.end());
        if (!Save(path, items)) { *err = "couldn't write todos.tsv"; return {}; }
        return json::Value::Bool(true);
    }
    *err = "unknown to-do command";
    return {};
}

}  // namespace bb::todos
