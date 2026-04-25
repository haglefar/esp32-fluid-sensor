#include <Arduino.h>
#include <HardwareSerial.h>

// --- Pin assignments ---
// JSN-SR04T ultrasonic sensor (UART2)
#define SENSOR_RX 16
#define SENSOR_TX 17

// MAX485 RS485 transceiver (UART1)
#define RS485_RX  25
#define RS485_TX  26
#define RS485_DE_RE 4   // Drive Enable / Receive Enable (active HIGH = transmit)

// --- Tank configuration ---
// Distance from sensor face to tank bottom when completely empty, in mm.
// Measure this physically and update before deployment.
#define TANK_DEPTH_MM 2000

// JSN-SR04T valid range: 200–4500 mm
#define SENSOR_MIN_MM 200
#define SENSOR_MAX_MM 4500

// --- Averaging ---
#define NUM_SAMPLES    5
#define SAMPLE_DELAY_MS 100  // Minimum time between sensor pings

// --- RS485 packet protocol ---
// [START, TYPE, DATA_H, DATA_L, CHECKSUM]
// CHECKSUM = (START + TYPE + DATA_H + DATA_L) & 0xFF
#define PACKET_START          0xAA
#define PACKET_TYPE_DISTANCE  0x01
#define PACKET_LEN            5

HardwareSerial sensorSerial(2);
HardwareSerial rs485Serial(1);

uint16_t readSensorDistanceMM();
uint16_t averagedDistanceMM();
void sendRS485Distance(uint16_t distance_mm);

void setup() {
    Serial.begin(115200);

    sensorSerial.begin(9600, SERIAL_8N1, SENSOR_RX, SENSOR_TX);

    rs485Serial.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
    pinMode(RS485_DE_RE, OUTPUT);
    digitalWrite(RS485_DE_RE, LOW);  // Start in receive mode

    Serial.println("Outdoor unit ready.");
    Serial.printf("Tank depth: %u mm\n", TANK_DEPTH_MM);
}

void loop() {
    uint16_t distance_mm = averagedDistanceMM();

    if (distance_mm > 0) {
        uint16_t level_mm = (distance_mm < TANK_DEPTH_MM) ? TANK_DEPTH_MM - distance_mm : 0;
        uint8_t  level_pct = (uint8_t)((uint32_t)level_mm * 100 / TANK_DEPTH_MM);

        Serial.printf("Distance: %4u mm | Level: %4u mm (%3u%%)\n",
                      distance_mm, level_mm, level_pct);

        sendRS485Distance(distance_mm);
    } else {
        Serial.println("Sensor read failed, skipping transmission.");
    }

    delay(2000);
}

// Send a single 0x55 trigger and read back the 4-byte JSN-SR04T response.
// Returns distance in mm, or 0 on timeout / bad packet.
uint16_t readSensorDistanceMM() {
    while (sensorSerial.available()) sensorSerial.read();  // flush stale data

    sensorSerial.write(0x55);

    uint32_t t = millis();
    while (sensorSerial.available() < 4) {
        if (millis() - t > 100) return 0;
    }

    uint8_t buf[4];
    sensorSerial.readBytes(buf, 4);

    if (buf[0] != 0xFF) return 0;

    uint8_t checksum = (buf[0] + buf[1] + buf[2]) & 0xFF;
    if (checksum != buf[3]) return 0;

    return ((uint16_t)buf[1] << 8) | buf[2];
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

    digitalWrite(RS485_DE_RE, HIGH);   // Enable transmit
    delayMicroseconds(100);

    rs485Serial.write(packet, PACKET_LEN);
    rs485Serial.flush();               // Wait until all bytes are out

    delayMicroseconds(100);
    digitalWrite(RS485_DE_RE, LOW);    // Back to receive mode
}
