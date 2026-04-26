# Septic Tank Level Monitor — Build & Operation Guide

A dual-ESP32 system that measures septic tank fill level with an ultrasonic
sensor, transmits the reading over RS485 cable, and makes it available on an
OLED display and via Bluetooth Low Energy (BLE) on a phone.

---

## Table of Contents

1. [Hardware Bill of Materials](#1-hardware-bill-of-materials)
2. [Project Structure](#2-project-structure)
3. [First-Time Setup](#3-first-time-setup)
4. [Calibration](#4-calibration)
5. [Flashing the Firmware](#5-flashing-the-firmware)
6. [Enabling the OLED Display](#6-enabling-the-oled-display)
7. [Reading Data on a Phone](#7-reading-data-on-a-phone)
8. [Deployment Checklist](#8-deployment-checklist)
9. [Troubleshooting](#9-troubleshooting)

---

## 1. Hardware Bill of Materials

| Qty | Component | Notes |
|-----|-----------|-------|
| 2 | ESP32 DevKit v1 (38-pin) | Any standard 30- or 38-pin board works |
| 1 | JSN-SR04T v3.0 ultrasonic sensor | Must be the **UART version** (single-wire mode); configure with resistor if needed — see sensor datasheet |
| 2 | MAX485 breakout module | Any HY-SN75176 / MAX485 module with DE, RE, RO, DI terminals |
| 1 | LM2596 adjustable buck converter module | For 12 V → 5 V at the outdoor unit |
| 1 | SSD1306 OLED 128×64 I2C | **Pending delivery** |
| 1 | 12 V / 1 A DC adapter | For indoor unit; also powers outdoor via cable |
| 1 | Cat5e cable, ≥ 10 m | Standard network cable |
| 1 | Waterproof enclosure (IP65+) | For outdoor unit |
| 1 | Indoor project enclosure | For indoor unit |
| — | 120 Ω resistor | RS485 line termination (one per cable end if run > 10 m) |
| — | Hookup wire, terminal blocks, heat-shrink | Assembly |

---

## 2. Project Structure

```
ultrasonic-sensor/
├── outdoor/               ← PlatformIO project for outdoor ESP32
│   ├── platformio.ini
│   └── src/main.cpp
├── indoor/                ← PlatformIO project for indoor ESP32
│   ├── platformio.ini
│   └── src/main.cpp
└── documentation/
    ├── wiring_diagram.md  ← Pin-level wiring with ASCII diagrams
    ├── README.md          ← This file
    └── CONTEXT.md         ← Technical summary for AI/future reference
```

Open `outdoor/` and `indoor/` as **separate PlatformIO projects** in VS Code
(File → Open Folder, one at a time). Each has its own build environment.

---

## 3. First-Time Setup

### 3.1 Install PlatformIO

Install the PlatformIO IDE extension in VS Code, or use the CLI:

```bash
pip install platformio
```

### 3.2 Adjust the buck converter

Before connecting the ESP32, set the LM2596:

1. Connect 12 V to the LM2596 input.
2. Measure the output with a multimeter.
3. Turn the trim potentiometer until the output reads **5.0 V**.
4. Double-check before plugging into ESP32 VIN.

### 3.3 Set the tank depth constant

`TANK_DEPTH_MM` represents the distance from the sensor face to the tank floor
when the tank is completely empty. It must be set identically in both files:

- [outdoor/src/main.cpp](../outdoor/src/main.cpp) — line 17
- [indoor/src/main.cpp](../indoor/src/main.cpp) — line 23

```cpp
#define TANK_DEPTH_MM 2000   // ← change this value in both files
```

**How to measure:** Lower a tape measure into the tank from the sensor mounting
position. Record the distance in millimetres.

### 3.4 Wire both units

Follow [wiring_diagram.md](wiring_diagram.md) exactly. Key rules:

- RS485 A and B **must** use the same twisted pair in the Cat5e cable.
- Do not swap A and B — if readings are absent or corrupt after connection,
  try reversing A and B at one end.
- GND of both ESP32 boards must share a common reference (connected via the
  Orange-White pair in the cable).

---

## 4. Calibration

1. Flash the outdoor firmware (see section 5).
2. Hold an object at a known distance from the sensor face (e.g., 500 mm).
3. Open the Serial Monitor at **115200 baud**.
4. Confirm the reported distance matches within ~5 mm.
5. If offset is consistent, the sensor is working correctly — the JSN-SR04T
   does not require software correction.

---

## 5. Flashing the Firmware

### Outdoor unit

```bash
cd outdoor
pio run --target upload
pio device monitor --baud 115200
```

Expected serial output when running:

```
Outdoor unit ready.
Tank depth: 2000 mm
Distance: 1423 mm | Level:  577 mm ( 28%)
Distance: 1421 mm | Level:  579 mm ( 28%)
```

`Sensor read failed, skipping transmission.` means the sensor is not responding —
check UART2 wiring (GPIO 16/17) and sensor power.

### Indoor unit

```bash
cd indoor
pio run --target upload
pio device monitor --baud 115200
```

Expected serial output when the RS485 cable is connected and outdoor is running:

```
Indoor unit ready. BLE advertising as "SepticMonitor".
Distance: 1423 mm | Level:  577 mm ( 28%)
```

---

## 6. Enabling the OLED Display

When the SSD1306 OLED arrives, wire it to GPIO 21 (SDA) and GPIO 22 (SCL)
then make the following changes to [indoor/src/main.cpp](../indoor/src/main.cpp):

**Step 1** — Uncomment the includes near the top of the file (lines 8–9):

```cpp
#include <Wire.h>
#include <U8g2lib.h>
```

**Step 2** — Uncomment the display object and pin definitions (lines 19–20):

```cpp
#define OLED_SDA 21
#define OLED_SCL 22
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
```

**Step 3** — Uncomment the four init lines inside `setup()` (lines 130–135):

```cpp
Wire.begin(OLED_SDA, OLED_SCL);
display.begin();
display.clearBuffer();
display.setFont(u8g2_font_ncenB08_tr);
display.drawStr(0, 14, "Waiting...");
display.sendBuffer();
```

**Step 4** — Uncomment the `updateDisplay(...)` call in `loop()` (line 176):

```cpp
updateDisplay(distance_mm, level_mm, level_pct);
```

**Step 5** — Uncomment the full `updateDisplay()` function body (lines 104–120).

Reflash with `pio run --target upload` from the `indoor/` folder.

---

## 7. Reading Data on a Phone

The indoor ESP32 advertises as **`SepticMonitor`** via BLE.

### Option A — nRF Connect (Android / iOS)

1. Open nRF Connect → Scanner tab.
2. Find `SepticMonitor` and tap Connect.
3. Navigate to the service `4f8a1e2b-...4a5b`.
4. Subscribe to notifications on either characteristic:
   - `...4a5c` — Distance (mm), 2-byte uint16 big-endian
   - `...4a5d` — Level: byte[0] = fill %, byte[1..2] = fill level mm big-endian

### Option B — LightBlue (iOS / Android)

Same steps — scan, connect, tap the service, read or subscribe to characteristics.

### Decoding the values manually

| Characteristic | Bytes | Meaning |
|----------------|-------|---------|
| Distance `...4a5c` | `[H, L]` | `(H << 8) | L` = mm to water surface |
| Level `...4a5d` | `[P, H, L]` | `P` = fill %, `(H << 8) | L` = fill mm |

Example: `[0x05, 0xBB]` → `(5 × 256) + 187 = 1467 mm`.

---

## 8. Deployment Checklist

- [ ] `TANK_DEPTH_MM` set correctly in both firmwares and flashed
- [ ] LM2596 output verified at 5.0 V before connecting ESP32
- [ ] RS485 A/B on same twisted pair in Cat5e
- [ ] 120 Ω termination resistor at the outdoor end of the cable
- [ ] Sensor pointed straight down, mounted securely above tank opening
- [ ] Outdoor enclosure sealed (IP65+), cable gland for Cat5e
- [ ] Serial monitor tested on both units before sealing enclosures
- [ ] BLE visible and data correct from phone before sealing indoor unit

---

## 9. Troubleshooting

### Outdoor unit prints "Sensor read failed" every cycle

| Check | Action |
|-------|--------|
| Sensor power | Verify 5 V between sensor VCC and GND |
| UART wiring | Confirm sensor TX → GPIO 16, sensor RX → GPIO 17 (not crossed) |
| Sensor mode | Confirm sensor is in UART mode (some JSN-SR04T modules default to trigger/echo mode — a 47 kΩ resistor between the R pin and GND selects UART mode) |
| Serial conflict | Make sure nothing else uses UART2 |

### Indoor unit receives nothing / RS485 checksum errors

| Check | Action |
|-------|--------|
| Cable polarity | Swap A and B at one end of the cable |
| Baud rate | Both units use 9600 — confirm no accidental edit |
| DE/RE pin | Outdoor DE/RE must go HIGH before transmitting; probe GPIO 4 with a multimeter — should pulse briefly |
| Cable ground | Orange-White (GND) wire must be connected at both ends |
| Termination | Try adding / removing the 120 Ω resistor across A–B at the outdoor end |

### BLE not visible on phone

| Check | Action |
|-------|--------|
| BLE init | Check serial for `Indoor unit ready.` — if absent, firmware crashed |
| Phone BLE | Toggle Bluetooth off/on on phone |
| Range | ESP32 BLE range is ~10 m open air; concrete walls reduce this significantly |
| Already connected | ESP32 only accepts one BLE client at a time; disconnect any other device |

### Distance reads correctly but level % is wrong

The `TANK_DEPTH_MM` constant is not set to the actual empty-tank depth.
Re-measure, update both source files, and reflash.

### Readings are noisy / fluctuating more than 10 mm

The JSN-SR04T can reflect off tank walls. Ensure the sensor is:
- Mounted at the centre of the tank opening
- Pointed straight down (no angle)
- At least 200 mm above the maximum water level (sensor minimum range)

The firmware already averages 5 samples with out-of-range rejection. If noise
persists, increase `NUM_SAMPLES` in [outdoor/src/main.cpp](../outdoor/src/main.cpp):

```cpp
#define NUM_SAMPLES 10
```
