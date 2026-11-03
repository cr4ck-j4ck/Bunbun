// bunbun-settings on Linux: the React settings UI in a WebKitGTK window.
//
// The UI is the same build as on Windows (settings-ui/). It talks to its host
// through window.chrome.webview, as under WebView2; a small script injected
// here provides that object on top of WebKit's script message handlers, so the
// UI needs no Linux branch. Files are served from a private bunbun-ui:// scheme,
// like WebView2's virtual host on Windows.
//
// Commands that need Windows-only parts (the live island preview, to-dos,
// character-sheet import, Local AI, the update check) answer with an empty or
// "not on Linux yet" result; see docs/linux.md.
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <gtk/gtk.h>
#include <webkit2/webkit2.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <string>

#include "../../agents/hook_installer.h"
#include "../../common/json.h"
#include "../../common/todo_commands.h"
#include "../../common/unix_socket.h"
#include "mascot_png.h"
#include "platform.h"
#include "settings.h"

namespace {

using bb::json::Value;
using bb::plat::Utf8;
using bb::plat::Wide;

constexpr char kScheme[] = "bunbun-ui";
WebKitWebView* g_view = nullptr;
std::string g_uiDir;

Value Str(const std::wstring& w) { return Value::String(Utf8(w)); }

// ── Settings ────────────────────────────────────────────────────────────────

Value GetSettings() {
    bb::settings::EnsureFile();
    bb::settings::Invalidate();
    size_t n = 0;
    const bb::settings::Default* d = bb::settings::Defaults(&n);
    Value values = Value::Object(), defaults = Value::Object(), custom = Value::Object();
    for (size_t i = 0; i < n; ++i) {
        const std::wstring name = std::wstring(d[i].section) + L"." + d[i].key;
        const std::string key = Utf8(name);
        values[key] = Str(bb::settings::Get(name));
        defaults[key] = Str(d[i].value);
        custom[key] = Value::Bool(bb::settings::IsSet(name));
    }
    Value out = Value::Object();
    out["values"] = std::move(values);
    out["defaults"] = std::move(defaults);
    out["custom"] = std::move(custom);
    out["path"] = Value::String(bb::settings::Path());
    return out;
}

bool WriteSetting(const std::string& name, const Value* value, std::string* err) {
    const std::wstring w = Wide(name);
    if (!bb::settings::IsKnown(w)) { *err = "unknown setting " + name; return false; }
    if (!value || value->isNull()) return bb::settings::Set(w, nullptr);
    std::wstring text;
    if (value->isString()) text = Wide(value->str);
    else if (value->isBool()) text = value->b ? L"true" : L"false";
    else if (value->isNumber()) text = Wide(bb::json::Serialize(*value));
    else { *err = "unsupported value type"; return false; }
    for (wchar_t c : text)
        if (c == L'\r' || c == L'\n') { *err = "line breaks are not allowed"; return false; }
    if (!bb::settings::Set(w, &text)) { *err = "couldn't write " + bb::settings::Path(); return false; }
    return true;
}

// ── Mascot ──────────────────────────────────────────────────────────────────

bb::mascot::Mood MoodFromName(const std::string& m) {
    using bb::mascot::Mood;
    if (m == "working") return Mood::Working;
    if (m == "thinking") return Mood::Thinking;
    if (m == "alert") return Mood::Alert;
    if (m == "happy") return Mood::Happy;
    if (m == "sad") return Mood::Sad;
    if (m == "sleepy") return Mood::Sleepy;
    if (m == "music") return Mood::Music;
    if (m == "snack") return Mood::Snack;
    if (m == "listening") return Mood::Listening;
    if (m == "talking") return Mood::Talking;
    if (m == "confused") return Mood::Confused;
    if (m == "streaming") return Mood::Streaming;
    return Mood::Idle;
}

std::string RenderMascot(const Value& a) {
    const int px = std::clamp(static_cast<int>(a.getNumber("size", 160)), 32, 512);
    const std::string character = a.getString("character", "bunny"), color = a.getString("color");
    const bb::mascot::Style style = bb::mascot::StyleFromSettings(
        Wide(character), Wide(color.empty() || color.find('=') != std::string::npos ? color : character + "=" + color),
        static_cast<int>(a.getNumber("motion", 100)), static_cast<int>(a.getNumber("hop", 100)),
        static_cast<int>(a.getNumber("blink", 4) * 10), a.getBool("eyesFollow", true), a.getBool("celebrate", true));
    const std::string png = bb::plat::RenderMascotPng(
        style, MoodFromName(a.getString("mood", "idle")), px, a.getNumber("t", 0), a.getNumber("since", 0),
        D2D1::Point2F(static_cast<float>(a.getNumber("lookX", 0)), static_cast<float>(a.getNumber("lookY", 0))));
    return png.empty() ? std::string() : "data:image/png;base64," + bb::plat::Base64(png);
}

std::string PacksFolder() { return bb::plat::ConfigDir() + "/mascots"; }

// Animated packs (from a character sheet) have idle_1.png instead of idle.png.
Value ListPacks() {
    Value list = Value::Array();
    std::error_code ec;
    for (const auto& e : std::filesystem::directory_iterator(PacksFolder(), ec))
        if (e.is_directory(ec) && (std::filesystem::exists(e.path() / "idle.png", ec) ||
                                   std::filesystem::exists(e.path() / "idle_1.png", ec)))
            list.push(Value::String(e.path().filename().string()));
    return list;
}

// ── System ──────────────────────────────────────────────────────────────────

std::string AutostartFile() {
    const char* xdg = getenv("XDG_CONFIG_HOME");
    return (xdg && xdg[0] == '/' ? std::string(xdg) : bb::plat::HomeDir() + "/.config") + "/autostart/bunbun.desktop";
}

bool AutostartEnabled() { return bb::plat::FileExists(AutostartFile()); }

// The island to start at login: the AppImage itself when we run from one (its
// mount point changes every run), else bunbun next to us.
std::string IslandCommand() {
    if (const char* appimage = getenv("APPIMAGE"); appimage && *appimage) return appimage;
    return bb::plat::ExeDir() + "/bunbun";
}

void SetAutostart(bool on) {
    if (!on) {
        unlink(AutostartFile().c_str());
        return;
    }
    std::string exe = IslandCommand();
    std::string quoted = "\"";
    for (char c : exe) {
        if (c == '"' || c == '\\' || c == '`' || c == '$') quoted += '\\';
        quoted += c;
    }
    quoted += '"';
    bb::plat::WriteFileAtomic(AutostartFile(), "[Desktop Entry]\n"
                                               "Type=Application\n"
                                               "Name=Bunbun\n"
                                               "Comment=Dynamic Island with a mascot and coding-agent companions\n"
                                               "Exec=" + quoted + "\n"
                                               "Icon=bunbun\n"
                                               "X-GNOME-Autostart-enabled=true\n"
                                               "NoDisplay=false\n");
}

// ── Page pictures (Island pages) ────────────────────────────────────────────
// The island saves them to <state>/previews/<page>.png (studio_linux.cpp).

// {key: {src: data URL, at: ms since 1970}} for pictures newer than `since`.
Value PagePreviews(double since) {
    Value out = Value::Object();
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(bb::plat::StateDir() + "/previews", ec)) {
        if (entry.path().extension() != ".png") continue;
        struct stat st{};
        if (stat(entry.path().c_str(), &st) != 0) continue;
        const double at = st.st_mtim.tv_sec * 1000.0 + st.st_mtim.tv_nsec / 1e6;
        std::string png;
        if (at <= since || !bb::plat::ReadFile(entry.path().string(), &png)) continue;
        Value item = Value::Object();
        item["src"] = Value::String("data:image/png;base64," + bb::plat::Base64(png));
        item["at"] = Value::Number(at);
        out[entry.path().stem().string()] = std::move(item);
    }
    return out;
}

