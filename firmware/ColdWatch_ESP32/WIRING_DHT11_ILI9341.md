# ColdWatch Sensor and Display Wiring

Target board: ESP32 DevKitC style board

## DHT11 humidity and temperature sensor

| DHT11 pin | ESP32 connection | Notes |
|---|---|---|
| VCC | 3.3 V | Use the voltage supported by the DHT11 module |
| DATA | GPIO18 | Firmware setting: `DHT11_GPIO` |
| GND | GND | Common ground is required |

Add a 4.7k-10k pull-up resistor from `DATA` to `3.3 V` if the DHT11 module does not already include one.

The firmware reads both humidity and temperature from the DHT11 every 2 seconds and shows the values on the TFT.

## ILI9341 240x320 SPI TFT

| TFT pin | ESP32 connection | Firmware setting |
|---|---|---|
| SDO / MISO | GPIO12 | `LCD_SPI_MISO_GPIO` |
| LED / BL | GPIO21 | `LCD_SPI_BACKLIGHT_GPIO` |
| SCK / CLK | GPIO14 | `LCD_SPI_SCLK_GPIO` |
| SDI / MOSI | GPIO13 | `LCD_SPI_MOSI_GPIO` |
| D/C or RS | GPIO2 | `LCD_SPI_DC_GPIO` |
| RESET | ESP32 EN / RESET | No separate GPIO; configured as `-1` |
| CS | GPIO15 | `LCD_SPI_CS_GPIO` |
| GND | GND | Common ground is required |
| VCC | 3.3 V or 5 V | Depends on the TFT module design |

Do not connect the old I2C LCD backpack pins. This display uses SPI.

### TFT power notes

- Use 3.3 V for a bare ILI9341 module.
- Use 5 V only when the TFT breakout board explicitly supports 5 V input and has level shifting or regulation.
- The ESP32, DHT11, and TFT must share GND.
- GPIO21 drives the backlight control. If the backlight current is more than an ESP32 GPIO can safely supply, use a transistor or MOSFET driver.

### ESP32 boot-pin warning

GPIO2, GPIO12, and GPIO15 are ESP32 strapping pins. The TFT must not force these pins to an invalid level while the ESP32 is booting. If the ESP32 does not boot reliably with the TFT connected, disconnect the TFT during boot or move the TFT signals to safer GPIOs.

## Other ColdWatch connections

| Device | Signal | ESP32 pin |
|---|---|---|
| DS18B20 | DATA | GPIO4 |
| DS18B20 | Pull-up | 4.7k resistor to 3.3 V |
| Buzzer | Positive/control | GPIO25 |
| Acknowledge button | Button input | GPIO27, button to GND |
| SIM800L | TX to ESP32 RX | GPIO16 |
| SIM800L | ESP32 TX to module RX | GPIO17 |

## Firmware checks

The pin definitions are in:

`components/common/include/config.h`

Expected display configuration:

```c
#define DHT11_GPIO              GPIO_NUM_18
#define LCD_SPI_SCLK_GPIO       GPIO_NUM_14
#define LCD_SPI_MOSI_GPIO       GPIO_NUM_13
#define LCD_SPI_MISO_GPIO       GPIO_NUM_12
#define LCD_SPI_CS_GPIO         GPIO_NUM_15
#define LCD_SPI_DC_GPIO         GPIO_NUM_2
#define LCD_SPI_RESET_GPIO      (-1)
#define LCD_SPI_BACKLIGHT_GPIO  GPIO_NUM_21
```
