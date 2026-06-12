#include "glances_api.h"
#include "gui.h"
#include "config.h"
#include <HTTPClient.h>
#include <WiFi.h>

bool GlancesAPI::fetchData(const char *endpoint, StaticJsonDocument<4096> &doc)
{
    if (WiFi.status() != WL_CONNECTED)
    {
        return false;
    }

    HTTPClient http;
    String url = "http://" + glances_host + ":" + String(glances_port) + endpoint;
    http.begin(url);
    int httpCode = http.GET();

    if (httpCode != HTTP_CODE_OK)
    {
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    DeserializationError error = deserializeJson(doc, payload);
    return !error;
}

void GlancesAPI::updateCPUData(StaticJsonDocument<4096> &doc)
{
    if (!fetchData("/api/4/cpu", doc))
        return;

    float cpuPercent = doc["total"].as<float>();
    int cpuCount = doc["cpucore"].as<int>();

    if (cpu_arc_obj.arc && cpu_arc_obj.label)
    {
        lv_obj_t **labels = (lv_obj_t **)lv_obj_get_user_data(cpu_arc_obj.arc);
        if (labels)
        {
            char buf[32];

            lv_label_set_text(labels[0], "CPU");
            snprintf(buf, sizeof(buf), "%d cores", cpuCount);
            lv_label_set_text(labels[1], buf);
            snprintf(buf, sizeof(buf), "%d%%", (int)cpuPercent);
            lv_label_set_text(labels[2], buf);
            lv_obj_set_style_text_font(labels[1], &lv_font_montserrat_10, 0);
            lv_obj_set_style_text_font(labels[2], &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(labels[1], lv_color_hex(0x808080), 0);
            lv_obj_set_style_text_color(labels[2], lv_color_white(), 0);
        }
        set_arc_value_animated(cpu_arc_obj.arc, cpuPercent);
    }
}

void GlancesAPI::updateMemoryData(StaticJsonDocument<4096> &doc)
{
    if (!fetchData("/api/4/mem", doc))
        return;

    float memPercent = doc["percent"].as<float>();
    float totalRam = doc["total"].as<float>() / (1024.0 * 1024.0 * 1024.0);

    if (ram_arc_obj.arc && ram_arc_obj.label)
    {
        lv_obj_t **labels = (lv_obj_t **)lv_obj_get_user_data(ram_arc_obj.arc);
        if (labels)
        {
            char buf[32];

            lv_label_set_text(labels[0], "RAM");
            snprintf(buf, sizeof(buf), "%d%%", (int)memPercent);
            lv_label_set_text(labels[1], buf);
            snprintf(buf, sizeof(buf), "/ %.1f GB", totalRam);
            lv_label_set_text(labels[2], buf);
        }
        set_arc_value_animated(ram_arc_obj.arc, memPercent);
    }
}

void GlancesAPI::updateContainerData()
{
    // Only fetch when the container page is showing (the response is large).
    if (!gui_container_page_active() || !container_label)
        return;
    if (WiFi.status() != WL_CONNECTED)
        return;

    HTTPClient http;
    String url = "http://" + glances_host + ":" + String(glances_port) + "/api/4/containers";
    http.begin(url);
    int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK)
    {
        http.end();
        return;
    }
    String payload = http.getString();
    http.end();

    // Keep only the fields we render so the (large) response fits in memory.
    StaticJsonDocument<192> filter;
    filter[0]["name"] = true;
    filter[0]["status"] = true;
    filter[0]["cpu_percent"] = true;
    filter[0]["memory_usage"] = true;

    DynamicJsonDocument doc(8192);
    if (deserializeJson(doc, payload, DeserializationOption::Filter(filter)))
        return;

    struct CRow
    {
        const char *name;
        const char *status;
        float cpu;
        long memMB;
    };
    static const int MAXC = 24;
    CRow rows[MAXC];
    int n = 0;
    for (JsonObject c : doc.as<JsonArray>())
    {
        if (n >= MAXC)
            break;
        rows[n].name = c["name"] | "?";
        rows[n].status = c["status"] | "?";
        rows[n].cpu = c["cpu_percent"] | 0.0f;
        rows[n].memMB = (long)((c["memory_usage"] | 0LL) / (1024 * 1024));
        n++;
    }

    // Sort by CPU descending (most active first), like the Glances UI.
    for (int i = 1; i < n; i++)
    {
        CRow k = rows[i];
        int j = i - 1;
        while (j >= 0 && rows[j].cpu < k.cpu)
        {
            rows[j + 1] = rows[j];
            j--;
        }
        rows[j + 1] = k;
    }

    String out;
    out.reserve(n * 40 + 16);
    char line[80];
    char mem[12];
    for (int i = 0; i < n; i++)
    {
        if (rows[i].memMB >= 1024)
            snprintf(mem, sizeof(mem), "%.1fG", rows[i].memMB / 1024.0);
        else
            snprintf(mem, sizeof(mem), "%ldM", rows[i].memMB);

        uint32_t color;
        const char *s = rows[i].status;
        if (strcmp(s, "running") == 0 || strcmp(s, "healthy") == 0)
            color = 0x33D17A; // green
        else if (strstr(s, "start") || strcmp(s, "paused") == 0)
            color = 0xE5A50A; // amber
        else
            color = 0xE0504F; // red: exited/dead/unhealthy/...

        snprintf(line, sizeof(line), CONTAINER_ROW_FMT,
                 rows[i].name, (unsigned)color, rows[i].status, rows[i].cpu, mem);
        out += line;
    }
    if (n == 0)
        out = "No containers";

    lv_label_set_text(container_label, out.c_str());
}

void updateGlancesData()
{
    static unsigned long lastGlancesUpdate = 0;
    if (millis() - lastGlancesUpdate < GLANCES_UPDATE_INTERVAL)
    {
        return;
    }
    static StaticJsonDocument<4096> doc;
    GlancesAPI::updateCPUData(doc);
    GlancesAPI::updateMemoryData(doc);

    if (GlancesAPI::fetchData("/api/4/sensors", doc))
    {
        for (JsonVariant sensor : doc.as<JsonArray>())
        {
            if (strcmp(sensor["label"], "Tctl") == 0)
            {
                int temp = (int)sensor["value"].as<float>();
                char buf[32];
                snprintf(buf, sizeof(buf), LV_SYMBOL_WARNING " Temp: %d°C", temp);
                update_compact_label(temp_label, buf);
                break;
            }
        }
    }

    // Disk: this host's root filesystem is exposed by Glances at "/host"
    // (was the Unraid "/rootfs/mnt/disk*" array on the upstream firmware).
    if (GlancesAPI::fetchData("/api/4/fs", doc))
    {
        for (JsonVariant fs : doc.as<JsonArray>())
        {
            const char *mnt_point = fs["mnt_point"].as<const char *>();
            if (strcmp(mnt_point, "/host") == 0)
            {
                float usagePercent = fs["percent"].as<float>();
                char buf[32];
                snprintf(buf, sizeof(buf), LV_SYMBOL_DRIVE " Disk: %.1f%%", usagePercent);
                update_compact_label(disk_label, buf);
                break;
            }
        }
    }

    // GPU (NVIDIA, via Glances /gpu plugin): "proc" = GPU utilization %, "mem" = VRAM %.
    if (GlancesAPI::fetchData("/api/4/gpu", doc))
    {
        JsonArray gpus = doc.as<JsonArray>();
        if (gpus.size() > 0)
        {
            JsonObject gpu = gpus[0];
            char buf[32];
            snprintf(buf, sizeof(buf), LV_SYMBOL_CHARGE " GPU: %d%%", (int)gpu["proc"].as<float>());
            update_compact_label(gpu_label, buf);
            snprintf(buf, sizeof(buf), LV_SYMBOL_SAVE " VRAM: %d%%", (int)gpu["mem"].as<float>());
            update_compact_label(vram_label, buf);
        }
    }

    if (GlancesAPI::fetchData("/api/4/uptime", doc))
    {
        String payload = doc.as<String>();
        payload.replace("\"", "");
        char buf[32];
        snprintf(buf, sizeof(buf), LV_SYMBOL_POWER "  %s", payload.c_str());
        update_compact_label(uptime_label, buf);
    }

    if (GlancesAPI::fetchData("/api/4/network", doc))
    {
        for (JsonVariant interface : doc.as<JsonArray>())
        {
            const char *interface_name = interface["interface_name"].as<const char *>();

            if (strcmp(interface_name, "enp4s0") == 0)
            {
                float recv_rate = interface["bytes_recv_rate_per_sec"].as<float>();
                float sent_rate = interface["bytes_sent_rate_per_sec"].as<float>();

                char down_str[16], up_str[16];
                auto formatSpeed = [](float bytes_per_sec, char *buffer)
                {
                    if (bytes_per_sec > 1024 * 1024)
                        sprintf(buffer, "%.1fM", bytes_per_sec / (1024.0 * 1024.0));
                    else if (bytes_per_sec > 1024)
                        sprintf(buffer, "%.1fK", bytes_per_sec / 1024.0);
                    else
                        sprintf(buffer, "%.0fB", bytes_per_sec);
                };

                formatSpeed(recv_rate, down_str);
                formatSpeed(sent_rate, up_str);

                char buf[64];
                snprintf(buf, sizeof(buf), LV_SYMBOL_DOWNLOAD " %s    " LV_SYMBOL_UPLOAD " %s", down_str, up_str);
                update_compact_label(network_label, buf);
                break;
            }
        }
    }

    GlancesAPI::updateContainerData();

    lastGlancesUpdate = millis();
}