// Asks the running island for fresh pictures; it draws them offscreen, so they
// are all saved by the time it answers.
Value CapturePages() {
    Value out = Value::Object();
    out["running"] = Value::Bool(false);
    out["count"] = Value::Number(0);
    const int fd = bb::usock::Connect();
    if (fd < 0) return out;
    std::string line;
    const bool ok = bb::usock::WriteAll(fd, "{\"bunbun_command\":\"capture_pages\"}\n") && bb::usock::ReadLine(fd, &line, 6000);
    close(fd);
    Value reply;
    out["running"] = Value::Bool(ok);
    if (ok && bb::json::Parse(line, &reply) && reply.isObject()) out["count"] = Value::Number(reply.getNumber("count"));
    return out;
}

bool IslandRunning() {
    // The island holds an flock on its lock file for as long as it runs.
    const std::string path = bb::plat::RuntimeDir() + "/island.lock";
    const int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return false;
    const bool held = flock(fd, LOCK_EX | LOCK_NB) != 0;
    if (!held) flock(fd, LOCK_UN);
    close(fd);
    return held;
}

Value ListMonitors() {
    Value list = Value::Array();
    GdkDisplay* display = gdk_display_get_default();
    const int n = gdk_display_get_n_monitors(display);
    for (int i = 0; i < n; ++i) {
        GdkMonitor* m = gdk_display_get_monitor(display, i);
        GdkRectangle geo{}, work{};
        gdk_monitor_get_geometry(m, &geo);
        gdk_monitor_get_workarea(m, &work);
        const int scale = gdk_monitor_get_scale_factor(m);
        const auto rect = [scale](const GdkRectangle& r) {
            Value o = Value::Object();
            o["x"] = Value::Number(r.x * scale);
            o["y"] = Value::Number(r.y * scale);
            o["w"] = Value::Number(r.width * scale);
            o["h"] = Value::Number(r.height * scale);
            return o;
        };
        Value v = Value::Object();
        v["index"] = Value::Number(i + 1);
        const char* model = gdk_monitor_get_model(m);
        v["name"] = Value::String(model && *model ? model : "Display " + std::to_string(i + 1));
        v["primary"] = Value::Bool(gdk_monitor_is_primary(m) || (n == 1));
        v["bounds"] = rect(geo);
        v["work"] = rect(work);
        list.push(std::move(v));
    }
    return list;
}

