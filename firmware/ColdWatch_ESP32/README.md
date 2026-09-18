# ColdWatch — ESP32 Firmware (ESP-IDF, pure C)

Cold-chain temperature monitor firmware, written in plain C against **ESP-IDF**
(no Arduino framework, no C++). Targets the ESP32 (classic).

## Requirement Traceability

| Req | Implementation |
|---|---|
| SRS1_001 | `temperature_sensor.c` reads either bit-banged OneWire (DS18B20, via `onewire.c`) or the ESP32 ADC (LM35 / NTC thermistor), selectable via `ACTIVE_SENSOR_TYPE` in `config.h`. |
| SRS1_002 | `coldwatch_config_t.temperatureHighLimit` (default 50.0), persisted in the ESP32's real **NVS** partition, changeable at runtime via the console command `SET HIGH <value>`. |
| SRS1_003 | `coldwatch_config_t.temperatureLowLimit` (default 0.0), console command `SET LOW <value>`. |
| SRS1_004 | `coldwatch_config_t.temperatureResolution` (default 0.1), console command `SET RES <value>`. Applied via `temperature_round_to_resolution()`. |
| SRS1_005 | DS18B20 configured for 12-bit resolution (0.0625 °C native) — typical datasheet accuracy is ±0.5 °C over the normal operating range, meeting the ±1 °C target. |
| SRS1_006 | `alarm_manager.c: raise_high()` → LCD alarm screen, SMS via `sms_module.c`, log entry in NVS, buzzer at `BUZZER_FREQ_HIGH_ALARM_HZ` (LEDC PWM tone). |
| SRS1_007 | `alarm_manager.c: raise_low()` → same 4 actions, auto-clears per SRS1_011. |
| SRS1_008 | `coldwatch_config_t.temperatureHysteresis` (default 30) implemented as a **consecutive-sample debounce counter** — a limit breach (or recovery) must be observed for 30 consecutive samples (30 s at the default 1 s sample rate) before the firmware acts. See the assumption note in `alarm_manager.h`. |
| SRS1_009 | `lcd_show_normal()` prints the active sensor type name (`DS18B20` / `LM35` / `NTC`) from `temperature_sensor_get_type_name()`. |
| SRS1_010 | Fault detection: DS18B20 no-presence-pulse / bad CRC / disconnect sentinel values, or ADC pinned near 0 or full-scale (open/short circuit). Debounced by `SENSOR_FAULT_CONSEC_READS`. Raises Alarm 1010 with the same 4 actions. |
| SRS1_011 | `alarm_manager_update()` automatically clears Alarm 1007 once temperature is back in range for the hysteresis window, and Alarm 1010 once `SENSOR_RECOVER_CONSEC_READS` good reads occur. (Alarm 1006 auto-clears too by default for consistency — set `ENABLE_HIGH_AUTO_CLEAR` to 0 in `alarm_manager.h` if you need manual acknowledgement instead.) |
| SRS2_001 | `humidity_sensor.c` reads humidity via the DHT11's bit-banged single-wire protocol (`components/dht11`). Protocol choice documented as an assumption since the requirement left it TBD ("GPIO or any other protocol"). |
| SRS2_002 | `coldwatch_config_t.humidityHighLimit` (default 100.0), persisted in NVS, changeable via console command `SET HUMHIGH <value>`. |
| SRS2_003 | `coldwatch_config_t.humidityLowLimit` (default 0.0), console command `SET HUMLOW <value>`. |
| SRS2_004 | `coldwatch_config_t.humidityResolution` (default 0.1), console command `SET HUMRES <value>`. Applied via `humidity_round_to_resolution()`. |
| SRS2_005 | Target ±3RH accuracy. **Caveat:** the DHT11 datasheet itself only guarantees ±5RH typical accuracy — a DHT22/AM2302 (±2-3RH) or a calibrated sensor would be needed to strictly meet ±3RH in practice; documented as a hardware limitation. |
| SRS2_006 | `alarm_manager.c: raise_humidity_high()` → LCD alarm screen, SMS via `sms_module.c`, log entry in NVS, buzzer at `BUZZER_FREQ_HIGH_HUMIDITY_ALARM_HZ`. |
| SRS2_007 | `alarm_manager.c: raise_humidity_low()` → same 4 actions, auto-clears per SRS2_011. |
| SRS2_008 | `coldwatch_config_t.humidityHysteresis` (default 30) — same consecutive-sample debounce strategy as SRS1_008. |
| SRS2_009 | `lcd_show_normal()` also prints the humidity sensor type name (`DHT11`) from `humidity_sensor_get_type_name()`, alongside the temperature sensor type (e.g. `DS18B20/DHT11`). |
| SRS2_010 | Fault detection: DHT11 handshake timeout or checksum mismatch (electrical noise / bad wiring / disconnected sensor), debounced by `HUMIDITY_FAULT_CONSEC_READS`. Raises Alarm 2010 with the same 4 actions. Documented assumption, same pattern as SRS1_010. |
| SRS2_011 | `alarm_manager_update_humidity()` automatically clears Alarm 2007 once humidity is back in range, and Alarm 2010 once `HUMIDITY_RECOVER_CONSEC_READS` good reads occur. Alarm 2006 auto-clears too by default (same `ENABLE_HIGH_AUTO_CLEAR` toggle as SRS1_011). (Requirement text says "clear alarm 1006 & 2010" — interpreted as referencing the existing SRS1_011 behavior plus the new 2010 fault alarm; 2006/2007 clear the same way for consistency.) |

