cmake_minimum_required(VERSION 3.16)

# ColdWatch does not use WiFi/Bluetooth networking. Exclude these components
# entirely so their prebuilt binary blobs (large git submodules) are not
# required at link time.
set(EXCLUDE_COMPONENTS "esp_wifi" "bt" "wifi_provisioning" "esp_local_ctrl" "openthread" "ieee802154")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(ColdWatch_ESP32)


