#include "beszel_api.h"
#include "gui.h"
#include "config.h"
#include "credentials.h"
#include <HTTPClient.h>
#include <WiFi.h>

BeszelSystem beszel_systems[BESZEL_MAX_SYSTEMS];
int beszel_system_count = 0;

// Cached PocketBase auth token. Beszel's systems/containers collections are
// owner-scoped, so every request must carry this. Empty == not logged in;
// cleared and re-fetched automatically on a 401.
static String g_token;
static WiFiClient g_realtime;
static String g_realtime_line;
static String g_realtime_event;
static String g_realtime_client_id;
static String g_realtime_chunk_line;
static bool g_realtime_headers = false;
static bool g_realtime_chunked = false;
static bool g_realtime_subscribed = false;
static int g_realtime_system_index = -2;
static long g_realtime_chunk_remaining = -1;
static uint8_t g_realtime_chunk_trailer = 0;
static unsigned long g_realtime_attempt = 0;
static unsigned long g_realtime_subscription_attempt = 0;

// How many dashboard pages the GUI has currently built, so we only rebuild when
// the set of monitored systems actually changes (rare).
static int g_built_count = -1;

// VRAM and total-RAM aren't in the realtime `info` summary -- they only live in
// the slower system_stats time-series. We pull those on a longer interval and
// cache the distilled values by system id so the fast 2s info refresh can merge
// them in without wiping them.
#define BESZEL_STATS_INTERVAL 1000
struct StatsCache
{
    char id[20];
    float cpu;
    float mem;
    float disk;
    float bw;
    float load1;
    float memTotalGB;
    float vram;
    bool hasLive;
    bool hasMemTotal;
    bool hasVram;
};
static StatsCache g_stats[BESZEL_MAX_SYSTEMS];
static int g_stats_count = 0;

static String beszelBaseUrl()
{
    return "http://" + beszel_host + ":" + String(beszel_port);
}

// POST the configured credentials to PocketBase and cache the returned token.
static bool beszelLogin()
{
    if (WiFi.status() != WL_CONNECTED)
        return false;

    HTTPClient http;
    http.begin(beszelBaseUrl() + "/api/collections/users/auth-with-password");
    http.addHeader("Content-Type", "application/json");

    StaticJsonDocument<192> body;
    body["identity"] = BESZEL_USERNAME;
    body["password"] = BESZEL_PASSWORD;
    String out;
    serializeJson(body, out);

    int code = http.POST(out);
    if (code != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }
    String payload = http.getString();
    http.end();

    // The response also carries the full user record; keep just the token.
    StaticJsonDocument<32> filter;
    filter["token"] = true;
    StaticJsonDocument<512> doc;
    if (deserializeJson(doc, payload, DeserializationOption::Filter(filter)))
        return false;

    const char *t = doc["token"] | "";
    if (!t[0])
        return false;
    g_token = t;
    return true;
}

// Authenticated GET. Logs in on demand and retries once if the token expired
// (401). Returns 200 on success (doc populated), or a negative/HTTP error code.
static int beszelGet(const String &endpoint, JsonDocument &doc, const JsonDocument *filter)
{
    if (WiFi.status() != WL_CONNECTED)
        return -1;
    if (g_token.isEmpty() && !beszelLogin())
        return -2;

    for (int attempt = 0; attempt < 2; attempt++)
    {
        HTTPClient http;
        http.begin(beszelBaseUrl() + endpoint);
        http.addHeader("Authorization", g_token);
        int code = http.GET();

        if (code == 401)
        {
            http.end();
            g_token = "";
            if (!beszelLogin())
                return -2;
            continue; // retry once with the fresh token
        }
        if (code != HTTP_CODE_OK)
        {
            http.end();
            return code;
        }

        String payload = http.getString();
        http.end();
        DeserializationError err = filter
                                       ? deserializeJson(doc, payload, DeserializationOption::Filter(*filter))
                                       : deserializeJson(doc, payload);
        return err ? -3 : HTTP_CODE_OK;
    }
    return -1;
}

