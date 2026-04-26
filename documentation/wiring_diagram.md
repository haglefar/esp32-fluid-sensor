# Wiring Diagrams — Septic Tank Level Monitor

---

## 1. System Overview

```
                     Cat5e cable (up to ~100 m)
  ┌─────────────┐   ┌──────────────────────────────────┐   ┌─────────────────┐
  │ OUTDOOR BOX │   │ Blue pair  : RS485 A / RS485 B   │   │   INDOOR BOX    │
  │             │───│ Orange pair: 12 V supply          │───│                 │
  │ ESP32       │   │ Green pair : GND return           │   │ ESP32           │
  │ JSN-SR04T   │   │ Brown pair : spare                │   │ MAX485          │
  │ MAX485      │   └──────────────────────────────────┘   │ OLED SSD1306    │
  │ LM2596      │                                           │ (pending)       │
  └─────────────┘                                           └─────────────────┘
        ▲                                                          │
        │ 12 V DC input                                            │ 12 V DC adapter
   (from indoor via cable,                                    (mains supply,
    or local 12 V supply)                                      e.g. 1 A wall-wart)
```

---

## 2. Outdoor Unit — Full Wiring

### LM2596 Buck Converter (12 V → 5 V)

```
  12 V supply ──────────┬──► LM2596 IN+
                         │
  GND ──────────────────┬┴──► LM2596 IN−
                         │
                   LM2596 OUT+ ──► 5 V rail ──► ESP32 VIN
                   LM2596 OUT− ──► GND rail
```

Set LM2596 output to **5.0 V** with the trim potentiometer before connecting the ESP32.

---

### JSN-SR04T Ultrasonic Sensor → ESP32 (UART2)

```
  JSN-SR04T                     ESP32 (Outdoor)
  ─────────                     ───────────────
  VCC  ───────────────────────► 5 V rail
  GND  ───────────────────────► GND
  TX   ───────────────────────► GPIO 16  (SENSOR_RX / UART2 RX)
  RX   ───────────────────────► GPIO 17  (SENSOR_TX / UART2 TX)
```

> The JSN-SR04T operates on 5 V. Its TX output is 5 V logic — most ESP32 GPIO
> inputs tolerate this, but a 10 kΩ series resistor on the TX→GPIO16 line is a
> safe addition if you want to be careful.

---

### MAX485 Module → ESP32 (UART1) — Outdoor

```
  MAX485 pin                    ESP32 (Outdoor)
  ──────────                    ───────────────
  VCC  ───────────────────────► 5 V rail
  GND  ───────────────────────► GND
  RO   (Receiver Output) ─────► GPIO 25  (RS485_RX / UART1 RX)
  /RE  (Recv Enable, active L)─┐
  DE   (Driver Enable)─────────┴─► GPIO 4  (RS485_DE_RE)
  DI   (Driver Input)  ────────► GPIO 26  (RS485_TX / UART1 TX)
  A    (non-inverting) ────────► Cable — Blue wire
  B    (inverting)     ────────► Cable — Blue-White wire
```

> RE and DE are tied together and driven by a single GPIO. HIGH = transmit,
> LOW = receive. The outdoor unit only transmits; it starts in receive mode and
> briefly switches to transmit for each packet.

---

### Outdoor Unit — Cable Terminals

```
  Screw terminal / cable end   Cat5e pair
  ─────────────────────────    ──────────
  RS485 A (MAX485 pin A) ────► Blue wire        ┐ Blue twisted pair
  RS485 B (MAX485 pin B) ────► Blue-White wire  ┘
  12 V in (from adapter) ────► Orange wire       ┐ Orange twisted pair
  GND                    ────► Orange-White wire  ┘
  (spare)                ────► Green / Green-White
  (spare)                ────► Brown / Brown-White
```

---

## 3. Indoor Unit — Full Wiring

### Power

```
  12 V DC adapter ──► DC barrel jack ──► 12 V rail
  Adapter GND     ──► GND rail

  (No buck converter needed if ESP32 has an onboard 3.3 V regulator.
   Power the ESP32 via VIN = 12 V — the onboard AMS1117 handles it,
   but ensure the adapter can supply ≥ 500 mA.)

  Alternatively, use a second LM2596 set to 5 V and power via VIN=5 V.
```

---

### MAX485 Module → ESP32 (UART1) — Indoor

