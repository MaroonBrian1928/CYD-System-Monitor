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
// consecutive "untouched" reads before treating it as a real release. Kept small
// so taps stay snappy; larger values add noticeable tap latency.
#define RELEASE_DEBOUNCE 3

// A press whose travel from the touch-down point exceeds this (screen pixels) is
// a drag -- it scrolls the content underneath (e.g. the container list) instead
// of counting as a tap. A tap still advances to the next page, on every page.
// Set above the panel's touch jitter so a still tap isn't read as a drag.
#define TAP_MOVE_THRESHOLD 22

// This resistive panel reports compressed travel (a full-screen swipe registers
// only a fraction of the screen), so we scroll the container list ourselves from
// the per-read touch delta, multiplied by this factor so a short drag covers a
// useful range. Increase for faster scrolling, decrease if it overshoots.
#define SCROLL_GAIN 4

// Per-read movement below this (screen px) is treated as touch jitter and not
// scrolled -- keeps a still tap from nudging the list (and from being mistaken
// for a scroll). A press whose total scroll exceeds SCROLL_PAGE_GUARD counts as
// a scroll, so it won't also flip the page on release.
#define SCROLL_DEADZONE 3
#define SCROLL_PAGE_GUARD 8

// Auto-recovery: if the controller reports "touched" but only emits invalid rail
// samples (e.g. a constant (4095,0) z=4095) for this long, its SPI/state may be
// wedged -- re-init the touch bus to try to recover without a physical replug.
// (Note: a true power brownout can't be fixed in firmware; clean power is the
// real cure. This only rescues a stuck controller *state*.)
#define TOUCH_STUCK_RECOVER_MS 3000

// (Re)initialise the XPT2046 on its dedicated SPI bus. Called once at startup and
// again by the stuck-controller recovery path below.
static void touch_begin_hw()
{
    // end() first so a re-init actually re-configures the bus: the ESP32
    // SPIClass::begin() is a no-op if the peripheral is already started. end() is
    // a safe no-op on the very first (startup) call when nothing is initialised.
    touchSPI.end();
    touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    ts.begin(touchSPI);
    ts.setRotation(0); // we apply our own rotation/calibration above
}

static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static bool pressed = false;
    static int16_t last_x, last_y;
    static int16_t press_start_x, press_start_y;
    static int16_t prev_y;          // last screen-y, for per-read scroll deltas
    static int16_t scroll_total;    // net raw px scrolled this press (signed)
    static int16_t last_scroll_vel; // last applied (scaled) scroll delta, for fling
    static bool moved = false;      // press travelled far enough to be a drag, not a tap
    static int release_count = 0;

    // Read and validate the sample first. This XPT2046 emits phantom "rail"
    // samples (e.g. raw (4095,0)) as electrical noise -- often a continuous
    // stream after a screen redraw -- which would otherwise look like a finger
    // stuck in the corner and block every later tap. A real press always lands
    // inside the panel's range, so reject anything at/near the rails.
    bool touched = false;
    TS_Point p;
    bool hw_touched = ts.touched();
    if (hw_touched)
    {
        p = ts.getPoint();
        if (p.x > 100 && p.x < 4000 && p.y > 100 && p.y < 4000)
            touched = true;
    }

    // Stuck-controller auto-recovery: a wedged XPT2046 asserts "touched"
    // continuously while only returning invalid rail samples. A real finger
    // produces valid coordinates (which clear the timer), so only a sustained
    // touched-but-invalid stream trips this and triggers a touch-bus re-init.
    static uint32_t stuck_since = 0;
    static bool recovering = false;
    if (hw_touched && !touched)
    {
        if (stuck_since == 0)
            stuck_since = millis();
        else if (millis() - stuck_since > TOUCH_STUCK_RECOVER_MS)
        {
            if (!recovering) // log once per stuck episode, not every retry
            {
                Serial.println("[touch] stuck controller detected -> re-initializing touch bus");
                recovering = true;
            }
            touch_begin_hw();
            stuck_since = millis(); // re-arm: retry again after another interval
        }
    }
    else
    {
        if (recovering)
        {
            Serial.println("[touch] touch recovered");
            recovering = false;
        }
        stuck_since = 0;
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
        if (!pressed) // touch-down: remember where the press began
        {
            press_start_x = x;
            press_start_y = y;
            prev_y = y;
            scroll_total = 0;
            last_scroll_vel = 0;
            moved = false;
            gui_container_fling_stop(); // a new touch halts any momentum coast
        }
        else
        {
            if (abs(x - press_start_x) > TAP_MOVE_THRESHOLD ||
                abs(y - press_start_y) > TAP_MOVE_THRESHOLD)
                moved = true; // dragged far enough to be a scroll, not a tap

            // Drive the container list scroll directly from the touch delta
            // (ignoring sub-pixel jitter), amplified for this panel's compressed
            // movement. Track net travel + last velocity for fling/page logic.
            int16_t dy_raw = y - prev_y;
            if (abs(dy_raw) >= SCROLL_DEADZONE)
            {
                if (gui_container_page_active())
                {
                    int16_t scaled = (int16_t)(dy_raw * SCROLL_GAIN);
                    gui_container_scroll_by(scaled);
                    last_scroll_vel = scaled;
                    scroll_total += dy_raw;
                }
                prev_y = y;
            }
        }
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

    // Real release:
    //  - If the gesture scrolled the container list, hand off to momentum and do
    //    NOT page (so scrolling can't accidentally flip the page).
    //  - Otherwise a tap (no significant movement) advances to the next page.
    if (pressed)
    {
        pressed = false;
        release_count = 0;
        if (gui_container_page_active() && abs(scroll_total) > SCROLL_PAGE_GUARD)
        {
            gui_container_fling(last_scroll_vel);
        }
        else if (!moved)
        {
#if TOUCH_DEBUG
            Serial.println("[touch] tap released -> gui_next_page()");
#endif
            gui_next_page();
        }
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
    touch_begin_hw();

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read;
    // Make scrolling cover more distance per swipe: engage sooner (lower limit)
    // and coast further after release (lower throw = more momentum). Defaults are
    // 10/10; lower throw retains more velocity.
    indev_drv.scroll_limit = 5;
    indev_drv.scroll_throw = 4;
    lv_indev_drv_register(&indev_drv);

    last_activity_ms = millis();
    lv_timer_create(idle_timer_cb, 1000, NULL); // checks the idle timeout

    Serial.println("Touch initialized (XPT2046)");
}