// Pull the most recent system_stats records and cache each system's total RAM
// and VRAM. We grab a few records per system (sort=-created) and keep the first
// one seen per system id. A filter trims the (large) stats blob to just m + g.
static void fetchStats()
{
    static DynamicJsonDocument doc(8192);
    doc.clear();

    StaticJsonDocument<256> filter;
    JsonObject item = filter["items"].createNestedObject();
    item["system"] = true;
    JsonObject stats = item.createNestedObject("stats");
    stats["cpu"] = true;
    stats["mp"] = true;
    stats["dp"] = true;
    stats["b"] = true;
    stats["la"] = true;
    stats["m"] = true;
    stats["g"] = true;

    const String endpoint =
        "/api/collections/system_stats/records?perPage=" + String(BESZEL_MAX_SYSTEMS * 3) +
        "&skipTotal=1&sort=-created&fields=system,stats";
    if (beszelGet(endpoint, doc, &filter) != HTTP_CODE_OK)
        return;

    g_stats_count = 0;
    for (JsonObject it : doc["items"].as<JsonArray>())
    {
        if (g_stats_count >= BESZEL_MAX_SYSTEMS)
            break;
        const char *sid = it["system"] | "";
        if (!sid[0])
            continue;

        bool seen = false;
        for (int k = 0; k < g_stats_count; k++)
            if (strcmp(g_stats[k].id, sid) == 0)
                seen = true;
        if (seen)
            continue; // already have this system's latest record

        StatsCache &c = g_stats[g_stats_count];
        strlcpy(c.id, sid, sizeof(c.id));
        JsonObject st = it["stats"];
        c.hasLive = true;
        c.cpu = st["cpu"] | 0.0f;
        c.mem = st["mp"] | 0.0f;
        c.disk = st["dp"] | 0.0f;
        c.load1 = st["la"][0] | 0.0f;
        c.bw = 0.0f;
        for (JsonVariant rate : st["b"].as<JsonArray>())
            c.bw += rate.as<float>(); // Beszel reports receive + transmit separately.
        c.hasMemTotal = st.containsKey("m");
        c.memTotalGB = st["m"] | 0.0f;
        c.hasVram = false;
        c.vram = 0.0f;
        JsonObject g = st["g"];
        if (!g.isNull())
        {
            for (JsonPair gpu : g) // first GPU only
            {
                float mu = gpu.value()["mu"] | 0.0f;
                float mt = gpu.value()["mt"] | 0.0f;
                if (mt > 0.0f)
                {
                    c.vram = mu / mt * 100.0f;
                    c.hasVram = true;
                }
                break;
            }
        }
        g_stats_count++;
    }
}

// Copy any cached stats (VRAM, total RAM) for `id` onto a system struct.
static void mergeStats(BeszelSystem &s)
{
    s.hasMemTotal = false;
    s.memTotalGB = 0.0f;
    s.hasVram = false;
    s.vram = 0.0f;
    for (int k = 0; k < g_stats_count; k++)
    {
        if (strcmp(g_stats[k].id, s.id) == 0)
        {
            if (g_stats[k].hasLive)
            {
                s.cpu = g_stats[k].cpu;
                s.mem = g_stats[k].mem;
                s.disk = g_stats[k].disk;
                s.bw = g_stats[k].bw;
                s.load1 = g_stats[k].load1;
            }
            s.hasMemTotal = g_stats[k].hasMemTotal;
            s.memTotalGB = g_stats[k].memTotalGB;
            s.hasVram = g_stats[k].hasVram;
            s.vram = g_stats[k].vram;
            return;
        }
    }
}