```
  MAX485 pin                    ESP32 (Indoor)
  ──────────                    ──────────────
  VCC  ───────────────────────► 3.3 V or 5 V rail  (MAX485 works on both)
  GND  ───────────────────────► GND
  RO   (Receiver Output) ─────► GPIO 25  (RS485_RX / UART1 RX)
  /RE  (Recv Enable, active L)─┐
  DE   (Driver Enable)─────────┴─► GPIO 4  (RS485_DE_RE)   [tied LOW permanently]
  DI   (Driver Input)  ────────► GPIO 26  (RS485_TX / UART1 TX)  [unused]
  A    (non-inverting) ────────► Cable — Blue wire
  B    (inverting)     ────────► Cable — Blue-White wire
```

> GPIO 4 is held LOW by the firmware so the indoor unit is always in receive mode.

---

### Indoor Unit — Cable Terminals

```
  Screw terminal / cable end   Cat5e pair
  ─────────────────────────    ──────────
  RS485 A (MAX485 pin A) ────► Blue wire        ┐ Blue twisted pair
  RS485 B (MAX485 pin B) ────► Blue-White wire  ┘
  12 V out (to outdoor)  ────► Orange wire       ┐ Orange twisted pair
  GND                    ────► Orange-White wire  ┘
```

---

### OLED SSD1306 128×64 → ESP32 (I2C) — Pending Hardware

```
  SSD1306 OLED                  ESP32 (Indoor)
  ────────────                  ──────────────
  VCC  ───────────────────────► 3.3 V
  GND  ───────────────────────► GND
  SDA  ───────────────────────► GPIO 21  (I2C SDA)
  SCL  ───────────────────────► GPIO 22  (I2C SCL)
```

> Add 4.7 kΩ pull-up resistors from SDA→3.3 V and SCL→3.3 V if not already
> present on the OLED breakout board (most modules include them).

---

## 4. Pin Reference Tables

### Outdoor ESP32

| GPIO | Function        | Connected to              |
|------|-----------------|---------------------------|
| 16   | UART2 RX        | JSN-SR04T TX              |
| 17   | UART2 TX        | JSN-SR04T RX              |
| 25   | UART1 RX        | MAX485 RO                 |
| 26   | UART1 TX        | MAX485 DI                 |
| 4    | RS485 DE/RE     | MAX485 DE + /RE (tied)    |
| VIN  | Power in        | LM2596 5 V output         |
| GND  | Ground          | Common GND                |

### Indoor ESP32

| GPIO | Function        | Connected to              |
|------|-----------------|---------------------------|
| 25   | UART1 RX        | MAX485 RO                 |
| 26   | UART1 TX        | MAX485 DI (unused)        |
| 4    | RS485 DE/RE     | MAX485 DE + /RE (LOW)     |
| 21   | I2C SDA         | OLED SDA (pending)        |
| 22   | I2C SCL         | OLED SCL (pending)        |
| VIN  | Power in        | 12 V adapter or 5 V reg.  |
| GND  | Ground          | Common GND                |

---

## 5. RS485 Line Termination

For cable runs over ~10 m, fit a **120 Ω resistor** across the A and B terminals
at the **far end** of the cable (the outdoor unit). Some MAX485 breakout boards
have a solder-jumper for this — check your specific module.

```
  Outdoor MAX485 terminal block
  ┌─────┐
  │  A  │───┬─── to cable
  │  B  │───┴─── to cable
  └─────┘  120 Ω (place across A–B at outdoor end)
```

---

## 6. Power Budget

| Component        | Voltage | Current (typ.) |
|------------------|---------|----------------|
| ESP32 (outdoor)  | 5 V     | 240 mA peak    |
| JSN-SR04T        | 5 V     | 30 mA          |
| MAX485 (outdoor) | 5 V     | 6 mA           |
| **Outdoor total**| **12 V**| **~300 mA after LM2596 efficiency** |
| ESP32 (indoor)   | 12 V→3.3 V | 240 mA peak |
| MAX485 (indoor)  | 3.3–5 V | 6 mA          |
| OLED SSD1306     | 3.3 V   | 20 mA          |
| **Indoor total** | **12 V**| **~350 mA**    |

Use a **12 V / 1 A** adapter for the indoor unit and route power over the
Orange pair of the Cat5e cable to the outdoor unit.
