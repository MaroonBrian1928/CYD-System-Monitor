#ifndef CONFIG_H
#define CONFIG_H

#include <lvgl.h>
#include <WString.h>

extern const char *const ssid;
extern const char *const password;

extern String beszel_host;
extern uint16_t beszel_port;
#define BESZEL_UPDATE_INTERVAL 2000

extern const uint16_t screenWidth;
extern const uint16_t screenHeight;
struct ThemeColors
{
    lv_color_t bg_color;
    lv_color_t card_bg_color;
    lv_color_t text_color;
    lv_color_t cpu_color;
    lv_color_t ram_color;
    lv_color_t border_color;
};

extern const ThemeColors light_theme;
extern const ThemeColors dark_theme;

#define DARK_MODE true

#endif