# Septic Tank Level Monitor

Two-unit ESP32 system that measures septic tank fill level and displays it locally and over BLE.

---

## System Overview

```
[JSN-SR04T]──trigger/echo──[ESP32 outdoor]──RS485──[Cat5e]──RS485──[ESP32 indoor]──BLE──[Phone]
                                                                           │
                                                                        I2C──[OLED SSD1306]
```

- **Outdoor unit** — reads the ultrasonic sensor, averages 5 samples, transmits a packet every ~2.5 s over RS485.
- **Indoor unit** — receives packets, shows fill level on the OLED display, and broadcasts over BLE.
- Communication is one-way: outdoor → indoor only.
- Power: 12 V DC adapter at the indoor unit, routed to the outdoor unit over the Cat5e Orange pair.

---

## Hardware

| Component | Location | Purpose |
|-----------|----------|---------|
| ESP32 DevKit v1 | Outdoor | Sensor polling + RS485 TX |
| JSN-SR04T ultrasonic sensor | Outdoor | Distance measurement (trigger/echo mode) |
| MAX485 module | Outdoor | RS485 line driver |
| LM2596 buck converter | Outdoor | 12 V → 5 V for ESP32 and sensor |
| ESP32 DevKit v1 | Indoor | RS485 RX + BLE + OLED |
| MAX485 module | Indoor | RS485 line receiver |
| SSD1306 OLED 128×64 | Indoor | Local display |
| Cat5e cable | Link | RS485 differential pair + 12 V power |

---

## Wiring

See [documentation/wiring_diagram.md](documentation/wiring_diagram.md) for full ASCII diagrams and pin tables.

**Key points:**
- RS485 A/B must be on the same twisted pair (Blue pair).
- LM2596 output must be trimmed to **4.9–5.0 V** before connecting the ESP32 — a higher voltage prevents USB enumeration when both 12 V and USB are connected simultaneously.
- For cable runs over 10 m, fit a 120 Ω resistor across A–B at the outdoor (far) end.

---

## Flashing

Each sub-folder is an independent PlatformIO project. Open the folder for the unit you want to flash.

```
ultrasonic-sensor/
├── indoor/   ← open this folder to flash the indoor unit
└── outdoor/  ← open this folder to flash the outdoor unit
```

1. Open `indoor/` or `outdoor/` as the VS Code workspace root.
2. Connect the ESP32 via USB (**12 V disconnected** if LM2596 has not yet been trimmed to 4.9 V).
3. Click **PlatformIO: Upload** or press the upload button in the toolbar.
4. Open the serial monitor at **115 200 baud** to verify output.

---

## Configuration

Both constants below must be kept identical in both firmware files.

| Constant | File | Default | Action required |
|----------|------|---------|----------------|
| `TANK_DEPTH_MM` | `outdoor/src/main.cpp` | `2000` | Set to the measured distance (mm) from sensor face to tank floor when completely empty |
| `TANK_DEPTH_MM` | `indoor/src/main.cpp` | `2000` | Set to the same value as outdoor |

Other tuneable constants:

| Constant | File | Default | Notes |
|----------|------|---------|-------|
| `NUM_SAMPLES` | outdoor | `5` | Increase to `10` for noisier environments |
| `CONSECUTIVE_FAIL_WARN` | outdoor | `10` | Consecutive sensor failures before E03 warning |
| `NO_SIGNAL_TIMEOUT_MS` | indoor | `30000` | ms before E01/E02 appears on display |
| `WDT_TIMEOUT_S` | both | `30` / `60` | Watchdog reset threshold |

---

## Error Codes

### Indoor display errors

Shown on the OLED when the indoor unit cannot display valid data.

| Code | Meaning | Likely cause |
|------|---------|-------------|
| **E01** | No signal | No valid RS485 packet received for 30 s — check outdoor power, RS485 wiring, and A/B polarity |
| **E02** | Bad packet | RS485 packets are arriving but failing checksum — electrical noise on the cable, bad connection, or A/B wires swapped mid-run |

The error clears automatically as soon as a valid packet is received.

### Outdoor serial warnings

Printed to the serial monitor only (no display on outdoor unit).

| Code | Meaning | Likely cause |
|------|---------|-------------|
| **E03** | Sensor unresponsive | 10 consecutive sensor read failures — check JSN-SR04T wiring and 5 V supply |

---

## BLE Interface

The indoor unit advertises as **SepticMonitor** and exposes one service with two characteristics.

**Service UUID:** `4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5b`

| Characteristic | UUID (last 4 hex) | Properties | Format |
|----------------|-------------------|------------|--------|
| Distance | `...4a5c` | READ + NOTIFY | 2 bytes, uint16 big-endian, mm to water surface |
| Level | `...4a5d` | READ + NOTIFY | 3 bytes: `[fill_pct, level_H, level_L]`; fill_pct is 0–100, level is mm big-endian |

**To connect:** use **nRF Connect** (Android / iOS), scan for SepticMonitor, connect, expand the service, and enable notifications on both characteristics. The first byte of the Level characteristic is the fill percentage as a plain decimal (e.g. `0x2A` = 42 %).

---

## Robustness

- **Hardware watchdog** — both units use `esp_task_wdt` to trigger a hard reset if `loop()` blocks for more than 30 s (outdoor) or 60 s (indoor). Catches hangs in BLE initialisation or any unexpected infinite loop.
- **Stale parser reset** — if the indoor RS485 parser is mid-packet and receives no new bytes for 1 s, it resets its state machine. Prevents mis-framing after a link outage.
- **Error display** — after 30 s without a valid packet, the indoor OLED switches to an error screen (E01 or E02) instead of silently showing stale data.
- **Consecutive failure warning** — the outdoor unit prints E03 to serial after 10 consecutive sensor read failures.
- **BLE auto-reconnect** — advertising restarts automatically whenever a BLE client disconnects.

---

## Troubleshooting

| Symptom | Check |
|---------|-------|
| Outdoor ESP32 not appearing as COM port | Disconnect 12 V — LM2596 output likely above USB 5 V. Trim to 4.9 V. |
| Serial monitor shows nothing | Wrong COM port or baud rate (must be 115 200). Press RST button while monitor is open. |
| "Sensor read failed" on every cycle | Check JSN-SR04T VCC (needs 5 V), check TX/RX not swapped |
| Display shows E01 | Outdoor unit not transmitting — check power and RS485 A/B connections |
| Display shows E02 | RS485 data corrupted — check cable connections, A/B polarity, and 120 Ω termination |
| Fill always 0 % | `TANK_DEPTH_MM` is smaller than the measured distance — update and reflash both units |
| LM2596 + USB conflict | Trim LM2596 output to 4.9 V with a multimeter (12 V in, no USB) |
