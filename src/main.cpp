#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>

// ============================================================================
// Device & BLE Profile Configuration
// ============================================================================
#define BLE_DEVICE_NAME       "JBD-BS-26A-072-005"
#define SERVICE_UUID          ((uint16_t)0xFF00)
#define NOTIFY_CHAR_UUID      ((uint16_t)0xFF01)
#define WRITE_CHAR_UUID       ((uint16_t)0xFF02)

#define SERVICE_FFF0_UUID     ((uint16_t)0xFFF0)
#define NOTIFY_FFF1_UUID      ((uint16_t)0xFFF1)
#define WRITE_FFF2_UUID       ((uint16_t)0xFFF2)

// Onboard WS2812 RGB LED for WeAct Studio ESP32-C6 Mini (GPIO 8)
#ifndef RGB_LED_PIN
#define RGB_LED_PIN 8
#endif

// PIN Code for JBD Authentication (System passkey & App-layer PIN)
#define JBD_PIN_CODE "123456"

// Target Bluetooth MAC Address
const uint8_t TARGET_BT_MAC[6] = {0xA5, 0xC2, 0x3A, 0x26, 0xF2, 0xC2};

// ============================================================================
// RGB LED Control with 50% Brightness Scaling
// ============================================================================
void setRgbLed(uint8_t r, uint8_t g, uint8_t b) {
    uint8_t scaled_r = r / 2;
    uint8_t scaled_g = g / 2;
    uint8_t scaled_b = b / 2;
    rgbLedWrite(RGB_LED_PIN, scaled_r, scaled_g, scaled_b);
}

enum OperationMode {
    MODE_IDLE = 0,
    MODE_DISCHARGE = 1,
    MODE_CHARGE = 2
};

// ============================================================================
// BMS State Structure (BS-26A-072-005 / 8S200A 24V LiFePO4 Profile)
// ============================================================================
struct BMSState {
    uint16_t pack_voltage_10mv = 2624;   // 26.24 V under 17A load (8S LiFePO4)
    int16_t  current_10ma      = -1700;  // -17.00 A (17A Discharge)
    uint16_t remain_cap_10mah  = 19400;  // 194.00 Ah
    uint16_t nominal_cap_10mah = 20000;  // 200.00 Ah (8S200A)
    uint16_t cycle_count       = 15;     // 15 cycles
    uint16_t prod_date         = 0x30AA; // 2024-05-10
    uint16_t balance_low       = 0x0000; // Balancing bitmask
    uint16_t balance_high      = 0x0000;
    uint16_t protection_status = 0x0000; // 0x0000 = Normal / No alarms
    uint8_t  software_version  = 0x13;   // v13 (matches DB24SF01 version 13)
    uint8_t  soc_percent       = 97;     // 97%
    uint8_t  fet_status        = 0x03;   // Bit0: Charge FET (1=ON), Bit1: Discharge FET (1=ON)
    uint8_t  cell_count        = 8;      // 8S LiFePO4 (24V)
    uint8_t  ntc_count         = 2;      // 2 NTC sensors
    uint16_t ntc1_temp_01k     = 2991;   // 26.0 °C (2731 + 260)
    uint16_t ntc2_temp_01k     = 2986;   // 25.5 °C (2731 + 255)
    uint16_t cell_mv[8]        = {3280, 3283, 3279, 3282, 3278, 3284, 3280, 3281};
    char     device_name[32]   = "BS-26A-072-005";

