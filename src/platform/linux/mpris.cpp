// Bunbun — MPRIS players (see mpris.h).
#include "mpris.h"

#include <gio/gio.h>

#include <map>
#include <memory>

namespace bb::mpris {
namespace {

constexpr char kPrefix[] = "org.mpris.MediaPlayer2.";
constexpr char kPath[] = "/org/mpris/MediaPlayer2";

struct Player {
    std::string bus;            // org.mpris.MediaPlayer2.spotify (or .instance123)
    GDBusProxy* player = nullptr;  // org.mpris.MediaPlayer2.Player
    std::string identity;
    gint64 lastPlaying = 0;     // monotonic µs of the last Playing we saw
};

GDBusConnection* g_bus = nullptr;
guint g_ownerSub = 0;
std::map<std::string, std::unique_ptr<Player>> g_players;
std::function<void()> g_onChange;

std::string StringProp(GDBusProxy* proxy, const char* name) {
    GVariant* v = g_dbus_proxy_get_cached_property(proxy, name);
    std::string out;
    if (v && g_variant_is_of_type(v, G_VARIANT_TYPE_STRING)) out = g_variant_get_string(v, nullptr);
    if (v) g_variant_unref(v);
    return out;
}

bool IsPlaying(const Player& p) { return p.player && StringProp(p.player, "PlaybackStatus") == "Playing"; }

void Changed() {
    for (auto& [bus, p] : g_players)
        if (IsPlaying(*p)) p->lastPlaying = g_get_monotonic_time();
    if (g_onChange) g_onChange();
}

void OnProps(GDBusProxy*, GVariant*, GStrv, gpointer) { Changed(); }

// The app's own name (Identity), read once from the root interface.
void ReadIdentity(Player* p) {
    GVariant* r = g_dbus_connection_call_sync(g_bus, p->bus.c_str(), kPath, "org.freedesktop.DBus.Properties", "Get",
                                              g_variant_new("(ss)", "org.mpris.MediaPlayer2", "Identity"),
                                              G_VARIANT_TYPE("(v)"), G_DBUS_CALL_FLAGS_NONE, 500, nullptr, nullptr);
    if (!r) return;
    GVariant* inner = nullptr;
    g_variant_get(r, "(v)", &inner);
    if (inner && g_variant_is_of_type(inner, G_VARIANT_TYPE_STRING)) p->identity = g_variant_get_string(inner, nullptr);
    if (inner) g_variant_unref(inner);
    g_variant_unref(r);
}

void Add(const std::string& bus) {
    if (g_players.count(bus)) return;
    auto p = std::make_unique<Player>();
    p->bus = bus;
    p->player = g_dbus_proxy_new_sync(g_bus, G_DBUS_PROXY_FLAGS_GET_INVALIDATED_PROPERTIES, nullptr, bus.c_str(), kPath,
                                      "org.mpris.MediaPlayer2.Player", nullptr, nullptr);
    if (!p->player) return;
    g_signal_connect(p->player, "g-properties-changed", G_CALLBACK(OnProps), nullptr);
    ReadIdentity(p.get());
    if (p->identity.empty()) p->identity = bus.substr(sizeof kPrefix - 1);
    if (IsPlaying(*p)) p->lastPlaying = g_get_monotonic_time();
    g_players[bus] = std::move(p);
}

void Remove(const std::string& bus) {
    auto it = g_players.find(bus);
    if (it == g_players.end()) return;
    g_object_unref(it->second->player);
    g_players.erase(it);
}

void OnOwnerChanged(GDBusConnection*, const gchar*, const gchar*, const gchar*, const gchar*, GVariant* params, gpointer) {
    const gchar *name = nullptr, *oldOwner = nullptr, *newOwner = nullptr;
    g_variant_get(params, "(&s&s&s)", &name, &oldOwner, &newOwner);
    if (!g_str_has_prefix(name, kPrefix)) return;
    if (newOwner && *newOwner) Add(name);
    else Remove(name);
    Changed();
}

Player* Best() {
    Player* best = nullptr;
    for (auto& [bus, p] : g_players) {
        if (!p->player) continue;
        const std::string status = StringProp(p->player, "PlaybackStatus");
        if (status == "Stopped" || status.empty()) continue;
        const bool playing = status == "Playing";
        if (!best) { best = p.get(); continue; }
        const bool bestPlaying = IsPlaying(*best);
        if ((playing && !bestPlaying) || (playing == bestPlaying && p->lastPlaying > best->lastPlaying)) best = p.get();
    }
    return best;
}

void Call(const char* method) {
    if (Player* p = Best())
        g_dbus_proxy_call(p->player, method, nullptr, G_DBUS_CALL_FLAGS_NONE, 1000, nullptr, nullptr, nullptr);
}

}  // namespace

void Start(std::function<void()> onChange) {
    g_onChange = std::move(onChange);
    g_bus = g_bus_get_sync(G_BUS_TYPE_SESSION, nullptr, nullptr);
    if (!g_bus) return;  // no session bus: no media, the island still runs
    // arg0namespace takes the namespace without its trailing dot.
    g_ownerSub = g_dbus_connection_signal_subscribe(g_bus, "org.freedesktop.DBus", "org.freedesktop.DBus", "NameOwnerChanged",
                                                    "/org/freedesktop/DBus", "org.mpris.MediaPlayer2",
                                                    G_DBUS_SIGNAL_FLAGS_MATCH_ARG0_NAMESPACE, OnOwnerChanged, nullptr, nullptr);
    GVariant* r = g_dbus_connection_call_sync(g_bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
                                              "ListNames", nullptr, G_VARIANT_TYPE("(as)"), G_DBUS_CALL_FLAGS_NONE, 1000,
                                              nullptr, nullptr);
    if (r) {
        GVariantIter* it = nullptr;
        const gchar* name = nullptr;
        g_variant_get(r, "(as)", &it);
        while (g_variant_iter_loop(it, "&s", &name))
            if (g_str_has_prefix(name, kPrefix)) Add(name);
        g_variant_iter_free(it);
        g_variant_unref(r);
    }
    Changed();
}

void Stop() {
    while (!g_players.empty()) Remove(g_players.begin()->first);
    if (g_bus && g_ownerSub) g_dbus_connection_signal_unsubscribe(g_bus, g_ownerSub);
    g_ownerSub = 0;
    if (g_bus) g_object_unref(g_bus);
    g_bus = nullptr;
}

Track Current() {
    Track t;
    Player* p = Best();
    if (!p) return t;
    t.active = true;
    t.playing = IsPlaying(*p);
    t.player = p->identity;
    if (GVariant* md = g_dbus_proxy_get_cached_property(p->player, "Metadata")) {
        GVariantDict dict;
        g_variant_dict_init(&dict, md);
        const gchar* s = nullptr;
        if (g_variant_dict_lookup(&dict, "xesam:title", "&s", &s)) t.title = s;
        if (g_variant_dict_lookup(&dict, "xesam:album", "&s", &s)) t.album = s;
        if (g_variant_dict_lookup(&dict, "mpris:artUrl", "&s", &s)) t.artUrl = s;
        if (GVariant* artists = g_variant_dict_lookup_value(&dict, "xesam:artist", G_VARIANT_TYPE_STRING_ARRAY)) {
            gsize n = 0;
            const gchar** list = g_variant_get_strv(artists, &n);
            for (gsize i = 0; i < n; ++i) t.artist += (i ? ", " : "") + std::string(list[i]);
            g_free(list);
            g_variant_unref(artists);
        }
        if (GVariant* len = g_variant_dict_lookup_value(&dict, "mpris:length", nullptr)) {
            if (g_variant_is_of_type(len, G_VARIANT_TYPE_INT64)) t.length = g_variant_get_int64(len) / 1e6;
            else if (g_variant_is_of_type(len, G_VARIANT_TYPE_UINT64)) t.length = g_variant_get_uint64(len) / 1e6;
            g_variant_unref(len);
        }
        g_variant_dict_clear(&dict);
        g_variant_unref(md);
    }
    if (t.title.empty() && !t.playing) t.active = false;  // a paused player with nothing loaded
    return t;
}

void PlayPause() { Call("PlayPause"); }
void Next() { Call("Next"); }
void Previous() { Call("Previous"); }

}  // namespace bb::mpris
