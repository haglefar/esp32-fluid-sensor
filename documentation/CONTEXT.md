# Project Context — Septic Tank Level Monitor

Compact technical reference for AI context. Describes every protocol, pin, UUID, and code convention.

---

## System Architecture

Two ESP32 DevKit v1 boards connected by a Cat5e cable.

```
[JSN-SR04T]──trigger/echo──[ESP32 outdoor]──RS485──[Cat5e ~10m]──RS485──[ESP32 indoor]──BLE──[Phone]
                                                                                 │
                                                                              I2C──[OLED SSD1306 128×64]
```

- The outdoor ESP32 polls the sensor, averages 5 readings, and transmits a packet every ~2.5 s.
- The indoor ESP32 receives, drives the OLED, and BLE-broadcasts. It never transmits on RS485.
- Communication is strictly outdoor → indoor (no feedback path).

---

## Source File Locations

| File | Purpose |
|------|---------|
| `outdoor/src/main.cpp` | Outdoor firmware: sensor + RS485 TX |
| `outdoor/platformio.ini` | Build config (no extra libs) |
| `indoor/src/main.cpp` | Indoor firmware: RS485 RX + BLE + OLED |
| `indoor/platformio.ini` | Build config (`olikraus/U8g2 @ ^2.34.22`) |
| `documentation/wiring_diagram.md` | ASCII wiring diagrams and pin tables |
| `documentation/CONTEXT.md` | This file |
| `README.md` | Setup, configuration, error codes, troubleshooting |

---

## Hardware Components

| Component | Role | Interface |
|-----------|------|-----------|
| JSN-SR04T (trigger/echo mode) | Distance measurement | GPIO trigger/echo pulses |
| MAX485 (outdoor) | RS485 line driver | UART1 9600 8N1 + DE/RE GPIO |
| LM2596 (outdoor) | 12 V → 5 V regulation | — |
| MAX485 (indoor) | RS485 line receiver | UART1 9600 8N1 + DE/RE GPIO (held LOW) |
| SSD1306 OLED 128×64 | Level display | I2C (U8g2 library) |
| ESP32 BLE stack (indoor) | Phone connectivity | BLE 4.x |

---

## Pin Assignments

### Outdoor ESP32

| GPIO | Signal | Direction | Notes |
|------|--------|-----------|-------|
| 17 | TRIG_PIN | OUT | JSN-SR04T RX/Trig — 10 µs HIGH pulse triggers measurement |
| 16 | ECHO_PIN | IN | JSN-SR04T TX/Echo — pulse width = travel time |
| 25 | RS485_RX (UART1 RX) | IN | MAX485 RO |
| 26 | RS485_TX (UART1 TX) | OUT | MAX485 DI |
| 4 | RS485_DE_RE | OUT | MAX485 DE+RE tied; HIGH = TX, LOW = RX |

### Indoor ESP32

| GPIO | Signal | Direction | Notes |
|------|--------|-----------|-------|
| 25 | RS485_RX (UART1 RX) | IN | MAX485 RO |
| 26 | RS485_TX (UART1 TX) | OUT | MAX485 DI (unused) |
| 4 | RS485_DE_RE | OUT | Held LOW permanently |
| 21 | OLED_SDA (I2C SDA) | BIDIR | SSD1306 |
| 22 | OLED_SCL (I2C SCL) | OUT | SSD1306 |

---

## Serial Port Mapping

| Unit | Port | Speed | Purpose |
|------|------|-------|---------|
| Both | Serial (UART0, GPIO1/3) | 115200 | USB debug output |
| Both | rs485Serial (UART1) | 9600 | RS485 link |

The outdoor unit no longer uses a HardwareSerial for the sensor — it uses plain GPIO trigger/echo.

---

## JSN-SR04T Sensor Protocol (trigger/echo mode)

This module uses trigger/echo mode identical to the HC-SR04. The pin labels "RX" and "TX" on the PCB are the trigger input and echo output respectively — not a UART interface.

**Trigger:** Pull TRIG_PIN HIGH for ≥ 10 µs, then LOW.

**Echo:** ECHO_PIN goes HIGH; measure pulse duration with `pulseIn(ECHO_PIN, HIGH, 30000)`.

**Distance:** `distance_mm = duration_µs × 17 / 100` (speed of sound 340 m/s, round-trip ÷ 2).

**Valid range:** 250–4500 mm. Readings outside this range are discarded.

The firmware takes 5 samples with 150 ms between them and returns the mean of valid readings.
If all 5 samples are invalid, the cycle is skipped and nothing is sent over RS485.

**Echo settling:** Before each trigger, the firmware waits (up to 50 ms) for ECHO_PIN to be LOW to avoid mis-timing from a lingering previous echo.

---

## RS485 Packet Protocol

5-byte packet, sent from outdoor to indoor only.

```
Byte 0: 0xAA                    (PACKET_START)
Byte 1: 0x01                    (PACKET_TYPE_DISTANCE)
Byte 2: distance_mm >> 8        (high byte)
Byte 3: distance_mm & 0xFF      (low byte)
Byte 4: checksum                = (0xAA + 0x01 + byte2 + byte3) & 0xFF
```

