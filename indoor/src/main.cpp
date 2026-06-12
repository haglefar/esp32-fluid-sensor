#include <Arduino.h>
#include <HardwareSerial.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <Wire.h>
#include <U8g2lib.h>
#include <esp_task_wdt.h>

// --- Watchdog ---
// Resets the device if loop() blocks for longer than this (e.g. BLE hang)
#define WDT_TIMEOUT_S        60

// --- Pin assignments ---
#define RS485_RX    25
#define RS485_TX    26
#define RS485_DE_RE  4

// OLED display I2C (SSD1306 128x64)
#define OLED_SDA 21
#define OLED_SCL 22
U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);

// --- Tank configuration (must match outdoor unit) ---
#define TANK_DEPTH_MM        2000

// --- Error thresholds ---
// Show E01/E02 on the display if no valid packet arrives within this window
#define NO_SIGNAL_TIMEOUT_MS 30000

// --- RS485 packet protocol (must match outdoor unit) ---
#define PACKET_START          0xAA
#define PACKET_TYPE_DISTANCE  0x01
#define PACKET_LEN            5

// --- BLE UUIDs ---
#define BLE_DEVICE_NAME    "SepticMonitor"
#define SERVICE_UUID       "4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5b"
// 2 bytes uint16 big-endian, mm to water surface
#define CHAR_DISTANCE_UUID "4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5c"
// 3 bytes: [fill_pct, level_H, level_L], fill mm big-endian
#define CHAR_LEVEL_UUID    "4f8a1e2b-3c7d-4e5f-8a9b-0c1d2e3f4a5d"

HardwareSerial rs485Serial(1);

BLECharacteristic *distanceChar = nullptr;
BLECharacteristic *levelChar    = nullptr;
bool bleConnected = false;

// --- Error tracking ---
uint32_t lastGoodPacketMs  = 0;
bool     receivedAnyPacket = false;
bool     hadCrcError       = false;
bool     errorDisplayed    = false;

// ------- BLE server callbacks -------
class ServerCallbacks : public BLEServerCallbacks {
    void onConnect(BLEServer *) override { bleConnected = true;  Serial.println("BLE: client connected."); }
    void onDisconnect(BLEServer *) override {
        bleConnected = false;
        Serial.println("BLE: client disconnected, restarting advertising.");
        BLEDevice::startAdvertising();
    }
};

// ------- RS485 parser -------
// PARSE_OK      — valid packet decoded, distance_mm is set
// PARSE_CRC_ERR — complete packet received but checksum failed (E02)
// PARSE_NONE    — no complete packet yet (normal idle return)
enum ParseResult { PARSE_NONE, PARSE_OK, PARSE_CRC_ERR };

ParseResult parseRS485Packet(uint16_t &distance_mm) {
    static uint8_t  buf[PACKET_LEN];
    static uint8_t  pos        = 0;
    static uint32_t lastByteMs = 0;

    // Reset stale partial packet so a link outage can't leave the
    // parser stuck mid-frame waiting for bytes that will never arrive
    if (pos > 0 && millis() - lastByteMs > 1000) {
        pos = 0;
    }

    while (rs485Serial.available()) {
        lastByteMs = millis();
        uint8_t b  = (uint8_t)rs485Serial.read();

        if (pos == 0 && b != PACKET_START) continue;

        buf[pos++] = b;

        if (pos == PACKET_LEN) {
            pos = 0;

            if (buf[1] != PACKET_TYPE_DISTANCE) return PARSE_NONE;

            uint8_t expected_crc = (buf[0] + buf[1] + buf[2] + buf[3]) & 0xFF;
            if (expected_crc != buf[4]) return PARSE_CRC_ERR;

            distance_mm = ((uint16_t)buf[2] << 8) | buf[3];
            return PARSE_OK;
        }
    }

    return PARSE_NONE;
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

// ------- OLED: normal data -------
void updateDisplay(uint16_t distance_mm, uint16_t level_mm, uint8_t level_pct) {
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB14_tr);
    char line[32];

    snprintf(line, sizeof(line), "Fill: %u%%", level_pct);
    display.drawStr(0, 18, line);

    display.setFont(u8g2_font_ncenB08_tr);
    snprintf(line, sizeof(line), "Level: %u cm", level_mm / 10);
    display.drawStr(0, 36, line);

    snprintf(line, sizeof(line), "Dist:  %u cm", distance_mm / 10);
    display.drawStr(0, 52, line);

    display.sendBuffer();
}

// ------- OLED: error screen -------
// code — short identifier shown on line 2, e.g. "E01"
// msg  — description shown on line 3, e.g. "No signal"
void showErrorDisplay(const char *code, const char *msg) {
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB14_tr);
    display.drawStr(0, 18, "! ERROR !");
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 36, code);
    display.drawStr(0, 52, msg);
    display.sendBuffer();
}

void setup() {
    Serial.begin(115200);

    // Watchdog: triggers a hard reset if loop() hangs for WDT_TIMEOUT_S seconds
    esp_task_wdt_init(WDT_TIMEOUT_S, true);
    esp_task_wdt_add(NULL);

    rs485Serial.begin(9600, SERIAL_8N1, RS485_RX, RS485_TX);
    pinMode(RS485_DE_RE, OUTPUT);
    digitalWrite(RS485_DE_RE, LOW);

    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin();
    display.clearBuffer();
    display.setFont(u8g2_font_ncenB08_tr);
    display.drawStr(0, 14, "Waiting...");
    display.sendBuffer();

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
    uint16_t    distance_mm;
    ParseResult pr = parseRS485Packet(distance_mm);

    if (pr == PARSE_OK) {
        lastGoodPacketMs  = millis();
        receivedAnyPacket = true;
        hadCrcError       = false;
        errorDisplayed    = false;

        uint16_t level_mm  = (distance_mm < TANK_DEPTH_MM) ? TANK_DEPTH_MM - distance_mm : 0;
        uint8_t  level_pct = (uint8_t)((uint32_t)level_mm * 100 / TANK_DEPTH_MM);

        Serial.printf("Distance: %4u cm | Level: %4u cm (%3u%%)\n",
                      distance_mm / 10, level_mm / 10, level_pct);

        updateBLE(distance_mm, level_mm, level_pct);
        updateDisplay(distance_mm, level_mm, level_pct);

    } else if (pr == PARSE_CRC_ERR) {
        hadCrcError = true;
        Serial.println("E02: RS485 checksum error");
    }

    // Determine how long we have been without a good packet
    uint32_t silenceMs = receivedAnyPacket ? millis() - lastGoodPacketMs : millis();

    if (silenceMs > NO_SIGNAL_TIMEOUT_MS && !errorDisplayed) {
        errorDisplayed = true;
        if (hadCrcError) {
            showErrorDisplay("E02", "Bad packet");
            Serial.println("E02: Bad packet — showing error on display");
        } else {
            showErrorDisplay("E01", "No signal");
            Serial.println("E01: No signal — showing error on display");
        }
    }

    esp_task_wdt_reset();
    delay(10);
}
