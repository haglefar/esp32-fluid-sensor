# Project Context — Septic Tank Level Monitor

This file is a compact technical reference for the project, intended for use
as AI context in future conversations. It describes every protocol, pin, UUID,
and code convention used in this codebase.

---

## System Architecture

Two ESP32 DevKit v1 boards connected by a Cat5e cable.

```
[JSN-SR04T]──UART──[ESP32 outdoor]──RS485──[Cat5e ~10m]──RS485──[ESP32 indoor]──BLE──[Phone]
                                                                                 │
                                                                              I2C──[OLED, pending]
```

- The outdoor ESP32 polls the sensor, averages readings, and transmits every ~2.5 s.
- The indoor ESP32 receives and forwards to BLE. It never transmits on RS485.
- There is no feedback path; communication is strictly outdoor → indoor.

---

## Source File Locations

| File | Purpose |
|------|---------|
| `outdoor/src/main.cpp` | Outdoor firmware: sensor + RS485 TX |
| `outdoor/platformio.ini` | Build config (no extra libs) |
| `indoor/src/main.cpp` | Indoor firmware: RS485 RX + BLE + OLED stub |
| `indoor/platformio.ini` | Build config (`olikraus/U8g2 @ ^2.34.22`) |
| `documentation/wiring_diagram.md` | ASCII wiring diagrams and pin tables |
| `documentation/README.md` | Build guide, calibration, troubleshooting |
| `documentation/CONTEXT.md` | This file |

---

## Hardware Components

| Component | Role | Interface |
|-----------|------|-----------|
| JSN-SR04T v3 (UART mode) | Distance measurement | UART 9600 8N1 |
| MAX485 (outdoor) | RS485 line driver | UART 9600 8N1 + DE/RE GPIO |
| LM2596 (outdoor) | 12 V → 5 V regulation | — |
| MAX485 (indoor) | RS485 line receiver | UART 9600 8N1 + DE/RE GPIO |
| SSD1306 OLED 128×64 | Level display (pending) | I2C |
| ESP32 BLE stack (indoor) | Phone connectivity | BLE 4.x |

---

## Pin Assignments

### Outdoor ESP32

| GPIO | Signal | Direction | Notes |
|------|--------|-----------|-------|
| 16 | SENSOR_RX (UART2 RX) | IN | JSN-SR04T TX |
| 17 | SENSOR_TX (UART2 TX) | OUT | JSN-SR04T RX |
| 25 | RS485_RX (UART1 RX) | IN | MAX485 RO |
| 26 | RS485_TX (UART1 TX) | OUT | MAX485 DI |
| 4 | RS485_DE_RE | OUT | MAX485 DE+RE tied; HIGH=TX, LOW=RX |

### Indoor ESP32

| GPIO | Signal | Direction | Notes |
|------|--------|-----------|-------|
| 25 | RS485_RX (UART1 RX) | IN | MAX485 RO |
| 26 | RS485_TX (UART1 TX) | OUT | MAX485 DI (unused) |
| 4 | RS485_DE_RE | OUT | Held LOW permanently |
| 21 | OLED_SDA (I2C SDA) | BIDIR | Pending hardware |
| 22 | OLED_SCL (I2C SCL) | OUT | Pending hardware |

---

## Serial Port Mapping

| Unit | Port | Speed | Purpose |
|------|------|-------|---------|
| Both | Serial (UART0, GPIO1/3) | 115200 | USB debug output |
| Outdoor | sensorSerial (UART2) | 9600 | JSN-SR04T |
| Both | rs485Serial (UART1) | 9600 | RS485 link |

---

## JSN-SR04T Sensor Protocol (UART mode, 9600 8N1)

**Trigger:** ESP32 sends `0x55`.

**Response:** Sensor replies with 4 bytes within ~30 ms:

```
Byte 0: 0xFF          (start marker)
Byte 1: H_data        (distance high byte)
Byte 2: L_data        (distance low byte)
Byte 3: checksum      = (0xFF + H_data + L_data) & 0xFF
```

