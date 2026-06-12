#include "web_server.h"
#include "settings_manager.h"
#include <WebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include "esp_timer.h"
#include "esp_system.h"
#include "SPIFFS.h"
#include "FS.h"
#include "display.h"
#include "touch.h"
#include "gui.h"

#define TFT_BL 27
#define TFT_BACKLIGHT_ON HIGH

extern const ThemeColors dark_theme;
extern const ThemeColors light_theme;
static unsigned long last_micros = 0;
static float cpu_usage = 0;
static const int SAMPLE_COUNT = 10;
static unsigned long samples[SAMPLE_COUNT] = {0};
static int sample_index = 0;

void updateCPUUsage()
{
    unsigned long current_micros = esp_timer_get_time();

    if (last_micros > 0)
    {
        unsigned long delta = current_micros - last_micros;
        samples[sample_index] = delta;
        sample_index = (sample_index + 1) % SAMPLE_COUNT;
        unsigned long sum = 0;
        unsigned long max_sample = 0;
        for (int i = 0; i < SAMPLE_COUNT; i++)
        {
            sum += samples[i];
            if (samples[i] > max_sample)
                max_sample = samples[i];
        }
        unsigned long avg = sum / SAMPLE_COUNT;
        float usage = (float)avg / max_sample * 100.0;
        cpu_usage = (cpu_usage * 0.8) + (usage * 0.2);
        if (cpu_usage < 0)
            cpu_usage = 0;
        if (cpu_usage > 100)
            cpu_usage = 100;
    }

    last_micros = current_micros;
}

WebServer server(80);

void handleRoot()
{
    Serial.println("Handling root request");
    File root = SPIFFS.open("/");
    File file = root.openNextFile();
    Serial.println("Files in SPIFFS:");
    while (file)
    {
        Serial.print("  ");
        Serial.println(file.name());
        file = root.openNextFile();
    }

    File htmlFile = SPIFFS.open("/index.html", "r");
    if (!htmlFile)
    {
        Serial.println("Failed to open /index.html");
        server.send(500, "text/plain", "Failed to load HTML file");
        return;
    }

    Serial.printf("File size: %d bytes\n", htmlFile.size());
    server.streamFile(htmlFile, "text/html");
    htmlFile.close();
}

