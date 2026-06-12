#pragma once

#include <Preferences.h>
#include <functional>
#include <cstdint>
#include "config.h"

// Resistive-touch calibration. Stored in NVS so it can be tuned from the web UI
// without reflashing (every panel reads slightly differently). Defaults below
// were measured on the original unit by tapping the four corners.
struct TouchCalibration {
    uint16_t rawXMin;
    uint16_t rawXMax;
    uint16_t rawYMin;
    uint16_t rawYMax;
    bool swapXY;
    bool invertX;
    bool invertY;
};

// These defaults can be overridden at build time via -D flags in platformio.ini
// (e.g. -D TOUCH_CAL_DEFAULT_X_MIN=400), so a panel with known-good values flashes
// ready-to-use. They only seed the NVS defaults; the web UI can still override and
// persist at runtime.
#ifndef TOUCH_CAL_DEFAULT_X_MIN
#define TOUCH_CAL_DEFAULT_X_MIN 390
#endif
#ifndef TOUCH_CAL_DEFAULT_X_MAX
#define TOUCH_CAL_DEFAULT_X_MAX 3670
#endif
#ifndef TOUCH_CAL_DEFAULT_Y_MIN
#define TOUCH_CAL_DEFAULT_Y_MIN 300
#endif
#ifndef TOUCH_CAL_DEFAULT_Y_MAX
#define TOUCH_CAL_DEFAULT_Y_MAX 3750
#endif
#ifndef TOUCH_CAL_DEFAULT_SWAP_XY
#define TOUCH_CAL_DEFAULT_SWAP_XY true
#endif
#ifndef TOUCH_CAL_DEFAULT_INVERT_X
#define TOUCH_CAL_DEFAULT_INVERT_X true
#endif
#ifndef TOUCH_CAL_DEFAULT_INVERT_Y
#define TOUCH_CAL_DEFAULT_INVERT_Y false
#endif

class SettingsManager {
public:
    using ThemeCallback = std::function<void(bool)>;

    static void begin();
    static bool getDarkMode();
    static void setDarkMode(bool isDark);
    static void saveSettings();
    static void updateThemeColor(const char* colorName, uint32_t color);
    static const ThemeColors& getCurrentTheme();
    static void setThemeChangeCallback(ThemeCallback callback) {
        themeCallback = callback;
    }
    static void clearSavedColors();
    static const String& getGlancesHost();
    static uint16_t getGlancesPort();
    static void setGlancesHost(const String& host);
    static void setGlancesPort(uint16_t port);

    static const TouchCalibration& getTouchCalibration();
    static void setTouchCalibration(const TouchCalibration& cal);
    static void resetTouchCalibration();

    static bool getAutoRotate();
    static uint16_t getAutoRotateInterval();
    static void setAutoRotate(bool enabled);
    static void setAutoRotateInterval(uint16_t seconds);

    static ThemeCallback themeCallback;

private:
    static Preferences preferences;
    static bool darkMode;
    static void loadSettings();
    static String glancesHost;
    static uint16_t glancesPort;
    static TouchCalibration touchCal;
    static bool autoRotate;
    static uint16_t autoRotateInterval;
};