**Distance:** `(H_data << 8) | L_data` in millimetres.

**Valid range:** 200–4500 mm. Readings outside this range are discarded.

The firmware takes 5 samples with 100 ms between them and returns the mean of
valid readings. If zero valid readings, the cycle is skipped and nothing is
sent over RS485.

**UART mode selection (hardware):** A 47 kΩ resistor between the sensor's
`R` pin and GND selects UART mode. Some v3 boards have this pre-fitted;
check the back of the sensor PCB.

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

**Direction control:** Outdoor DE/RE (GPIO 4) goes HIGH → send packet →
`rs485Serial.flush()` → 100 µs delay → goes LOW. Indoor DE/RE stays LOW.

**Parser (indoor):** Stateful byte-stream state machine. Hunts for `0xAA`,
accumulates 5 bytes, verifies checksum. Bad packets are discarded and logged.

---

## BLE Service Definition (Indoor)

**Device name:** `SepticMonitor`

**Service UUID:** `4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5b`

| Characteristic | UUID suffix | Properties | Format |
|----------------|-------------|------------|--------|
| Distance | `...4a5c` | READ + NOTIFY | 2 bytes, uint16 big-endian, mm to water surface |
| Level | `...4a5d` | READ + NOTIFY | 3 bytes: `[fill_%, level_H, level_L]`, fill mm big-endian |

Both characteristics have a CCCD (BLE2902) descriptor for notification enable.

Notifications are sent on every successful RS485 packet receive, only when a
client is connected. Advertising restarts automatically on disconnect.

---

## Tank Level Calculation

```
level_mm  = TANK_DEPTH_MM - distance_mm   (clamped to 0 if distance > depth)
level_pct = level_mm * 100 / TANK_DEPTH_MM
```

`TANK_DEPTH_MM` (default 2000) is the distance from sensor face to tank floor
when completely empty. **Must be identical in both firmwares.**

---

## OLED Display (Pending)

Library: `olikraus/U8g2 @ ^2.34.22` — already in `indoor/platformio.ini`.

Display type configured: `U8G2_SSD1306_128X64_NONAME_F_HW_I2C`.

All display code is present and compiled out with `//` comments. Four locations
must be uncommented to activate it — see README.md §6 for the exact lines.

Layout when active:
- Row 1 (font ncenB14): `Fill: XX%`
- Row 2 (font ncenB08): `Level: XXXX mm`
- Row 3 (font ncenB08): `Dist:  XXXX mm`

---

## Timing

| Event | Period |
|-------|--------|
| Sensor samples | 5 × 100 ms = 500 ms |
| Loop delay after send | 2000 ms |
| Total outdoor cycle | ~2.5 s |
| Indoor polling (loop delay) | 10 ms |

---

## Cable — Cat5e Pair Assignment

| Pair | Colour | Signal |
|------|--------|--------|
| 1 | Blue / Blue-White | RS485 A / RS485 B |
| 2 | Orange / Orange-White | 12 V / GND |
| 3 | Green / Green-White | Spare |
| 4 | Brown / Brown-White | Spare |

RS485 A/B must stay on the same twisted pair for differential noise rejection.
120 Ω termination resistor across A–B at the outdoor (far) end for runs > 10 m.

---

## Key Constants to Change Before Deployment

| Constant | File | Default | Required action |
|----------|------|---------|----------------|
| `TANK_DEPTH_MM` | outdoor + indoor | 2000 | Measure and set to actual empty depth in mm |
| `NUM_SAMPLES` | outdoor | 5 | Increase to 10 if readings are noisy |
| `BLE_DEVICE_NAME` | indoor | `SepticMonitor` | Optional rename |

---

## Build Dependencies

| Project | Library | Version |
|---------|---------|---------|
| outdoor | None beyond ESP32 Arduino framework | — |
| indoor | U8g2 | `^2.34.22` |
| indoor | ESP32 BLE (bundled in framework) | — |

PlatformIO platform: `espressif32`, board: `esp32dev`, framework: `arduino`.
