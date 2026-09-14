#!/usr/bin/env bash
# setup_esp_idf.sh
# One-time environment setup for building ColdWatch_ESP32 on a NEW machine.
#
# This project's CMakeLists.txt files are all correct out-of-the-box (they
# already declare "REQUIRES driver" etc. wherever needed). If you see errors
# like "driver/gpio.h not found" it means ESP-IDF itself (the SDK that
# provides that header) is simply not installed yet on this machine -
# cloning this git repo does NOT include ESP-IDF, which is a separate,
# large (~2-3GB) toolchain that must be installed once per machine.
#
# This script installs ESP-IDF v5.1.4 into ~/esp/esp-idf (override with
# IDF_INSTALL_DIR) and runs its installer for the esp32 target.
#
# Usage:
#   ./setup_esp_idf.sh
#
# After it finishes, use ./build.sh (in this same folder) to build/flash -
# it automatically sources the environment this script installs.

set -e

IDF_VERSION="${IDF_VERSION:-v5.1.4}"
IDF_INSTALL_DIR="${IDF_INSTALL_DIR:-$HOME/esp/esp-idf}"

echo "=== ColdWatch / ESP-IDF setup ==="
echo "Target install dir: $IDF_INSTALL_DIR"
echo "ESP-IDF version:     $IDF_VERSION"
echo

if [ -f "$IDF_INSTALL_DIR/export.sh" ]; then
  echo "ESP-IDF already found at $IDF_INSTALL_DIR - skipping clone."
else
  echo "--- Cloning ESP-IDF ($IDF_VERSION) into $IDF_INSTALL_DIR ---"
  echo "(This downloads ~2-3GB including submodules; may take a while.)"
  mkdir -p "$(dirname "$IDF_INSTALL_DIR")"
  git clone -b "$IDF_VERSION" --recursive https://github.com/espressif/esp-idf.git "$IDF_INSTALL_DIR"
fi

echo
echo "--- Running ESP-IDF installer (esp32 target) ---"
(cd "$IDF_INSTALL_DIR" && ./install.sh esp32)

echo
echo "=== Setup complete ==="
echo "ESP-IDF is installed at: $IDF_INSTALL_DIR"
echo
echo "You can now build this project with:"
echo "  ./build.sh build"
echo "  ./build.sh -p /dev/ttyUSB0 flash monitor"
echo
echo "(If IDF_INSTALL_DIR differs from ~/esp/esp-idf, export IDF_INSTALL_DIR"
echo " before calling build.sh, e.g.:"
echo "   IDF_INSTALL_DIR=$IDF_INSTALL_DIR ./build.sh build)"
echo
echo "Linux users: if you also hit a 'port is not readable' /dev/ttyUSB0"
echo "error when flashing, add yourself to the 'dialout' group and log out/in:"
echo "  sudo usermod -a -G dialout \$USER"

