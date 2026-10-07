#include <Arduino.h>
#include <NimBLEDevice.h>
#include "esp_mac.h"

// Device profile matching the real JBD BMS (BS-26A-072-005)
#define BLE_DEVICE_NAME       "BS-26A-072-005"
#define SERVICE_UUID          "0000FF00-0000-1000-8000-00805F9B34FB"
#define NOTIFY_CHAR_UUID      "0000FF01-0000-1000-8000-00805F9B34FB"
#define WRITE_CHAR_UUID       "0000FF02-0000-1000-8000-00805F9B34FB"

#define SERVICE_FFF0_UUID     "0000FFF0-0000-1000-8000-00805F9B34FB"
#define NOTIFY_FFF1_UUID      "0000FFF1-0000-1000-8000-00805F9B34FB"
#define WRITE_FFF2_UUID       "0000FFF2-0000-1000-8000-00805F9B34FB"

#define LED_PIN 8

void setLedColor(uint8_t r, uint8_t g, uint8_t b) {
    rgbLedWrite(LED_PIN, r, g, b);
}

enum OperationMode {
    MODE_IDLE = 0,
    MODE_DISCHARGE = 1,
    MODE_CHARGE = 2
};

// BMS State Structure
struct BMSState {
    uint16_t pack_voltage_10mv = 2624;   // 26.24 V under 17A load (8S LiFePO4)
    int16_t  current_10ma      = -1700;  // -17.00 A (17A Discharge)
    uint16_t remain_cap_10mah  = 14700;  // 147.00 Ah
    uint16_t nominal_cap_10mah = 15000;  // 150.00 Ah (8S150A)
    uint16_t cycle_count       = 15;     // 15 cycles
    uint16_t prod_date         = 0x30AA; // 2024-05-10
    uint16_t balance_low       = 0x0000; // Balancing bitmask
    uint16_t balance_high      = 0x0000;
    uint16_t protection_status = 0x0000; // 0x0000 = Normal / No alarms
    uint8_t  software_version  = 0x21;   // v2.1
    uint8_t  soc_percent       = 97;     // 97%
    uint8_t  fet_status        = 0x03;   // Bit0: Charge FET (1=ON), Bit1: Discharge FET (1=ON)
    uint8_t  cell_count        = 8;      // 8S LiFePO4 (24V)
    uint8_t  ntc_count         = 2;      // 2 NTC sensors
    uint16_t ntc1_temp_01k     = 2991;   // 26.0 °C
    uint16_t ntc2_temp_01k     = 2986;   // 25.5 °C
    uint16_t cell_mv[8]        = {3280, 3283, 3279, 3282, 3278, 3284, 3280, 3281};
    char     device_name[32]   = "BS-26A-072-005";

    // Mode and Simulation state
    OperationMode mode         = MODE_DISCHARGE;
    float    fractional_mah    = 0.0f;
};

BMSState g_bms;

NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pNotifyChar = nullptr;
NimBLECharacteristic* pWriteChar = nullptr;
NimBLECharacteristic* pNotifyFff1 = nullptr;
NimBLECharacteristic* pWriteFff2 = nullptr;
bool g_deviceConnected = false;
unsigned long g_lastLogTime = 0;
unsigned long g_lastSimTime = 0;

// Calculate JBD Checksum: 0x10000 - sum(bytes), or 0xFFFF if sum is 0
uint16_t calculateJbdCRC(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    if (sum == 0) return 0xFFFF;
    return (uint16_t)(0x10000 - sum);
}

// Server connection callbacks
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        g_deviceConnected = true;
        setLedColor(0, 35, 0); // GREEN: Connected
        Serial.printf("\n[BLE] *** Client connected! Peer address: %s ***\n", connInfo.getAddress().toString().c_str());
        pServer->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 200);
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        g_deviceConnected = false;
        setLedColor(35, 0, 0); // RED: Advertising
        Serial.printf("[BLE] Client disconnected (reason: %d). Restarting advertising...\n", reason);
        NimBLEDevice::startAdvertising();
    }
};

