// --- Weather Fetching Helpers ---
std::string HttpGet(const wchar_t* host, const wchar_t* path, bool https = true) {
    std::string response;
    HINTERNET hSession = WinHttpOpen(L"DynamicIsland/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        Wh_Log(L"Weather HttpGet: WinHttpOpen failed with error %lu", GetLastError());
        return response;
    }
    // Bunbun: resolve, connect, send, receive (ms). Without them a weather service
    // that stops answering (wttr.in does, now and then) held every refresh for
    // minutes, so the fallback below never got its turn.
    WinHttpSetTimeouts(hSession, 5000, 5000, 8000, 10000);

    HINTERNET hConnect = WinHttpConnect(hSession, host, https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0);
        if (hRequest) {
            if (WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD size = 0;
                DWORD downloaded = 0;
                do {
                    if (WinHttpQueryDataAvailable(hRequest, &size) && size > 0) {
                        std::vector<char> buffer(size + 1);
                        if (WinHttpReadData(hRequest, buffer.data(), size, &downloaded)) {
                            buffer[downloaded] = '\0';
                            response.append(buffer.data());
                        } else {
                            Wh_Log(L"Weather HttpGet: WinHttpReadData failed with error %lu", GetLastError());
                        }
                    }
                } while (size > 0);
            } else {
                Wh_Log(L"Weather HttpGet: WinHttpSendRequest/ReceiveResponse failed with error %lu", GetLastError());
            }
            WinHttpCloseHandle(hRequest);
        } else {
            Wh_Log(L"Weather HttpGet: WinHttpOpenRequest failed with error %lu", GetLastError());
        }
        WinHttpCloseHandle(hConnect);
    } else {
        Wh_Log(L"Weather HttpGet: WinHttpConnect failed with error %lu", GetLastError());
    }
    WinHttpCloseHandle(hSession);
    return response;
}

// HTTP GET that distinguishes "the server answered" from "the transport failed".
// HttpGet() above returns an empty string for both, which is why a lyrics cache
// cannot be built on it: a dropped connection would look like "no lyrics".
struct HttpResult {
    bool ok = false;     // headers received AND the body was read to the end
    DWORD status = 0;    // HTTP status, valid only when ok
    std::string body;

    // A real answer: lyrics (200 with a body) or "no such track" (404). Anything
    // else (timeout, 429, 5xx, empty 200) is a failure that must never be cached.
    bool IsDefinitive() const {
        return ok && ((status == 200 && !body.empty()) || status == 404);
    }
};

HttpResult HttpGetEx(const wchar_t* host, const wchar_t* path, bool https, const wchar_t* userAgent,
                     const wchar_t* headers = nullptr) {
    HttpResult result;
    HINTERNET hSession = WinHttpOpen(userAgent, WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                     WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) {
        return result;
    }
    // resolve, connect, send, receive (ms). Snappy timeouts for responsive lookups.
    WinHttpSetTimeouts(hSession, 3000, 3000, 4000, 5000);

    HINTERNET hConnect = WinHttpConnect(hSession, host,
                                        https ? INTERNET_DEFAULT_HTTPS_PORT : INTERNET_DEFAULT_HTTP_PORT, 0);
    if (hConnect) {
        HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path, nullptr, WINHTTP_NO_REFERER,
                                                WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                https ? WINHTTP_FLAG_SECURE : 0);
        if (hRequest) {
            const DWORD hLen = headers ? static_cast<DWORD>(wcslen(headers)) : 0;
            if (WinHttpSendRequest(hRequest, headers ? headers : WINHTTP_NO_ADDITIONAL_HEADERS, hLen,
                                   WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hRequest, nullptr)) {
                DWORD status = 0;
                DWORD statusSize = sizeof(status);
                if (WinHttpQueryHeaders(hRequest,
                                        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize,
                                        WINHTTP_NO_HEADER_INDEX)) {
                    bool readOk = true;
                    for (;;) {
                        DWORD size = 0;
                        if (!WinHttpQueryDataAvailable(hRequest, &size)) {
                            readOk = false;
                            break;
                        }
                        if (size == 0) {
                            break;
                        }
                        std::vector<char> buffer(size);
                        DWORD downloaded = 0;
                        if (!WinHttpReadData(hRequest, buffer.data(), size, &downloaded)) {
                            readOk = false;
                            break;
                        }
                        result.body.append(buffer.data(), downloaded);
                        if (result.body.size() > 4u * 1024u * 1024u) {  // sanity cap
                            readOk = false;
                            break;
                        }
                    }
                    if (readOk) {
                        result.ok = true;
                        result.status = status;
                    }
                }
            }
            WinHttpCloseHandle(hRequest);
        }
        WinHttpCloseHandle(hConnect);
    }
    WinHttpCloseHandle(hSession);
    return result;
}

