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

// How many dashboard pages the GUI has currently built, so we only rebuild when
// the set of monitored systems actually changes (rare).
static int g_built_count = -1;

// VRAM and total-RAM aren't in the realtime `info` summary -- they only live in
// the slower system_stats time-series. We pull those on a longer interval and
// cache the distilled values by system id so the fast 2s info refresh can merge
// them in without wiping them.
#define BESZEL_STATS_INTERVAL 30000
struct StatsCache
{
    char id[20];
    float memTotalGB;
    float vram;
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

    StaticJsonDocument<160> filter;
    JsonObject item = filter["items"].createNestedObject();
    item["system"] = true;
    JsonObject stats = item.createNestedObject("stats");
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

    if (beszelGet(endpoint, doc, nullptr) != HTTP_CODE_OK)
        return false;

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

    lv_label_set_text(container_label, out.c_str());
    if (container_header)
    {
        char title[40];
        snprintf(title, sizeof(title), LV_SYMBOL_LIST " Containers (%d)", shown);
        lv_label_set_text(container_header, title);
    }
}

void updateBeszelData()
{
    static unsigned long lastUpdate = 0;
    if (millis() - lastUpdate < BESZEL_UPDATE_INTERVAL)
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

    updateContainerData();
}