// Response helper functions
void sendJBDResponse(uint8_t reg, const uint8_t* payload, uint8_t payloadLen, uint8_t status = 0x00) {
    if (!g_deviceConnected) return;

    size_t totalLen = 7 + payloadLen;
    uint8_t frame[64];
    
    frame[0] = 0xDD;
    frame[1] = reg;
    frame[2] = status;
    frame[3] = payloadLen;

    if (payload && payloadLen > 0) {
        memcpy(&frame[4], payload, payloadLen);
    }

    uint16_t crc = calculateJbdCRC(&frame[2], 2 + payloadLen);
    frame[4 + payloadLen] = (uint8_t)(crc >> 8);
    frame[5 + payloadLen] = (uint8_t)(crc & 0xFF);
    frame[6 + payloadLen] = 0x77;

    Serial.printf("[JBD TX -> Reg 0x%02X] (%u bytes): ", reg, (unsigned int)totalLen);
    for (size_t i = 0; i < totalLen; ++i) {
        Serial.printf("%02X ", frame[i]);
    }
    Serial.println();

    if (pNotifyChar) {
        pNotifyChar->setValue(frame, totalLen);
        pNotifyChar->notify(frame, totalLen);
    }
    if (pNotifyFff1) {
        pNotifyFff1->setValue(frame, totalLen);
        pNotifyFff1->notify(frame, totalLen);
    }
}

// Build and send Basic Info (Register 0x03)
void sendBasicInfo() {
    uint8_t data[27];

    data[0] = (uint8_t)(g_bms.pack_voltage_10mv >> 8);
    data[1] = (uint8_t)(g_bms.pack_voltage_10mv & 0xFF);

    data[2] = (uint8_t)((uint16_t)g_bms.current_10ma >> 8);
    data[3] = (uint8_t)((uint16_t)g_bms.current_10ma & 0xFF);

    data[4] = (uint8_t)(g_bms.remain_cap_10mah >> 8);
    data[5] = (uint8_t)(g_bms.remain_cap_10mah & 0xFF);

    data[6] = (uint8_t)(g_bms.nominal_cap_10mah >> 8);
    data[7] = (uint8_t)(g_bms.nominal_cap_10mah & 0xFF);

    data[8] = (uint8_t)(g_bms.cycle_count >> 8);
    data[9] = (uint8_t)(g_bms.cycle_count & 0xFF);

    data[10] = (uint8_t)(g_bms.prod_date >> 8);
    data[11] = (uint8_t)(g_bms.prod_date & 0xFF);

    data[12] = (uint8_t)(g_bms.balance_low >> 8);
    data[13] = (uint8_t)(g_bms.balance_low & 0xFF);

    data[14] = (uint8_t)(g_bms.balance_high >> 8);
    data[15] = (uint8_t)(g_bms.balance_high & 0xFF);

    data[16] = (uint8_t)(g_bms.protection_status >> 8);
    data[17] = (uint8_t)(g_bms.protection_status & 0xFF);

    data[18] = g_bms.software_version;
    data[19] = g_bms.soc_percent;
    data[20] = g_bms.fet_status;
    data[21] = g_bms.cell_count;
    data[22] = g_bms.ntc_count;

    data[23] = (uint8_t)(g_bms.ntc1_temp_01k >> 8);
    data[24] = (uint8_t)(g_bms.ntc1_temp_01k & 0xFF);

    data[25] = (uint8_t)(g_bms.ntc2_temp_01k >> 8);
    data[26] = (uint8_t)(g_bms.ntc2_temp_01k & 0xFF);

    sendJBDResponse(0x03, data, sizeof(data));
}

// Build and send Cell Voltages (Register 0x04)
void sendCellVoltages() {
    uint8_t data[32];
    uint8_t count = (g_bms.cell_count > 16) ? 16 : g_bms.cell_count;
    for (int i = 0; i < count; ++i) {
        data[i * 2]     = (uint8_t)(g_bms.cell_mv[i] >> 8);
        data[i * 2 + 1] = (uint8_t)(g_bms.cell_mv[i] & 0xFF);
    }
    sendJBDResponse(0x04, data, count * 2);
}

// Build and send Device Name / Hardware Version (Register 0x05)
void sendDeviceName() {
    size_t len = strlen(g_bms.device_name);
    sendJBDResponse(0x05, (const uint8_t*)g_bms.device_name, len);
}

