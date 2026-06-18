// !! REMEMBER TO RENAME THE FILE TO credentials.h !!

#ifndef CREDENTIALS_H
#define CREDENTIALS_H

#include <cstdint>

// WiFi Credentials
const char* const WIFI_SSID = "your_ssid_here"; // Change this to your WiFi SSID
const char* const WIFI_PASSWORD = "your_password_here"; // Change this to your WiFi Password

// Beszel hub (PocketBase). The ESP32 logs in with the username/password below to
// obtain an auth token, because the systems/containers collections are
// owner-scoped and return no data unless the request is authenticated.
// Use the plain-HTTP address of your Beszel instance on the LAN.
const char* const BESZEL_HOST = "192.168.1.50";
const uint16_t BESZEL_PORT = 8090;
const char* const BESZEL_USERNAME = "you@example.com";
const char* const BESZEL_PASSWORD = "change_me";

#endif