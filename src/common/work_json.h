// Bunbun — Work mode's days as JSON, for the settings apps (Windows and Linux).
#pragma once

#include "json.h"
#include "work.h"

namespace bb::work {

// Bunbun: Work mode's days (common/work.h) for the Work page's report, newest last:
// [{date, seconds:{work, away, …}, breaks, first, last, segments:[[start, end, kind]…]}…].
inline bb::json::Value WorkDaysJson(const std::string& text, int fromDate, int toDate) {
    using bb::json::Value;
    namespace wk = bb::work;
    const wk::Log log = wk::Parse(text);
    Value days = Value::Array();
    for (const auto& [date, day] : log.days) {
        if (date < fromDate || date > toDate) continue;
        const wk::Summary s = wk::Summarize(day);
        Value d = Value::Object();
        d["date"] = Value::Number(date);
        Value secs = Value::Object();
        for (int k = 0; k < static_cast<int>(wk::Kind::Count); ++k)
            secs[wk::KindKey(static_cast<wk::Kind>(k))] = Value::Number(s.seconds[k]);
        d["seconds"] = std::move(secs);
        d["breaks"] = Value::Number(s.breaks);
        d["first"] = Value::Number(s.first);
        d["last"] = Value::Number(s.last);
        Value segs = Value::Array();
        for (const wk::Segment& g : day.segments) {
            Value one = Value::Array();
            one.arr.push_back(Value::Number(g.start));
            one.arr.push_back(Value::Number(g.end));
            one.arr.push_back(Value::String(wk::KindKey(g.kind)));
            segs.arr.push_back(std::move(one));
        }
        d["segments"] = std::move(segs);
        Value subjects = Value::Object();  // Study: seconds per subject
        for (const auto& [name, sec] : day.subjects) subjects[wk::ToUtf8(name)] = Value::Number(sec);
        d["subjects"] = std::move(subjects);
        days.arr.push_back(std::move(d));
    }
    return days;
}

}  // namespace bb::work