// Percent-encodes a city name for use as a wttr.in path segment. Only spaces
// used to be escaped, so a city containing a comma, an accent or any other
// non-ASCII character produced a malformed request; wttr.in then answered with
// an error page that the parser happily turned into "0 degrees".
std::wstring PercentEncodeUtf8(const std::wstring& text) {
    // Some users worked around the old space-only escaping by typing the escape
    // themselves ("New%20York"). Re-encoding that would send "New%2520York", so
    // an already-encoded value is passed through untouched. Only the exact
    // "%XX" shape counts, so a literal percent sign in a name still encodes.
    auto looksPreEncoded = [](const std::wstring& value) {
        const size_t pos = value.find(L'%');
        if (pos == std::wstring::npos || pos + 2 >= value.size()) {
            return false;
        }
        return iswxdigit(value[pos + 1]) && iswxdigit(value[pos + 2]);
    };
    if (looksPreEncoded(text)) {
        return text;
    }

    const int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (bytes <= 1) {
        return std::wstring();
    }

    std::string utf8(static_cast<size_t>(bytes - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), -1, utf8.data(), bytes, nullptr, nullptr);

    static constexpr wchar_t kHexDigits[] = L"0123456789ABCDEF";
    std::wstring out;
    out.reserve(utf8.size() * 3);
    for (const unsigned char c : utf8) {
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') ||
                                c == '-' || c == '_' || c == '.' || c == '~';
        if (unreserved) {
            out.push_back(static_cast<wchar_t>(c));
        } else {
            out.push_back(L'%');
            out.push_back(kHexDigits[(c >> 4) & 0x0f]);
            out.push_back(kHexDigits[c & 0x0f]);
        }
    }
    return out;
}

// ── Bunbun: weather from Open-Meteo ─────────────────────────────────────────
// Open-Meteo is free, needs no key and answers in well under a second (wttr.in,
// used before, went down for hours at a time and made every refresh wait).
// Where: a city typed in Ask Bun or set in Settings is found with Open-Meteo's
// geocoder; otherwise this PC's position comes from Windows' own location
// service (no IP lookup), and its name from the coordinates. Open-Meteo's WMO
// weather codes are mapped to the WWO codes the island's icons use.
struct OpenMeteoReport {
    std::wstring city, country;
    float temp = 0, feels = 0;
    int humidity = 0, wwo = 0;
    std::wstring desc, wind, windDir;
    float hi[2] = {}, lo[2] = {};
    int rain[2] = {}, dayWwo[2] = {};
    std::wstring dayDesc[2];
};

// WMO code (Open-Meteo) → WWO code (wttr.in, the icons) and words.
int WmoToWwo(int wmo, bool day, std::wstring* desc) {
    struct Map { int wmo, wwo; const wchar_t* text; };
    static const Map kMap[] = {
        {0, 113, L"Sunny"}, {1, 116, L"Mainly clear"}, {2, 116, L"Partly cloudy"}, {3, 122, L"Overcast"},
        {45, 248, L"Fog"}, {48, 260, L"Freezing fog"}, {51, 266, L"Light drizzle"}, {53, 266, L"Drizzle"},
        {55, 266, L"Heavy drizzle"}, {56, 281, L"Freezing drizzle"}, {57, 284, L"Heavy freezing drizzle"},
        {61, 296, L"Light rain"}, {63, 302, L"Moderate rain"}, {65, 308, L"Heavy rain"},
        {66, 311, L"Light freezing rain"}, {67, 314, L"Freezing rain"}, {71, 326, L"Light snow"},
        {73, 332, L"Moderate snow"}, {75, 338, L"Heavy snow"}, {77, 326, L"Snow grains"},
        {80, 353, L"Light rain shower"}, {81, 356, L"Rain shower"}, {82, 359, L"Torrential rain shower"},
        {85, 368, L"Light snow showers"}, {86, 371, L"Snow showers"}, {95, 389, L"Thunderstorm"},
        {96, 395, L"Thunderstorm with hail"}, {99, 395, L"Thunderstorm with heavy hail"},
    };
    for (const Map& m : kMap)
        if (m.wmo == wmo) {
            *desc = (wmo == 0 && !day) ? L"Clear" : m.text;
            return m.wwo;
        }
    *desc = L"Cloudy";
    return 119;
}

