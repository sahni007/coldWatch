#!/usr/bin/env bash
# Convenience wrapper: sources the ESP-IDF environment (installed at
# ~/esp/esp-idf) and then runs idf.py with whatever arguments you pass in.
#
# Usage examples:
#   ./build.sh build                      # compile the firmware
#   ./build.sh set-target esp32           # (re)configure for the esp32 target
#   ./build.sh -p /dev/ttyUSB0 flash monitor
#   ./build.sh fullclean
#
# If no arguments are given, defaults to "build".

set -e

IDF_INSTALL_DIR="${IDF_INSTALL_DIR:-$HOME/esp/esp-idf}"

if [ ! -f "$IDF_INSTALL_DIR/export.sh" ]; then
  echo "ESP-IDF not found at $IDF_INSTALL_DIR (set IDF_INSTALL_DIR to override)." >&2
  echo "See firmware/ColdWatch_ESP32/README.md 'Building' section for setup steps." >&2
  exit 1
fi

# shellcheck disable=SC1091
. "$IDF_INSTALL_DIR/export.sh" > /tmp/idf_export.log 2>&1

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

if [ "$#" -eq 0 ]; then
  set -- build
fi

exec idf.py "$@"