Value HookResult(bool ok, const std::wstring& report) {
    Value o = Value::Object();
    o["ok"] = Value::Bool(ok);
    o["report"] = Str(report);
    return o;
}

Value Dispatch(const std::string& cmd, const Value& args, std::string* err) {
    namespace hooks = bb::hooks;
    if (cmd == "getSettings") return GetSettings();
    if (cmd == "setSettings") {
        const Value* values = args.find("values");
        if (!values || !values->isObject()) { *err = "values must be an object"; return {}; }
        for (const auto& [k, v] : values->obj)
            if (!WriteSetting(k, &v, err)) return {};
        return Value::Bool(true);
    }
    if (cmd == "resetSettings") {
        const Value* keys = args.find("keys");
        if (!keys || !keys->isArray()) { *err = "keys must be an array"; return {}; }
        for (const Value& k : keys->arr)
            if (!k.isString() || !WriteSetting(k.str, nullptr, err)) return {};
        return Value::Bool(true);
    }
    if (cmd == "renderMascot") return Value::String(RenderMascot(args));
    if (cmd == "listMonitors") return ListMonitors();
    if (cmd == "listPacks") return ListPacks();
    if (cmd == "openPacksFolder") {
        bb::plat::MakeDirs(PacksFolder());
        return Value::Bool(bb::plat::OpenWithDefault(PacksFolder()));
    }
    if (cmd == "openSettingsFile") {
        bb::settings::EnsureFile();
        return Value::Bool(bb::plat::OpenWithDefault(bb::settings::Path()));
    }
    if (cmd == "agentStatus") {
        Value o = Value::Object();
        o["claudeConnected"] = Value::Bool(hooks::ClaudeHooksInstalled());
        o["claudeSettingsPath"] = Str(hooks::ClaudeSettingsPath());
        o["antigravityConnected"] = Value::Bool(hooks::AntigravityHooksInstalled());
        o["antigravitySettingsPath"] = Str(hooks::AntigravitySettingsPath());
        o["cursorConnected"] = Value::Bool(hooks::CursorHooksInstalled());
        o["cursorSettingsPath"] = Str(hooks::CursorSettingsPath());
        o["copilotConnected"] = Value::Bool(hooks::CopilotHooksInstalled());
        o["copilotSettingsPath"] = Str(hooks::CopilotHooksPath());
        o["codexConnected"] = Value::Bool(hooks::CodexHooksInstalled());
        o["codexSettingsPath"] = Str(hooks::CodexHooksPath());
        o["hookExePath"] = Str(hooks::HookExePath());
        o["hookCmdVscode"] = Value::String(hooks::HookCommandFor("vscode", ""));
        o["hookCmdAntigravity"] = Value::String(hooks::HookCommandFor("antigravity", ""));
        o["hookCmdCursor"] = Value::String(hooks::HookCommandFor("cursor", ""));
        o["hookCmdCli"] = Value::String(hooks::HookCommandFor("cli", ""));
        return o;
    }
    std::wstring report;
    if (cmd == "connectClaude") return HookResult(hooks::InstallClaudeHooks(&report), report);
    if (cmd == "disconnectClaude") return HookResult(hooks::UninstallClaudeHooks(&report), report);
    if (cmd == "connectAntigravity") return HookResult(hooks::InstallAntigravityHooks(&report), report);
    if (cmd == "disconnectAntigravity") return HookResult(hooks::UninstallAntigravityHooks(&report), report);
    if (cmd == "connectCursor") return HookResult(hooks::InstallCursorHooks(&report), report);
    if (cmd == "disconnectCursor") return HookResult(hooks::UninstallCursorHooks(&report), report);
    if (cmd == "connectCopilot") return HookResult(hooks::InstallCopilotHooks(&report), report);
    if (cmd == "disconnectCopilot") return HookResult(hooks::UninstallCopilotHooks(&report), report);
    if (cmd == "connectCodex") return HookResult(hooks::InstallCodexHooks(&report), report);
    if (cmd == "disconnectCodex") return HookResult(hooks::UninstallCodexHooks(&report), report);
    if (cmd == "getSystem") {
        Value o = Value::Object();
        o["platform"] = Value::String("linux");
        o["autostart"] = Value::Bool(AutostartEnabled());
        o["islandRunning"] = Value::Bool(IslandRunning());
        o["version"] = Value::String(BUNBUN_VERSION_STR);
        o["packsFolder"] = Value::String(PacksFolder());
        return o;
    }
    if (cmd == "setAutostart") {
        SetAutostart(args.getBool("on"));
        return Value::Bool(AutostartEnabled());
    }
    if (cmd == "startIsland") {
        if (!IslandRunning()) bb::plat::Spawn({IslandCommand()});
        return Value::Bool(true);
    }
    if (cmd == "openUrl") {
        // Only web links; never arbitrary programs from the page.
        const std::string url = args.getString("url");
        if (url.rfind("https://", 0) != 0) { *err = "only https links"; return {}; }
        return Value::Bool(bb::plat::OpenWithDefault(url));
    }
    // Not ported to Linux yet: answer the way an empty Windows install would.
    if (cmd == "capturePreview") {
        Value o = Value::Object();
        o["running"] = Value::Bool(IslandRunning());
        o["shown"] = Value::Bool(false);
        return o;
    }
    if (cmd == "listTodos") return Value::Array();
    if (cmd == "localAiStatus") {
        Value o = Value::Object();
        o["helper"] = Value::Bool(false);
        o["modelsFolder"] = Value::String("");
        o["models"] = Value::Array();
        return o;
    }
    if (cmd == "checkUpdate") {
        Value o = Value::Object();
        o["current"] = Value::String(BUNBUN_VERSION_STR);
        o["checked"] = Value::Bool(false);
        o["error"] = Value::String("Update your package manager's copy, or download the new AppImage.");
        return o;
    }
    if (cmd == "getPagePreviews") return PagePreviews(args.getNumber("since"));
    if (cmd == "listTodos" || cmd == "saveTodo" || cmd == "deleteTodo" || cmd == "clearDoneTodos")
        return bb::todos::Command(cmd, args, bb::plat::Wide(bb::plat::ConfigDir() + "/todos.tsv"), err);
    if (cmd == "capturePages") return CapturePages();
    *err = cmd + " isn't available on Linux yet";
    return {};
}

