# DRAFT: Multi-Screen Touch-Navigated LCD UI

> **STATUS: DRAFT / NOT YET IMPLEMENTED.** This document is a design proposal
> only. No existing source file has been modified to produce this draft -
> the current build (`lcd_show_normal()` / `lcd_show_alarm()` single-screen
> UI) is untouched and still works exactly as before. Review this, then ask
> to implement it and it will be wired in for real.

## 1. Goal

Replace the current single always-on-screen layout with **4 swipeable/
tappable screens**, navigated entirely via the LCD's existing (currently
unused) **touch controller** - no new physical buttons.

| # | Screen    | Content |
|---|-----------|---------|
| 1 | **HOME**   | Project name "COLD STORAGE", date, time, temperature, humidity, one-line overall status (NORMAL / N ALARMS ACTIVE) |
| 2 | **ALARMS** | How many alarms are currently active + a scrollable/paged list of each active alarm's ID + short name |
| 3 | **POWER**  | Power source (MAIN/BATTERY), battery %, battery voltage, low/critical battery flags |
| 4 | **GSM**    | GSM module status: init OK/fail, network registration, signal quality, last SMS send result |

Any screen can be viewed at any time; a **real alarm always force-overrides
whatever screen is selected** (safety-critical - same behavior as today),
then returns to the user's last-selected screen once cleared.

---

## 2. Touch hardware plan (from the earlier feasibility check)

- Controller: **XPT2046** (assumed - standard on ILI9341+touch modules).
- Driver: `atanisoft/esp_lcd_touch_xpt2046` (registry-verified, IDF-6.1
  compatible, depends on `espressif/esp_lcd_touch`).
- Wiring - only **2 new wires**, everything else shares the existing TFT SPI bus:

| Touch pin | Connects to | Notes |
|---|---|---|
| `T_CLK` | GPIO14 (`LCD_SPI_SCLK_GPIO`) | shared with TFT |
| `T_DIN` | GPIO13 (`LCD_SPI_MOSI_GPIO`) | shared with TFT |
| `T_DO`  | GPIO12 (`LCD_SPI_MISO_GPIO`) | shared with TFT |
| `T_CS`  | **GPIO32** (new)  | dedicated chip-select for the touch device on the same SPI bus |
| `T_IRQ` | **GPIO33** (new, optional) | lets firmware skip polling when no touch is present (recommended) |

Proposed new `config.h` macros (draft, not yet added):
```c
#define TOUCH_SPI_CS_GPIO    GPIO_NUM_32
#define TOUCH_IRQ_GPIO       GPIO_NUM_33   // -1 if not wired; driver falls back to polling
#define TOUCH_SWAP_XY        1  // must match LCD's esp_lcd_panel_swap_xy(true)
#define TOUCH_MIRROR_X       0  // calibrate on first boot
#define TOUCH_MIRROR_Y       0  // calibrate on first boot
```

**Calibration caveat:** `swap_xy`/`mirror_x`/`mirror_y` values are
hardware/orientation-dependent and can only be confirmed on the real board
(tap a corner, see if reported X/Y matches) - documented as a first-boot
tuning step, not guessable from the sandbox.

---

## 3. New component: `touch_input` (draft API)

A thin wrapper so the rest of the firmware never touches `esp_lcd_touch`
directly:

```c
// components/touch_input/include/touch_input.h  (NEW - draft)
void touch_input_init(void);

// Returns true if the panel is currently being touched, and fills the
// touch point already converted into LCD panel coordinates (0..239 x,
// 0..319 y - same space used by draw_text()/LCD_WIDTH/LCD_HEIGHT).
bool touch_input_get_point(uint16_t *x, uint16_t *y);
```

Internally: one `esp_lcd_touch_handle_t`, created once in
`touch_input_init()` sharing `SPI2_HOST` (the same bus object already
initialized in `lcd_init()`), reading via `esp_lcd_touch_read_data()` +
`esp_lcd_touch_get_coordinates()` on each call, debounced with a simple
"pressed / not pressed" edge check (same pattern already used for
`handle_ack_button()` in `main.c`) so a single tap isn't registered twice.

---

## 4. Navigation model

Every screen draws a thin **bottom nav bar** (2 touch zones, reusing the
existing `draw_text()` primitive - no new graphics primitives needed):

```
+--------------------------------------+
|                                      |
|            (screen content)         |
|                                      |
|                                      |
|  < PREV                     NEXT >  |   <- y = 296..319 (24px tall)
+--------------------------------------+
```

