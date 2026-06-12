#include "touch.h"
#include "config.h"
#include "gui.h"
#include "settings_manager.h"
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

// Dedicated SPI bus for the touch controller. The display (TFT_eSPI) uses the
// VSPI peripheral on this CYD, so the touch controller MUST use HSPI -- sharing
// VSPI double-claims the peripheral and crash-loops the device at boot.
static SPIClass touchSPI(HSPI);
static XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// Landscape screen geometry (rotation 3): 320 wide x 240 tall.
static const int16_t SCREEN_W = 320;
static const int16_t SCREEN_H = 240;

static int16_t clamp(int16_t v, int16_t lo, int16_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

// Last raw (uncalibrated) sample, exposed for live calibration in the web UI.
static volatile int16_t last_raw_x = 0;
static volatile int16_t last_raw_y = 0;

void touch_get_last_raw(int16_t &x, int16_t &y)
{
    x = last_raw_x;
    y = last_raw_y;
}

// Map a raw XPT2046 reading to calibrated screen coordinates using the
// user-configurable calibration stored in NVS.
static void map_touch(const TS_Point &p, int16_t &x, int16_t &y)
{
    const TouchCalibration &c = SettingsManager::getTouchCalibration();

    int32_t mx, my;
    if (c.swapXY)
    {
        // Axes swapped: raw-X drives the screen's vertical axis and vice versa.
        mx = map(p.y, c.rawYMin, c.rawYMax, 0, SCREEN_W);
        my = map(p.x, c.rawXMin, c.rawXMax, 0, SCREEN_H);
    }
    else
    {
        mx = map(p.x, c.rawXMin, c.rawXMax, 0, SCREEN_W);
        my = map(p.y, c.rawYMin, c.rawYMax, 0, SCREEN_H);
    }

    if (c.invertX)
        mx = SCREEN_W - mx;
    if (c.invertY)
        my = SCREEN_H - my;

    x = clamp((int16_t)mx, 0, SCREEN_W - 1);
    y = clamp((int16_t)my, 0, SCREEN_H - 1);
}

// Resistive panels briefly drop the touch during a press; hold for this many
// consecutive "untouched" reads before treating it as a real release. This keeps
// a single tap from registering as several presses (and several page changes).
#define RELEASE_DEBOUNCE 3

static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static bool pressed = false;
    static int16_t last_x, last_y;
    static int release_count = 0;

    if (ts.touched())
    {
        TS_Point p = ts.getPoint();
        last_raw_x = p.x;
        last_raw_y = p.y;
        int16_t x, y;
        map_touch(p, x, y);

#if TOUCH_DEBUG
        Serial.printf("[touch] raw=(%d,%d) -> screen=(%d,%d)\n", p.x, p.y, x, y);
#endif
        pressed = true;
        last_x = x;
        last_y = y;
        release_count = 0;

        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }

    // Not currently touched. Bridge short dropouts by holding the last press.
    if (pressed && release_count < RELEASE_DEBOUNCE)
    {
        release_count++;
        data->point.x = last_x;
        data->point.y = last_y;
        data->state = LV_INDEV_STATE_PR;
        return;
    }

    // Real release: a completed tap advances to the next page.
    if (pressed)
    {
        pressed = false;
        release_count = 0;
        gui_next_page();
    }

    data->state = LV_INDEV_STATE_REL;
}

void touch_init()
{
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSPI);
    ts.setRotation(0); // we apply our own rotation/calibration above

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    lv_indev_drv_register(&indev_drv);

    Serial.println("Touch initialized (XPT2046)");
}
