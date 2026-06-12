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
// Raw XPT2046 readings run ~0..4095. These map the raw range onto the 320x240
// landscape (rotation 3) screen. Resistive panels vary unit-to-unit, so expect
// to tune these once: enable TOUCH_DEBUG below, tap the four corners, read the
// raw min/max off the serial monitor, and drop them in here.
#define TOUCH_RAW_X_MIN 200
#define TOUCH_RAW_X_MAX 3700
#define TOUCH_RAW_Y_MIN 240
#define TOUCH_RAW_Y_MAX 3800

// Orientation adjustments for rotation 3. If taps land on the wrong axis or are
// mirrored, flip these (0/1) rather than rewriting the mapping math.
#define TOUCH_SWAP_XY 1
#define TOUCH_INVERT_X 0
#define TOUCH_INVERT_Y 1

// Set to 1 to print raw + mapped coordinates to Serial on every touch (for
// calibration). Leave at 0 for normal use.
#define TOUCH_DEBUG 0

// Initialise the touch controller and register it as an LVGL pointer input
// device. Call once, after lv_init()/display init and after the GUI is built.
void touch_init();

#endif