    // Mode and Simulation state
    OperationMode mode         = MODE_DISCHARGE;
    float    fractional_mah    = 0.0f;
    bool     authenticated     = true; // App layer authentication status
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

// ============================================================================
// Multi-color Dynamic Status LED Indicator
// ============================================================================
void updateStatusLed() {
    if (!g_deviceConnected) {
        // 🔴 Red: Advertising / Waiting for Bluetooth client
        setRgbLed(40, 0, 0);
    } else {
        // Connected to Client
        if (g_bms.protection_status != 0) {
            // 🟡 Yellow: Protection / Alarm active
            setRgbLed(40, 32, 0);
        } else if (g_bms.soc_percent >= 100) {
            // ⚪ White: 100% Fully Charged
            setRgbLed(25, 25, 25);
        } else if (g_bms.mode == MODE_CHARGE && (g_bms.fet_status & 0x01)) {
            // 🩵 Cyan: Active Charging (+25A)
            setRgbLed(0, 35, 35);
        } else if (g_bms.mode == MODE_DISCHARGE && (g_bms.fet_status & 0x02)) {
            // 🟠 Orange: Active Discharging (-17A)
            setRgbLed(40, 14, 0);
        } else {
            // 🟢 Green: Standby / Idle (0A, connected)
            setRgbLed(0, 35, 0);
        }
    }
}

// Calculate JBD Checksum: 0x10000 - sum(bytes), or 0xFFFF if sum is 0
uint16_t calculateJbdCRC(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    if (sum == 0) return 0xFFFF;
    return (uint16_t)(0x10000 - sum);
}

// ============================================================================
// Server Connection & Security Callbacks (Passkey = 123456)
// ============================================================================
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        g_deviceConnected = true;
        updateStatusLed();
        Serial.printf("\n[BLE] *** Client connected! Peer address: %s ***\n", connInfo.getAddress().toString().c_str());
        pServer->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 400); // 15-30ms for iOS/Android
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        g_deviceConnected = false;
        updateStatusLed();
        Serial.printf("[BLE] Client disconnected (reason: %d). Restarting advertising...\n", reason);
        NimBLEDevice::startAdvertising();
    }

    uint32_t onPassKeyDisplay() override {
        Serial.println("[BLE SEC] >> onPassKeyDisplay: Returning 123456 <<");
        return 123456;
    }

    void onPassKeyEntry(NimBLEConnInfo& connInfo) override {
        Serial.println("[BLE SEC] >> onPassKeyEntry: Injecting 123456 <<");
        NimBLEDevice::injectPassKey(connInfo, 123456);
    }

    void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override {
        Serial.printf("[BLE SEC] >> onConfirmPassKey: %06u <<\n", (unsigned int)pin);
        NimBLEDevice::injectConfirmPasskey(connInfo, pin == 123456);
    }

    void onAuthenticationComplete(NimBLEConnInfo& connInfo) override {
        if (connInfo.isEncrypted()) {
            Serial.println("[BLE SEC] *** Pairing & Authentication SUCCESSFUL! ***");
        } else {
            Serial.println("[BLE SEC] !!! Pairing FAILED / Unencrypted !!!");
        }
    }
};

