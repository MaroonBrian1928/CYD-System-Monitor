#include "settings_manager.h"
#include "config.h"
#include <lvgl.h>
#include <string.h>

Preferences SettingsManager::preferences;
bool SettingsManager::darkMode = true;
SettingsManager::ThemeCallback SettingsManager::themeCallback = nullptr;

static ThemeColors mutable_dark_theme = dark_theme;
static ThemeColors mutable_light_theme = light_theme;

String SettingsManager::glancesHost;
uint16_t SettingsManager::glancesPort;

TouchCalibration SettingsManager::touchCal = {
    TOUCH_CAL_DEFAULT_X_MIN, TOUCH_CAL_DEFAULT_X_MAX,
    TOUCH_CAL_DEFAULT_Y_MIN, TOUCH_CAL_DEFAULT_Y_MAX,
    TOUCH_CAL_DEFAULT_SWAP_XY, TOUCH_CAL_DEFAULT_INVERT_X, TOUCH_CAL_DEFAULT_INVERT_Y};

bool SettingsManager::autoRotate = false;
uint16_t SettingsManager::autoRotateInterval = 10;
uint16_t SettingsManager::backlightTimeout = 0;
bool SettingsManager::displayFlip = false;

void SettingsManager::begin()
{
    preferences.begin("settings", false);
    darkMode = preferences.getBool("darkMode", true);

    glancesHost = preferences.getString("glances_host", "192.168.1.50");
    glancesPort = preferences.getUInt("glances_port", 61208);

    glances_host = glancesHost;
    glances_port = glancesPort;

    touchCal.rawXMin = preferences.getUShort("tch_xmin", TOUCH_CAL_DEFAULT_X_MIN);
    touchCal.rawXMax = preferences.getUShort("tch_xmax", TOUCH_CAL_DEFAULT_X_MAX);
    touchCal.rawYMin = preferences.getUShort("tch_ymin", TOUCH_CAL_DEFAULT_Y_MIN);
    touchCal.rawYMax = preferences.getUShort("tch_ymax", TOUCH_CAL_DEFAULT_Y_MAX);
    touchCal.swapXY = preferences.getBool("tch_swap", TOUCH_CAL_DEFAULT_SWAP_XY);
    touchCal.invertX = preferences.getBool("tch_invx", TOUCH_CAL_DEFAULT_INVERT_X);
    touchCal.invertY = preferences.getBool("tch_invy", TOUCH_CAL_DEFAULT_INVERT_Y);

    autoRotate = preferences.getBool("auto_rotate", false);
    autoRotateInterval = preferences.getUShort("rotate_secs", 10);
    backlightTimeout = preferences.getUShort("bl_timeout", 0);
    displayFlip = preferences.getBool("disp_flip", false);

    mutable_dark_theme = dark_theme;
    mutable_light_theme = light_theme;

    if (preferences.isKey("bg_color"))
    {
        uint32_t color = preferences.getUInt("bg_color", 0);
        mutable_dark_theme.bg_color = lv_color_hex(color);
    }
    if (preferences.isKey("card_bg_color"))
    {
        uint32_t color = preferences.getUInt("card_bg_color", 0);
        mutable_dark_theme.card_bg_color = lv_color_hex(color);
    }
    if (preferences.isKey("text_color"))
    {
        uint32_t color = preferences.getUInt("text_color", 0);
        mutable_dark_theme.text_color = lv_color_hex(color);
    }
    if (preferences.isKey("cpu_color"))
    {
        uint32_t color = preferences.getUInt("cpu_color", 0);
        mutable_dark_theme.cpu_color = lv_color_hex(color);
    }
    if (preferences.isKey("ram_color"))
    {
        uint32_t color = preferences.getUInt("ram_color", 0);
        mutable_dark_theme.ram_color = lv_color_hex(color);
    }
    if (preferences.isKey("border_color"))
    {
        uint32_t color = preferences.getUInt("border_color", 0);
        mutable_dark_theme.border_color = lv_color_hex(color);
    }

    if (themeCallback)
    {
        themeCallback(darkMode);
    }
}

bool SettingsManager::getDarkMode()
{
    return darkMode;
}

void SettingsManager::setDarkMode(bool enabled)
{
    darkMode = enabled;
    saveSettings();
    if (themeCallback)
    {
        themeCallback(enabled);
    }
}

void SettingsManager::saveSettings()
{
    preferences.putBool("darkMode", darkMode);
}