// Pull the systems list and distill each record's compact `info` block into the
// beszel_systems array.
static bool fetchSystems()
{
    static StaticJsonDocument<8192> doc; // static storage: keep it off the stack
    const String endpoint =
        "/api/collections/systems/records?perPage=" + String(BESZEL_MAX_SYSTEMS) +
        "&skipTotal=1&sort=%2Bname&fields=id,name,host,status,info";

    int result = beszelGet(endpoint, doc, nullptr);
    if (result != HTTP_CODE_OK)
    {
        Serial.printf("Beszel systems refresh failed: %d\n", result);
        return false;
    }

    beszel_system_count = 0;
    for (JsonObject it : doc["items"].as<JsonArray>())
    {
        if (beszel_system_count >= BESZEL_MAX_SYSTEMS)
            break;
        BeszelSystem &s = beszel_systems[beszel_system_count];
        strlcpy(s.id, it["id"] | "", sizeof(s.id));
        strlcpy(s.name, it["name"] | "?", sizeof(s.name));
        strlcpy(s.host, it["host"] | "", sizeof(s.host));
        s.up = strcmp(it["status"] | "", "up") == 0;

        JsonObject info = it["info"];
        s.cpu = info["cpu"] | 0.0f;
        s.mem = info["mp"] | 0.0f;
        s.disk = info["dp"] | 0.0f;
        s.hasTemp = info.containsKey("dt");
        s.temp = info["dt"] | 0.0f;
        s.hasGpu = info.containsKey("g");
        s.gpu = info["g"] | 0.0f;
        s.bw = info["bb"] | 0.0f;
        s.load1 = info["la"][0] | 0.0f;
        s.cores = info["t"] | 0;
        s.uptime = info["u"] | 0UL;
        mergeStats(s); // fold in cached VRAM / total RAM
        beszel_system_count++;
    }
    return true;
}

static void applyRealtimeRecord(JsonObject record)
{
    const char *id = record["system"] | record["id"] | "";
    for (int i = 0; i < beszel_system_count; i++)
    {
        if (strcmp(beszel_systems[i].id, id) != 0)
            continue;
        BeszelSystem &s = beszel_systems[i];
        JsonObject info = record["info"];
        JsonObject stats = record["stats"];
        JsonObject data = !stats.isNull() ? stats : info;
        if (data.isNull())
            return;
        s.cpu = data["cpu"] | s.cpu;
        s.mem = data["mp"] | s.mem;
        s.disk = data["dp"] | s.disk;
        s.load1 = data["la"][0] | s.load1;
        if (!stats.isNull())
        {
            float bw = 0;
            for (JsonVariant rate : stats["b"].as<JsonArray>())
                bw += rate.as<float>();
            if (!stats["b"].isNull())
                s.bw = bw;
        }
        else
            s.bw = info["bb"] | s.bw;
        gui_update_dashboard(i, s);
        return;
    }
}

static void applyRealtimeMetrics(const String &event, JsonObject data)
{
    const String marker = "%22system%22%3A%22";
    int start = event.indexOf(marker);
    if (start < 0)
        return;
    start += marker.length();
    int end = event.indexOf("%22", start);
    if (end < 0)
        return;
    String id = event.substring(start, end);
    for (int i = 0; i < beszel_system_count; i++)
    {
        if (id != beszel_systems[i].id)
            continue;
        // A page may change while an event from the previous subscription is
        // still buffered. Never apply that stale page's payload.
        if (i != gui_active_system_index())
            return;
        BeszelSystem &s = beszel_systems[i];
        JsonObject stats = data["stats"];
        s.cpu = stats["cpu"] | s.cpu;
        s.mem = stats["mp"] | s.mem;
        s.disk = stats["dp"] | s.disk;
        s.load1 = stats["la"][0] | s.load1;
        float bw = 0;
        for (JsonVariant rate : stats["b"].as<JsonArray>())
            bw += rate.as<float>();
        if (!stats["b"].isNull())
            s.bw = bw;
        gui_update_dashboard(i, s);
        return;
    }
}