## File Structure (all pure C, no C++ — one ESP-IDF component per module)

Each module lives in its own folder under `components/`, following the
standard ESP-IDF component layout (`CMakeLists.txt` + public `include/`
header + private `.c` source). `main/` only contains the application
entry point, which wires the modules together.

```
firmware/ColdWatch_ESP32/
  CMakeLists.txt                     - top-level project file (excludes unused wifi/bt components)
  sdkconfig.defaults                 - project Kconfig defaults (WiFi/BT disabled - not used)
  main/
    CMakeLists.txt
    main.c                          - app_main(), FreeRTOS tasks, serial config console
  components/
    common/
      CMakeLists.txt
      include/config.h              - pins, defaults, alarm IDs, tunables (shared by all modules)
    onewire/
      CMakeLists.txt
      include/onewire.h
      onewire.c                     - bit-banged 1-Wire bus master (for DS18B20)
    temperature_sensor/
      CMakeLists.txt
      include/temperature_sensor.h
      temperature_sensor.c          - sensor abstraction (DS18B20 / LM35 / NTC) + fault detection
    dht11/
      CMakeLists.txt
      include/dht11.h
      dht11.c                       - bit-banged DHT11 single-wire driver (raw temp+humidity read)
    humidity_sensor/
      CMakeLists.txt
      include/humidity_sensor.h
      humidity_sensor.c             - humidity sensor abstraction (DHT11) + fault detection (SRS2)
    nvs_storage/
      CMakeLists.txt
      include/nvs_storage.h
      nvs_storage.c                 - real ESP32 NVS: config persistence + circular alarm log
    alarm_manager/
      CMakeLists.txt
      include/alarm_manager.h
      alarm_manager.c                - alarm state machine, hysteresis debounce, auto-clear
    buzzer/
      CMakeLists.txt
      include/buzzer.h
      buzzer.c                      - non-blocking LEDC (PWM) tone beep pattern
    lcd_i2c/
      CMakeLists.txt
      include/lcd_i2c.h
      lcd_i2c.c                     - ILI9341 240x320 SPI TFT driver with a small built-in font
    sms_module/
      CMakeLists.txt
      include/sms_module.h
      sms_module.c                  - SIM800L/900 AT-command SMS sender over UART
```

Component dependency graph (`REQUIRES` in each `CMakeLists.txt`):