// ── WebKit plumbing ─────────────────────────────────────────────────────────

void Reply(const Value& reply) {
    // The reply is JSON, which is a valid JavaScript expression.
    const std::string script = "window.__bunbunDeliver(" + bb::json::Serialize(reply) + ");";
#if WEBKIT_CHECK_VERSION(2, 40, 0)
    webkit_web_view_evaluate_javascript(g_view, script.c_str(), -1, nullptr, nullptr, nullptr, nullptr, nullptr);
#else
    webkit_web_view_run_javascript(g_view, script.c_str(), nullptr, nullptr, nullptr);
#endif
}

bool FromOurPage() {
    const char* uri = webkit_web_view_get_uri(g_view);
    if (!uri) return false;
    const char* dev = getenv("BUNBUN_UI_DEV");
    return g_str_has_prefix(uri, "bunbun-ui://") || (dev && *dev && g_str_has_prefix(uri, dev));
}

void OnMessage(WebKitUserContentManager*, WebKitJavascriptResult* result, gpointer) {
    if (!FromOurPage()) return;
    JSCValue* v = webkit_javascript_result_get_js_value(result);
    if (!jsc_value_is_string(v)) return;
    char* text = jsc_value_to_string(v);
    Value msg;
    const bool ok = bb::json::Parse(text, &msg) && msg.isObject();
    g_free(text);
    if (!ok) return;
    Value reply = Value::Object();
    reply["id"] = msg.find("id") ? *msg.find("id") : Value::Null();
    static const Value kEmpty = Value::Object();
    const Value* args = msg.find("args");
    std::string err;
    Value result2 = Dispatch(msg.getString("cmd"), args && args->isObject() ? *args : kEmpty, &err);
    reply["ok"] = Value::Bool(err.empty());
    if (err.empty()) reply["result"] = std::move(result2);
    else reply["error"] = Value::String(err);
    Reply(reply);
}

