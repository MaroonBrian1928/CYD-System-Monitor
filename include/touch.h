#ifndef TOUCH_H
#define TOUCH_H

// XPT2046 resistive touch for the Cheap Yellow Display (ESP32-2432S028).
//
// The touch controller sits on its OWN SPI bus, separate from the ST7789
// display, so it gets a dedicated SPIClass (it cannot share TFT_eSPI's bus).
//
// CYD touch pin map:
//   T_CLK = 25, T_MOSI = 32, T_MISO = 39, T_CS = 33, T_IRQ = 36
#define XPT2046_CLK 25
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CS 33
#define XPT2046_IRQ 36

// --- Calibration -----------------------------------------------------------
// Calibration lives in NVS (see SettingsManager / TouchCalibration) so it can be
// tuned from the web UI without reflashing. The factory defaults are in
// settings_manager.h (TOUCH_CAL_DEFAULT_*).

// Set to 1 to also print raw + mapped coordinates to Serial on every touch.
// Leave at 0 for normal use (the web UI shows the live raw values instead).
// Configurable in credentials.h (alongside the other local build settings); the
// default below keeps things building when it isn't defined there.
#if defined(__has_include)
#  if __has_include("credentials.h")
#    include "credentials.h"
#  endif
#endif
#ifndef TOUCH_DEBUG
#define TOUCH_DEBUG 0
#endif

#include <cstdint>

// Initialise the touch controller and register it as an LVGL pointer input
// device. Call once, after lv_init()/display init and after the GUI is built.
void touch_init();

// Most recent raw (uncalibrated) XPT2046 reading, for live calibration in the
// web UI. Holds the last touched sample (0..4095).
void touch_get_last_raw(int16_t &x, int16_t &y);

// Turn the screen off after this many seconds of no touch (0 = never). A touch
// wakes it (and that wake tap is swallowed, not treated as a page change).
void touch_set_backlight_timeout(uint32_t seconds);

#endif