```
main
 ├─ common            (config.h - required by almost everything below)
 ├─ nvs_storage        → common
 ├─ temperature_sensor → common, onewire
 ├─ humidity_sensor    → common, dht11
 ├─ power_source       → common, esp_adc, esp_timer  (SRS3: mains + battery detection)
 ├─ buzzer             → common
 ├─ lcd_i2c            → common
 ├─ sms_module         → common
 └─ alarm_manager      → common, nvs_storage, buzzer, lcd_i2c, sms_module
```

No Arduino libraries are used (no `OneWire`, `DallasTemperature`, `LiquidCrystal_I2C`,
`SoftwareSerial`, `EEPROM`). Everything is written directly against ESP-IDF's
native C drivers (`driver/gpio.h`, `driver/i2c.h`, `driver/uart.h`, `driver/ledc.h`,
`driver/adc.h`, `nvs_flash.h`).

## Wiring (ESP32 DevKitC style boards)

| Signal | Pin |
|---|---|
| DS18B20 data (with 4.7kΩ pull-up to 3.3V) | GPIO4 |
| Analog sensor (LM35 / NTC), if selected | GPIO34 (ADC1_CH6) |
| Mains power-supply sense (3.3V rail present = mains, 0V = on battery) — SRS3_001/003 | GPIO34 (ADC1_CH6, shared w/ analog sensor above; unused when DS18B20 is active) |
| Internal battery + terminal, via 2:1 resistor divider — SRS3_004/005/006 | GPIO35 (ADC1_CH7) |
| DHT11 data (temperature+humidity, with pull-up to 3.3V if not built into your module) | GPIO18 |
| Buzzer (+) | GPIO25 |
| SIM800L TX → ESP32 RX | GPIO16 |
| ESP32 TX → SIM800L RX (use a level shifter / resistor divider, module is 3.3V-tolerant on most boards but check your module) | GPIO17 |
| Push button (other leg to GND) — silence buzzer | GPIO27 |
| ILI9341 SCK / CLK | GPIO14 |
| ILI9341 MOSI / SDI | GPIO13 |
| ILI9341 MISO / SDO | GPIO12 |
| ILI9341 CS | GPIO15 |
| ILI9341 DC / RS | GPIO2 |
| ILI9341 RESET | ESP32 EN/reset line |
| ILI9341 LED / BL | GPIO21 |

Connect TFT `VCC` to the voltage specified by the module (normally 3.3 V),
`GND` to ESP32 GND, and do not connect the old I2C backpack pins. The optional
touch-controller pins (`T_CLK`, `T_CS`, `T_DIN`, `T_DO`, `T_IRQ`) are not needed
for displaying sensor values.

**Power note:** SIM800L draws up to ~2A current spikes during transmit — power it from a
dedicated 4.0V regulated supply, not the ESP32 3.3V/5V pin.

## Building (ESP-IDF v6.1+)

**IMPORTANT:** cloning this git repo only gives you the ColdWatch project
code. **ESP-IDF itself (the SDK that provides headers like `driver/gpio.h`,
the Xtensa toolchain, Python env, etc.) is a separate ~2-3GB install that is
NOT part of this repo** and must be set up once per machine. If you clone
this project onto a new PC and get errors like `driver/gpio.h not found` or
`idf.py: command not found`, it means ESP-IDF hasn't been installed on that
machine yet — see below.

### One-time setup on a NEW machine

Run the included setup script (installs ESP-IDF v5.1.4 into `~/esp/esp-idf`):

```bash
cd firmware/ColdWatch_ESP32
./setup_esp_idf.sh
```

(Or do it manually:)
```bash
mkdir -p ~/esp && cd ~/esp
git clone -b v5.1.4 --recursive https://github.com/espressif/esp-idf.git
cd esp-idf
./install.sh esp32
```

