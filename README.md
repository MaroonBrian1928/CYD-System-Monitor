# CYD System Monitor (ESP32 "Cheap Yellow Display")

A sleek system monitoring display powered by ESP32 that shows real-time system metrics from a Glances server. Features a customizable UI with dark/light theme support using LVGL graphics library, a **touchscreen interface**, and power-saving display controls.

![Unraid](Images/device.jpeg)

> **Note — this fork.** This build targets the **dual-USB Cheap Yellow Display
> (ESP32-2432S028, CH340, ST7789 panel)** and pulls from the **Glances v4 API**.
> Relative to upstream it adds **GPU/VRAM monitoring**, a **touchscreen UI**
> (swipe paging, a Docker container page, and tap-for-detail popups), a fix for
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
  - **Swipe left/right** between pages (page-dot indicator at the bottom)
  - **Page 1** — the metrics dashboard
  - **Page 2** — a live **Docker container** table (name / status / CPU% / memory),
    sorted by CPU, with color-coded status, scrollable for long lists
  - **Tap any tile** on the dashboard for a detail popup of that metric

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
(CLK 25, MOSI 32, MISO 39, CS 33, IRQ 36), configured in `include/touch.h`.

Resistive panels vary unit-to-unit, so taps may need a one-time calibration:

1. Set `#define TOUCH_DEBUG 1` in `include/touch.h` and reflash.
2. Open the serial monitor, tap the four corners, and note the raw min/max X/Y.
3. Put those values in `TOUCH_RAW_X_MIN/MAX` and `TOUCH_RAW_Y_MIN/MAX`.
4. If taps land on the wrong axis or are mirrored, flip `TOUCH_SWAP_XY`,
   `TOUCH_INVERT_X`, or `TOUCH_INVERT_Y` (0/1).
5. Set `TOUCH_DEBUG` back to `0` and reflash.

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