// ============================================================================
// JBD Protocol Response Helper (Unfragmented full-frame for MTU >= 23)
// ============================================================================
void sendJBDResponse(uint8_t reg, const uint8_t* payload, uint8_t payloadLen, uint8_t status = 0x00) {
    if (!g_deviceConnected) return;

    size_t totalLen = 7 + payloadLen;
    uint8_t frame[128];
    
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

    // Send notification immediately on primary and secondary characteristics
    if (pNotifyChar) {
        pNotifyChar->setValue(frame, totalLen);
        pNotifyChar->notify();
    }
    if (pNotifyFff1) {
        pNotifyFff1->setValue(frame, totalLen);
        pNotifyFff1->notify();
    }

    if (Serial) {
        Serial.printf("[JBD TX -> Reg 0x%02X] (%u bytes)\n", reg, (unsigned int)totalLen);
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

// Build and send Barcode / Serial Info (Register 0xA0)
void sendBarcode() {
    const char* barcode = "BS-26A-072-005";
    sendJBDResponse(0xA0, (const uint8_t*)barcode, strlen(barcode));
}

// Build and send Manufacturer Name (Register 0xA1)
void sendManufacturerName() {
    const char* mfg = "JBD BMS";
    sendJBDResponse(0xA1, (const uint8_t*)mfg, strlen(mfg));
}

// Build and send Hardware Info (Register 0xA2)
void sendHardwareVersion() {
    const char* hw = "BS-26A-072-005 (8S200A)";
    sendJBDResponse(0xA2, (const uint8_t*)hw, strlen(hw));
}

// Build and send Parameter / EEPROM registers (0x10 - 0x3F)
void sendParameterRegister(uint8_t reg) {
    uint16_t val = 0;
    switch (reg) {
        case 0x10: val = 3650; break;  // Cell Overvoltage trigger (mV)
        case 0x11: val = 3550; break;  // Cell Overvoltage release (mV)
        case 0x12: val = 2500; break;  // Cell Undervoltage trigger (mV)
        case 0x13: val = 2800; break;  // Cell Undervoltage release (mV)
        case 0x14: val = 2920; break;  // Pack Overvoltage trigger (10mV)
        case 0x15: val = 2840; break;  // Pack Overvoltage release (10mV)
        case 0x16: val = 2000; break;  // Pack Undervoltage trigger (10mV)
        case 0x17: val = 2240; break;  // Pack Undervoltage release (10mV)
        case 0x18: val = 3281; break;  // Charge Overtemp trigger (55.0°C)
        case 0x19: val = 3181; break;  // Charge Overtemp release (45.0°C)
        case 0x1A: val = 2731; break;  // Charge Undertemp trigger (0.0°C)
        case 0x1B: val = 2781; break;  // Charge Undertemp release (5.0°C)
        case 0x1C: val = 3381; break;  // Discharge Overtemp trigger (65.0°C)
        case 0x1D: val = 3281; break;  // Discharge Overtemp release (55.0°C)
        case 0x1E: val = 2531; break;  // Discharge Undertemp trigger (-20.0°C)
        case 0x1F: val = 2631; break;  // Discharge Undertemp release (-10.0°C)
        case 0x20: val = 10000; break; // Charge Overcurrent (100.0A)
        case 0x21: val = 20000; break; // Discharge Overcurrent (200.0A for 8S200A)
        case 0x22: val = 3350; break;  // Balance Start Voltage (mV)
        case 0x23: val = 15; break;    // Balance Delta Voltage (mV)
        case 0x24: val = 3650; break;  // Cell Overvoltage Protection (3650mV)
        case 0x25: val = 3500; break;  // Cell Overvoltage Release (3500mV)
        case 0x26: val = 2500; break;  // Cell Undervoltage Protection (2500mV)
        case 0x27: val = 2800; break;  // Cell Undervoltage Release (2800mV)
        case 0x28: val = 5000; break;  // Charge Overcurrent (50.00A)
        case 0x29: val = 20000; break; // Discharge Overcurrent (200.00A)
        case 0x2A: val = 3400; break;  // Balance Start Voltage (3400mV)
        case 0x2B: val = 10; break;    // Balance Window (10mV)
        case 0x2C: val = 5; break;     // Shunt Resistor (0.5mOhm)
        case 0x2D: val = 0x001F; break;// Function Configuration Mask
        case 0x2E: val = 0x0003; break;// NTC Configuration (NTC1 & NTC2 enabled)
        case 0x2F: val = 8; break;     // Cell Count (8 cells)
        default: val = 0; break;
    }
    uint8_t data[2];
    data[0] = (uint8_t)(val >> 8);
    data[1] = (uint8_t)(val & 0xFF);
    sendJBDResponse(reg, data, sizeof(data));
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

    if (pNotifyChar) {
        pNotifyChar->setValue(frame, totalLen);
        pNotifyChar->notify();
    }
    if (pNotifyFff1) {
        pNotifyFff1->setValue(frame, totalLen);
        pNotifyFff1->notify();
    }

    if (Serial) {
        Serial.printf("[BLE MODULE TX -> Cmd 0x%02X] (%u bytes)\n", cmd, (unsigned int)totalLen);
    }
}

// ============================================================================
// Write Callback Handler for Incoming Packets
// ============================================================================
class WriteCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        std::string rxData = pCharacteristic->getValue();
        size_t len = rxData.length();
        if (len == 0) return;

        const uint8_t* buf = (const uint8_t*)rxData.data();
        if (Serial) {
            Serial.printf("\n[JBD RX <- App on Char %s] (%u bytes): ", 
                          pCharacteristic->getUUID().toString().c_str(), (unsigned int)len);
            for (size_t i = 0; i < len; ++i) {
                Serial.printf("%02X ", buf[i]);
            }
            Serial.println();
        }

        // 1. Handle BLE Module Proprietary Commands (starts with FF AA or FF 55)
        if (len >= 4 && buf[0] == 0xFF && (buf[1] == 0xAA || buf[1] == 0x55)) {
            uint8_t cmd = buf[2];
            uint8_t dataLen = buf[3];
            Serial.printf("[JBD RX] BLE Module Command 0x%02X (len %u)\n", cmd, dataLen);

            if (cmd == 0x15) {
                // PIN Verification / Authentication with "123456"
                char receivedPin[16] = {0};
                if (dataLen >= 1 && dataLen <= 15) {
                    memcpy(receivedPin, &buf[4], dataLen);
                }
                Serial.printf("[JBD RX] -> PIN Verification with '%s'. Responding SUCCESS (0x00).\n", receivedPin);
                g_bms.authenticated = true;
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
                Serial.println("[JBD RX] -> AT Command. Responding DB24SF01 V1.0.");
                const char* ver = "DB24SF01 V1.0";
                sendBleModuleResponse(0x80, (const uint8_t*)ver, strlen(ver));
                return;
            } else {
                uint8_t okPayload[1] = { 0x00 };
                sendBleModuleResponse(cmd, okPayload, 1);
                return;
            }
        }

        // 2. Handle Sinowealth / Jiabaida 3B 3C queries
        if (len >= 2 && buf[0] == 0x3B && buf[1] == 0x3C) {
            Serial.println("[JBD RX] 3B 3C Query frame received. Responding status OK.");
            uint8_t resp3b[15] = { 0x3B, 0x3C, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 0x00, 0x97, 0x71, 0x0D };
            if (pNotifyChar) {
                pNotifyChar->setValue(resp3b, sizeof(resp3b));
                pNotifyChar->notify();
            }
            if (pNotifyFff1) {
                pNotifyFff1->setValue(resp3b, sizeof(resp3b));
                pNotifyFff1->notify();
            }
            return;
        }

        // 3. Validate Standard JBD Start Byte (0xDD) and Stop Byte (0x77)
        if (buf[0] != 0xDD || buf[len - 1] != 0x77) {
            Serial.println("[JBD RX] Unrecognized framing. Replying generic ACK...");
            sendJBDResponse(buf[0], nullptr, 0, 0x00);
            return;
        }

        uint8_t cmd      = buf[1]; // 0xA5 (Read), 0x5A (Write), or 0xAA (Check)
        uint8_t reg      = buf[2]; // Target Register
        uint8_t dataLen  = buf[3]; // Payload data length

        if (cmd == 0xA5) {
            // Read Command
            Serial.printf("[JBD CMD] Read Register 0x%02X\n", reg);
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
                case 0x06: { // PIN Code / Password Query -> "123456"
                    const char* pin = JBD_PIN_CODE;
                    sendJBDResponse(0x06, (const uint8_t*)pin, strlen(pin));
                    break;
                }
                case 0xA0: // Barcode String
                    sendBarcode();
                    break;
                case 0xA1: // Manufacturer Name
                    sendManufacturerName();
                    break;
                case 0xA2: // Hardware Model String
                    sendHardwareVersion();
                    break;
                case 0xAA: { // Error Counts / Check status
                    uint8_t errs[2] = {0x00, 0x00};
                    sendJBDResponse(0xAA, errs, 2);
                    break;
                }
                case 0xFA: { // Extended Memory / AFE RAM Read Command (Android JBD app)
                    uint8_t page = (dataLen >= 1) ? buf[4] : 0x00;
                    uint8_t offset = (dataLen >= 2) ? buf[5] : 0x00;
                    uint8_t reqLen = (dataLen >= 3) ? buf[6] : 0x02;

                    Serial.printf("[JBD FA EXT] Read Page 0x%02X, Offset 0x%02X, Length %u\n", page, offset, reqLen);

                    uint8_t respBuf[64] = {0};
                    if (offset == 0x58 && reqLen >= 16) {
                        // Cell Voltages 1..8 in AFE RAM (16 bytes)
                        for (int i = 0; i < 8; ++i) {
                            respBuf[i * 2]     = (uint8_t)(g_bms.cell_mv[i] >> 8);
                            respBuf[i * 2 + 1] = (uint8_t)(g_bms.cell_mv[i] & 0xFF);
                        }
                        sendJBDResponse(0xFA, respBuf, 16);
                    } else if (offset == 0x00 && reqLen == 1) {
                        respBuf[0] = 0x22;
                        sendJBDResponse(0xFA, respBuf, 1);
                    } else if (offset == 0x01 && reqLen == 1) {
                        respBuf[0] = 0x08; // 8S
                        sendJBDResponse(0xFA, respBuf, 1);
                    } else if (offset == 0x05 && reqLen == 1) {
                        respBuf[0] = 0x20;
                        sendJBDResponse(0xFA, respBuf, 1);
                    } else if (offset == 0x9B && reqLen == 1) {
                        respBuf[0] = 0x08; // 8S
                        sendJBDResponse(0xFA, respBuf, 1);
                    } else if (offset == 0x9E && reqLen <= 12) {
                        const char* sn = "DB24SF012024";
                        memcpy(respBuf, sn, strlen(sn));
                        sendJBDResponse(0xFA, respBuf, reqLen);
                    } else if (offset == 0xB0 && reqLen <= 8) {
                        const char* m = "DB24SF01";
                        memcpy(respBuf, m, strlen(m));
                        sendJBDResponse(0xFA, respBuf, reqLen);
                    } else {
                        if (reqLen > 32) reqLen = 32;
                        sendJBDResponse(0xFA, respBuf, reqLen);
                    }
                    break;
                }
                default:
                    if (reg >= 0x10 && reg <= 0x3F) {
                        sendParameterRegister(reg);
                    } else {
                        uint8_t defData[2] = {0x00, 0x00};
                        sendJBDResponse(reg, defData, 2);
                    }
                    break;
            }
        } else if (cmd == 0xAA) {
            // Check / Handshake Command
            Serial.printf("[JBD CMD] Check Command 0x%02X\n", reg);
            uint8_t ackData[2] = {0x00, 0x00};
            sendJBDResponse(reg, ackData, 2);
        } else if (cmd == 0x5A) {
            // Write Command
            Serial.printf("[JBD CMD] Write Register 0x%02X (dataLen=%u)\n", reg, dataLen);
            if (reg == 0xE1) {
                // MOSFET Control
                if (dataLen >= 1) {
                    uint8_t newFetState = buf[4];
                    if (dataLen >= 2 && buf[4] == 0x00) {
                        newFetState = buf[5];
                    }
                    g_bms.fet_status = newFetState & 0x03;
                    updateStatusLed();
                    Serial.printf("[JBD FET] Updated MOSFET State: 0x%02X (Charge FET: %s, Discharge FET: %s)\n",
                                  g_bms.fet_status,
                                  (g_bms.fet_status & 0x01) ? "ON" : "OFF",
                                  (g_bms.fet_status & 0x02) ? "ON" : "OFF");
                }
                sendJBDResponse(0xE1, nullptr, 0, 0x00);
            } else if (reg == 0x06) {
                // Password / PIN Write (e.g. DD 5A 06 06 31 32 33 34 35 36 ...)
                char inputPin[16] = {0};
                if (dataLen >= 1 && dataLen <= 15) {
                    memcpy(inputPin, &buf[4], dataLen);
                }
                Serial.printf("[JBD CMD] Password Write / Verification with PIN: '%s'\n", inputPin);
                g_bms.authenticated = true;
                sendJBDResponse(0x06, nullptr, 0, 0x00);
            } else if (reg == 0x00 || reg == 0x01) {
                // Factory / EEPROM Unlock commands (0x00 = unlock 56 78, 0x01 = lock)
                Serial.printf("[JBD CMD] Unlock / Configuration command 0x%02X acknowledged.\n", reg);
                sendJBDResponse(reg, nullptr, 0, 0x00);
            } else {
                Serial.printf("[JBD CMD] General Write Register 0x%02X acknowledged.\n", reg);
                sendJBDResponse(reg, nullptr, 0, 0x00);
            }
        }
    }
};

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // Non-blocking USB CDC output - never block BLE thread
    delay(500);

    Serial.println("\n=======================================================");
    Serial.println("   ESP32-C6 JBD BMS BLE Emulator (DB24SF01 / 8S200A)   ");
    Serial.println("=======================================================");
    Serial.printf("Device Name: %s\n", BLE_DEVICE_NAME);
    Serial.printf("Target MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  TARGET_BT_MAC[0], TARGET_BT_MAC[1], TARGET_BT_MAC[2],
                  TARGET_BT_MAC[3], TARGET_BT_MAC[4], TARGET_BT_MAC[5]);
    Serial.printf("Security PIN: %s (Passkey: 123456)\n", JBD_PIN_CODE);
    Serial.printf("Pack Voltage: %.2fV | SOC: %u%% | Cells: %uS\n", 
                  g_bms.pack_voltage_10mv / 100.0f, g_bms.soc_percent, g_bms.cell_count);
    Serial.printf("MOSFET Status: 0x%02X (Charge: %s, Discharge: %s)\n",
                  g_bms.fet_status,
                  (g_bms.fet_status & 0x01) ? "ON" : "OFF",
                  (g_bms.fet_status & 0x02) ? "ON" : "OFF");
    Serial.println("=======================================================\n");

    // Configure Custom Bluetooth MAC Address (A5:C2:3A:26:F2:C2)
    esp_base_mac_addr_set(TARGET_BT_MAC);
    esp_iface_mac_addr_set(TARGET_BT_MAC, ESP_MAC_BT);

    // Initialize NimBLE Device
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setMTU(512);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum TX power (+9dBm)

    // Set Security (Passkey 123456)
    NimBLEDevice::setSecurityAuth(true, true, true); // Bonding, MITM protection, Secure Connections
    NimBLEDevice::setSecurityPasskey(123456);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);

    Serial.printf("[BLE] Hardware BLE MAC Address: %s\n", NimBLEDevice::getAddress().toString().c_str());

    // Create Server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // 1. Primary Service 0xFF00 (Classic & Standard JBD / Xiaoxiang)
    NimBLEService* pService = pServer->createService(NimBLEUUID(SERVICE_UUID));
    pNotifyChar = pService->createCharacteristic(
        NimBLEUUID(NOTIFY_CHAR_UUID),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
    );
    pWriteChar = pService->createCharacteristic(
        NimBLEUUID(WRITE_CHAR_UUID),
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pWriteChar->setCallbacks(new WriteCallbacks());

    // 2. Secondary Service 0xFFF0 (Jiabaida New Revision)
    NimBLEService* pServiceFff0 = pServer->createService(NimBLEUUID(SERVICE_FFF0_UUID));
    pNotifyFff1 = pServiceFff0->createCharacteristic(
        NimBLEUUID(NOTIFY_FFF1_UUID),
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
    );
    pWriteFff2 = pServiceFff0->createCharacteristic(
        NimBLEUUID(WRITE_FFF2_UUID),
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
    pMfgChar->setValue("JBD BMS");

    NimBLECharacteristic* pSerialChar = pDevInfo->createCharacteristic(
        NimBLEUUID((uint16_t)0x2A25), NIMBLE_PROPERTY::READ
    );
    pSerialChar->setValue("BS-26A-072-005");

    NimBLECharacteristic* pFwChar = pDevInfo->createCharacteristic(
        NimBLEUUID((uint16_t)0x2A26), NIMBLE_PROPERTY::READ
    );
    pFwChar->setValue("V1.3");

    NimBLECharacteristic* pHwChar = pDevInfo->createCharacteristic(
        NimBLEUUID((uint16_t)0x2A27), NIMBLE_PROPERTY::READ
    );
    pHwChar->setValue("V1.0");

    // Start Services
    pServer->start();

    // Universal BLE Advertising (16-bit Service 0xFF00 + 0xFFF0 + Device Name)
    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName(BLE_DEVICE_NAME);
    pAdvertising->addServiceUUID(NimBLEUUID(SERVICE_UUID));
    pAdvertising->addServiceUUID(NimBLEUUID(SERVICE_FFF0_UUID));
    pAdvertising->enableScanResponse(true);
    pAdvertising->setMinInterval(32); // 20ms
    pAdvertising->setMaxInterval(64); // 40ms
    pAdvertising->start();

    // Initial state: Red LED indicates Advertising / Waiting for client
    updateStatusLed();

    Serial.println("[BLE] Advertising started successfully as 'DB24SF01' with PIN 123456.");
    Serial.println("[BLE] Ready for connection from XiaoxiangBMS, JBD BMS, or ESP32 Universal Monitor...");
}

// ============================================================================
// Main Loop (Physics Simulation + Telemetry + Interactive Control)
// ============================================================================
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
            g_bms.ntc1_temp_01k = 2991; // 26.0 °C
            g_bms.ntc2_temp_01k = 2986; // 25.5 °C

            uint32_t totalMv = 0;
            for (int i = 0; i < 8; ++i) totalMv += g_bms.cell_mv[i];
            g_bms.pack_voltage_10mv = totalMv / 10;
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
            g_bms.ntc1_temp_01k = 2986; // 25.5 °C
            g_bms.ntc2_temp_01k = 2981; // 25.0 °C

            uint32_t totalMv = 0;
            for (int i = 0; i < 8; ++i) totalMv += g_bms.cell_mv[i];
            g_bms.pack_voltage_10mv = totalMv / 10;
        } else {
            // Idle Mode (0A)
            g_bms.current_10ma = 0;
            for (int i = 0; i < 8; ++i) g_bms.cell_mv[i] = 3320;
            g_bms.pack_voltage_10mv = 2656; // 8 * 3.32V
            g_bms.ntc1_temp_01k = 2981; // 25.0 °C
            g_bms.ntc2_temp_01k = 2976; // 24.5 °C
        }

        // Calculate SOC (%)
        if (g_bms.nominal_cap_10mah > 0) {
            g_bms.soc_percent = (uint8_t)(((uint32_t)g_bms.remain_cap_10mah * 100) / g_bms.nominal_cap_10mah);
        }
    }

    // Periodic telemetry log (every 2000ms)
    if (now - g_lastLogTime >= 2000) {
        g_lastLogTime = now;
        updateStatusLed();

        float packV = g_bms.pack_voltage_10mv / 100.0f;
        float currA = g_bms.current_10ma / 100.0f;
        float powerW = packV * fabs(currA);

        const char* modeStr = (g_bms.current_10ma > 50) ? "CHARGING (+)" :
                              ((g_bms.current_10ma < -50) ? "DISCHARGING (-)" : "STANDBY (IDLE)");

        Serial.printf("[STATUS] BLE: %s | Mode: %s | Pack: %.2fV | Current: %+.2fA | Power: %.1fW | SOC: %u%% (%.2fAh)\n",
                      g_deviceConnected ? "CONNECTED" : "ADVERTISING",
                      modeStr, packV, currA, powerW, g_bms.soc_percent, g_bms.remain_cap_10mah / 100.0f);
    }

    // Interactive Serial Control
    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == '1' || ch == 'd' || ch == 'D') {
            g_bms.mode = MODE_DISCHARGE;
            updateStatusLed();
            Serial.println(">>> Switched to MODE_DISCHARGE (-17.00 A) <<<");
        } else if (ch == '2' || ch == 'c' || ch == 'C' || ch == 'g' || ch == 'G') {
            g_bms.mode = MODE_CHARGE;
            updateStatusLed();
            Serial.println(">>> Switched to MODE_CHARGE (+25.00 A) <<<");
        } else if (ch == '0' || ch == 'i' || ch == 'I') {
            g_bms.mode = MODE_IDLE;
            updateStatusLed();
            Serial.println(">>> Switched to MODE_IDLE (0.00 A Standby) <<<");
        } else if (ch == 'f' || ch == 'F') {
            g_bms.fet_status = (g_bms.fet_status == 0x03) ? 0x00 : 0x03;
            updateStatusLed();
            Serial.printf(">>> Toggled MOSFETs: %s <<<\n", (g_bms.fet_status == 0x03) ? "ENABLED (ON)" : "DISABLED (OFF)");
        } else if (ch == '+' || ch == '=') {
            if (g_bms.soc_percent < 100) g_bms.soc_percent++;
            g_bms.remain_cap_10mah = (uint16_t)(((uint32_t)g_bms.nominal_cap_10mah * g_bms.soc_percent) / 100);
            updateStatusLed();
            Serial.printf(">>> SOC set to %u%% (%.2f Ah)\n", g_bms.soc_percent, g_bms.remain_cap_10mah / 100.0f);
        } else if (ch == '-' || ch == '_') {
            if (g_bms.soc_percent > 0) g_bms.soc_percent--;
            g_bms.remain_cap_10mah = (uint16_t)(((uint32_t)g_bms.nominal_cap_10mah * g_bms.soc_percent) / 100);
            updateStatusLed();
            Serial.printf(">>> SOC set to %u%% (%.2f Ah)\n", g_bms.soc_percent, g_bms.remain_cap_10mah / 100.0f);
        }
    }

    delay(10);
}
