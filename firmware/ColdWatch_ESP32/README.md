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
      lcd_i2c.c                     - I2C 16x2 HD44780/PCF8574 LCD driver (bit-level, no Arduino lib)
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
| Buzzer (+) | GPIO25 |
| SIM800L TX → ESP32 RX | GPIO16 |
| ESP32 TX → SIM800L RX (use a level shifter / resistor divider, module is 3.3V-tolerant on most boards but check your module) | GPIO17 |
| Push button (other leg to GND) — silence buzzer | GPIO27 |
| LCD I2C SDA / SCL | GPIO21 / GPIO22 |

**Power note:** SIM800L draws up to ~2A current spikes during transmit — power it from a
dedicated 4.0V regulated supply, not the ESP32 3.3V/5V pin.

## Building (ESP-IDF v5.1+)

```bash
. $IDF_PATH/export.sh
cd firmware/ColdWatch_ESP32
idf.py set-target esp32
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## Runtime Configuration (Serial Monitor / console, 115200 baud)

```
GET                  - print current config
SET HIGH 55.0        - set temperatureHighLimit
SET LOW -5.0         - set temperatureLowLimit
SET RES 0.5          - set temperatureResolution
SET HYST 20          - set temperatureHysteresis (sample count)
LOG                  - dump the NVS alarm log
RESET                - restore factory defaults
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

This project was configured and built successfully against **ESP-IDF v5.1.4**
for the `esp32` target using `idf.py build`:

```
[891/891] Generated /home/.../build/ColdWatch_ESP32.bin
ColdWatch_ESP32.bin binary size 0x40720 bytes. Smallest app partition is 0x100000 bytes. 0xbf8e0 bytes (75%) free.
Project build complete.
```

WiFi/Bluetooth are excluded from the build (`sdkconfig.defaults` +
`EXCLUDE_COMPONENTS` in the top-level `CMakeLists.txt`) since this firmware
does not use networking, keeping the binary small and avoiding those large
component dependencies entirely.