// Send BLE Module proprietary response (starts with FF AA)
void sendBleModuleResponse(uint8_t cmd, const uint8_t* payload, uint8_t payloadLen) {
    if (!g_deviceConnected) return;

    size_t totalLen = 4 + payloadLen + 1;
    uint8_t frame[64];
    
    frame[0] = 0xFF;
    frame[1] = 0xAA;
    frame[2] = cmd;
    frame[3] = payloadLen;

    uint8_t sum = cmd + payloadLen;
    if (payload && payloadLen > 0) {
        memcpy(&frame[4], payload, payloadLen);
        for (size_t i = 0; i < payloadLen; ++i) {
            sum += payload[i];
        }
    }
    frame[4 + payloadLen] = sum;

    Serial.printf("[BLE MODULE TX -> Cmd 0x%02X] (%u bytes): ", cmd, (unsigned int)totalLen);
    for (size_t i = 0; i < totalLen; ++i) {
        Serial.printf("%02X ", frame[i]);
    }
    Serial.println();

    if (pNotifyChar) {
        pNotifyChar->setValue(frame, totalLen);
        pNotifyChar->notify(frame, totalLen);
    }
    if (pNotifyFff1) {
        pNotifyFff1->setValue(frame, totalLen);
        pNotifyFff1->notify(frame, totalLen);
    }
}

char g_pin[16] = "123456";

