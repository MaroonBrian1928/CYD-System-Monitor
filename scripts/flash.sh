#!/usr/bin/env bash
#
# Build and flash the CYD System Monitor without sudo.
#
# One-time setup (so the serial port is accessible without root):
#     sudo usermod -aG dialout "$USER"
#     # then log out and back in (or reboot) for the group to take effect
#
# Find your serial port first (it depends on your machine / what else is plugged
# in, so it will likely NOT be the default below):
#     ls /dev/ttyUSB* /dev/ttyACM*     # or: pio device list
#
# Usage:
#     scripts/flash.sh           # firmware + filesystem (web UI)
#     scripts/flash.sh fw        # firmware only
#     scripts/flash.sh fs        # filesystem (web UI) only
#     scripts/flash.sh monitor   # open the serial monitor
#     PORT=/dev/ttyUSB0 scripts/flash.sh        # use a specific port
#
set -euo pipefail

# Default port -- override per-run with `PORT=/dev/ttyXXX scripts/flash.sh`, or
# change this line to match your setup.
PORT="${PORT:-/dev/ttyUSB1}"
PIO="${PIO:-$HOME/.cyd-pio-venv/bin/pio}"
export PLATFORMIO_CORE_DIR="${PLATFORMIO_CORE_DIR:-$HOME/.platformio}"

cd "$(dirname "$0")/.."

if [ ! -x "$PIO" ]; then
  echo "Error: pio not found at '$PIO'. Set PIO=/path/to/pio." >&2
  exit 1
fi

target="${1:-all}"

# The monitor and uploads all need read/write access to the serial port.
if [ ! -e "$PORT" ]; then
  echo "Error: serial port $PORT not found. Is the device plugged in?" >&2
  echo "Available serial ports:" >&2
  ports=$(ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null || true)
  if [ -n "$ports" ]; then
    echo "$ports" | sed 's/^/    /' >&2
  else
    echo "    (none found)" >&2
  fi
  echo "Pick yours and pass it, e.g.: PORT=/dev/ttyUSB0 $0" >&2
  exit 1
fi
if [ ! -w "$PORT" ]; then
  echo "Error: $PORT is not writable by $(whoami) (needs the 'dialout' group)." >&2
  echo "Run once, then log out/in:" >&2
  echo "    sudo usermod -aG dialout $USER" >&2
  exit 1
fi

case "$target" in
  fw | firmware)
    "$PIO" run -t upload --upload-port "$PORT"
    ;;
  fs | spiffs | uploadfs)
    "$PIO" run -t uploadfs --upload-port "$PORT"
    ;;
  all)
    "$PIO" run -t upload --upload-port "$PORT"
    "$PIO" run -t uploadfs --upload-port "$PORT"
    ;;
  monitor)
    exec "$PIO" device monitor -p "$PORT" -b 115200
    ;;
  *)
    echo "Usage: $0 [fw|fs|all|monitor]" >&2
    exit 1
    ;;
esac

echo "Done."