const wchar_t* CompassPoint(double degrees) {
    static const wchar_t* kPoints[] = {L"N", L"NNE", L"NE", L"ENE", L"E", L"ESE", L"SE", L"SSE",
                                       L"S", L"SSW", L"SW", L"WSW", L"W", L"WNW", L"NW", L"NNW"};
    const int i = static_cast<int>(std::floor(std::fmod(degrees + 371.25, 360.0) / 22.5)) % 16;
    return kPoints[i];
}

// This PC's position from Windows' location service (Wi-Fi based, usually a
// second or two), and its place name from the coordinates, cached for 30
// minutes. False when location is off for desktop apps (Settings → Privacy &
// security → Location).
bool DeviceLocation(double* lat, double* lon, std::wstring* city, std::wstring* country) {
    static std::mutex s_mutex;
    static double s_at = -1e9, s_lat = 0, s_lon = 0;
    static std::wstring s_city, s_country;
    std::lock_guard lock(s_mutex);
    if (NowSeconds() - s_at > 1800) {
        try {
            try {
                winrt::init_apartment(winrt::apartment_type::multi_threaded);
            } catch (...) {
            }  // this thread already has one
            namespace geo = winrt::Windows::Devices::Geolocation;
            geo::Geolocator locator;
            locator.DesiredAccuracy(geo::PositionAccuracy::Default);
            const geo::Geoposition pos = locator.GetGeopositionAsync(std::chrono::minutes(30), std::chrono::seconds(10)).get();
            const geo::BasicGeoposition p = pos.Coordinate().Point().Position();
            s_lat = p.Latitude;
            s_lon = p.Longitude;
            s_at = NowSeconds();
        } catch (...) {
            Wh_Log(L"Weather: Windows location isn't available (off for desktop apps?)");
            if (s_at < 0) return false;  // never had a position
        }
        // The place's name, from the coordinates only.
        wchar_t path[256];
        swprintf(path, 256, L"/data/reverse-geocode-client?latitude=%.4f&longitude=%.4f&localityLanguage=en", s_lat, s_lon);
        bb::json::Value v;
        const std::string res = HttpGet(L"api.bigdatacloud.net", path, true);
        if (!res.empty() && bb::json::Parse(res, &v)) {
            std::string name = v.getString("city");
            if (name.empty()) name = v.getString("locality");
            s_city = bb::todos::FromUtf8(name);
            s_country = bb::todos::FromUtf8(v.getString("countryName"));
        }
    }
    *lat = s_lat;
    *lon = s_lon;
    *city = s_city;
    *country = s_country;
    return true;
}

// Where to look: a place name through Open-Meteo's geocoder, or (empty) here.
bool LocateForWeather(const std::wstring& place, double* lat, double* lon, std::wstring* city, std::wstring* country,
                      bool* noLocation) {
    if (place.empty()) {
        const bool ok = DeviceLocation(lat, lon, city, country);
        if (noLocation) *noLocation = !ok;
        return ok;
    }
    const std::wstring path = L"/v1/search?count=1&language=en&format=json&name=" + PercentEncodeUtf8(place);
    const std::string res = HttpGet(L"geocoding-api.open-meteo.com", path.c_str(), true);
    bb::json::Value v;
    if (res.empty() || !bb::json::Parse(res, &v)) return false;
    const bb::json::Value* results = v.find("results");
    if (!results || !results->isArray() || results->arr.empty()) return false;
    const bb::json::Value& r = results->arr.front();
    *lat = r.getNumber("latitude", 1000);
    *lon = r.getNumber("longitude", 1000);
    *city = bb::todos::FromUtf8(r.getString("name"));
    *country = bb::todos::FromUtf8(r.getString("country"));
    return *lat < 1000 && *lon < 1000;
}