static void subscribeRealtime(const String &clientId, int activeSystem)
{
    DynamicJsonDocument doc(2048);
    doc["clientId"] = clientId;
    JsonArray topics = doc.createNestedArray("subscriptions");
    topics.add("systems/*?options={\"query\":{\"fields\":\"id,name,host,port,info,status\"}}");
    if (activeSystem >= 0 && activeSystem < beszel_system_count)
    {
        String topic = "rt_metrics?options=%7B%22query%22%3A%7B%22system%22%3A%22";
        topic += beszel_systems[activeSystem].id;
        topic += "%22%7D%7D";
        topics.add(topic);
    }
    String body;
    serializeJson(doc, body);
    HTTPClient http;
    http.begin(beszelBaseUrl() + "/api/realtime");
    http.addHeader("Authorization", g_token);
    http.addHeader("Content-Type", "application/json");
    g_realtime_subscription_attempt = millis();
    g_realtime_subscribed = (http.POST(body) == HTTP_CODE_NO_CONTENT);
    if (g_realtime_subscribed)
    {
        g_realtime_system_index = activeSystem;
        if (activeSystem >= 0)
            Serial.printf("Beszel realtime active: %s (%s)\n",
                          beszel_systems[activeSystem].name,
                          beszel_systems[activeSystem].id);
        else
            Serial.println("Beszel realtime active: containers (metrics paused)");
    }
    else
        Serial.println("Beszel realtime subscription: failed");
    http.end();
}

static void processRealtimeSseLine()
{
    g_realtime_line.trim();
    if (g_realtime_line.startsWith("event:"))
    {
        g_realtime_event = g_realtime_line.substring(6);
        g_realtime_event.trim();
    }
    else if (g_realtime_line.startsWith("data:"))
    {
        DynamicJsonDocument doc(4096);
        String payload = g_realtime_line.substring(5);
        payload.trim();
        StaticJsonDocument<256> filter;
        if (g_realtime_event == "PB_CONNECT")
            filter["clientId"] = true;
        else if (g_realtime_event.startsWith("rt_metrics"))
        {
            JsonObject stats = filter.createNestedObject("stats");
            stats["cpu"] = true;
            stats["mp"] = true;
            stats["dp"] = true;
            stats["la"] = true;
            stats["b"] = true;
        }
        else
            filter["record"] = true;
        DeserializationError error = deserializeJson(
            doc, payload, DeserializationOption::Filter(filter));
        if (!error)
        {
            if (g_realtime_event == "PB_CONNECT")
            {
                Serial.println("Beszel realtime handshake received");
                g_realtime_client_id = doc["clientId"].as<String>();
                subscribeRealtime(g_realtime_client_id, gui_active_system_index());
            }
            else if (g_realtime_event.startsWith("rt_metrics"))
                applyRealtimeMetrics(g_realtime_event, doc.as<JsonObject>());
            else
                applyRealtimeRecord(doc["record"].as<JsonObject>());
        }
        else
            Serial.printf("Beszel realtime JSON error: %s (%u bytes)\n",
                          error.c_str(), payload.length());
    }
    g_realtime_line = "";
}

static void processRealtimeSseChar(char ch)
{
    if (ch == '\n')
        processRealtimeSseLine();
    else
        g_realtime_line += ch;
}

