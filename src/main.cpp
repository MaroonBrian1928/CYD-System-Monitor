#include <Arduino.h>
#include <WiFi.h>
#include "lvgl.h"
#include "config.h"
#include "display.h"
#include "gui.h"
#include "touch.h"
#include "glances_api.h"
#include "settings_manager.h"
#include "web_server.h"
#include "credentials.h"
#include "SPIFFS.h"

void setup()
{
    Serial.begin(115200);
    delay(2000);

    Serial.println("\n\n======================");
    Serial.println("Starting System Monitor");
    Serial.println("======================");
    Serial.println("Step 1: Serial Test");
    delay(100);
    Serial.println("Step 2: Setting hostname");
    WiFi.hostname("systemmonitor");
    delay(100);
    Serial.print("Step 3: SSID to connect: ");
    Serial.println(WIFI_SSID);
    delay(100);
    Serial.println("Step 4: Starting WiFi connection");
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20)
    {
        delay(500);
        Serial.print(".");
        attempts++;
        Serial.printf(" (Status: %d)", WiFi.status());
    }

    Serial.println("\nStep 5: WiFi connection result:");
    if (WiFi.status() == WL_CONNECTED)
    {
        Serial.println("  - Connected successfully!");
        Serial.print("  - Hostname: ");
        Serial.println(WiFi.getHostname());
        Serial.print("  - IP Address: ");
        Serial.println(WiFi.localIP());
    }
    else
    {
        Serial.println("  - Connection FAILED!");
        Serial.print("  - Status code: ");
        Serial.println(WiFi.status());
    }

    Serial.println("Step 6: Initializing display");
    lv_init();
    init_display();

#if LV_USE_LOG != 0
    lv_log_register_print_cb([](const char *buf)
                             {
        Serial.printf(buf);
        Serial.flush(); });
#endif

    create_system_monitor_gui();
    touch_init();
    SettingsManager::begin();
    gui_set_auto_rotate(SettingsManager::getAutoRotate(),
                        SettingsManager::getAutoRotateInterval());
    if (!SPIFFS.begin(true))
    {
        Serial.println("SPIFFS Mount Failed");
        return;
    }

    setupWebServer();

    Serial.println("======================");
    Serial.println("Setup complete!");
    Serial.println("======================");
}

void loop()
{
    lv_timer_handler();
    updateGlancesData();
    handleWebServer();
    delay(1);
}