bool FetchOpenMeteo(const std::wstring& place, bool fahrenheit, OpenMeteoReport* out, bool* noLocation = nullptr) {
    double lat = 0, lon = 0;
    if (!LocateForWeather(place, &lat, &lon, &out->city, &out->country, noLocation)) {
        Wh_Log(L"Weather: couldn't locate \"%s\"", place.c_str());
        return false;
    }
    wchar_t path[512];
    swprintf(path, 512,
             L"/v1/forecast?latitude=%.4f&longitude=%.4f&timezone=auto&forecast_days=2"
             L"&current=temperature_2m,apparent_temperature,relative_humidity_2m,weather_code,is_day,wind_speed_10m,wind_direction_10m"
             L"&daily=temperature_2m_max,temperature_2m_min,precipitation_probability_max,weather_code%ls",
             lat, lon, fahrenheit ? L"&temperature_unit=fahrenheit&wind_speed_unit=mph" : L"");
    const std::string res = HttpGet(L"api.open-meteo.com", path, true);
    bb::json::Value v;
    if (res.empty() || !bb::json::Parse(res, &v)) return false;
    const bb::json::Value* cur = v.find("current");
    const bb::json::Value* daily = v.find("daily");
    if (!cur || !cur->isObject() || !cur->find("temperature_2m")) return false;
    out->temp = static_cast<float>(cur->getNumber("temperature_2m"));
    out->feels = static_cast<float>(cur->getNumber("apparent_temperature"));
    out->humidity = static_cast<int>(std::lround(cur->getNumber("relative_humidity_2m")));
    out->wwo = WmoToWwo(static_cast<int>(cur->getNumber("weather_code")), cur->getNumber("is_day", 1) != 0, &out->desc);
    out->wind = std::to_wstring(std::lround(cur->getNumber("wind_speed_10m")));
    out->windDir = CompassPoint(cur->getNumber("wind_direction_10m"));
    auto day = [&](const char* key, size_t i) -> double {
        const bb::json::Value* a = daily ? daily->find(key) : nullptr;
        return a && a->isArray() && a->arr.size() > i && a->arr[i].isNumber() ? a->arr[i].num : 0.0;
    };
    for (size_t i = 0; i < 2; ++i) {
        out->hi[i] = static_cast<float>(day("temperature_2m_max", i));
        out->lo[i] = static_cast<float>(day("temperature_2m_min", i));
        out->rain[i] = static_cast<int>(day("precipitation_probability_max", i));
        out->dayWwo[i] = WmoToWwo(static_cast<int>(day("weather_code", i)), true, &out->dayDesc[i]);
    }
    return true;
}

// Bunbun: Ask Bun "weather", "weather in Mumbai", "rain tomorrow in Pune".
// `place` empty = the Weather city setting, else here. Day 0 today, 1 tomorrow.
// Blocking: call it from a worker thread.
bool FetchWeatherReport(const std::wstring& place, int days, std::wstring* title, std::wstring* subtitle, std::wstring* body) {
    const Settings settings = GetSettingsCopy();
    const bool f = settings.weatherFahrenheit;
    const std::wstring where = place.empty() ? settings.weatherCity : place;
    OpenMeteoReport r;
    bool noLocation = false;
    if (!FetchOpenMeteo(where, f, &r, &noLocation)) {
        if (!noLocation) return false;
        *title = L"Weather";
        *subtitle = L"Location is off";
        *body = L"Turn on location for desktop apps (Settings → Privacy & security → Location), or ask for a city: "
                L"“weather in Mumbai”.";
        return true;
    }
    // "Mumbai, India": the place as asked, with the country so a misheard place shows.
    std::wstring name = where.empty() ? r.city : where;
    for (size_t i = 0; i < name.size(); ++i)
        if (i == 0 || name[i - 1] == L' ') name[i] = towupper(name[i]);
    if (name.empty()) name = L"Here";
    if (!r.country.empty() && r.country != name) name += L", " + r.country;
    *title = name;
    const wchar_t* unit = f ? L"F" : L"C";
    wchar_t buf[512];
    if (days == 0) {
        *subtitle = L"Weather now · Open-Meteo";
        swprintf(buf, 512, L"%.0f°%ls and %ls. Feels like %.0f°, humidity %d%%. Today %.0f° / %.0f°.", r.temp, unit,
                 r.desc.c_str(), r.feels, r.humidity, r.hi[0], r.lo[0]);
    } else {
        *subtitle = L"Tomorrow · Open-Meteo";
        swprintf(buf, 512, L"%.0f° / %.0f°%ls, %ls. Chance of rain up to %d%%.", r.hi[1], r.lo[1], unit, r.dayDesc[1].c_str(),
                 r.rain[1]);
    }
    *body = buf;
    return true;
}

