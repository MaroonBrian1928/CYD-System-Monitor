# CYD System Monitor (ESP32 "Cheap Yellow Display")

A sleek system monitoring display powered by ESP32 that shows real-time system metrics from a Glances server. Features a customizable UI with dark/light theme support using LVGL graphics library, a **touchscreen interface**, and power-saving display controls.

![Unraid](Images/device.jpeg)

> **Note — this fork.** This build targets the **dual-USB Cheap Yellow Display
> (ESP32-2432S028, CH340, ST7789 panel)** and pulls from the **Glances v4 API**.
> Relative to upstream it adds **GPU/VRAM monitoring**, a **touchscreen UI**
> (tap-to-switch paging and a Docker container page), a fix for
> the panel's color inversion, and a larger flash partition. The `platformio.ini`
> here is **self-contained** — TFT_eSPI is configured entirely via build flags, so
> you do not need an external Arduino libraries folder.

## Features

- Real-time monitoring of:
  - CPU usage with core count
  - RAM utilization with total capacity
  - Disk usage percentage
  - GPU utilization (NVIDIA, via the Glances `gpu` plugin)
  - VRAM utilization
  - System temperature with color-coded warnings
  - Network traffic (upload/download) with auto-scaling units (B/KB/MB)
  - Uptime

- Touchscreen interface (XPT2046):
  - **Tap the screen** to switch pages (page-dot indicator at the bottom)
  - **Page 1** — the metrics dashboard
  - **Page 2** — a live **Docker container** table (name / status / CPU% / memory),
    sorted by CPU, with color-coded status, scrollable for long lists
  - **Auto-cycle pages** — optionally rotate through the UI pages on a
    configurable timer (this cycles between pages; it does not change the screen
    orientation). Toggle it and set the seconds-per-page from the web UI.

- Web interface for configuration:
  - Real-time theme customization
  - System statistics dashboard
  - Glances server configuration
  - Device control and monitoring
  - Display power management

![Web UI](Images/web.png)

- Home Assistant integration:
  - REST API endpoints
  - Device status monitoring & control
  - Remote theme control

## Requirements

- ESP32 development board
- TFT display compatible with TFT_eSPI library
  - I'm using this cheap yellow display with ESP32 built in: [aliexpress](https://s.click.aliexpress.com/e/_olrdG2w)
  - The settings in this project are for this display.
- Glances server running on the target system

## Setup

1. Clone this repository
2. Open the project in PlatformIO

   - Copy the config into place (`platformio.ini` is gitignored):

     ```bash
     cp platformio.example.ini platformio.ini
     ```

   - This config is **self-contained** and ready to build for the dual-USB CYD —
     TFT_eSPI is configured via build flags and LVGL/ArduinoJson/XPT2046 are pulled
     in as managed dependencies. You do **not** need an external Arduino libraries
     folder.

3. Configure your TFT display settings:
   - Modify TFT_eSPI settings according to your display's configuration
   - Adjust screen resolution in config.h if needed:

   ```cpp

   extern const uint16_t screenWidth = 240;
   extern const uint16_t screenHeight = 320;

   ```

4. Configure your network settings in credentials.example.h:

   - Rename the file to `credentials.h`
   - Edit the file to set your WiFi SSID and password

   ```cpp

    const char*const WIFI_SSID = "your_ssid_here";
    const char* const WIFI_PASSWORD = "your_password_here";

   ```

5. Build and upload the project using PlatformIO

6. Set up your Glances server configuration in the web interface:

   - Access the web interface at the device's IP address
   - Configure the Glances server IP address and port
   - Choose theme colors if you want to change them
   - Save the configuration

### Display & flashing notes (this fork)

- **Color inversion fix.** The dual-USB CYD's ST7789 panel renders inverted by
  default (dark theme shows as light, cyan as red, purple as green). This is
  corrected with `-D TFT_INVERSION_OFF` in `platformio.ini`. If your panel ever
  looks like a photo negative, switch it to `TFT_INVERSION_ON`.