// Write callback handler for incoming BLE packets
class WriteCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        std::string rxData = pCharacteristic->getValue();
        size_t len = rxData.length();
        if (len == 0) return;

        const uint8_t* buf = (const uint8_t*)rxData.data();
        Serial.printf("\n[JBD RAW RX <- App on Char %s] (%u bytes): ", 
                      pCharacteristic->getUUID().toString().c_str(), (unsigned int)len);
        for (size_t i = 0; i < len; ++i) {
            Serial.printf("%02X ", buf[i]);
        }
        Serial.println();

        // Handle BLE Module commands (starts with FF AA or FF 55)
        if (len >= 4 && buf[0] == 0xFF && (buf[1] == 0xAA || buf[1] == 0x55)) {
            uint8_t cmd = buf[2];
            uint8_t dataLen = buf[3];
            Serial.printf("[JBD RX] BLE Module Command 0x%02X (len %u)\n", cmd, dataLen);

            if (cmd == 0x15) {
                // PIN Verification / Authentication
                if (dataLen >= 1 && dataLen <= 15) {
                    memcpy(g_pin, &buf[4], dataLen);
                    g_pin[dataLen] = '\0';
                }
                Serial.printf("[JBD RX] -> PIN Verification with '%s'. Responding SUCCESS (0x00).\n", g_pin);
                uint8_t okPayload[1] = { 0x00 };
                sendBleModuleResponse(0x15, okPayload, 1);
                return;
            } else if (cmd == 0x19) {
                // Module Status / Capability Query
                Serial.println("[JBD RX] -> Module Status query. Responding SUCCESS (0x00).");
                uint8_t okPayload[1] = { 0x00 };
                sendBleModuleResponse(0x19, okPayload, 1);
                return;
            } else if (cmd == 0x80) {
                // AT command (e.g. AT^VERSION?)
                Serial.println("[JBD RX] -> AT Command. Responding SP04S034.");
                const char* ver = "SP04S034";
                sendBleModuleResponse(0x80, (const uint8_t*)ver, strlen(ver));
                return;
            } else {
                // Generic ACK for any other BLE module command
                uint8_t okPayload[1] = { 0x00 };
                sendBleModuleResponse(cmd, okPayload, 1);
                return;
            }
        }

        // Handle 3B 3C Sinowealth / Jiabaida telemetry queries
        if (len >= 2 && buf[0] == 0x3B && buf[1] == 0x3C) {
            Serial.println("[JBD RX] 3B 3C Query frame received. Responding status OK.");
            uint8_t resp3b[15] = { 0x3B, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x97, 0x71, 0x0D };
            if (pNotifyChar) {
                pNotifyChar->setValue(resp3b, sizeof(resp3b));
                pNotifyChar->notify(resp3b, sizeof(resp3b));
            }
            if (pNotifyFff1) {
                pNotifyFff1->setValue(resp3b, sizeof(resp3b));
                pNotifyFff1->notify(resp3b, sizeof(resp3b));
            }
            return;
        }

        // Validate Start Byte (0xDD) and Stop Byte (0x77)
        if (buf[0] != 0xDD || buf[len - 1] != 0x77) {
            Serial.println("[JBD RX] Framing mismatch. Replying generic ACK...");
            sendJBDResponse(buf[0], nullptr, 0, 0x00);
            return;
        }

        uint8_t cmd      = buf[1]; // 0xA5 (Read) or 0x5A (Write)
        uint8_t reg      = buf[2]; // Target Register
        uint8_t dataLen  = buf[3]; // Payload data length

        if (cmd == 0xA5) {
            // Read Command
            Serial.printf("[JBD CMD] >> Read Request for Register 0x%02X <<\n", reg);
            switch (reg) {
                case 0x00: { // Device Status / Unlock Status
                    uint8_t status0[2] = {0x00, 0x00};
                    sendJBDResponse(0x00, status0, 2);
                    break;
                }
                case 0x03: // Basic Info
                    sendBasicInfo();
                    break;
                case 0x04: // Cell Voltages
                    sendCellVoltages();
                    break;
                case 0x05: // Device Name
                    sendDeviceName();
                    break;
                case 0xA0: { // Manufacturer Name
                    const char* mfg = "Jiabaida";
                    sendJBDResponse(0xA0, (const uint8_t*)mfg, strlen(mfg));
                    break;
                }
                case 0xA1: { // Device Model String
                    const char* dev = "BS-26A-072-005";
                    sendJBDResponse(0xA1, (const uint8_t*)dev, strlen(dev));
                    break;
                }
                case 0xA2: { // Barcode String
                    const char* hw = "BS-26A-072-005";
                    sendJBDResponse(0xA2, (const uint8_t*)hw, strlen(hw));
                    break;
                }
                case 0x10: { // Design Capacity (150.00Ah = 15000 = 0x3A98)
                    uint8_t cap[2] = {0x3A, 0x98};
                    sendJBDResponse(0x10, cap, 2);
                    break;
                }
                case 0x11: { // Cycle Capacity (147.00Ah = 14700 = 0x396C)
                    uint8_t cap[2] = {0x39, 0x6C};
                    sendJBDResponse(0x11, cap, 2);
                    break;
                }
                case 0x12: { // Chg Overcurrent (50.00A = 5000 = 0x1388)
                    uint8_t chgOc[2] = {0x13, 0x88};
                    sendJBDResponse(0x12, chgOc, 2);
                    break;
                }
                case 0x13: { // Dischg Overcurrent (100.00A = 10000 = 0x2710)
                    uint8_t dsgOc[2] = {0x27, 0x10};
                    sendJBDResponse(0x13, dsgOc, 2);
                    break;
                }
                case 0x14: { // Short Circuit Delay (200us = 0x00C8)
                    uint8_t scd[2] = {0x00, 0xC8};
                    sendJBDResponse(0x14, scd, 2);
                    break;
                }
                case 0x15: { // Manufacture Date
                    uint8_t mfgDate[2] = {0x30, 0xAA};
                    sendJBDResponse(0x15, mfgDate, 2);
                    break;
                }
                case 0x16: { // Serial Number String
                    const char* sn = "BS-26A-072-005";
                    sendJBDResponse(0x16, (const uint8_t*)sn, strlen(sn));
                    break;
                }
                case 0x17: { // Bluetooth Name String
                    const char* btName = "BS-26A-072-005";
                    sendJBDResponse(0x17, (const uint8_t*)btName, strlen(btName));
                    break;
                }
                case 0x18: { // Bluetooth PIN String
                    sendJBDResponse(0x18, (const uint8_t*)g_pin, strlen(g_pin));
                    break;
                }
                case 0x19: { // User Data / Barcode String
                    const char* uData = "BS-26A-072-005";
                    sendJBDResponse(0x19, (const uint8_t*)uData, strlen(uData));
                    break;
                }
                case 0x20: { // High Cell Voltage Alarm (3600mV)
                    uint8_t v[2] = {0x0E, 0x10};
                    sendJBDResponse(0x20, v, 2);
                    break;
                }
                case 0x21: { // High Cell Voltage Alarm Release (3450mV)
                    uint8_t v[2] = {0x0D, 0x7A};
                    sendJBDResponse(0x21, v, 2);
                    break;
                }
                case 0x22: { // Low Cell Voltage Alarm (2700mV)
                    uint8_t v[2] = {0x0A, 0x8C};
                    sendJBDResponse(0x22, v, 2);
                    break;
                }
                case 0x23: { // Low Cell Voltage Alarm Release (2900mV)
                    uint8_t v[2] = {0x0B, 0x54};
                    sendJBDResponse(0x23, v, 2);
                    break;
                }
                case 0x24: { // Cell Overvoltage Protection (3650mV = 0x0E42)
                    uint8_t covp[2] = {0x0E, 0x42};
                    sendJBDResponse(0x24, covp, 2);
                    break;
                }
                case 0x25: { // Cell Overvoltage Release (3500mV = 0x0DAC)
                    uint8_t covpRel[2] = {0x0D, 0xAC};
                    sendJBDResponse(0x25, covpRel, 2);
                    break;
                }
                case 0x26: { // Cell Undervoltage Protection (2500mV = 0x09C4)
                    uint8_t cuvp[2] = {0x09, 0xC4};
                    sendJBDResponse(0x26, cuvp, 2);
                    break;
                }
                case 0x27: { // Cell Undervoltage Release (2800mV = 0x0AF0)
                    uint8_t cuvpRel[2] = {0x0A, 0xF0};
                    sendJBDResponse(0x27, cuvpRel, 2);
                    break;
                }
                case 0x28: { // Pack Overvoltage Protection (29200mV = 0x7210)
                    uint8_t v[2] = {0x72, 0x10};
                    sendJBDResponse(0x28, v, 2);
                    break;
                }
                case 0x29: { // Pack Overvoltage Release (28000mV = 0x6D60)
                    uint8_t v[2] = {0x6D, 0x60};
                    sendJBDResponse(0x29, v, 2);
                    break;
                }
                case 0x2A: { // Pack Undervoltage Protection (20000mV = 0x4E20)
                    uint8_t v[2] = {0x4E, 0x20};
                    sendJBDResponse(0x2A, v, 2);
                    break;
                }
                case 0x2B: { // Pack Undervoltage Release (22400mV = 0x5780)
                    uint8_t v[2] = {0x57, 0x80};
                    sendJBDResponse(0x2B, v, 2);
                    break;
                }
                case 0x2C: { // Chg High Temp (55C = 3281 = 0x0CD1)
                    uint8_t t[2] = {0x0C, 0xD1};
                    sendJBDResponse(0x2C, t, 2);
                    break;
                }
                case 0x2D: { // Function Configuration Mask
                    uint8_t fnCfg[2] = {0x00, 0x1F}; // All features active
                    sendJBDResponse(0x2D, fnCfg, 2);
                    break;
                }
                case 0x2E: { // Chg Low Temp (0C = 2731 = 0x0AAB)
                    uint8_t t[2] = {0x0A, 0xAB};
                    sendJBDResponse(0x2E, t, 2);
                    break;
                }
                case 0x2F: { // Chg Low Temp Release (5C = 2781 = 0x0ADD)
                    uint8_t t[2] = {0x0A, 0xDD};
                    sendJBDResponse(0x2F, t, 2);
                    break;
                }
                case 0x30: { // Dischg High Temp (65C = 3381 = 0x0D35)
                    uint8_t t[2] = {0x0D, 0x35};
                    sendJBDResponse(0x30, t, 2);
                    break;
                }
                case 0x31: { // Dischg High Temp Release (55C = 3281 = 0x0CD1)
                    uint8_t t[2] = {0x0C, 0xD1};
                    sendJBDResponse(0x31, t, 2);
                    break;
                }
                case 0x32: { // Dischg Low Temp (-20C = 2531 = 0x09E3)
                    uint8_t t[2] = {0x09, 0xE3};
                    sendJBDResponse(0x32, t, 2);
                    break;
                }
                case 0x33: { // Dischg Low Temp Release (-10C = 2631 = 0x0A47)
                    uint8_t t[2] = {0x0A, 0x47};
                    sendJBDResponse(0x33, t, 2);
                    break;
                }
                case 0x34: { // Balance Start Voltage (3400mV = 0x0D48)
                    uint8_t b[2] = {0x0D, 0x48};
                    sendJBDResponse(0x34, b, 2);
                    break;
                }
                case 0x35: { // Balance Delta (10mV = 0x000A)
                    uint8_t b[2] = {0x00, 0x0A};
                    sendJBDResponse(0x35, b, 2);
                    break;
                }
                case 0xAA: { // Error Counts (11 U16 = 22 bytes zeroes)
                    uint8_t errs[22] = {0};
                    sendJBDResponse(0xAA, errs, 22);
                    break;
                }
                default: {
                    uint8_t dummy[2] = {0x00, 0x00};
                    Serial.printf("[JBD CMD] General Register: 0x%02X acknowledged.\n", reg);
                    sendJBDResponse(reg, dummy, 2, 0x00);
                    break;
                }
            }
        } else if (cmd == 0x5A) {
            // Write Command
            Serial.printf("[JBD CMD] >> Write Command for Register 0x%02X (dataLen=%u) <<\n", reg, dataLen);
            if (reg == 0xE1) {
                // MOSFET Control
                if (dataLen >= 1) {
                    uint8_t newFetState = buf[4];
                    if (dataLen >= 2 && buf[4] == 0x00) {
                        newFetState = buf[5];
                    }
                    g_bms.fet_status = newFetState & 0x03;
                    Serial.printf("[JBD FET] Updated MOSFET State: 0x%02X (Charge FET: %s, Discharge FET: %s)\n",
                                  g_bms.fet_status,
                                  (g_bms.fet_status & 0x01) ? "ON" : "OFF",
                                  (g_bms.fet_status & 0x02) ? "ON" : "OFF");
                }
                sendJBDResponse(0xE1, nullptr, 0, 0x00);
            } else {
                Serial.printf("[JBD CMD] Unlock / Parameter Write 0x%02X acknowledged.\n", reg);
                sendJBDResponse(reg, nullptr, 0, 0x00);
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    delay(1000);

    // Set custom Bluetooth MAC address to match real BMS: A5:C2:3A:26:F2:C2
    uint8_t custom_mac[6] = {0xA5, 0xC2, 0x3A, 0x26, 0xF2, 0xC2};
    esp_base_mac_addr_set(custom_mac);

    Serial.println("\n=======================================================");
    Serial.println("       ESP32-C6 JBD BMS BLE Emulator (BS-26A profile)  ");
    Serial.println("=======================================================");
    Serial.printf("Device Name: %s\n", BLE_DEVICE_NAME);
    Serial.printf("Target MAC: A5:C2:3A:26:F2:C2\n");
    Serial.printf("Pack Voltage: %.2fV | SOC: %u%% | Cells: %uS\n", 
                  g_bms.pack_voltage_10mv / 100.0f, g_bms.soc_percent, g_bms.cell_count);
    Serial.printf("MOSFET Status: 0x%02X (Charge: %s, Discharge: %s)\n",
                  g_bms.fet_status,
                  (g_bms.fet_status & 0x01) ? "ON" : "OFF",
                  (g_bms.fet_status & 0x02) ? "ON" : "OFF");
    Serial.println("=======================================================\n");

    // Initialize NimBLE Device
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(false, false, false); // Open pairing

    // Create Server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // 1. Primary Service 0xFF00 (Classic & Standard JBD)
    NimBLEService* pService = pServer->createService(SERVICE_UUID);
    pNotifyChar = pService->createCharacteristic(
        NOTIFY_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
    );
    pWriteChar = pService->createCharacteristic(
        WRITE_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pWriteChar->setCallbacks(new WriteCallbacks());

    // 2. Secondary Service 0xFFF0 (Jiabaida New Revision)
    NimBLEService* pServiceFff0 = pServer->createService(SERVICE_FFF0_UUID);
    pNotifyFff1 = pServiceFff0->createCharacteristic(
        NOTIFY_FFF1_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
    );
    pWriteFff2 = pServiceFff0->createCharacteristic(
        WRITE_FFF2_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pWriteFff2->setCallbacks(new WriteCallbacks());

    // 3. Device Info Service 0x180A
    NimBLEService* pDevInfo = pServer->createService(NimBLEUUID((uint16_t)0x180A));
    NimBLECharacteristic* pModelChar = pDevInfo->createCharacteristic(
        NimBLEUUID((uint16_t)0x2A24), NIMBLE_PROPERTY::READ
    );
    pModelChar->setValue("BS-26A-072-005");

    NimBLECharacteristic* pMfgChar = pDevInfo->createCharacteristic(
        NimBLEUUID((uint16_t)0x2A29), NIMBLE_PROPERTY::READ
    );
    pMfgChar->setValue("Jiabaida");

    // Start Server
    pServer->start();

    // Configure Advertising for iOS CoreBluetooth (Strict <= 31 bytes per packet)
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

    // Primary Advertisement Packet (25 bytes <= 31 bytes): Flags + Complete Name + 16-bit Service UUID
    NimBLEAdvertisementData advData;
    advData.setFlags(0x06); // General Discoverable + BR_EDR_NOT_SUPPORTED
    advData.setName(BLE_DEVICE_NAME);
    advData.setCompleteServices(NimBLEUUID((uint16_t)0xFF00));

    // Scan Response Packet (24 bytes <= 31 bytes): 128-bit UUID + pure 6-byte MAC
    NimBLEAdvertisementData scanData;
    scanData.setCompleteServices(NimBLEUUID("0000FF00-0000-1000-8000-00805F9B34FB"));
    uint8_t mfgBytes[6] = {0xA5, 0xC2, 0x3A, 0x26, 0xF2, 0xC2};
    scanData.setManufacturerData(std::string((char*)mfgBytes, 6));

    pAdvertising->setAdvertisementData(advData);
    pAdvertising->setScanResponseData(scanData);
    pAdvertising->setMinInterval(32); // 20ms advertising interval for instant discovery
    pAdvertising->setMaxInterval(64); // 40ms
    pAdvertising->start();

    Serial.println("[BLE] 100% Compliant Dual-UUID iOS Advertising started as 'BS-26A-072-005'.");
    Serial.println("[BLE] Ready for connection from JBD BMS iOS app...");
}

void loop() {
    unsigned long now = millis();

    // High frequency physics & telemetry simulation (every 200ms)
    if (now - g_lastSimTime >= 200) {
        float dt_sec = (now - g_lastSimTime) / 1000.0f;
        g_lastSimTime = now;

        // Realistic small current jitter (+/- 0.10A)
        static int stepCount = 0;
        stepCount++;
        float jitter = ((stepCount % 5) - 2) * 0.05f;
        int drift = (stepCount % 3 == 0) ? 1 : ((stepCount % 3 == 1) ? -1 : 0);

        if (g_bms.mode == MODE_DISCHARGE && (g_bms.fet_status & 0x02)) {
            // Active Discharge (-17A)
            float currentA = -17.0f + jitter;
            g_bms.current_10ma = (int16_t)(currentA * 100.0f); // -1700 (10mA)

            // Integrate capacity: delta_Ah = I * dt
            float delta_mah = (17.0f * 1000.0f) * (dt_sec / 3600.0f);
            g_bms.fractional_mah += delta_mah;
            while (g_bms.fractional_mah >= 10.0f) {
                if (g_bms.remain_cap_10mah > 0) g_bms.remain_cap_10mah--;
                g_bms.fractional_mah -= 10.0f;
            }

            // Cell voltages sag under 17A discharge (~3.28V)
            g_bms.cell_mv[0] = 3280 + drift;
            g_bms.cell_mv[1] = 3283 - drift;
            g_bms.cell_mv[2] = 3279 + drift;
            g_bms.cell_mv[3] = 3282 - drift;
            g_bms.cell_mv[4] = 3278 + drift;
            g_bms.cell_mv[5] = 3284 - drift;
            g_bms.cell_mv[6] = 3280 + drift;
            g_bms.cell_mv[7] = 3281 - drift;

            uint32_t sum_mv = 0;
            for (int i = 0; i < 8; ++i) sum_mv += g_bms.cell_mv[i];
            g_bms.pack_voltage_10mv = (uint16_t)(sum_mv / 10);
        } else if (g_bms.mode == MODE_CHARGE && (g_bms.fet_status & 0x01)) {
            // Active Charge (+25A)
            float currentA = 25.0f + jitter;
            g_bms.current_10ma = (int16_t)(currentA * 100.0f);

            float delta_mah = (25.0f * 1000.0f) * (dt_sec / 3600.0f);
            g_bms.fractional_mah += delta_mah;
            while (g_bms.fractional_mah >= 10.0f) {
                if (g_bms.remain_cap_10mah < g_bms.nominal_cap_10mah) g_bms.remain_cap_10mah++;
                g_bms.fractional_mah -= 10.0f;
            }

            // Cell voltages rise during charging (~3.45V)
            g_bms.cell_mv[0] = 3450 + drift;
            g_bms.cell_mv[1] = 3452 - drift;
            g_bms.cell_mv[2] = 3448 + drift;
            g_bms.cell_mv[3] = 3451 - drift;
            g_bms.cell_mv[4] = 3449 + drift;
            g_bms.cell_mv[5] = 3453 - drift;
            g_bms.cell_mv[6] = 3450 + drift;
            g_bms.cell_mv[7] = 3451 - drift;

            uint32_t sum_mv = 0;
            for (int i = 0; i < 8; ++i) sum_mv += g_bms.cell_mv[i];
            g_bms.pack_voltage_10mv = (uint16_t)(sum_mv / 10);
        } else {
            // Idle Mode (0A)
            g_bms.current_10ma = 0;
            for (int i = 0; i < 8; ++i) g_bms.cell_mv[i] = 3320;
            g_bms.pack_voltage_10mv = 2656; // 8 * 3.32V
        }

        // Calculate SOC (%) based on capacity
        if (g_bms.nominal_cap_10mah > 0) {
            g_bms.soc_percent = (uint8_t)(((uint32_t)g_bms.remain_cap_10mah * 100) / g_bms.nominal_cap_10mah);
        }
    }

    // Periodic Serial Status Log (every 2000ms)
    if (now - g_lastLogTime >= 2000) {
        g_lastLogTime = now;
        float packV = g_bms.pack_voltage_10mv / 100.0f;
        float currentA = g_bms.current_10ma / 100.0f;
        float powerW = packV * fabs(currentA);
        float remainAh = g_bms.remain_cap_10mah / 100.0f;

        const char* modeStr = (g_bms.mode == MODE_DISCHARGE) ? "DISCHARGING (-)" : 
                              (g_bms.mode == MODE_CHARGE) ? "CHARGING (+)" : "IDLE";

        Serial.printf("[STATUS] BLE: %s | Mode: %s | Pack: %.2fV | Current: %.2fA | Power: %.1fW | SOC: %u%% (%.2fAh)\n",
                      g_deviceConnected ? "CONNECTED" : "ADVERTISING",
                      modeStr, packV, currentA, powerW, g_bms.soc_percent, remainAh);
    }

    // Handle interactive serial commands for test bench control
    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == 'd' || ch == 'D') {
            g_bms.mode = MODE_DISCHARGE;
            Serial.println("\n[CMD] Switched to 17A DISCHARGE mode.");
        } else if (ch == 'c' || ch == 'C') {
            g_bms.mode = MODE_CHARGE;
            Serial.println("\n[CMD] Switched to 25A CHARGE mode.");
        } else if (ch == 'i' || ch == 'I') {
            g_bms.mode = MODE_IDLE;
            Serial.println("\n[CMD] Switched to IDLE mode.");
        } else if (ch == 'f' || ch == 'F') {
            // Toggle MOSFETs
            g_bms.fet_status = (g_bms.fet_status == 0x03) ? 0x00 : 0x03;
            Serial.printf("\n[CMD] Toggled MOSFETs: %s\n", (g_bms.fet_status == 0x03) ? "ENABLED (ON)" : "DISABLED (OFF)");
        }
    }
}