void handleGetSettings()
{
    StaticJsonDocument<1536> doc;

    updateCPUUsage();
    doc["cpuUsage"] = (int)cpu_usage;
    doc["wifiStrength"] = WiFi.RSSI();
    doc["chipModel"] = ESP.getChipModel();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["sdkVersion"] = ESP.getSdkVersion();
    doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    doc["cycleCount"] = ESP.getCycleCount();
    doc["efuseMac"] = ESP.getEfuseMac();
    doc["temperature"] = String((temperatureRead() - 32) / 1.8, 2);
    doc["hallSensor"] = hallRead();
    doc["uptime"] = millis() / 1000;
    doc["lastResetReason"] = esp_reset_reason();
    uint32_t free_heap = ESP.getFreeHeap();
    uint32_t max_alloc = ESP.getMaxAllocHeap();
    uint32_t fragmentation = 100 - (max_alloc * 100) / free_heap;
    doc["totalHeap"] = ESP.getHeapSize() / 1024;
    doc["freeHeap"] = free_heap / 1024;
    doc["minFreeHeap"] = ESP.getMinFreeHeap() / 1024;
    doc["maxAllocHeap"] = max_alloc / 1024;
    doc["heapFragmentation"] = fragmentation;
    doc["psramSize"] = ESP.getPsramSize() / 1024;
    doc["freePsram"] = ESP.getFreePsram() / 1024;
    doc["minFreePsram"] = ESP.getMinFreePsram() / 1024;
    doc["maxAllocPsram"] = ESP.getMaxAllocPsram() / 1024;
    doc["flashChipSize"] = ESP.getFlashChipSize() / 1024;
    doc["flashChipSpeed"] = ESP.getFlashChipSpeed() / 1000000;
    doc["flashChipMode"] = ESP.getFlashChipMode();
    doc["sketchSize"] = ESP.getSketchSize() / 1024;
    doc["sketchMD5"] = ESP.getSketchMD5();
    doc["freeSketchSpace"] = ESP.getFreeSketchSpace() / 1024;
    doc["wifiSSID"] = WiFi.SSID();
    doc["wifiBSSID"] = WiFi.BSSIDstr();
    doc["ipAddress"] = WiFi.localIP().toString();
    doc["macAddress"] = WiFi.macAddress();
    doc["channel"] = WiFi.channel();
    doc["wifiTxPower"] = WiFi.getTxPower();
    doc["dnsIP"] = WiFi.dnsIP().toString();
    doc["gatewayIP"] = WiFi.gatewayIP().toString();
    doc["subnetMask"] = WiFi.subnetMask().toString();
    doc["hostname"] = WiFi.getHostname();
    doc["wifiMode"] = WiFi.getMode();
    doc["isHidden"] = WiFi.SSID().length() == 0;
    doc["autoReconnect"] = WiFi.getAutoReconnect();

    const ThemeColors &theme = SettingsManager::getCurrentTheme();
    char hexColor[8];
    uint32_t color;

    color = lv_color_to32(theme.bg_color);
    sprintf(hexColor, "#%02X%02X%02X",
            (uint8_t)((color >> 16) & 0xFF),
            (uint8_t)((color >> 8) & 0xFF),
            (uint8_t)(color & 0xFF));
    doc["bgColor"] = hexColor;

    color = lv_color_to32(theme.text_color);
    sprintf(hexColor, "#%02X%02X%02X",
            (uint8_t)((color >> 16) & 0xFF),
            (uint8_t)((color >> 8) & 0xFF),
            (uint8_t)(color & 0xFF));
    doc["textColor"] = hexColor;

    color = lv_color_to32(theme.cpu_color);
    sprintf(hexColor, "#%02X%02X%02X",
            (uint8_t)((color >> 16) & 0xFF),
            (uint8_t)((color >> 8) & 0xFF),
            (uint8_t)(color & 0xFF));
    doc["cpuColor"] = hexColor;

    color = lv_color_to32(theme.ram_color);
    sprintf(hexColor, "#%02X%02X%02X",
            (uint8_t)((color >> 16) & 0xFF),
            (uint8_t)((color >> 8) & 0xFF),
            (uint8_t)(color & 0xFF));
    doc["ramColor"] = hexColor;

    color = lv_color_to32(theme.border_color);
    sprintf(hexColor, "#%02X%02X%02X",
            (uint8_t)((color >> 16) & 0xFF),
            (uint8_t)((color >> 8) & 0xFF),
            (uint8_t)(color & 0xFF));
    doc["borderColor"] = hexColor;

    color = lv_color_to32(theme.card_bg_color);
    sprintf(hexColor, "#%02X%02X%02X",
            (uint8_t)((color >> 16) & 0xFF),
            (uint8_t)((color >> 8) & 0xFF),
            (uint8_t)(color & 0xFF));
    doc["cardBgColor"] = hexColor;
    doc["darkMode"] = SettingsManager::getDarkMode();
    doc["glances_host"] = SettingsManager::getGlancesHost();
    doc["glances_port"] = SettingsManager::getGlancesPort();

    const TouchCalibration &tc = SettingsManager::getTouchCalibration();
    doc["touch_x_min"] = tc.rawXMin;
    doc["touch_x_max"] = tc.rawXMax;
    doc["touch_y_min"] = tc.rawYMin;
    doc["touch_y_max"] = tc.rawYMax;
    doc["touch_swap_xy"] = tc.swapXY;
    doc["touch_invert_x"] = tc.invertX;
    doc["touch_invert_y"] = tc.invertY;

    int16_t rawX, rawY;
    touch_get_last_raw(rawX, rawY);
    doc["touch_raw_x"] = rawX;
    doc["touch_raw_y"] = rawY;

    doc["auto_rotate"] = SettingsManager::getAutoRotate();
    doc["auto_rotate_interval"] = SettingsManager::getAutoRotateInterval();
    doc["backlight_timeout"] = SettingsManager::getBacklightTimeout();

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleUpdateSettings()
{
    String json = server.arg("plain");
    StaticJsonDocument<512> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (!error)
    {
        if (doc.containsKey("darkMode"))
        {
            SettingsManager::setDarkMode(doc["darkMode"].as<bool>());
        }
        if (doc.containsKey("bg_color"))
        {
            uint32_t color = doc["bg_color"].as<uint32_t>();
            Serial.printf("Updating bg_color to: %06X\n", color);
            SettingsManager::updateThemeColor("bg_color", color);
        }
        if (doc.containsKey("text_color"))
        {
            SettingsManager::updateThemeColor("text_color", doc["text_color"].as<uint32_t>());
        }
        if (doc.containsKey("cpu_color"))
        {
            SettingsManager::updateThemeColor("cpu_color", doc["cpu_color"].as<uint32_t>());
        }
        if (doc.containsKey("ram_color"))
        {
            SettingsManager::updateThemeColor("ram_color", doc["ram_color"].as<uint32_t>());
        }
        if (doc.containsKey("border_color"))
        {
            SettingsManager::updateThemeColor("border_color", doc["border_color"].as<uint32_t>());
        }
        if (doc.containsKey("card_bg_color"))
        {
            SettingsManager::updateThemeColor("card_bg_color", doc["card_bg_color"].as<uint32_t>());
        }
        if (doc.containsKey("glances_host"))
        {
            SettingsManager::setGlancesHost(doc["glances_host"].as<String>());
        }
        if (doc.containsKey("glances_port"))
        {
            SettingsManager::setGlancesPort(doc["glances_port"].as<uint16_t>());
        }
        if (doc.containsKey("reset_touch") && doc["reset_touch"].as<bool>())
        {
            SettingsManager::resetTouchCalibration();
        }
        else if (doc.containsKey("touch_x_min") || doc.containsKey("touch_x_max") ||
                 doc.containsKey("touch_y_min") || doc.containsKey("touch_y_max") ||
                 doc.containsKey("touch_swap_xy") || doc.containsKey("touch_invert_x") ||
                 doc.containsKey("touch_invert_y"))
        {
            // Apply only the provided fields on top of the current calibration.
            TouchCalibration tc = SettingsManager::getTouchCalibration();
            if (doc.containsKey("touch_x_min"))
                tc.rawXMin = doc["touch_x_min"].as<uint16_t>();
            if (doc.containsKey("touch_x_max"))
                tc.rawXMax = doc["touch_x_max"].as<uint16_t>();
            if (doc.containsKey("touch_y_min"))
                tc.rawYMin = doc["touch_y_min"].as<uint16_t>();
            if (doc.containsKey("touch_y_max"))
                tc.rawYMax = doc["touch_y_max"].as<uint16_t>();
            if (doc.containsKey("touch_swap_xy"))
                tc.swapXY = doc["touch_swap_xy"].as<bool>();
            if (doc.containsKey("touch_invert_x"))
                tc.invertX = doc["touch_invert_x"].as<bool>();
            if (doc.containsKey("touch_invert_y"))
                tc.invertY = doc["touch_invert_y"].as<bool>();
            SettingsManager::setTouchCalibration(tc);
        }
        if (doc.containsKey("auto_rotate") || doc.containsKey("auto_rotate_interval"))
        {
            if (doc.containsKey("auto_rotate"))
                SettingsManager::setAutoRotate(doc["auto_rotate"].as<bool>());
            if (doc.containsKey("auto_rotate_interval"))
                SettingsManager::setAutoRotateInterval(doc["auto_rotate_interval"].as<uint16_t>());
            // Apply immediately to the running UI.
            gui_set_auto_rotate(SettingsManager::getAutoRotate(),
                                SettingsManager::getAutoRotateInterval());
        }
        if (doc.containsKey("backlight_timeout"))
        {
            uint16_t secs = doc["backlight_timeout"].as<uint16_t>();
            SettingsManager::setBacklightTimeout(secs);
            touch_set_backlight_timeout(secs);
        }
        server.send(200, "application/json", "{\"status\":\"success\"}");
    }
    else
    {
        server.send(400, "application/json", "{\"status\":\"error\",\"message\":\"Invalid JSON\"}");
    }
}

void handleRestart()
{
    server.send(200, "application/json", "{\"status\":\"success\"}");
    delay(500);
    ESP.restart();
}

void handleResetTheme()
{
    if (SettingsManager::getDarkMode())
    {
        const_cast<ThemeColors &>(SettingsManager::getCurrentTheme()) = dark_theme;
    }
    else
    {
        const_cast<ThemeColors &>(SettingsManager::getCurrentTheme()) = light_theme;
    }
    SettingsManager::clearSavedColors();
    if (SettingsManager::themeCallback)
    {
        SettingsManager::themeCallback(SettingsManager::getDarkMode());
    }

    server.send(200, "application/json", "{\"status\":\"success\"}");
}

void handleHaStatus()
{
    StaticJsonDocument<256> doc;

    doc["temperature"] = String((temperatureRead() - 32) / 1.8, 2);
    doc["free_heap"] = ESP.getFreeHeap() / 1024;
    doc["wifi_strength"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000;
    doc["dark_mode"] = SettingsManager::getDarkMode();
    doc["display"] = digitalRead(TFT_BL) == TFT_BACKLIGHT_ON;

    String response;
    serializeJson(doc, response);
    server.send(200, "application/json", response);
}

void handleHaCommand()
{
    if (server.method() != HTTP_POST)
    {
        server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
        return;
    }

    String json = server.arg("plain");
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error)
    {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    bool success = false;
    String message = "Unknown command";

    if (doc.containsKey("dark_mode"))
    {
        SettingsManager::setDarkMode(doc["dark_mode"].as<bool>());
        success = true;
        message = "Dark mode updated";
    }

    if (doc.containsKey("display"))
    {
        display_sleep(!doc["display"].as<bool>());
        success = true;
        message = "Display state updated";
    }

    if (doc.containsKey("restart"))
    {
        if (doc["restart"].as<bool>())
        {
            success = true;
            message = "Restarting device";
            server.send(200, "application/json", "{\"success\":true,\"message\":\"Restarting device\"}");
            delay(500);
            ESP.restart();
            return;
        }
    }

    if (doc.containsKey("reset_theme"))
    {
        if (doc["reset_theme"].as<bool>())
        {
            if (SettingsManager::getDarkMode())
            {
                const_cast<ThemeColors &>(SettingsManager::getCurrentTheme()) = dark_theme;
            }
            else
            {
                const_cast<ThemeColors &>(SettingsManager::getCurrentTheme()) = light_theme;
            }
            SettingsManager::clearSavedColors();
            if (SettingsManager::themeCallback)
            {
                SettingsManager::themeCallback(SettingsManager::getDarkMode());
            }
            success = true;
            message = "Theme reset to defaults";
        }
    }

    StaticJsonDocument<128> response;
    response["success"] = success;
    response["message"] = message;

    String responseStr;
    serializeJson(response, responseStr);
    server.send(200, "application/json", responseStr);
}

void handleDisplaySleep()
{
    if (server.method() != HTTP_POST)
    {
        server.send(405, "application/json", "{\"error\":\"Method not allowed\"}");
        return;
    }

    String json = server.arg("plain");
    StaticJsonDocument<256> doc;
    DeserializationError error = deserializeJson(doc, json);

    if (error)
    {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    if (doc.containsKey("sleep"))
    {
        bool sleep = doc["sleep"].as<bool>();
        display_sleep(sleep);
        server.send(200, "application/json", "{\"success\":true,\"message\":\"Display state updated\"}");
    }
    else
    {
        server.send(400, "application/json", "{\"error\":\"Missing sleep parameter\"}");
    }
}

void setupWebServer()
{
    server.on("/", HTTP_GET, handleRoot);
    server.on("/settings", HTTP_GET, handleGetSettings);
    server.on("/settings", HTTP_POST, handleUpdateSettings);
    server.on("/restart", HTTP_POST, handleRestart);
    server.on("/resetTheme", HTTP_POST, handleResetTheme);
    server.on("/api/status", HTTP_GET, handleHaStatus);
    server.on("/api/command", HTTP_POST, handleHaCommand);
    server.on("/css/styles.css", HTTP_GET, []()
              {
        File file = SPIFFS.open("/css/styles.css", "r");
        server.streamFile(file, "text/css");
        file.close(); });

    server.on("/js/main.js", HTTP_GET, []()
              {
        File file = SPIFFS.open("/js/main.js", "r");
        server.streamFile(file, "application/javascript");
        file.close(); });

    server.on("/displaySleep", HTTP_POST, handleDisplaySleep);

    server.begin();
}

void handleWebServer()
{
    server.handleClient();
}