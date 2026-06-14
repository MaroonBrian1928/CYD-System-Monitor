#include "touch.h"
#include "config.h"
#include "gui.h"
#include "settings_manager.h"
#include "display.h"
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

// Dedicated SPI bus for the touch controller. The display (TFT_eSPI) uses the
// VSPI peripheral on this CYD, so the touch controller MUST use HSPI -- sharing
// VSPI double-claims the peripheral and crash-loops the device at boot.
static SPIClass touchSPI(HSPI);
static XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);

// Backlight idle-sleep state.
static uint32_t backlight_timeout_ms = 0; // 0 = never sleep
static uint32_t last_activity_ms = 0;
static bool screen_asleep = false;
static bool wake_swallow = false; // ignore the touch that wakes the screen

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

    // When the display is flipped 180, the same physical touch maps to the
    // opposite screen coordinate, so flip both axes to match.
    if (SettingsManager::getDisplayFlip())
    {
        mx = SCREEN_W - mx;
        my = SCREEN_H - my;
    }

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

    // Read and validate the sample first. This XPT2046 emits phantom "rail"
    // samples (e.g. raw (4095,0)) as electrical noise -- often a continuous
    // stream after a screen redraw -- which would otherwise look like a finger
    // stuck in the corner and block every later tap. A real press always lands
    // inside the panel's range, so reject anything at/near the rails.
    bool touched = false;
    TS_Point p;
    if (ts.touched())
    {
        p = ts.getPoint();
        if (p.x > 100 && p.x < 4000 && p.y > 100 && p.y < 4000)
            touched = true;
    }

    if (touched)
    {
        last_activity_ms = millis();

        // If the screen had gone to sleep, this touch only wakes it -- swallow
        // the whole press so it isn't also treated as a page change.
        if (screen_asleep)
        {
            display_sleep(false);
            screen_asleep = false;
            wake_swallow = true;
        }
        if (wake_swallow)
        {
            data->state = LV_INDEV_STATE_REL;
            return;
        }

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

    // Finger lifted: a swallowed wake-touch is now finished.
    wake_swallow = false;

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
#if TOUCH_DEBUG
        Serial.println("[touch] tap released -> gui_next_page()");
#endif
        gui_next_page();
    }

    data->state = LV_INDEV_STATE_REL;
}

// Periodically turns the screen off after the configured idle time.
static void idle_timer_cb(lv_timer_t *t)
{
    LV_UNUSED(t);
    if (backlight_timeout_ms == 0 || screen_asleep)
        return;
    if (millis() - last_activity_ms >= backlight_timeout_ms)
    {
        display_sleep(true);
        screen_asleep = true;
    }
}

void touch_set_backlight_timeout(uint32_t seconds)
{
    backlight_timeout_ms = seconds * 1000UL;
    last_activity_ms = millis();
    // If sleeping was just disabled, make sure the screen is on.
    if (backlight_timeout_ms == 0 && screen_asleep)
    {
        display_sleep(false);
        screen_asleep = false;
    }
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

    last_activity_ms = millis();
    lv_timer_create(idle_timer_cb, 1000, NULL); // checks the idle timeout

    Serial.println("Touch initialized (XPT2046)");
}
