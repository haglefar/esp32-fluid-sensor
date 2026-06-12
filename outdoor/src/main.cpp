#include <Arduino.h>
#include <HardwareSerial.h>
#include <esp_task_wdt.h>

// --- Watchdog ---
// Resets the device if loop() blocks for longer than this
#define WDT_TIMEOUT_S       30

// --- Pin assignments ---
// JSN-SR04T ultrasonic sensor (trigger/echo mode)
// Module labels these pins "RX" (trig input) and "TX" (echo output)
#define TRIG_PIN 17   // GPIO17 → JSN-SR04T RX/Trig
#define ECHO_PIN 16   // GPIO16 → JSN-SR04T TX/Echo

// MAX485 RS485 transceiver (UART1)
#define RS485_RX    25
#define RS485_TX    26
#define RS485_DE_RE  4   // Drive Enable / Receive Enable (active HIGH = transmit)

// --- Tank configuration ---
// Distance from sensor face to tank bottom when completely empty, in mm.
// Measure this physically and update before deployment.
#define TANK_DEPTH_MM 2000

// JSN-SR04T valid range: 250–4500 mm (250 mm blind zone per datasheet)
#define SENSOR_MIN_MM  250
#define SENSOR_MAX_MM 4500

// --- Averaging ---
#define NUM_SAMPLES     5
#define SAMPLE_DELAY_MS 150

// --- Fault detection ---
// Log a warning after this many consecutive sensor failures
#define CONSECUTIVE_FAIL_WARN 10

// --- RS485 packet protocol ---
// [START, TYPE, DATA_H, DATA_L, CHECKSUM]
// CHECKSUM = (START + TYPE + DATA_H + DATA_L) & 0xFF
#define PACKET_START          0xAA
#define PACKET_TYPE_DISTANCE  0x01
#define PACKET_LEN            5

HardwareSerial rs485Serial(1);

uint16_t readSensorDistanceMM();
uint16_t averagedDistanceMM();
void     sendRS485Distance(uint16_t distance_mm);

void setup() {
    Serial.begin(115200);

    // Watchdog: triggers a hard reset if loop() hangs for WDT_TIMEOUT_S seconds
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    pinMode(TRIG_PIN, OUTPUT);
    pinMode(ECHO_PIN, INPUT);
    digitalWrite(TRIG_PIN, LOW);

    rs485Serial.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
    pinMode(RS485_DE_RE, OUTPUT);
    digitalWrite(RS485_DE_RE, LOW);

    Serial.println("Outdoor unit ready.");
    Serial.printf("Tank depth: %u mm\n", TANK_DEPTH_MM);
}

void loop() {
    static uint8_t consecutiveFails = 0;

    uint16_t distance_mm = averagedDistanceMM();

    if (distance_mm > 0) {
        consecutiveFails = 0;

        uint16_t level_mm  = (distance_mm < TANK_DEPTH_MM) ? TANK_DEPTH_MM - distance_mm : 0;
        uint8_t  level_pct = (uint8_t)((uint32_t)level_mm * 100 / TANK_DEPTH_MM);

        Serial.printf("Distance: %4u mm | Level: %4u mm (%3u%%)\n",
                      distance_mm, level_mm, level_pct);

        sendRS485Distance(distance_mm);
    } else {
        consecutiveFails++;
        Serial.printf("Sensor read failed (%u consecutive)\n", consecutiveFails);
        if (consecutiveFails == CONSECUTIVE_FAIL_WARN) {
            Serial.println("E03: Sensor unresponsive — check wiring and power");
        }
    }

    esp_task_wdt_reset();
    delay(2000);
}

// Trigger a measurement with a 10 µs pulse and measure the echo duration.
// Returns distance in mm, or 0 on timeout.
uint16_t readSensorDistanceMM() {
    // Wait for echo to settle LOW before triggering (up to 50 ms)
    uint32_t t0 = millis();
    while (digitalRead(ECHO_PIN) == HIGH) {
        if (millis() - t0 > 50) return 0;
    }

    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);
    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    // pulseIn timeout = 30 000 µs, covers the full 4.5 m range
    uint32_t duration = pulseIn(ECHO_PIN, HIGH, 30000);
    if (duration == 0) return 0;

    // distance_mm = duration_µs × (340 m/s ÷ 2) converted to mm/µs = × 17 / 100
    return (uint16_t)((uint32_t)duration * 17 / 100);
}

// Take NUM_SAMPLES readings, discard out-of-range values, return the average.
uint16_t averagedDistanceMM() {
    uint32_t sum   = 0;
    uint8_t  valid = 0;

    for (uint8_t i = 0; i < NUM_SAMPLES; i++) {
        uint16_t d = readSensorDistanceMM();
        if (d >= SENSOR_MIN_MM && d <= SENSOR_MAX_MM) {
            sum += d;
            valid++;
        }
        delay(SAMPLE_DELAY_MS);
    }

    return (valid > 0) ? (uint16_t)(sum / valid) : 0;
}

// Transmit a 5-byte distance packet over RS485.
void sendRS485Distance(uint16_t distance_mm) {
    uint8_t hi  = (distance_mm >> 8) & 0xFF;
    uint8_t lo  = distance_mm & 0xFF;
    uint8_t crc = (PACKET_START + PACKET_TYPE_DISTANCE + hi + lo) & 0xFF;

    uint8_t packet[PACKET_LEN] = {PACKET_START, PACKET_TYPE_DISTANCE, hi, lo, crc};

    digitalWrite(RS485_DE_RE, HIGH);
    delayMicroseconds(100);

    rs485Serial.write(packet, PACKET_LEN);
    rs485Serial.flush();

    delayMicroseconds(100);
    digitalWrite(RS485_DE_RE, LOW);
}
