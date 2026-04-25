#include <Arduino.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
// Uncomment the two lines below when OLED hardware arrives:
// #include <Wire.h>
// #include <U8g2lib.h>

// --- Pin assignments ---
// MAX485 RS485 transceiver (UART1) — same pins as outdoor unit for symmetry
#define RS485_RX   25
#define RS485_TX   26
#define RS485_DE_RE 4   // Keep LOW (receive) at all times on the indoor unit

// OLED display I2C (SSD1306 128x64) — uncomment when hardware arrives
// #define OLED_SDA 21
// #define OLED_SCL 22
// U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// --- Tank configuration (must match outdoor unit) ---
#define TANK_DEPTH_MM 2000

// --- RS485 packet protocol (must match outdoor unit) ---
#define PACKET_START          0xAA
#define PACKET_TYPE_DISTANCE  0x01
#define PACKET_LEN            5

// --- BLE UUIDs (custom 128-bit, randomly generated) ---
#define BLE_DEVICE_NAME   "SepticMonitor"
#define SERVICE_UUID      "4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5b"
// Distance to water surface from sensor, uint16 big-endian, millimetres
#define CHAR_DISTANCE_UUID "4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5c"
// 3 bytes: [fill_pct (0-100), fill_level_high, fill_level_low], mm big-endian
#define CHAR_LEVEL_UUID    "4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5d"

HardwareSerial rs485Serial(1);

BLECharacteristic *distanceChar = nullptr;
BLECharacteristic *levelChar    = nullptr;
bool bleConnected = false;

// ------- BLE server callbacks -------
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *) override    { bleConnected = true;  Serial.println("BLE client connected."); }
    void onDisconnect(BLEServer *) override {
        bleConnected = false;
        Serial.println("BLE client disconnected, restarting advertising.");
        BLEDevice::startAdvertising();
    }
};

// ------- RS485 parser -------
// State machine that picks complete 5-byte packets out of the byte stream.
// Returns true and sets distance_mm when a valid packet is decoded.
bool parseRS485Packet(uint16_t &distance_mm) {
    static uint8_t buf[PACKET_LEN];
    static uint8_t pos = 0;

    while (rs485Serial.available()) {
        uint8_t b = (uint8_t)rs485Serial.read();

        if (pos == 0 && b != PACKET_START) continue;   // hunt for start byte

        buf[pos++] = b;

        if (pos == PACKET_LEN) {
            pos = 0;

            if (buf[1] != PACKET_TYPE_DISTANCE) return false;

            uint8_t expected_crc = (buf[0] + buf[1] + buf[2] + buf[3]) & 0xFF;
            if (expected_crc != buf[4]) {
                Serial.println("RS485: checksum mismatch, packet discarded.");
                return false;
            }

            distance_mm = ((uint16_t)buf[2] << 8) | buf[3];
            return true;
        }
    }

    return false;
}

// ------- BLE update -------
void updateBLE(uint16_t distance_mm, uint16_t level_mm, uint8_t level_pct) {
    uint8_t distBuf[2] = {(uint8_t)(distance_mm >> 8), (uint8_t)(distance_mm & 0xFF)};
    distanceChar->setValue(distBuf, 2);

    uint8_t levelBuf[3] = {level_pct,
                            (uint8_t)(level_mm >> 8),
                            (uint8_t)(level_mm & 0xFF)};
    levelChar->setValue(levelBuf, 3);

    if (bleConnected) {
        distanceChar->notify();
        levelChar->notify();
    }
}

// ------- OLED update (enable when hardware arrives) -------
// void updateDisplay(uint16_t distance_mm, uint16_t level_mm, uint8_t level_pct) {
//     display.clearBuffer();
//     display.setFont(u8g2_font_ncenB14_tr);
//     char line[32];
//
//     snprintf(line, sizeof(line), "Fill: %u%%", level_pct);
//     display.drawStr(0, 18, line);
//
//     display.setFont(u8g2_font_ncenB08_tr);
//     snprintf(line, sizeof(line), "Level: %u mm", level_mm);
//     display.drawStr(0, 36, line);
//
//     snprintf(line, sizeof(line), "Dist:  %u mm", distance_mm);
//     display.drawStr(0, 52, line);
//
//     display.sendBuffer();
// }

void setup() {
    Serial.begin(115200);

    rs485Serial.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
    pinMode(RS485_DE_RE, OUTPUT);
    digitalWrite(RS485_DE_RE, LOW);   // Always receive on indoor unit

    // Uncomment when OLED arrives:
    // Wire.begin(OLED_SDA, OLED_SCL);
    // display.begin();
    // display.clearBuffer();
    // display.setFont(u8g2_font_ncenB08_tr);
    // display.drawStr(0, 14, "Waiting...");
    // display.sendBuffer();

    BLEDevice::init(BLE_DEVICE_NAME);
    BLEServer *server = BLEDevice::createServer();
    server->setCallbacks(new ServerCallbacks());

    BLEService *service = server->createService(SERVICE_UUID);

    distanceChar = service->createCharacteristic(
        CHAR_DISTANCE_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    distanceChar->addDescriptor(new BLE2902());

    levelChar = service->createCharacteristic(
        CHAR_LEVEL_UUID,
        BLECharacteristic::PROPERTY_READ | BLECharacteristic::PROPERTY_NOTIFY
    );
    levelChar->addDescriptor(new BLE2902());

    service->start();

    BLEAdvertising *adv = BLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);
    BLEDevice::startAdvertising();

    Serial.println("Indoor unit ready. BLE advertising as \"" BLE_DEVICE_NAME "\".");
}

void loop() {
    uint16_t distance_mm;

    if (parseRS485Packet(distance_mm)) {
        uint16_t level_mm  = (distance_mm < TANK_DEPTH_MM) ? TANK_DEPTH_MM - distance_mm : 0;
        uint8_t  level_pct = (uint8_t)((uint32_t)level_mm * 100 / TANK_DEPTH_MM);

        Serial.printf("Distance: %4u mm | Level: %4u mm (%3u%%)\n",
                      distance_mm, level_mm, level_pct);

        updateBLE(distance_mm, level_mm, level_pct);
        // updateDisplay(distance_mm, level_mm, level_pct);
    }

    delay(10);
}
