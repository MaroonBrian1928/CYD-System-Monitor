#include "touch.h"
#include "config.h"
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <lvgl.h>

// Dedicated SPI bus for the touch controller (separate from the display).
static SPIClass touchSPI(VSPI);
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

static void touchpad_read(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    if (!ts.touched())
    {
        data->state = LV_INDEV_STATE_REL;
        return;
    }

    TS_Point p = ts.getPoint();

    // Map raw 0..4095 readings onto the screen via the calibration window.
    int32_t mx = map(p.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, SCREEN_W);
    int32_t my = map(p.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, SCREEN_H);

#if TOUCH_SWAP_XY
    {
        // After a swap the axes change length, so remap each raw value onto the
        // *other* dimension's pixel range.
        int32_t tmpx = map(p.y, TOUCH_RAW_Y_MIN, TOUCH_RAW_Y_MAX, 0, SCREEN_W);
        int32_t tmpy = map(p.x, TOUCH_RAW_X_MIN, TOUCH_RAW_X_MAX, 0, SCREEN_H);
        mx = tmpx;
        my = tmpy;
    }
#endif

#if TOUCH_INVERT_X
    mx = SCREEN_W - mx;
#endif
#if TOUCH_INVERT_Y
    my = SCREEN_H - my;
#endif

    int16_t x = clamp((int16_t)mx, 0, SCREEN_W - 1);
    int16_t y = clamp((int16_t)my, 0, SCREEN_H - 1);

#if TOUCH_DEBUG
    Serial.printf("[touch] raw=(%d,%d) -> screen=(%d,%d)\n", p.x, p.y, x, y);
#endif

    data->point.x = x;
    data->point.y = y;
    data->state = LV_INDEV_STATE_PR;
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