void SettingsManager::updateThemeColor(const char *colorName, uint32_t color)
{
    ThemeColors &theme = darkMode ? mutable_dark_theme : mutable_light_theme;

    if (strcmp(colorName, "bg_color") == 0)
    {
        theme.bg_color = lv_color_hex(color);
        preferences.putUInt("bg_color", color);
    }
    else if (strcmp(colorName, "card_bg_color") == 0)
    {
        theme.card_bg_color = lv_color_hex(color);
        preferences.putUInt("card_bg_color", color);
    }
    else if (strcmp(colorName, "text_color") == 0)
    {
        theme.text_color = lv_color_hex(color);
        preferences.putUInt("text_color", color);
    }
    else if (strcmp(colorName, "cpu_color") == 0)
    {
        theme.cpu_color = lv_color_hex(color);
        preferences.putUInt("cpu_color", color);
    }
    else if (strcmp(colorName, "ram_color") == 0)
    {
        theme.ram_color = lv_color_hex(color);
        preferences.putUInt("ram_color", color);
    }
    else if (strcmp(colorName, "border_color") == 0)
    {
        theme.border_color = lv_color_hex(color);
        preferences.putUInt("border_color", color);
    }
    if (themeCallback)
    {
        themeCallback(darkMode);
    }
}

const ThemeColors &SettingsManager::getCurrentTheme()
{
    return darkMode ? mutable_dark_theme : mutable_light_theme;
}

void SettingsManager::clearSavedColors()
{
    preferences.remove("bg_color");
    preferences.remove("card_bg_color");
    preferences.remove("text_color");
    preferences.remove("cpu_color");
    preferences.remove("ram_color");
    preferences.remove("border_color");
}

const String &SettingsManager::getGlancesHost()
{
    return glancesHost;
}

uint16_t SettingsManager::getGlancesPort()
{
    return glancesPort;
}

void SettingsManager::setGlancesHost(const String &host)
{
    glancesHost = host;
    preferences.putString("glances_host", host);
    glances_host = host;
}

void SettingsManager::setGlancesPort(uint16_t port)
{
    glancesPort = port;
    preferences.putUInt("glances_port", port);
    glances_port = port;
}

const TouchCalibration &SettingsManager::getTouchCalibration()
{
    return touchCal;
}

void SettingsManager::setTouchCalibration(const TouchCalibration &cal)
{
    touchCal = cal;
    preferences.putUShort("tch_xmin", cal.rawXMin);
    preferences.putUShort("tch_xmax", cal.rawXMax);
    preferences.putUShort("tch_ymin", cal.rawYMin);
    preferences.putUShort("tch_ymax", cal.rawYMax);
    preferences.putBool("tch_swap", cal.swapXY);
    preferences.putBool("tch_invx", cal.invertX);
    preferences.putBool("tch_invy", cal.invertY);
}

void SettingsManager::resetTouchCalibration()
{
    TouchCalibration def = {
        TOUCH_CAL_DEFAULT_X_MIN, TOUCH_CAL_DEFAULT_X_MAX,
        TOUCH_CAL_DEFAULT_Y_MIN, TOUCH_CAL_DEFAULT_Y_MAX,
        TOUCH_CAL_DEFAULT_SWAP_XY, TOUCH_CAL_DEFAULT_INVERT_X, TOUCH_CAL_DEFAULT_INVERT_Y};
    setTouchCalibration(def);
}

bool SettingsManager::getAutoRotate()
{
    return autoRotate;
}

uint16_t SettingsManager::getAutoRotateInterval()
{
    return autoRotateInterval;
}

void SettingsManager::setAutoRotate(bool enabled)
{
    autoRotate = enabled;
    preferences.putBool("auto_rotate", enabled);
}

void SettingsManager::setAutoRotateInterval(uint16_t seconds)
{
    if (seconds < 1)
        seconds = 1;
    autoRotateInterval = seconds;
    preferences.putUShort("rotate_secs", seconds);
}

uint16_t SettingsManager::getBacklightTimeout()
{
    return backlightTimeout;
}

void SettingsManager::setBacklightTimeout(uint16_t seconds)
{
    backlightTimeout = seconds;
    preferences.putUShort("bl_timeout", seconds);
}

bool SettingsManager::getDisplayFlip()
{
    return displayFlip;
}

void SettingsManager::setDisplayFlip(bool flipped)
{
    displayFlip = flipped;
    preferences.putBool("disp_flip", flipped);
}