- Left third of the bottom bar (`x: 0..79`) = **PREV** zone.
- Right third of the bottom bar (`x: 160..239`) = **NEXT** zone.
- Tapping cycles the screen index `0..3` (wraps around), same as pressing
  left/right on a 4-item carousel.
- A `screen_state_t` enum (`SCREEN_HOME=0, SCREEN_ALARMS, SCREEN_POWER,
  SCREEN_GSM`) is tracked in `main.c` (or a new small `ui_state` module),
  persisted across LCD refreshes (500ms `LCD_REFRESH_INTERVAL_MS` cycle).

### Alarm override behavior (unchanged principle, new detail)
- If any alarm is active, the alarm banner screen (today's merged
  `lcd_show_alarm()` full-status screen) is shown **regardless of
  `screen_state_t`** - exactly like today.
- Touch is still read even while an alarm screen is shown, so the user can
  navigate away to check e.g. the POWER screen while an alarm is active
  (nice-to-have, not required for v1 - simplest v1 behavior: ignore touch
  while any alarm is raised, matching "alarms take priority").

---

## 5. Per-screen content draft

All screens keep the same header style (`"COLD STORAGE"` title bar at
y=8) and the same bottom nav bar, for visual consistency. Middle content
differs:

### 5.1 HOME (`lcd_show_home()`, replaces today's `lcd_show_normal()`)
```
COLD STORAGE                     (y=8)
DATE : dd/mm/yy                  (y=38)
TIME : hh:mm:ss                  (y=68)
TEMP : xx.x C                    (y=98)   green
HUM  : xx.x %RH                  (y=128)  cyan
STATUS: NORMAL  /  N ALARM(S)!   (y=158)  green / red
< PREV                  NEXT >   (y=296)  nav bar
```
Note: power/GSM lines are *removed* from HOME (they move to their own
screens) - HOME becomes the lightweight "at a glance" view the user asked
for ("just display the sensor reading and status only").

### 5.2 ALARMS (`lcd_show_alarms_screen()`, NEW)
Needs a new `alarm_manager` capability: enumerate active alarms (today
`alarm_manager` only exposes individual `alarm_manager_is_X_active()`
booleans, no count/list). Draft additions to `alarm_manager.h`:
```c
// Returns how many of the 10 possible alarms are currently ALARM_STATE_RAISED.
uint8_t alarm_manager_get_active_count(void);

// Fills 'out_ids'/'out_names' (both arrays of length >= max_count) with the
// currently active alarms, most-severe-first (same priority order already
// used in alarm_manager_refresh_outputs()). Returns how many were filled.
uint8_t alarm_manager_get_active_list(uint16_t *out_ids, const char **out_names, uint8_t max_count);
```
Screen layout (up to 4 visible at once, matches available vertical space):
```
COLD STORAGE                     (y=8)
ACTIVE ALARMS: 2                 (y=38)  red if >0, green "NONE" if 0
3005 LOW BATTERY                 (y=68)
1006 HIGH TEMP                   (y=98)
(blank if fewer than 4 active)
(blank)
< PREV                  NEXT >   (y=296)
```

### 5.3 POWER (`lcd_show_power_screen()`, NEW)
All data already available today via `power_source.h` getters - no new
backend work needed here, just a new screen layout:
```
COLD STORAGE                     (y=8)
SOURCE : MAIN / BATTERY          (y=38)
BATTERY: xx %                    (y=68)
VOLTAGE: x.xx V                  (y=98)
STATUS : OK / LOW / CRITICAL     (y=128)  green/yellow/red
< PREV                  NEXT >   (y=296)
```
(`STATUS` derived from `alarm_manager_is_battery_low_active()` /
`alarm_manager_is_battery_critical_active()`, already exposed today.)

### 5.4 GSM (`lcd_show_gsm_screen()`, NEW)
Needs new `sms_module` capability - today `sms_module.c` only has
`sms_module_init()`/`sms_module_send()`, no persisted status. Draft
additions to `sms_module.h`:
```c
typedef enum {
    GSM_STATE_UNKNOWN = 0,
    GSM_STATE_INIT_FAILED,
    GSM_STATE_NOT_REGISTERED,
    GSM_STATE_REGISTERED,
} gsm_state_t;

gsm_state_t sms_module_get_state(void);         // updated by polling AT+CREG? periodically
int8_t      sms_module_get_signal_quality(void); // AT+CSQ, 0-31 (99 = unknown), or -1 if not polled yet
bool        sms_module_get_last_send_ok(void);   // result of the most recent sms_module_send() call
```
Implementation note: `sms_module.c` would gain a lightweight periodic poll
(e.g. every 30s from `coldwatch_task()`, similar cadence to the existing
power-source sampling) sending `AT+CREG?` and `AT+CSQ`, parsing the
one-line reply the same way `wait_for_response()` already scans for `"OK"`.
Screen layout:
```
COLD STORAGE                     (y=8)
GSM   : REGISTERED / SEARCHING.. (y=38)  green / yellow
SIGNAL: xx/31                    (y=68)
LAST SMS: SENT OK / FAILED / --  (y=98)
< PREV                  NEXT >   (y=296)
```