void updateBeszelRealtime()
{
    if (WiFi.status() != WL_CONNECTED)
        return;
    if (!g_realtime.connected())
    {
        if (millis() - g_realtime_attempt < 5000)
            return;
        g_realtime_attempt = millis();
        g_realtime.stop();
        g_realtime_headers = false;
        g_realtime_chunked = false;
        g_realtime_subscribed = false;
        g_realtime_client_id = "";
        g_realtime_system_index = -2;
        g_realtime_chunk_remaining = -1;
        g_realtime_chunk_trailer = 0;
        g_realtime_chunk_line = "";
        g_realtime_line = "";
        g_realtime_line.reserve(8192);
        if (g_token.isEmpty() && !beszelLogin())
            return;
        if (!g_realtime.connect(beszel_host.c_str(), beszel_port))
        {
            Serial.println("Beszel realtime socket: failed");
            return;
        }
        Serial.println("Beszel realtime socket: open");
        g_realtime.print("GET /api/realtime HTTP/1.1\r\nHost: " + beszel_host + "\r\nAuthorization: " + g_token + "\r\nAccept: text/event-stream\r\nCache-Control: no-cache\r\nConnection: keep-alive\r\n\r\n");
    }
    for (int n = 0; n < 192 && g_realtime.available(); n++)
    {
        char ch = (char)g_realtime.read();
        if (!g_realtime_headers)
        {
            if (ch != '\n')
            {
                g_realtime_line += ch;
                continue;
            }
            g_realtime_line.trim();
            if (g_realtime_line.equalsIgnoreCase("Transfer-Encoding: chunked"))
                g_realtime_chunked = true;
            if (g_realtime_line.isEmpty())
                g_realtime_headers = true;
            g_realtime_line = "";
            continue;
        }

        if (!g_realtime_chunked)
        {
            processRealtimeSseChar(ch);
            continue;
        }

        if (g_realtime_chunk_trailer > 0)
        {
            g_realtime_chunk_trailer--;
            if (g_realtime_chunk_trailer == 0)
                g_realtime_chunk_remaining = -1;
            continue;
        }
        if (g_realtime_chunk_remaining < 0)
        {
            if (ch != '\n')
            {
                g_realtime_chunk_line += ch;
                continue;
            }
            g_realtime_chunk_line.trim();
            g_realtime_chunk_remaining = strtoul(g_realtime_chunk_line.c_str(), nullptr, 16);
            g_realtime_chunk_line = "";
            if (g_realtime_chunk_remaining == 0)
            {
                g_realtime.stop();
                break;
            }
            continue;
        }

        processRealtimeSseChar(ch);
        g_realtime_chunk_remaining--;
        if (g_realtime_chunk_remaining == 0)
            g_realtime_chunk_trailer = 2;
    }

    const int activeSystem = gui_active_system_index();
    if (!g_realtime_client_id.isEmpty() &&
        activeSystem != g_realtime_system_index &&
        millis() - g_realtime_subscription_attempt >= 250)
        subscribeRealtime(g_realtime_client_id, activeSystem);
}

// Pick a status colour from the container's Docker status string.
static uint32_t containerColor(const char *status)
{
    if (strncmp(status, "Up", 2) == 0)
        return 0x33D17A; // green: running
    if (strstr(status, "Restarting") || strstr(status, "Created") || strstr(status, "Paused"))
        return 0xE5A50A; // amber: transient
    return 0xE0504F;     // red: exited/dead/...
}