// bunbun-ui://app/<path>: a file from the built UI, never outside its folder.
void OnScheme(WebKitURISchemeRequest* req, gpointer) {
    std::string path = webkit_uri_scheme_request_get_path(req);
    if (path.empty() || path == "/") path = "/index.html";
    std::error_code ec;
    const auto root = std::filesystem::weakly_canonical(g_uiDir, ec);
    const auto file = std::filesystem::weakly_canonical(g_uiDir + path, ec);
    std::string data;
    const std::string rel = file.string().substr(0, root.string().size());
    if (ec || rel != root.string() || !bb::plat::ReadFile(file.string(), &data)) {
        GError* e = g_error_new(G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "not found: %s", path.c_str());
        webkit_uri_scheme_request_finish_error(req, e);
        g_error_free(e);
        return;
    }
    const std::string ext = file.extension().string();
    const char* mime = ext == ".html" ? "text/html"
                     : ext == ".js"   ? "text/javascript"
                     : ext == ".css"  ? "text/css"
                     : ext == ".svg"  ? "image/svg+xml"
                     : ext == ".png"  ? "image/png"
                     : ext == ".json" ? "application/json"
                     : ext == ".woff2" ? "font/woff2"
                     : "application/octet-stream";
    auto* bytes = new std::string(std::move(data));
    GInputStream* stream = g_memory_input_stream_new_from_data(bytes->data(), static_cast<gssize>(bytes->size()), nullptr);
    // The string lives as long as the stream reading from it.
    g_object_set_data_full(G_OBJECT(stream), "bunbun-bytes", bytes, [](gpointer p) { delete static_cast<std::string*>(p); });
    webkit_uri_scheme_request_finish(req, stream, static_cast<gint64>(bytes->size()), mime);
    g_object_unref(stream);
}

// Links leave the app: https opens in the browser, anything else is ignored.
gboolean OnPolicy(WebKitWebView*, WebKitPolicyDecision* decision, WebKitPolicyDecisionType type, gpointer) {
    if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION && type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION)
        return FALSE;
    WebKitNavigationAction* action = webkit_navigation_policy_decision_get_navigation_action(WEBKIT_NAVIGATION_POLICY_DECISION(decision));
    const char* uri = webkit_uri_request_get_uri(webkit_navigation_action_get_request(action));
    const char* dev = getenv("BUNBUN_UI_DEV");
    if (uri && (g_str_has_prefix(uri, "bunbun-ui://") || (dev && *dev && g_str_has_prefix(uri, dev)))) return FALSE;
    if (uri && g_str_has_prefix(uri, "https://")) bb::plat::OpenWithDefault(uri);
    webkit_policy_decision_ignore(decision);
    return TRUE;
}