**Baud:** 9600 8N1.

**Direction control:** Outdoor DE/RE (GPIO 4) goes HIGH → packet sent → `flush()` → 100 µs delay → LOW. Indoor DE/RE stays LOW.

**Parser (indoor):** Stateful byte-stream state machine returning `ParseResult` enum:
- `PARSE_OK` — valid packet, `distance_mm` set
- `PARSE_CRC_ERR` — full packet received but checksum failed
- `PARSE_NONE` — no complete packet available yet

Stale partial packets are discarded if no byte arrives within 1 s (prevents mis-framing after link outage).

---

## Error Codes

| Code | Unit | Trigger | Shown on |
|------|------|---------|----------|
| E01 | Indoor | No valid RS485 packet for 30 s | OLED display + serial |
| E02 | Indoor | CRC errors since last good packet, timeout reached | OLED display + serial |
| E03 | Outdoor | 10 consecutive sensor read failures | Serial only |

E01/E02 clear automatically when a valid packet is received.

---

## Robustness Features

- **Hardware watchdog** (`esp_task_wdt`): 30 s outdoor, 60 s indoor. Hard reset if `loop()` hangs.
- **Stale parser reset**: Indoor parser resets `pos` to 0 if no byte received for > 1 s mid-packet.
- **Echo pin settling**: Outdoor waits up to 50 ms for ECHO_PIN LOW before triggering.
- **BLE auto-reconnect**: `BLEDevice::startAdvertising()` called in `onDisconnect` callback.
- **Error display**: Indoor shows E01/E02 on OLED rather than silently showing stale data.

---

## BLE Service Definition (Indoor)

**Device name:** `SepticMonitor`

**Service UUID:** `4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5b`

| Characteristic | UUID suffix | Properties | Format |
|----------------|-------------|------------|--------|
| Distance | `...4a5c` | READ + NOTIFY | 2 bytes, uint16 big-endian, mm to water surface |
| Level | `...4a5d` | READ + NOTIFY | 3 bytes: `[fill_pct, level_H, level_L]`, fill mm big-endian |

Both characteristics have a CCCD (BLE2902) descriptor. Notifications sent on every valid packet, only when a client is connected.

---

## OLED Display Layout

Library: `olikraus/U8g2 @ ^2.34.22`, type `U8G2_SSD1306_128X64_NONAME_F_HW_I2C`.

**Normal screen:**
- Row 1 (font ncenB14, y=18): `Fill: XX%`
- Row 2 (font ncenB08, y=36): `Level: XXX cm`
- Row 3 (font ncenB08, y=52): `Dist:  XXX cm`

**Error screen:**
- Row 1 (font ncenB14, y=18): `! ERROR !`
- Row 2 (font ncenB08, y=36): error code e.g. `E01`
- Row 3 (font ncenB08, y=52): description e.g. `No signal`

**Startup:** `Waiting...` shown in setup() until first valid packet.

---

## Tank Level Calculation

```
level_mm  = TANK_DEPTH_MM - distance_mm   (clamped to 0 if distance > depth)
level_pct = level_mm * 100 / TANK_DEPTH_MM
```

`TANK_DEPTH_MM` (default 2000) = distance from sensor face to tank floor when empty.
**Must be identical in both firmwares.** Measure on-site before deployment.

---

## Timing

| Event | Period |
|-------|--------|
| Sensor samples | 5 × 150 ms = 750 ms |
| Loop delay after send | 2000 ms |
| Total outdoor cycle | ~2.75 s |
| Indoor loop delay | 10 ms |
| Indoor signal timeout (E01/E02) | 30 000 ms |

---

## Cable — Cat5e Pair Assignment

| Pair | Colour | Signal |
|------|--------|--------|
| 1 | Blue / Blue-White | RS485 A / RS485 B |
| 2 | Orange / Orange-White | 12 V / GND |
| 3 | Green / Green-White | Spare |
| 4 | Brown / Brown-White | Spare |

RS485 A/B must stay on the same twisted pair. 120 Ω termination across A–B at the outdoor end for runs > 10 m.

---

## Build Dependencies

| Project | Library | Version |
|---------|---------|---------|
| outdoor | None beyond ESP32 Arduino framework | — |
| indoor | U8g2 | `^2.34.22` |
| indoor | ESP32 BLE (bundled in framework) | — |
| both | esp_task_wdt (bundled in ESP-IDF / framework) | — |

PlatformIO platform: `espressif32`, board: `esp32dev`, framework: `arduino`.

---

## Key Constants to Change Before Deployment

| Constant | File | Default | Required action |
|----------|------|---------|----------------|
| `TANK_DEPTH_MM` | outdoor + indoor | 2000 | Measure and set to actual empty-tank distance in mm |
| `NUM_SAMPLES` | outdoor | 5 | Increase to 10 if readings are noisy |
| `BLE_DEVICE_NAME` | indoor | `SepticMonitor` | Optional rename |