// Build the combined container page: every container across all systems, grouped
// under its host and sorted by CPU within each group.
static void updateContainerData()
{
    if (!gui_container_page_active() || !container_label)
        return;
    if (WiFi.status() != WL_CONNECTED)
        return;

    static DynamicJsonDocument doc(16384);
    doc.clear();
    const char *endpoint =
        "/api/collections/containers/records?perPage=2000&skipTotal=1"
        "&fields=name,cpu,memory,status,system";
    if (beszelGet(endpoint, doc, nullptr) != HTTP_CODE_OK)
        return;

    struct CRow
    {
        const char *name;
        const char *status;
        const char *sysId;
        float cpu;
        float memMB; // Beszel reports container memory already in MB
    };
    static const int MAXC = 64;
    CRow rows[MAXC];
    int n = 0;
    for (JsonObject c : doc["items"].as<JsonArray>())
    {
        if (n >= MAXC)
            break;
        rows[n].name = c["name"] | "?";
        rows[n].status = c["status"] | "?";
        rows[n].sysId = c["system"] | "";
        rows[n].cpu = c["cpu"] | 0.0f;
        rows[n].memMB = c["memory"] | 0.0f;
        n++;
    }

    String out;
    out.reserve(n * 44 + 64);
    char line[96];
    char mem[12];
    int shown = 0;

    // Walk systems in display order so groups are stable; "" group id catches any
    // container whose host isn't in the systems list.
    for (int si = 0; si <= beszel_system_count; si++)
    {
        const char *gid = (si < beszel_system_count) ? beszel_systems[si].id : "";
        const char *gname = (si < beszel_system_count) ? beszel_systems[si].name : "other";

        // Collect indices for this group.
        int idx[MAXC];
        int gn = 0;
        for (int i = 0; i < n; i++)
        {
            bool match = (si < beszel_system_count)
                             ? strcmp(rows[i].sysId, gid) == 0
                             : true; // last pass: anything not yet shown
            if (si == beszel_system_count)
            {
                // Only orphans (system id not matching any known system).
                bool known = false;
                for (int k = 0; k < beszel_system_count; k++)
                    if (strcmp(rows[i].sysId, beszel_systems[k].id) == 0)
                        known = true;
                match = !known;
            }
            if (match)
                idx[gn++] = i;
        }
        if (gn == 0)
            continue;

        // Sort this group's rows by CPU descending.
        for (int a = 1; a < gn; a++)
        {
            int key = idx[a];
            int b = a - 1;
            while (b >= 0 && rows[idx[b]].cpu < rows[key].cpu)
            {
                idx[b + 1] = idx[b];
                b--;
            }
            idx[b + 1] = key;
        }

        // Group header (host name + count), recoloured cyan.
        snprintf(line, sizeof(line), "#07FFF7 %s (%d)#\n", gname, gn);
        out += line;

        for (int a = 0; a < gn; a++)
        {
            CRow &r = rows[idx[a]];
            if (r.memMB >= 1024.0f)
                snprintf(mem, sizeof(mem), "%.1fG", r.memMB / 1024.0f);
            else
                snprintf(mem, sizeof(mem), "%dM", (int)r.memMB);

            snprintf(line, sizeof(line), CONTAINER_ROW_FMT,
                     (unsigned)containerColor(r.status), r.name, r.cpu, mem);
            out += line;
            shown++;
        }
    }
    if (shown == 0)
        out = "No containers";

    // Preserve the scroll position across the 2s text refresh so an in-progress
    // scroll isn't yanked back to the top each update.
    lv_obj_t *view = lv_obj_get_parent(container_label);
    lv_coord_t saved_scroll = view ? lv_obj_get_scroll_y(view) : 0;
    lv_label_set_text(container_label, out.c_str());
    if (view)
        lv_obj_scroll_to_y(view, saved_scroll, LV_ANIM_OFF);
    if (container_header)
    {
        char title[40];
        snprintf(title, sizeof(title), LV_SYMBOL_LIST " Containers (%d)", shown);
        lv_label_set_text(container_header, title);
    }
}

void refreshContainerData()
{
    static unsigned long lastUpdate = 0;
    static bool hasUpdated = false;

    // Reset the throttle while hidden. That makes the first refresh after
    // entering the Containers page happen immediately instead of waiting for
    // a previous page visit's timer to expire.
    if (!gui_container_page_active() || !container_label)
    {
        hasUpdated = false;
        return;
    }

    if (hasUpdated && millis() - lastUpdate < CONTAINER_UPDATE_INTERVAL)
        return;

    lastUpdate = millis();
    hasUpdated = true;
    updateContainerData();
}

void updateBeszelData()
{
    static unsigned long lastUpdate = 0;
    const unsigned long interval = g_realtime_subscribed ? 30000 : BESZEL_UPDATE_INTERVAL;
    if (millis() - lastUpdate < interval)
        return;
    lastUpdate = millis();

    // Refresh the slow VRAM/total-RAM cache first (so fetchSystems can merge the
    // fresh values), but only on its longer interval.
    static unsigned long lastStats = 0;
    if (lastStats == 0 || millis() - lastStats >= BESZEL_STATS_INTERVAL)
    {
        fetchStats();
        lastStats = millis();
    }

    if (!fetchSystems())
        return;

    // (Re)build the per-system dashboard pages when the system set changes.
    if (beszel_system_count != g_built_count)
    {
        gui_build_system_pages(beszel_systems, beszel_system_count);
        g_built_count = beszel_system_count;
    }

    for (int i = 0; i < beszel_system_count; i++)
        gui_update_dashboard(i, beszel_systems[i]);

}