DWORD WINAPI WeatherThreadProc(void*) {
    // Initial delay to avoid slowing down startup
    WaitForSingleObject(g_stopEvent, 3000);
    // Bunbun: Windows' location service is a WinRT API.
    try {
        winrt::init_apartment(winrt::apartment_type::multi_threaded);
    } catch (...) {
    }

    while (WaitForSingleObject(g_stopEvent, 0) == WAIT_TIMEOUT) {
        std::wstring cityOverride;
        bool isFahrenheit = false;
        bool weatherEnabled = true;
        {
            // g_settingsMutex, not g_stateMutex: all three values below live in
            // g_settings, so the old g_stateMutex here guarded nothing and left
            // the weatherCity string copy racing against LoadSettings().
            std::lock_guard lock(g_settingsMutex);
            weatherEnabled = g_settings.weather;
            cityOverride = g_settings.weatherCity;
            isFahrenheit = g_settings.weatherFahrenheit;
        }

        if (!weatherEnabled) {
            WaitForSingleObject(g_stopEvent, 2000);
            continue;
        }

        // Bunbun: Open-Meteo for the set city, or for here (Windows location).
        OpenMeteoReport r;
        const bool weatherCommitted = FetchOpenMeteo(cityOverride, isFahrenheit, &r);
        if (weatherCommitted) {
            Wh_Log(L"Weather: city=%s, temp=%.1f, desc=%s", r.city.c_str(), r.temp, r.desc.c_str());
            std::lock_guard lock(g_stateMutex);
            g_state.weather.hasData = true;
            g_state.weather.temperature = r.temp;
            g_state.weather.weatherCode = r.wwo;
            g_state.weather.city = cityOverride.empty() ? (r.city.empty() ? std::wstring(L"Local Weather") : r.city) : cityOverride;
            g_state.weather.weatherDesc = r.desc;
            g_state.weather.windSpeed = r.wind;
            g_state.weather.windDir = r.windDir;
            g_state.weather.humidity = std::to_wstring(r.humidity);
            wchar_t feels[16];
            swprintf(feels, 16, L"%.0f", r.feels);
            g_state.weather.feelsLike = feels;
            g_state.weather.lastUpdated = NowSeconds();
        }

        // Retry soon after a failure instead of leaving the dashboard empty for
        // a full refresh interval, but back off on repeated failures.
        static const DWORD kBackoffMs[] = {30 * 1000, 2 * 60 * 1000, 5 * 60 * 1000, 15 * 60 * 1000};
        static size_t backoffIndex = 0;

        DWORD waitMs;
        if (weatherCommitted) {
            backoffIndex = 0;
            waitMs = 15 * 60 * 1000;
        } else {
            waitMs = kBackoffMs[backoffIndex];
            if (backoffIndex + 1 < ARRAYSIZE(kBackoffMs)) {
                ++backoffIndex;
            }
        }

        HANDLE events[] = {g_stopEvent, g_settingsChangedEvent};
        DWORD waitResult = WaitForMultipleObjects(2, events, FALSE, waitMs);
        if (waitResult == WAIT_OBJECT_0) {
            break;
        }
        if (waitResult == WAIT_OBJECT_0 + 1) {
            // The city changed, so retry immediately at full speed.
            backoffIndex = 0;
        }
    }
    return 0;
}