---

## 6. Main loop changes (draft pseudocode, not yet applied)

```c
// New in main.c (draft)
typedef enum { SCREEN_HOME = 0, SCREEN_ALARMS, SCREEN_POWER, SCREEN_GSM, SCREEN_COUNT } screen_state_t;
static screen_state_t s_current_screen = SCREEN_HOME;

static void handle_touch_navigation(void) {
    uint16_t x, y;
    if (!touch_input_get_point(&x, &y)) return;      // debounced single-tap edge inside touch_input
    if (y < 296) return;                              // only the bottom nav bar is a hit zone
    if (x < 80)       s_current_screen = (s_current_screen + SCREEN_COUNT - 1) % SCREEN_COUNT; // PREV
    else if (x >= 160) s_current_screen = (s_current_screen + 1) % SCREEN_COUNT;                // NEXT
}

// In coldwatch_task()'s LCD refresh block (replaces the current single call):
handle_touch_navigation();
if (any_alarm_active()) {                 // same priority check alarm_manager already does internally
    alarm_manager_refresh_outputs(...);   // unchanged - still forces the alarm screen
} else {
    switch (s_current_screen) {
        case SCREEN_HOME:   lcd_show_home(...);         break;
        case SCREEN_ALARMS: lcd_show_alarms_screen(...); break;
        case SCREEN_POWER:  lcd_show_power_screen(...);  break;
        case SCREEN_GSM:    lcd_show_gsm_screen(...);    break;
    }
}
```

---

## 7. Full checklist to go from draft -> real implementation

1. **Wiring**: connect `T_CS`→GPIO32, `T_IRQ`→GPIO33 (2 jumper wires) on
   the physical board; add the 2 macros to `config.h`.
2. **New dependency**: add `atanisoft/esp_lcd_touch_xpt2046` to a new
   `components/touch_input/idf_component.yml` (same pattern as
   `lcd_i2c/idf_component.yml` today).
3. **New component**: `components/touch_input/` (`touch_input.h/.c`) per
   section 3.
4. **`alarm_manager.h`/`.c`**: add `alarm_manager_get_active_count()` /
   `alarm_manager_get_active_list()` (section 5.2).
5. **`sms_module.h`/`.c`**: add `gsm_state_t` + the 3 new getters and the
   periodic `AT+CREG?`/`AT+CSQ` poll (section 5.4).
6. **`lcd_i2c.h`/`.c`**: rename/refactor `lcd_show_normal()` into
   `lcd_show_home()` (drop the power/GSM lines it currently has - they move
   out), add `lcd_show_alarms_screen()`, `lcd_show_power_screen()`,
   `lcd_show_gsm_screen()`, and a shared `draw_nav_bar()` helper. Keep
   `lcd_show_alarm()` (the full-status alarm-override screen) as-is.
7. **`main.c`**: add `screen_state_t` + `handle_touch_navigation()` +
   dispatch switch (section 6).
8. **First-boot calibration**: verify/tune `TOUCH_SWAP_XY` /
   `TOUCH_MIRROR_X` / `TOUCH_MIRROR_Y` against the real touch panel
   orientation (tap each corner via a temporary debug printout of raw
   x/y, compare to expected corner).
9. Rebuild (`idf.py build`) and flash-test the nav bar taps on real
   hardware.

---

## 8. Open questions for you before implementation

1. Confirm the touch controller is indeed **XPT2046** (resistive, 4-wire +
   CS/IRQ) - check the silkscreen near the touch header on your display
   module. If it's a capacitive controller (e.g. FT6236/GT911) instead,
   the driver component/API differs slightly, though the plan/screens
   above stay identical.
2. OK to physically wire `T_CS`→GPIO32 and `T_IRQ`→GPIO33, or do you
   prefer different free GPIOs?
3. Should the ALARMS screen list update as alarms come and go (scroll if
   more than 4 are active simultaneously), or is "top 4 by priority" with
   a count is enough for v1? (Draft above assumes top-4 is enough.)
4. Should touch still be readable while an alarm screen is being force-
   displayed (i.e. can the user "peek" at another screen during an active
   alarm), or should touch be ignored until the alarm clears (simpler,
   safer default assumed above)?