// window.chrome.webview, as WebView2 provides it, on WebKit's message handlers.
constexpr char kBridgeScript[] = R"JS(
(() => {
  const listeners = [];
  window.chrome = window.chrome || {};
  window.chrome.webview = {
    postMessage: (m) => window.webkit.messageHandlers.bunbun.postMessage(JSON.stringify(m)),
    addEventListener: (type, fn) => { if (type === "message") listeners.push(fn); },
    removeEventListener: (type, fn) => { const i = listeners.indexOf(fn); if (i >= 0) listeners.splice(i, 1); },
  };
  window.__bunbunDeliver = (data) => listeners.forEach((fn) => fn({ data }));
})();
)JS";

}  // namespace

int main(int argc, char** argv) {
    gtk_init(&argc, &argv);
    g_uiDir = bb::plat::DataDir() + "/settings-ui";
    std::string start;
    for (int i = 1; i < argc; ++i)
        if (argv[i][0] != '-') start = argv[i];  // a page to open ("agents", "mascot")

    WebKitWebContext* context = webkit_web_context_get_default();
    webkit_web_context_register_uri_scheme(context, kScheme, OnScheme, nullptr, nullptr);
    WebKitSecurityManager* security = webkit_web_context_get_security_manager(context);
    webkit_security_manager_register_uri_scheme_as_secure(security, kScheme);
    webkit_security_manager_register_uri_scheme_as_cors_enabled(security, kScheme);

    WebKitUserContentManager* ucm = webkit_user_content_manager_new();
    webkit_user_content_manager_add_script(
        ucm, webkit_user_script_new(kBridgeScript, WEBKIT_USER_CONTENT_INJECT_TOP_FRAME, WEBKIT_USER_SCRIPT_INJECT_AT_DOCUMENT_START,
                                    nullptr, nullptr));
    g_signal_connect(ucm, "script-message-received::bunbun", G_CALLBACK(OnMessage), nullptr);
    webkit_user_content_manager_register_script_message_handler(ucm, "bunbun");

    g_view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_user_content_manager(ucm));
    WebKitSettings* ws = webkit_web_view_get_settings(g_view);
    webkit_settings_set_enable_developer_extras(ws, getenv("BUNBUN_DEVTOOLS") != nullptr);
    webkit_settings_set_enable_back_forward_navigation_gestures(ws, FALSE);
    g_signal_connect(g_view, "decide-policy", G_CALLBACK(OnPolicy), nullptr);
    // Dark until the page paints, so the window doesn't flash white.
    GdkRGBA bg{0.07, 0.07, 0.08, 1};
    webkit_web_view_set_background_color(g_view, &bg);

    GtkWidget* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_window_set_title(GTK_WINDOW(window), "Bunbun Settings");
    // The icon theme has it once installed; the AppImage and a build tree carry their own.
    const std::string iconFile = bb::plat::DataDir() + "/../icons/hicolor/256x256/apps/bunbun.png";
    if (gtk_icon_theme_has_icon(gtk_icon_theme_get_default(), "bunbun") || !bb::plat::FileExists(iconFile))
        gtk_window_set_icon_name(GTK_WINDOW(window), "bunbun");
    else
        gtk_window_set_icon_from_file(GTK_WINDOW(window), iconFile.c_str(), nullptr);
    gtk_window_set_default_size(GTK_WINDOW(window), 1120, 760);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(g_view));
    g_signal_connect(window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);

    const char* dev = getenv("BUNBUN_UI_DEV");
    std::string url = dev && *dev ? dev : std::string(kScheme) + "://app/index.html";
    if (!start.empty()) url += "#" + start;
    if (!(dev && *dev) && !bb::plat::FileExists(g_uiDir + "/index.html")) {
        const std::string msg = "<body style='font:15px sans-serif;background:#121214;color:#eee;padding:32px'>"
                                "The settings UI wasn't found in " + g_uiDir + ".<br>Edit " + bb::settings::Path() +
                                " directly, or reinstall Bunbun.</body>";
        webkit_web_view_load_html(g_view, msg.c_str(), nullptr);
    } else {
        webkit_web_view_load_uri(g_view, url.c_str());
    }
    gtk_widget_show_all(window);
    gtk_main();
    return 0;
}