- **Partition scheme.** `platformio.ini` sets `board_build.partitions =
  min_spiffs.csv` (1.9 MB app / ~190 KB SPIFFS) to make room for the touch UI.
  Because the partition layout differs from the default, you must run the
  filesystem upload **once** after flashing firmware so the web UI is re-written
  to the new SPIFFS region:

  ```bash
  pio run -t upload      # firmware
  pio run -t uploadfs    # web UI (one-time, after the partition change)
  ```

### Touchscreen calibration

Touch uses the on-board **XPT2046** controller on a dedicated SPI bus
(CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36). Calibration is **stored in NVS and
configurable from the web UI** (no reflashing) — resistive panels vary
unit-to-unit, so each one may need tuning:

1. Open the web interface and find the **Touch Calibration** card.
2. Press each corner of the device's screen and watch the **Live raw (X, Y)**
   value update; note the raw min/max for X and Y.
3. Enter those in the Raw X/Y min/max fields and click **Save Calibration** — it
   applies instantly.
4. If taps land on the wrong axis or are mirrored, toggle **Swap X/Y axes**,
   **Invert X**, or **Invert Y** and save again.
5. **Reset Calibration** restores the built-in defaults.

If you already know a panel's values, set them once as **build flags** in
`platformio.ini` so it flashes ready-to-use (no web config needed):

```ini
-D TOUCH_CAL_DEFAULT_X_MIN=390
-D TOUCH_CAL_DEFAULT_X_MAX=3670
-D TOUCH_CAL_DEFAULT_Y_MIN=300
-D TOUCH_CAL_DEFAULT_Y_MAX=3750
-D TOUCH_CAL_DEFAULT_SWAP_XY=true
-D TOUCH_CAL_DEFAULT_INVERT_X=true
-D TOUCH_CAL_DEFAULT_INVERT_Y=false
```

These only seed the NVS defaults (the web UI still overrides at runtime). If
omitted, the fallback values in `include/settings_manager.h` are used. For
low-level debugging you can also set `#define TOUCH_DEBUG 1` in `include/touch.h`
to print raw + mapped coordinates to the serial monitor.

### Home Assistant Integration

The device exposes REST API endpoints for Home Assistant integration:

- GET `/api/status` - Device status and metrics
- POST `/api/command` - Control endpoints for theme switching and device restart

#### Easy Integration

A complete Home Assistant configuration example is provided in [homeassistant_example.yml](homeassistant_example.yml). This includes:

- System sensors (temperature, memory, WiFi signal, uptime)
- Binary sensors for dark mode and display state
- Switches for controlling dark mode and display power
- Commands for device restart and theme reset

Simply copy the configuration, replace `YOUR.DEVICE.IP.HERE` with your device's IP address, and add it to your Home Assistant configuration.
You should see the entities show up in home assistant after a restart.

## API Endpoints

### Web Interface Endpoints

- GET `/settings` - Returns:
  - Current device settings and theme colors
  - System metrics (CPU, memory, temperature)
  - Network information
  - Device information (chip model, SDK version, etc.)
  - Hardware statistics (heap, PSRAM, flash)

- POST `/settings` - Update device settings:
  - Theme colors
  - Dark/light mode
  - Glances server configuration

- POST `/restart` - Restart device
- POST `/resetTheme` - Reset theme to defaults
- POST `/displaySleep` - Control display power state:

  ```json
  {
    "sleep": true|false
  }
  ```

### Home Assistant Endpoints

- GET `/api/status` - Returns:
  - Temperature
  - Free heap memory
  - WiFi signal strength
  - Uptime
  - Dark mode state
  - Display state

- POST `/api/command` - Control endpoints for:
  - Theme switching (`dark_mode`: true|false)
  - Display power (`display`: true|false)
  - Device restart (`restart`: true)
  - Theme reset (`reset_theme`: true)

## Contributing

Feel free to submit issues, fork the repository, and create pull requests for any improvements.