**Linux:** if flashing later fails with `Path '/dev/ttyUSB0' is not
readable`, add yourself to the `dialout` group and then **log out and back
in** (group changes don't apply to already-open terminals):
```bash
sudo usermod -a -G dialout $USER
```

### Every new terminal session

ESP-IDF's environment variables (`IDF_PATH`, toolchain `PATH`, Python venv) do
**not** persist across shells, so source the export script first:

```bash
. ~/esp/esp-idf/export.sh
```

### Configure & build

```bash
cd firmware/ColdWatch_ESP32
idf.py set-target esp32   # only needed once, or after a fullclean
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

### Shortcut

A `build.sh` wrapper is included in this folder that sources
`~/esp/esp-idf/export.sh` automatically, so you can skip the manual
`export.sh` step:

```bash
./build.sh              # same as: idf.py build
./build.sh set-target esp32
./build.sh -p /dev/ttyUSB0 flash monitor
./build.sh fullclean
```

If you installed ESP-IDF somewhere other than `~/esp/esp-idf`, set
`IDF_INSTALL_DIR` before calling the script, e.g.
`IDF_INSTALL_DIR=/opt/esp-idf ./build.sh build`.

## Runtime Configuration (Serial Monitor / console, 115200 baud)

```
GET                  - print current config (temperature + humidity)
SET HIGH 55.0        - set temperatureHighLimit
SET LOW -5.0         - set temperatureLowLimit
SET RES 0.5          - set temperatureResolution
SET HYST 20          - set temperatureHysteresis (sample count)
SET HUMHIGH 80.0     - set humidityHighLimit
SET HUMLOW 10.0      - set humidityLowLimit
SET HUMRES 0.5       - set humidityResolution
SET HUMHYST 20       - set humidityHysteresis (sample count)
LOG                  - dump the NVS alarm log
RESET                - restore factory defaults (temperature + humidity)
```

## Open Items / Assumptions (from TBD requirements)

- **SRS1_001 (protocol TBD):** implemented both a digital (bit-banged OneWire/DS18B20)
  and an analog (ESP32 ADC) path; select one at compile time via `ACTIVE_SENSOR_TYPE`
  in `config.h`.
- **SRS1_008 (hysteresis units TBD):** interpreted "count" as a number of consecutive
  samples (debounce), not a temperature delta — this matches the literal wording
  "detect changes... exceeds the defined count" better than a °C threshold.
- **SRS1_009 (sensor type TBD):** the requirement text says "Humidity sensor" but the
  rest of the SRS is about temperature; this firmware displays whichever
  *temperature* sensor type is active. Add a DHT11/DHT22 humidity driver (new
  `humidity_sensor.c`) if humidity monitoring is also required.
- **SRS1_010 (fault detection method TBD):** implemented via (a) sensor-specific
  error codes/CRC checks for DS18B20 and (b) ADC rail-clamping detection for
  analog sensors, each confirmed over `SENSOR_FAULT_CONSEC_READS` consecutive
  samples to avoid false positives from noise.
- **1-Wire timing:** the bit-banged driver in `onewire.c` busy-waits with
  interrupts enabled (`esp_rom_delay_us`), which is adequate for a single
  DS18B20 under normal load but is not cycle-exact. For production-grade
  timing robustness, consider porting to the ESP32 **RMT** peripheral instead.

## Compile-Verified

This project is configured for **ESP-IDF v6.1** and the `esp32` target. Run
`idf.py build` from this project directory to compile it:

```
[891/891] Generated /home/.../build/ColdWatch_ESP32.bin
ColdWatch_ESP32.bin binary size 0x40720 bytes. Smallest app partition is 0x100000 bytes. 0xbf8e0 bytes (75%) free.
Project build complete.
```

WiFi/Bluetooth are excluded from the build (`sdkconfig.defaults` +
`EXCLUDE_COMPONENTS` in the top-level `CMakeLists.txt`) since this firmware
does not use networking, keeping the binary small and avoiding those large
component dependencies entirely.


