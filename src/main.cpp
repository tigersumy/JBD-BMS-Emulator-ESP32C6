#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <Preferences.h>

// ============================================================================
// Device & BLE Profile Configuration
// ============================================================================
#define BLE_DEVICE_NAME       "BS-26A-072-005"
#define SERVICE_UUID          ((uint16_t)0xFF00)
#define NOTIFY_CHAR_UUID      ((uint16_t)0xFF01)
#define WRITE_CHAR_UUID       ((uint16_t)0xFF02)

#define SERVICE_FFF0_UUID     ((uint16_t)0xFFF0)
#define NOTIFY_FFF1_UUID      ((uint16_t)0xFFF1)
#define WRITE_FFF2_UUID       ((uint16_t)0xFFF2)

// Daly Profile Configuration
#define DALY_DEVICE_NAME      "DL-401804015491"
#define DALY_SERVICE_UUID     ((uint16_t)0xFFE0)
#define DALY_NOTIFY_CHAR_UUID ((uint16_t)0xFFE1)
#define DALY_WRITE_CHAR_UUID  ((uint16_t)0xFFE2)

// Hardware Pins
#ifndef RGB_LED_PIN
#define RGB_LED_PIN 8
#endif

#ifndef BOOT_BUTTON_PIN
#define BOOT_BUTTON_PIN 9
#endif

enum EmulatorProtocol {
    PROTO_JBD = 0,
    PROTO_DALY = 1
};

// PIN Code for JBD Authentication (System passkey & App-layer PIN)
#define JBD_PIN_CODE "123456"

// Target Bluetooth MAC Addresses (Separate MACs prevent GATT caching collisions)
const uint8_t TARGET_BT_MAC[6] = {0xA5, 0xC2, 0x3A, 0x26, 0xF2, 0xC2};
const uint8_t DALY_BT_MAC[6]   = {0xA5, 0xC2, 0x3A, 0x26, 0xF2, 0xD1};

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
    uint16_t cycle_cap_10mah   = 16000;  // 160.00 Ah (80% of 200Ah)
    uint16_t cycle_count       = 15;     // 15 cycles
    uint16_t prod_date         = 0x30AA; // 2024-05-10
    uint16_t balance_low       = 0x0000; // Balancing bitmask
    uint16_t balance_high      = 0x0000;
    uint16_t protection_status = 0x0000; // 0x0000 = Normal / No alarms
    uint8_t  software_version  = 0x76;   // v11.8 (matches live JBD BMS firmware 0x76)
    uint8_t  soc_percent       = 97;     // 97%
    uint8_t  fet_status        = 0x03;   // Bit0: Charge FET (1=ON), Bit1: Discharge FET (1=ON)
    uint8_t  cell_count        = 8;      // 8S LiFePO4 (24V)
    uint8_t  ntc_count         = 3;      // 3 NTC sensors (NTC1, NTC2, MOS Temp)
    uint16_t ntc1_temp_01k     = 2991;   // 26.0 °C (2731 + 260)
    uint16_t ntc2_temp_01k     = 2986;   // 25.5 °C (2731 + 255)
    uint16_t ntc3_temp_01k     = 2955;   // 22.4 °C (2731 + 224) - MOS Temp
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

// Daly Characteristics
NimBLECharacteristic* pDalyNotifyChar = nullptr;
NimBLECharacteristic* pDalyWriteChar = nullptr;
NimBLECharacteristic* pDalyNotifyFff1 = nullptr;
NimBLECharacteristic* pDalyFff2 = nullptr;
NimBLECharacteristic* pDalyFff3 = nullptr;
NimBLECharacteristic* pDalyNotifyFff4 = nullptr;
NimBLECharacteristic* pDalyNotifyFf01 = nullptr;
NimBLECharacteristic* pDalyFf02 = nullptr;
NimBLECharacteristic* pDalyNusTx = nullptr;
NimBLECharacteristic* pDalyNusRx = nullptr;

bool g_deviceConnected = false;
unsigned long g_lastLogTime = 0;
unsigned long g_lastSimTime = 0;

static Preferences s_prefs;
static EmulatorProtocol g_proto = PROTO_JBD;
static unsigned long g_bootTime = 0;
static bool g_doubleResetPending = false;

void initDoubleResetAndMode() {
    s_prefs.begin("bms_emu", false);
    bool doubleReset = s_prefs.getBool("dr_flag", false);
    uint8_t savedProto = s_prefs.getUChar("proto", PROTO_JBD);
    if (savedProto > PROTO_DALY) savedProto = PROTO_JBD;

    if (doubleReset) {
        savedProto = (savedProto == PROTO_JBD) ? PROTO_DALY : PROTO_JBD;
        s_prefs.putUChar("proto", savedProto);
        s_prefs.putBool("dr_flag", false);
        Serial.printf("\n=======================================================\n");
        Serial.printf("[SYSTEM] >> DOUBLE RESET DETECTED! Switched to: %s <<\n", 
                      (savedProto == PROTO_DALY) ? "DALY BMS (DL-401804015491)" : "JBD BMS (BS-26A-072-005)");
        Serial.printf("=======================================================\n\n");
    } else {
        s_prefs.putBool("dr_flag", true);
        g_doubleResetPending = true;
    }

    g_proto = (EmulatorProtocol)savedProto;
    g_bootTime = millis();
}

void toggleEmulatorMode() {
    uint8_t newProto = (g_proto == PROTO_JBD) ? PROTO_DALY : PROTO_JBD;
    s_prefs.putUChar("proto", newProto);
    s_prefs.putBool("dr_flag", false);
    Serial.printf("\n[SYSTEM] Mode toggled -> Restarting as %s...\n", 
                  (newProto == PROTO_DALY) ? "DALY BMS" : "JBD BMS");
    for (int i = 0; i < 3; ++i) {
        setRgbLed(50, 50, 0);
        delay(100);
        setRgbLed(0, 0, 0);
        delay(50);
    }
    esp_restart();
}

void setEmulatorMode(EmulatorProtocol p) {
    if (g_proto == p) return;
    s_prefs.putUChar("proto", (uint8_t)p);
    s_prefs.putBool("dr_flag", false);
    Serial.printf("\n[SYSTEM] Mode set to %s -> Restarting...\n", 
                  (p == PROTO_DALY) ? "DALY BMS" : "JBD BMS");
    for (int i = 0; i < 3; ++i) {
        setRgbLed(50, 50, 0);
        delay(100);
        setRgbLed(0, 0, 0);
        delay(50);
    }
    esp_restart();
}

// ============================================================================
// Multi-color Dynamic Status LED Indicator
// ============================================================================
void updateStatusLed() {
    if (g_proto == PROTO_DALY) {
        if (!g_deviceConnected) {
            // 🟣 Purple/Magenta: Daly Advertising / Waiting for BLE client
            setRgbLed(40, 0, 40);
        } else {
            if (g_bms.protection_status != 0) {
                // 🟡 Yellow: Protection / Alarm active
                setRgbLed(40, 32, 0);
            } else if (g_bms.mode == MODE_CHARGE && (g_bms.fet_status & 0x01)) {
                // 🩵 Cyan: Active Charging (+25A)
                setRgbLed(0, 35, 35);
            } else if (g_bms.mode == MODE_DISCHARGE && (g_bms.fet_status & 0x02)) {
                // 🔵 Blue: Daly Active Discharging (-17A)
                setRgbLed(0, 10, 50);
            } else {
                // 🟢 Green: Standby / Idle (0A, connected)
                setRgbLed(0, 35, 0);
            }
        }
    } else {
        if (!g_deviceConnected) {
            // 🔴 Red: JBD Advertising / Waiting for Bluetooth client
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

static NimBLECharacteristic* s_lastJbdTargetNotifyChar = nullptr;
static NimBLECharacteristic* s_lastDalyTargetNotifyChar = nullptr;
static uint16_t s_lastDalyReadCount = 0;

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
        s_lastJbdTargetNotifyChar = nullptr;
        s_lastDalyTargetNotifyChar = nullptr;
        s_lastDalyReadCount = 0;
        updateStatusLed();
        Serial.printf("[BLE] Client disconnected (reason: %d). Restarting advertising...\n", reason);
        NimBLEDevice::startAdvertising();
    }

    void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override {
        Serial.printf("[BLE] MTU negotiated to: %u bytes\n", MTU);
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
// JBD Protocol Response Helper (BLE MTU chunking: 20 bytes per chunk)
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

    // Determine target notify characteristic (avoid notifying on both simultaneously)
    NimBLECharacteristic* targetChar = s_lastJbdTargetNotifyChar;
    if (!targetChar) {
        targetChar = pNotifyChar ? pNotifyChar : pNotifyFff1;
    }
    if (!targetChar) return;

    // Send notifications in 20-byte chunks matching genuine JBD BLE hardware (MTU=23)
    const size_t CHUNK_SIZE = 20;
    for (size_t offset = 0; offset < totalLen && g_deviceConnected; offset += CHUNK_SIZE) {
        size_t sendLen = totalLen - offset;
        if (sendLen > CHUNK_SIZE) sendLen = CHUNK_SIZE;

        targetChar->notify(&frame[offset], sendLen);

        if (offset + sendLen < totalLen) {
            delay(25); // 25ms pacing matching BLE connection interval to ensure packet delivery
        }
    }

    if (Serial) {
        Serial.printf("[JBD TX -> Reg 0x%02X on %s] (%u bytes in %u chunks)\n", 
                      reg, targetChar->getUUID().toString().c_str(),
                      (unsigned int)totalLen, (unsigned int)((totalLen + CHUNK_SIZE - 1) / CHUNK_SIZE));
    }
}

// Build and send Basic Info (Register 0x03)
void sendBasicInfo() {
    uint8_t data[38]; // 38 bytes (0x26) matching original JBD BMS v11.8+ (45 bytes total frame)

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
    data[22] = g_bms.ntc_count; // 3 NTC sensors

    data[23] = (uint8_t)(g_bms.ntc1_temp_01k >> 8);
    data[24] = (uint8_t)(g_bms.ntc1_temp_01k & 0xFF);

    data[25] = (uint8_t)(g_bms.ntc2_temp_01k >> 8);
    data[26] = (uint8_t)(g_bms.ntc2_temp_01k & 0xFF);

    data[27] = (uint8_t)(g_bms.ntc3_temp_01k >> 8);
    data[28] = (uint8_t)(g_bms.ntc3_temp_01k & 0xFF);

    // Extended factory setting registers (9 bytes tail)
    // Matches live JBD BMS: 00 00 00 4B 00 4A A5 00 00
    data[29] = 0x00;
    data[30] = 0x00;
    data[31] = 0x00;
    data[32] = 0x4B;
    data[33] = (uint8_t)(g_bms.nominal_cap_10mah >> 8);
    data[34] = (uint8_t)(g_bms.nominal_cap_10mah & 0xFF);
    data[35] = (uint8_t)(g_bms.remain_cap_10mah >> 8);
    data[36] = (uint8_t)(g_bms.remain_cap_10mah & 0xFF);
    data[37] = 0x00;

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
        case 0x10: val = g_bms.nominal_cap_10mah; break;  // Nominal / Design Capacity (10mAh units -> e.g. 30000 = 300.00Ah)
        case 0x11: val = g_bms.cycle_cap_10mah;   break;  // Cycle Capacity (10mAh units -> e.g. 24000 = 240.00Ah)
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
        case 0x2F: val = g_bms.cell_count; break;     // Cell Count (8 cells)
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
        if (pCharacteristic == pWriteChar) {
            s_lastJbdTargetNotifyChar = pNotifyChar;
        } else if (pCharacteristic == pWriteFff2) {
            s_lastJbdTargetNotifyChar = pNotifyFff1;
        }

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
            if (reg == 0x10) {
                // Nominal / Design Capacity (unit: 10mAh)
                if (dataLen >= 2) {
                    uint16_t newNom = (uint16_t(buf[4]) << 8) | uint16_t(buf[5]);
                    if (newNom > 0) {
                        g_bms.nominal_cap_10mah = newNom;
                        Serial.printf("[JBD EEPROM] Nominal Capacity updated: %u (%.2f Ah)\n", newNom, newNom / 100.0f);
                    }
                }
                sendJBDResponse(0x10, nullptr, 0, 0x00);
            } else if (reg == 0x11) {
                // Cycle Capacity (unit: 10mAh)
                if (dataLen >= 2) {
                    uint16_t newCyc = (uint16_t(buf[4]) << 8) | uint16_t(buf[5]);
                    if (newCyc > 0) {
                        g_bms.cycle_cap_10mah = newCyc;
                        Serial.printf("[JBD EEPROM] Cycle Capacity updated: %u (%.2f Ah)\n", newCyc, newCyc / 100.0f);
                    }
                }
                sendJBDResponse(0x11, nullptr, 0, 0x00);
            } else if (reg == 0xE0) {
                // Real-time remaining capacity calibration (unit: 10mAh)
                if (dataLen >= 2) {
                    uint16_t newRem = (uint16_t(buf[4]) << 8) | uint16_t(buf[5]);
                    g_bms.remain_cap_10mah = newRem;
                    if (g_bms.nominal_cap_10mah > 0) {
                        g_bms.soc_percent = (uint8_t)(((uint32_t)g_bms.remain_cap_10mah * 100) / g_bms.nominal_cap_10mah);
                    }
                    Serial.printf("[JBD RAM] Remaining Capacity calibrated: %u (%.2f Ah, SOC: %u%%)\n", newRem, newRem / 100.0f, g_bms.soc_percent);
                }
                sendJBDResponse(0xE0, nullptr, 0, 0x00);
            } else if (reg == 0x2F) {
                // Cell Count
                if (dataLen >= 1) {
                    uint8_t newCnt = (dataLen >= 2) ? buf[5] : buf[4];
                    if (newCnt >= 3 && newCnt <= 32) {
                        g_bms.cell_count = newCnt;
                        Serial.printf("[JBD EEPROM] Cell Count updated: %uS\n", newCnt);
                    }
                }
                sendJBDResponse(0x2F, nullptr, 0, 0x00);
            } else if (reg == 0xE1) {
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
// Modbus CRC-16 Calculation (Poly 0xA001, Initial 0xFFFF, Little-Endian)
// ============================================================================
uint16_t calculateModbusCRC(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (uint16_t)data[i];
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = crc >> 1;
            }
        }
    }
    return crc;
}

// Send BLE frame directly to the subscribed / active Daly notify characteristic
void sendDalyBleFrame(const uint8_t* frame, size_t len) {
    if (!g_deviceConnected) return;

    NimBLECharacteristic* target = s_lastDalyTargetNotifyChar;
    if (!target) {
        target = pDalyNotifyFff1 ? pDalyNotifyFff1 : pDalyNotifyChar;
    }
    if (target) {
        target->notify(frame, len);
    }
}

// ============================================================================
// Daly Modbus Protocol (D2) Implementations (Official Smart BMS App DL-xxxx)
// ============================================================================

// Universal Daly Modbus Holding Register Provider (16-bit Big-Endian values)
uint16_t getDalyHoldingRegister(uint16_t reg) {
    // 0x0000..0x001F (0..31): Cell voltages 1..32 in mV
    if (reg < 32) {
        if (reg < g_bms.cell_count && reg < 8) {
            return g_bms.cell_mv[reg];
        }
        return 0;
    }

    // 0x0020..0x0027 (32..39): Temperatures 1..8 in °C (+40 offset)
    if (reg >= 32 && reg <= 39) {
        int idx = reg - 32;
        if (idx == 0) return (uint16_t)(((int)g_bms.ntc1_temp_01k - 2731) / 10 + 40);
        if (idx == 1) return (uint16_t)(((int)g_bms.ntc2_temp_01k - 2731) / 10 + 40);
        if (idx == 2) return (uint16_t)(((int)g_bms.ntc3_temp_01k - 2731) / 10 + 40);
        return 0;
    }

    // Extremes and averages
    uint16_t maxV = 0, minV = 9999, sumV = 0;
    for (int i = 0; i < 8 && i < g_bms.cell_count; ++i) {
        if (g_bms.cell_mv[i] > maxV) maxV = g_bms.cell_mv[i];
        if (g_bms.cell_mv[i] < minV) minV = g_bms.cell_mv[i];
        sumV += g_bms.cell_mv[i];
    }
    uint16_t avgV = (g_bms.cell_count > 0) ? (sumV / g_bms.cell_count) : 3280;
    uint16_t deltaV = (maxV >= minV) ? (maxV - minV) : 0;
    int t1 = ((int)g_bms.ntc1_temp_01k - 2731) / 10 + 40;
    int t2 = ((int)g_bms.ntc2_temp_01k - 2731) / 10 + 40;
    int t3 = ((int)g_bms.ntc3_temp_01k - 2731) / 10 + 40;

    switch (reg) {
        // Telemetry & Status Block (0x0028..0x0043 / 40..67)
        case 40: return g_bms.pack_voltage_10mv / 10;                         // 0x0028: Total pack voltage in 0.1V
        case 41: {                                                            // 0x0029: Current in 0.1A (+30000)
            int32_t cur01a = g_bms.current_10ma / 10;
            int32_t val = 30000 + cur01a;
            if (val < 0) val = 0;
            if (val > 65535) val = 65535;
            return (uint16_t)val;
        }
        case 42: return (uint16_t)g_bms.soc_percent * 10;                     // 0x002A: SOC in 0.1%
        case 43: return maxV;                                                 // 0x002B: Max cell voltage (mV)
        case 44: return minV;                                                 // 0x002C: Min cell voltage (mV)
        case 45: return (uint16_t)max(t1, t2);                                // 0x002D: Max cell temp (+40)
        case 46: return (uint16_t)min(t1, t2);                                // 0x002E: Min cell temp (+40)
        case 47: {                                                            // 0x002F: Charge/discharge state (0=idle, 1=chg, 2=dsg)
            if (g_bms.current_10ma > 50) return 1;
            if (g_bms.current_10ma < -50) return 2;
            return 0;
        }
        case 48: return (uint16_t)(g_bms.remain_cap_10mah / 10);              // 0x0030: Remaining capacity (0.1Ah)
        case 49: return g_bms.cell_count;                                     // 0x0031: Cell count (8)
        case 50: return g_bms.ntc_count;                                      // 0x0032: Temp sensor count (3)
        case 51: return g_bms.cycle_count;                                    // 0x0033: Cycle count
        case 52: return (g_bms.balance_low != 0 || g_bms.balance_high != 0) ? 1 : 0; // 0x0034: Balancer status
        case 53: return (g_bms.fet_status & 0x01) ? 1 : 0;                    // 0x0035: Charging MOS status (0: off, 1: on)
        case 54: return (g_bms.fet_status & 0x02) ? 1 : 0;                    // 0x0036: Discharging MOS status (0: off, 1: on)
        case 55: return avgV;                                                 // 0x0037: Average cell voltage (mV)
        case 56: return deltaV;                                               // 0x0038: Delta cell voltage (mV)
        case 57: {                                                            // 0x0039: Power in Watts
            float packVf = g_bms.pack_voltage_10mv / 100.0f;
            float currAf = fabs(g_bms.current_10ma / 100.0f);
            return (uint16_t)(packVf * currAf);
        }
        case 58: return 0;                                                    // 0x003A: Alarm register 1
        case 59: return 0;                                                    // 0x003B: Alarm register 2
        case 60: return 0;                                                    // 0x003C: Alarm register 3
        case 61: return 0;                                                    // 0x003D: Alarm register 4
        case 62: return g_bms.balance_low;                                    // 0x003E: Cell balance bitmask 1-16
        case 63: return g_bms.balance_high;                                   // 0x003F: Cell balance bitmask 17-32
        case 64: return (g_bms.balance_low != 0) ? 31000 : 30000;             // 0x0040: Balance current (+30000)
        case 65: return 0;                                                    // 0x0041: Reserved
        case 66: return (uint16_t)t3;                                         // 0x0042: MOSFET temp (+40)
        case 67: return (uint16_t)t1;                                         // 0x0043: Board temp (+40)

        // Settings Block (0x0080..0x00A8 / 41 registers)
        case 0x0080: return (uint16_t)(g_bms.nominal_cap_10mah / 10);        // Rated capacity (0.1Ah -> 2000 = 200.0Ah)
        case 0x0081: return 3200;                                             // Cell ref voltage (3200 mV)
        case 0x0082: return 1;                                                // Boards (1)
        case 0x0083: return g_bms.cell_count;                                 // Cell count (8)
        case 0x0084: return 0;                                                // Board 2 cells (0)
        case 0x0085: return 0;                                                // Board 3 cells (0)
        case 0x0086: return g_bms.ntc_count;                                  // Temp sensors board 1 (3)
        case 0x0087: return 0;
        case 0x0088: return 0;
        case 0x0089: return 0;                                                // Battery type (0 = LiFePO4)
        case 0x008A: return 7200;                                             // Sleep wait time (7200s)
        case 0x008B: return 3600;                                             // Warning cell overvoltage (3600mV)
        case 0x008C: return 3650;                                             // Critical cell overvoltage (3650mV)
        case 0x008D: return 2600;                                             // Warning cell undervoltage (2600mV)
        case 0x008E: return 2500;                                             // Critical cell undervoltage (2500mV)
        case 0x008F: return 288;                                              // Warning total overvoltage (28.8V)
        case 0x0090: return 292;                                              // Critical total overvoltage (29.2V)
        case 0x0091: return 216;                                              // Warning total undervoltage (21.6V)
        case 0x0092: return 200;                                              // Critical total undervoltage (20.0V)
        case 0x0093: return 31000;                                            // Warning charge current (+100A, offset 30000)
        case 0x0094: return 31200;                                            // Critical charge current (+120A, offset 30000)
        case 0x0095: return 28000;                                            // Warning discharge current (-200A, offset 30000)
        case 0x0096: return 27800;                                            // Critical discharge current (-220A, offset 30000)
        case 0x0097: return 85;                                               // Warning charge temp high (45°C + 40)
        case 0x0098: return 90;                                               // Critical charge temp high (50°C + 40)
        case 0x0099: return 40;                                               // Warning charge temp low (0°C + 40)
        case 0x009A: return 35;                                               // Critical charge temp low (-5°C + 40)
        case 0x009B: return 105;                                              // Warning discharge temp high (65°C + 40)
        case 0x009C: return 110;                                              // Critical discharge temp high (70°C + 40)
        case 0x009D: return 25;                                               // Warning discharge temp low (-15°C + 40)
        case 0x009E: return 20;                                               // Critical discharge temp low (-20°C + 40)
        case 0x009F: return 50;                                               // Warning delta V (50mV)
        case 0x00A0: return 100;                                              // Critical delta V (100mV)
        case 0x00A1: return 10;                                               // Warning delta T (10°C)
        case 0x00A2: return 15;                                               // Critical delta T (15°C)
        case 0x00A3: return 3200;                                             // Balancing turn on voltage (3200mV)
        case 0x00A4: return 20;                                               // Equilibrium opening delta (20mV)
        case 0x00A5: return (g_bms.fet_status & 0x01) ? 1 : 0;                // Charging MOS switch (0: off, 1: on)
        case 0x00A6: return (g_bms.fet_status & 0x02) ? 1 : 0;                // Discharge MOS switch (0: off, 1: on)
        case 0x00A7: return (uint16_t)g_bms.soc_percent * 10;                 // SOC setting (0.1%)
        case 0x00A8: return 130;                                              // MOS temp protection alarm (90°C + 40)

        // Extended & Alias Control Registers
        case 0x00CF: return (g_bms.balance_low != 0 || g_bms.balance_high != 0) ? 1 : 0; // Balancer switch
        case 0x00D9: return (g_bms.fet_status & 0x02) ? 1 : 0;                // Legacy Discharge MOS
        case 0x00DA: return (g_bms.fet_status & 0x01) ? 1 : 0;                // Legacy Charge MOS

        default:
            return 0;
    }
}

// Version Frame (Registers 0x00A9..0x00C8: 32 regs software + 32 regs hardware = 64 bytes payload)
void sendDalyModbusVersionD2() {
    uint8_t frame[70];
    memset(frame, 0, sizeof(frame));

    frame[0] = 0xD2;
    frame[1] = 0x03;
    frame[2] = 0x40; // 64 bytes

    const char* sw = "401804015491";
    memcpy(&frame[3], sw, strlen(sw));

    const char* hw = "DALY-BMS-8S200A";
    memcpy(&frame[35], hw, strlen(hw));

    uint16_t crc = calculateModbusCRC(frame, 67);
    frame[67] = (uint8_t)(crc & 0xFF);
    frame[68] = (uint8_t)((crc >> 8) & 0xFF);

    sendDalyBleFrame(frame, 69);
    Serial.println("[DALY D2 TX -> Version (sw: 401804015491, hw: DALY-BMS-8S200A)] (69 bytes)");
}

// Password response (Register 0x00C9)
void sendDalyModbusPasswordD2() {
    uint8_t frame[11];
    frame[0] = 0xD2;
    frame[1] = 0x03;
    frame[2] = 0x06;
    const char* pwd = "123456";
    memcpy(&frame[3], pwd, 6);
    uint16_t crc = calculateModbusCRC(frame, 9);
    frame[9]  = (uint8_t)(crc & 0xFF);
    frame[10] = (uint8_t)((crc >> 8) & 0xFF);
    sendDalyBleFrame(frame, 11);
}

// Dynamic Modbus Holding Register Read Responder (Function 0x03)
void sendDalyModbusResponseD2(uint16_t addr, uint16_t count) {
    if (!g_deviceConnected) return;
    if (count == 0) count = 1;
    if (count > 90) count = 90;

    if (addr == 0x0000) {
        s_lastDalyReadCount = count;
    }

    // Special case: Version string (ASCII)
    if (addr == 0x00A9) {
        sendDalyModbusVersionD2();
        return;
    }
    // Special case: Password
    if (addr == 0x00C9) {
        sendDalyModbusPasswordD2();
        return;
    }

    uint8_t byteCount = (uint8_t)(count * 2);
    size_t totalLen = 3 + byteCount + 2;
    uint8_t frame[200];
    memset(frame, 0, sizeof(frame));

    frame[0] = 0xD2;
    frame[1] = 0x03;
    frame[2] = byteCount;

    for (uint16_t i = 0; i < count; ++i) {
        uint16_t val = getDalyHoldingRegister(addr + i);
        frame[3 + i * 2]     = (uint8_t)(val >> 8);
        frame[3 + i * 2 + 1] = (uint8_t)(val & 0xFF);
    }

    uint16_t crc = calculateModbusCRC(frame, 3 + byteCount);
    frame[3 + byteCount]     = (uint8_t)(crc & 0xFF);
    frame[3 + byteCount + 1] = (uint8_t)((crc >> 8) & 0xFF);

    sendDalyBleFrame(frame, totalLen);

    if (Serial) {
        Serial.printf("[DALY D2 TX -> Read Resp Addr 0x%04X, Count %u, Bytes %u] (%u total)\n",
                      addr, count, byteCount, (unsigned int)totalLen);
    }
}

// Backward-compatible wrappers
void sendDalyModbusStatusD2(bool is80Regs = false) {
    sendDalyModbusResponseD2(0x0000, is80Regs ? 80 : 62);
}

void sendDalyModbusSettingsD2() {
    sendDalyModbusResponseD2(0x0080, 41);
}

void sendDalyModbusBalancerD2(bool on) {
    sendDalyModbusResponseD2(0x00CF, 1);
}

// Modbus Function 0x06 Write Single Register Handler
void handleDalyModbusWrite(uint16_t addr, uint16_t val) {
    Serial.printf("[DALY D2 WRITE] Addr 0x%04X, Val 0x%04X\n", addr, val);

    if (addr == 0x00A5 || addr == 0x00DA) {
        // Charging MOS Switch Control
        bool chg = (val != 0);
        if (chg) {
            g_bms.fet_status |= 0x01;
            if (g_bms.mode == MODE_IDLE) g_bms.mode = MODE_CHARGE;
        } else {
            g_bms.fet_status &= ~0x01;
            if (g_bms.mode == MODE_CHARGE) g_bms.mode = MODE_IDLE;
        }
        updateStatusLed();
        Serial.printf("[DALY D2] Charging Switch set to: %s (fet_status: 0x%02X)\n",
                      chg ? "ON" : "OFF", g_bms.fet_status);
    } else if (addr == 0x00A6 || addr == 0x00D9) {
        // Discharging MOS Switch Control
        bool dsg = (val != 0);
        if (dsg) {
            g_bms.fet_status |= 0x02;
            if (g_bms.mode == MODE_IDLE) g_bms.mode = MODE_DISCHARGE;
        } else {
            g_bms.fet_status &= ~0x02;
            if (g_bms.mode == MODE_DISCHARGE) g_bms.mode = MODE_IDLE;
        }
        updateStatusLed();
        Serial.printf("[DALY D2] Discharging Switch set to: %s (fet_status: 0x%02X)\n",
                      dsg ? "ON" : "OFF", g_bms.fet_status);
    } else if (addr == 0x00A7 || addr == 0x00CF) {
        // Active Balancer Control
        bool bal = (val != 0);
        g_bms.balance_low = bal ? 0x000F : 0x0000;
        Serial.printf("[DALY D2] Active Balancer set to: %s\n", bal ? "ON" : "OFF");
    } else if (addr == 0x0080) {
        // Rated capacity
        if (val > 0) {
            g_bms.nominal_cap_10mah = (uint32_t)val * 10;
            g_bms.cycle_cap_10mah = (g_bms.nominal_cap_10mah * 8) / 10;
            Serial.printf("[DALY D2] Rated capacity updated: %.1f Ah\n", val / 10.0f);
        }
    } else if (addr == 0x0083) {
        // Cell count
        if (val >= 3 && val <= 32) {
            g_bms.cell_count = (uint8_t)val;
            Serial.printf("[DALY D2] Cell count updated: %uS\n", val);
        }
    }

    // Standard Modbus 0x06 response: Echo back the 8-byte request frame with valid CRC
    uint8_t resp[8];
    resp[0] = 0xD2;
    resp[1] = 0x06;
    resp[2] = (uint8_t)(addr >> 8);
    resp[3] = (uint8_t)(addr & 0xFF);
    resp[4] = (uint8_t)(val >> 8);
    resp[5] = (uint8_t)(val & 0xFF);
    uint16_t crc = calculateModbusCRC(resp, 6);
    resp[6] = (uint8_t)(crc & 0xFF);
    resp[7] = (uint8_t)((crc >> 8) & 0xFF);

    sendDalyBleFrame(resp, 8);
}

// ============================================================================
// Daly Protocol 81 Implementations
// ============================================================================
void sendDalyP81Cells() {
    uint8_t frame[140];
    memset(frame, 0, sizeof(frame));

    frame[0] = 0x81;
    frame[1] = 0x03;
    frame[2] = 0x80; // 128 bytes (64 registers)

    auto setReg = [&](size_t regIdx, uint16_t val) {
        frame[3 + regIdx * 2]     = (uint8_t)(val >> 8);
        frame[3 + regIdx * 2 + 1] = (uint8_t)(val & 0xFF);
    };

    for (int i = 0; i < 8; ++i) {
        setReg(i, g_bms.cell_mv[i]);
    }

    int t1 = (int)((g_bms.ntc1_temp_01k - 2731) / 10) + 40;
    int t2 = (int)((g_bms.ntc2_temp_01k - 2731) / 10) + 40;
    int t3 = (int)((g_bms.ntc3_temp_01k - 2731) / 10) + 40;
    setReg(48, t1);
    setReg(49, t2);
    setReg(50, t3);

    setReg(56, g_bms.pack_voltage_10mv / 10);
    int32_t cur01a = g_bms.current_10ma / 10;
    setReg(57, (uint16_t)(30000 + cur01a));
    setReg(58, (uint16_t)g_bms.soc_percent * 10);

    float packVf = g_bms.pack_voltage_10mv / 100.0f;
    float currAf = fabs(g_bms.current_10ma / 100.0f);
    setReg(59, (uint16_t)(packVf * currAf));
    setReg(60, g_bms.cell_count);
    setReg(61, g_bms.ntc_count);
    setReg(62, g_bms.cycle_count);

    uint16_t crc = calculateModbusCRC(frame, 131);
    frame[131] = (uint8_t)(crc & 0xFF);
    frame[132] = (uint8_t)((crc >> 8) & 0xFF);

    sendDalyBleFrame(frame, 133);
}

void sendDalyP81Status() {
    uint8_t frame[140];
    memset(frame, 0, sizeof(frame));

    frame[0] = 0x81;
    frame[1] = 0x03;
    frame[2] = 0x7C; // 124 bytes (62 registers)

    auto setOffsetReg = [&](uint8_t reg, uint16_t val) {
        size_t idx = (reg - 0x41);
        frame[3 + idx * 2]     = (uint8_t)(val >> 8);
        frame[3 + idx * 2 + 1] = (uint8_t)(val & 0xFF);
    };

    int t1 = (int)((g_bms.ntc1_temp_01k - 2731) / 10) + 40;
    int t2 = (int)((g_bms.ntc2_temp_01k - 2731) / 10) + 40;

    setOffsetReg(0x43, t1);
    setOffsetReg(0x44, 1);
    setOffsetReg(0x45, t2);
    setOffsetReg(0x46, 2);

    uint16_t st = (g_bms.current_10ma > 50) ? 1 : ((g_bms.current_10ma < -50) ? 2 : 0);
    setOffsetReg(0x48, st);
    setOffsetReg(0x4B, (uint16_t)(g_bms.remain_cap_10mah / 10));
    setOffsetReg(0x4C, g_bms.cycle_count);
    setOffsetReg(0x4D, (g_bms.balance_low != 0 || g_bms.balance_high != 0) ? 1 : 0);
    setOffsetReg(0x4E, 30000);

    uint16_t crc = calculateModbusCRC(frame, 127);
    frame[127] = (uint8_t)(crc & 0xFF);
    frame[128] = (uint8_t)((crc >> 8) & 0xFF);

    sendDalyBleFrame(frame, 129);
}

// ============================================================================
// Legacy Daly Protocol (0xA5) Responses (For Universal Diagnostic Monitor)
// ============================================================================
void sendDalyResponse(uint8_t cmd, const uint8_t* data8) {
    if (!g_deviceConnected) return;

    uint8_t frame[13];
    frame[0] = 0xA5;
    frame[1] = 0x01; // From BMS (Address 0x01)
    frame[2] = cmd;
    frame[3] = 0x08; // 8 bytes data
    if (data8) {
        memcpy(&frame[4], data8, 8);
    } else {
        memset(&frame[4], 0, 8);
    }
    uint8_t sum = 0;
    for (int i = 0; i < 12; ++i) sum += frame[i];
    frame[12] = sum;

    sendDalyBleFrame(frame, 13);

    if (Serial) {
        Serial.printf("[DALY A5 TX -> Cmd 0x%02X] (13 bytes)\n", cmd);
    }
}

void sendDalyTelemetry90() {
    uint8_t data[8];
    uint16_t v01 = g_bms.pack_voltage_10mv / 10; // 0.1V units
    data[0] = (uint8_t)(v01 >> 8);
    data[1] = (uint8_t)(v01 & 0xFF);
    data[2] = data[0]; // Acquisition voltage
    data[3] = data[1];

    // Current in 0.1A units with offset 30000
    int32_t cur01a = g_bms.current_10ma / 10;
    int32_t val30000 = 30000 + cur01a;
    if (val30000 < 0) val30000 = 0;
    if (val30000 > 65535) val30000 = 65535;
    data[4] = (uint8_t)((uint16_t)val30000 >> 8);
    data[5] = (uint8_t)((uint16_t)val30000 & 0xFF);

    // SoC in 0.1% units
    uint16_t socDaly = (uint16_t)g_bms.soc_percent * 10;
    data[6] = (uint8_t)(socDaly >> 8);
    data[7] = (uint8_t)(socDaly & 0xFF);

    sendDalyResponse(0x90, data);
}

void sendDalyExtremes91() {
    uint8_t data[8];
    uint16_t maxV = 0, minV = 9999;
    uint8_t maxIdx = 1, minIdx = 1;
    for (uint8_t i = 0; i < g_bms.cell_count && i < 8; ++i) {
        if (g_bms.cell_mv[i] > maxV) { maxV = g_bms.cell_mv[i]; maxIdx = i + 1; }
        if (g_bms.cell_mv[i] < minV) { minV = g_bms.cell_mv[i]; minIdx = i + 1; }
    }
    data[0] = (uint8_t)(maxV >> 8);
    data[1] = (uint8_t)(maxV & 0xFF);
    data[2] = maxIdx;
    data[3] = (uint8_t)(minV >> 8);
    data[4] = (uint8_t)(minV & 0xFF);
    data[5] = minIdx;
    data[6] = 0x00;
    data[7] = 0x00;
    sendDalyResponse(0x91, data);
}

void sendDalyTemps92() {
    uint8_t data[8];
    int8_t t1 = (int8_t)(((int)g_bms.ntc1_temp_01k - 2731) / 10);
    int8_t t2 = (int8_t)(((int)g_bms.ntc2_temp_01k - 2731) / 10);
    data[0] = (uint8_t)(t1 + 40);
    data[1] = 1;
    data[2] = (uint8_t)(t2 + 40);
    data[3] = 2;
    data[4] = 0; data[5] = 0; data[6] = 0; data[7] = 0;
    sendDalyResponse(0x92, data);
}

void sendDalyMosStatus93() {
    uint8_t data[8];
    if (g_bms.current_10ma > 50) data[0] = 1;      // Charge
    else if (g_bms.current_10ma < -50) data[0] = 2;// Discharge
    else data[0] = 0;                             // Stationary

    data[1] = (g_bms.fet_status & 0x01) ? 1 : 0;
    data[2] = (g_bms.fet_status & 0x02) ? 1 : 0;
    static uint8_t s_bmsLife = 0;
    data[3] = s_bmsLife++;
    uint32_t remMah = (uint32_t)g_bms.remain_cap_10mah * 10;
    data[4] = (uint8_t)(remMah >> 24);
    data[5] = (uint8_t)(remMah >> 16);
    data[6] = (uint8_t)(remMah >> 8);
    data[7] = (uint8_t)(remMah & 0xFF);
    sendDalyResponse(0x93, data);
}

void sendDalyStatus94() {
    uint8_t data[8];
    data[0] = g_bms.cell_count;
    data[1] = g_bms.ntc_count;
    data[2] = (g_bms.mode == MODE_CHARGE) ? 1 : 0;
    data[3] = (g_bms.mode == MODE_DISCHARGE) ? 1 : 0;
    data[4] = 0;
    data[5] = (uint8_t)(g_bms.cycle_count >> 8);
    data[6] = (uint8_t)(g_bms.cycle_count & 0xFF);
    data[7] = 0;
    sendDalyResponse(0x94, data);
}

void sendDalyCells95() {
    // 8S cells -> 3 frames (3 cells per frame)
    uint8_t f1[8] = {
        1,
        (uint8_t)(g_bms.cell_mv[0] >> 8), (uint8_t)(g_bms.cell_mv[0] & 0xFF),
        (uint8_t)(g_bms.cell_mv[1] >> 8), (uint8_t)(g_bms.cell_mv[1] & 0xFF),
        (uint8_t)(g_bms.cell_mv[2] >> 8), (uint8_t)(g_bms.cell_mv[2] & 0xFF),
        0
    };
    sendDalyResponse(0x95, f1);
    delay(40);

    uint8_t f2[8] = {
        2,
        (uint8_t)(g_bms.cell_mv[3] >> 8), (uint8_t)(g_bms.cell_mv[3] & 0xFF),
        (uint8_t)(g_bms.cell_mv[4] >> 8), (uint8_t)(g_bms.cell_mv[4] & 0xFF),
        (uint8_t)(g_bms.cell_mv[5] >> 8), (uint8_t)(g_bms.cell_mv[5] & 0xFF),
        0
    };
    sendDalyResponse(0x95, f2);
    delay(40);

    uint8_t f3[8] = {
        3,
        (uint8_t)(g_bms.cell_mv[6] >> 8), (uint8_t)(g_bms.cell_mv[6] & 0xFF),
        (uint8_t)(g_bms.cell_mv[7] >> 8), (uint8_t)(g_bms.cell_mv[7] & 0xFF),
        0, 0,
        0
    };
    sendDalyResponse(0x95, f3);
}

void sendDalySensors96() {
    int8_t t1 = (int8_t)(((int)g_bms.ntc1_temp_01k - 2731) / 10);
    int8_t t2 = (int8_t)(((int)g_bms.ntc2_temp_01k - 2731) / 10);
    int8_t t3 = (int8_t)(((int)g_bms.ntc3_temp_01k - 2731) / 10);
    uint8_t data[8] = {
        1,
        (uint8_t)(t1 + 40),
        (uint8_t)(t2 + 40),
        (uint8_t)(t3 + 40),
        0, 0, 0, 0
    };
    sendDalyResponse(0x96, data);
}

void sendDalyBalancing97() {
    uint8_t data[8] = {
        (uint8_t)(g_bms.balance_high >> 8),
        (uint8_t)(g_bms.balance_high & 0xFF),
        (uint8_t)(g_bms.balance_low >> 8),
        (uint8_t)(g_bms.balance_low & 0xFF),
        0, 0, 0, 0
    };
    sendDalyResponse(0x97, data);
}

void sendDalyFaults98() {
    uint8_t data[8] = { 0 };
    sendDalyResponse(0x98, data);
}

void sendAllDalyTelemetry() {
    sendDalyTelemetry90();
    delay(20);
    sendDalyExtremes91();
    delay(20);
    sendDalyTemps92();
    delay(20);
    sendDalyMosStatus93();
    delay(20);
    sendDalyStatus94();
    delay(20);
    sendDalyCells95();
    delay(20);
    sendDalySensors96();
    delay(20);
    sendDalyBalancing97();
    delay(20);
    sendDalyFaults98();
}

class DalyWriteCallbacks : public NimBLECharacteristicCallbacks {
    void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo, uint16_t subValue) override {
        Serial.printf("[DALY BLE] Client subscribed to %s (subValue: %u)\n", 
                      pCharacteristic->getUUID().toString().c_str(), subValue);
        if (subValue > 0) {
            s_lastDalyTargetNotifyChar = pCharacteristic;
        }
    }

    void onRead(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        Serial.printf("[DALY BLE] Client READ on %s\n", pCharacteristic->getUUID().toString().c_str());
        sendDalyModbusResponseD2(0x0000, 62);
    }

    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        std::string rxVal = pCharacteristic->getValue();
        if (rxVal.empty()) return;

        const uint8_t* buf = (const uint8_t*)rxVal.data();
        size_t len = rxVal.length();

        // Update target notify characteristic based on incoming write characteristic
        if (pCharacteristic == pDalyFff2 && pDalyNotifyFff1) {
            s_lastDalyTargetNotifyChar = pDalyNotifyFff1;
        } else if (pCharacteristic == pDalyWriteChar && pDalyNotifyChar) {
            s_lastDalyTargetNotifyChar = pDalyNotifyChar;
        } else if (pCharacteristic == pDalyFf02 && pDalyNotifyFf01) {
            s_lastDalyTargetNotifyChar = pDalyNotifyFf01;
        } else if (pCharacteristic == pDalyNusRx && pDalyNusTx) {
            s_lastDalyTargetNotifyChar = pDalyNusTx;
        }

        Serial.printf("[DALY RX <- Client on %s] (%u bytes): ", 
                      pCharacteristic->getUUID().toString().c_str(), (unsigned int)len);
        for (size_t i = 0; i < len; ++i) Serial.printf("%02X ", buf[i]);
        Serial.println();

        // 1. Modbus Protocol 0xD2 handler (Smart BMS App)
        if (len >= 6 && buf[0] == 0xD2) {
            uint8_t func = buf[1];
            uint16_t addr = (uint16_t(buf[2]) << 8) | buf[3];
            uint16_t val  = (uint16_t(buf[4]) << 8) | buf[5];

            if (func == 0x03) {
                // Read Holding Registers (dynamic slice responder)
                Serial.printf("[DALY D2 READ] Addr 0x%04X, Count %u\n", addr, val);
                sendDalyModbusResponseD2(addr, val);
                return;
            } else if (func == 0x06) {
                // Write Single Register
                handleDalyModbusWrite(addr, val);
                return;
            }
        }

        // 2. Protocol 0x81 handler
        if (len >= 6 && buf[0] == 0x81) {
            uint8_t func = buf[1];
            uint16_t addr = (uint16_t(buf[2]) << 8) | buf[3];
            uint16_t val  = (uint16_t(buf[4]) << 8) | buf[5];

            Serial.printf("[DALY P81 READ] Func 0x%02X, Addr 0x%04X, Val %u\n", func, addr, val);
            if (addr == 0x0000) {
                sendDalyP81Cells();
            } else if (addr == 0x0041) {
                sendDalyP81Status();
            } else if (addr == 0x0178) {
                sendDalyModbusVersionD2();
            } else {
                sendDalyP81Cells();
            }
            return;
        }

        // 3. Legacy Protocol 0xA5 handler (Universal Monitor)
        for (size_t i = 0; i + 13 <= len; ++i) {
            if (buf[i] == 0xA5) {
                buf = &buf[i];
                len = 13;
                break;
            }
        }

        if (len >= 13 && buf[0] == 0xA5) {
            uint8_t cmd = buf[2];
            uint8_t expectedCrc = buf[12];
            uint8_t sum = 0;
            for (size_t i = 0; i < 12; ++i) sum += buf[i];
            if (sum != expectedCrc) {
                Serial.printf("[DALY CRC] Invalid CRC: calc=0x%02X, expected=0x%02X\n", sum, expectedCrc);
                return;
            }

            switch (cmd) {
                case 0x90: sendDalyTelemetry90(); break;
                case 0x91: sendDalyExtremes91(); break;
                case 0x92: sendDalyTemps92(); break;
                case 0x93: sendDalyMosStatus93(); break;
                case 0x94: sendDalyStatus94(); break;
                case 0x95: sendDalyCells95(); break;
                case 0x96: sendDalySensors96(); break;
                case 0x97: sendDalyBalancing97(); break;
                case 0x98: sendDalyFaults98(); break;

                case 0xD9: { // Discharge MOS control
                    uint8_t state = buf[4];
                    if (state == 0x01) {
                        g_bms.fet_status |= 0x02;
                        Serial.println("[DALY] Discharge MOS ENABLED");
                    } else {
                        g_bms.fet_status &= ~0x02;
                        Serial.println("[DALY] Discharge MOS DISABLED");
                    }
                    updateStatusLed();
                    uint8_t ack[8] = { state, 0, 0, 0, 0, 0, 0, 0 };
                    sendDalyResponse(0xD9, ack);
                    break;
                }

                case 0xDA: { // Charge MOS control
                    uint8_t state = buf[4];
                    if (state == 0x01) {
                        g_bms.fet_status |= 0x01;
                        Serial.println("[DALY] Charge MOS ENABLED");
                    } else {
                        g_bms.fet_status &= ~0x01;
                        Serial.println("[DALY] Charge MOS DISABLED");
                    }
                    updateStatusLed();
                    uint8_t ack[8] = { state, 0, 0, 0, 0, 0, 0, 0 };
                    sendDalyResponse(0xDA, ack);
                    break;
                }

                case 0x21:
                case 0x51: { // Set rated capacity (mAh)
                    uint32_t mah = ((uint32_t)buf[4] << 24) | ((uint32_t)buf[5] << 16) | ((uint32_t)buf[6] << 8) | buf[7];
                    if (mah > 0) {
                        g_bms.nominal_cap_10mah = mah / 10;
                        g_bms.cycle_cap_10mah = (g_bms.nominal_cap_10mah * 8) / 10;
                        Serial.printf("[DALY] Nominal Capacity updated: %.1f Ah\n", g_bms.nominal_cap_10mah / 100.0f);
                    }
                    sendDalyResponse(cmd, &buf[4]);
                    break;
                }

                case 0x50:
                case 0xD6: { // Set cell count
                    uint8_t cnt = buf[4];
                    if (cnt >= 3 && cnt <= 32) {
                        g_bms.cell_count = cnt;
                        Serial.printf("[DALY] Cell count updated: %uS\n", cnt);
                    }
                    sendDalyResponse(cmd, &buf[4]);
                    break;
                }

                default: {
                    Serial.printf("[DALY] Unhandled command: 0x%02X\n", cmd);
                    uint8_t emptyAck[8] = { 0 };
                    sendDalyResponse(cmd, emptyAck);
                    break;
                }
            }
            return;
        }

        // Generic fallback
        Serial.println("[DALY] Unrecognized frame -> sending status frame as fallback");
        sendDalyModbusStatusD2(false);
    }
};

// ============================================================================
// Setup
// ============================================================================
void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // Non-blocking USB CDC output - never block BLE thread
    delay(500);

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    initDoubleResetAndMode();

    // Flash LED 3 times on boot to indicate selected mode
    // JBD: 3 Red flashes
    // Daly: 3 Magenta flashes
    for (int i = 0; i < 3; ++i) {
        if (g_proto == PROTO_DALY) {
            setRgbLed(60, 0, 60); // Magenta
        } else {
            setRgbLed(60, 0, 0);  // Red
        }
        delay(120);
        setRgbLed(0, 0, 0);
        delay(100);
    }

    Serial.println("\n=======================================================");
    if (g_proto == PROTO_DALY) {
        Serial.println("     ESP32-C6 DALY Smart BMS BLE Emulator             ");
        Serial.printf("Device Name: %s\n", DALY_DEVICE_NAME);
    } else {
        Serial.println("   ESP32-C6 JBD BMS BLE Emulator (DB24SF01 / 8S200A)   ");
        Serial.printf("Device Name: %s\n", BLE_DEVICE_NAME);
        Serial.printf("Security PIN: %s (Passkey: 123456)\n", JBD_PIN_CODE);
    }
    Serial.println("=======================================================");
    const uint8_t* activeMac = (g_proto == PROTO_DALY) ? DALY_BT_MAC : TARGET_BT_MAC;
    Serial.printf("Target MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  activeMac[0], activeMac[1], activeMac[2],
                  activeMac[3], activeMac[4], activeMac[5]);
    Serial.printf("Pack Voltage: %.2fV | SOC: %u%% | Cells: %uS\n", 
                  g_bms.pack_voltage_10mv / 100.0f, g_bms.soc_percent, g_bms.cell_count);
    Serial.printf("MOSFET Status: 0x%02X (Charge: %s, Discharge: %s)\n",
                  g_bms.fet_status,
                  (g_bms.fet_status & 0x01) ? "ON" : "OFF",
                  (g_bms.fet_status & 0x02) ? "ON" : "OFF");
    Serial.println("=======================================================\n");

    // Configure Custom Bluetooth MAC Address
    uint8_t custom_bt_mac[6];
    memcpy(custom_bt_mac, activeMac, 6);
    esp_iface_mac_addr_set(custom_bt_mac, ESP_MAC_BT);

    if (g_proto == PROTO_DALY) {
        // Initialize NimBLE Device for Daly
        NimBLEDevice::init(DALY_DEVICE_NAME);
        NimBLEDevice::setMTU(512);
        NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum TX power (+9dBm)
        NimBLEDevice::setSecurityAuth(false, false, false);

        Serial.printf("[BLE] Hardware BLE MAC Address: %s\n", NimBLEDevice::getAddress().toString().c_str());

        // Create Server
        pServer = NimBLEDevice::createServer();
        pServer->setCallbacks(new ServerCallbacks());

        // Primary Daly Service 0xFFE0
        NimBLEService* pDalyService = pServer->createService(NimBLEUUID(DALY_SERVICE_UUID));
        pDalyNotifyChar = pDalyService->createCharacteristic(
            NimBLEUUID(DALY_NOTIFY_CHAR_UUID),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
        );
        pDalyNotifyChar->setCallbacks(new DalyWriteCallbacks());

        pDalyWriteChar = pDalyService->createCharacteristic(
            NimBLEUUID(DALY_WRITE_CHAR_UUID),
            NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        pDalyWriteChar->setCallbacks(new DalyWriteCallbacks());

        // Secondary Daly Service 0xFFF0 (standard for official Daly Bluetooth modules)
        NimBLEService* pDalyFff0 = pServer->createService(NimBLEUUID((uint16_t)0xFFF0));
        pDalyNotifyFff1 = pDalyFff0->createCharacteristic(
            NimBLEUUID((uint16_t)0xFFF1),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
        );
        pDalyNotifyFff1->setCallbacks(new DalyWriteCallbacks());

        pDalyFff2 = pDalyFff0->createCharacteristic(
            NimBLEUUID((uint16_t)0xFFF2),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        pDalyFff2->setCallbacks(new DalyWriteCallbacks());

        pDalyFff3 = pDalyFff0->createCharacteristic(
            NimBLEUUID((uint16_t)0xFFF3),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        pDalyFff3->setCallbacks(new DalyWriteCallbacks());

        pDalyNotifyFff4 = pDalyFff0->createCharacteristic(
            NimBLEUUID((uint16_t)0xFFF4),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
        );
        pDalyNotifyFff4->setCallbacks(new DalyWriteCallbacks());

        // Service 0xFF00 (for apps expecting classic 0xFF00 profile)
        NimBLEService* pDalyFf00 = pServer->createService(NimBLEUUID((uint16_t)0xFF00));
        pDalyNotifyFf01 = pDalyFf00->createCharacteristic(
            NimBLEUUID((uint16_t)0xFF01),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR |
            NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
        );
        pDalyNotifyFf01->setCallbacks(new DalyWriteCallbacks());

        pDalyFf02 = pDalyFf00->createCharacteristic(
            NimBLEUUID((uint16_t)0xFF02),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        pDalyFf02->setCallbacks(new DalyWriteCallbacks());

        // Nordic UART Service (6E400001-B5A3-F393-E0A9-E50E24DCCA9E)
        NimBLEService* pNus = pServer->createService(NimBLEUUID("6E400001-B5A3-F393-E0A9-E50E24DCCA9E"));
        pDalyNusTx = pNus->createCharacteristic(
            NimBLEUUID("6E400003-B5A3-F393-E0A9-E50E24DCCA9E"),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY | NIMBLE_PROPERTY::INDICATE
        );
        pDalyNusTx->setCallbacks(new DalyWriteCallbacks());

        pDalyNusRx = pNus->createCharacteristic(
            NimBLEUUID("6E400002-B5A3-F393-E0A9-E50E24DCCA9E"),
            NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
        );
        pDalyNusRx->setCallbacks(new DalyWriteCallbacks());

        // Device Information Service 0x180A (Essential for iOS Daly Smart BMS App)
        NimBLEService* pDevInfo = pServer->createService(NimBLEUUID((uint16_t)0x180A));
        NimBLECharacteristic* pModelChar = pDevInfo->createCharacteristic(
            NimBLEUUID((uint16_t)0x2A24), NIMBLE_PROPERTY::READ
        );
        pModelChar->setValue("DALY-BMS");

        NimBLECharacteristic* pMfgChar = pDevInfo->createCharacteristic(
            NimBLEUUID((uint16_t)0x2A29), NIMBLE_PROPERTY::READ
        );
        pMfgChar->setValue("DALY");

        NimBLECharacteristic* pSerialChar = pDevInfo->createCharacteristic(
            NimBLEUUID((uint16_t)0x2A25), NIMBLE_PROPERTY::READ
        );
        pSerialChar->setValue("401804015491");

        NimBLECharacteristic* pFwChar = pDevInfo->createCharacteristic(
            NimBLEUUID((uint16_t)0x2A26), NIMBLE_PROPERTY::READ
        );
        pFwChar->setValue("V1.0");

        NimBLECharacteristic* pHwChar = pDevInfo->createCharacteristic(
            NimBLEUUID((uint16_t)0x2A27), NIMBLE_PROPERTY::READ
        );
        pHwChar->setValue("V1.0");

        pServer->start();

        // Configure Advertising for Daly (0xFFF0 primary + 0xFFE0 scan response)
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
        NimBLEAdvertisementData advData;
        advData.setFlags(0x06);
        advData.setName(DALY_DEVICE_NAME);
        advData.setCompleteServices(NimBLEUUID((uint16_t)0xFFF0));

        NimBLEAdvertisementData scanData;
        scanData.setCompleteServices(NimBLEUUID(DALY_SERVICE_UUID));
        scanData.setManufacturerData(std::string((char*)DALY_BT_MAC, 6));

        pAdvertising->setAdvertisementData(advData);
        pAdvertising->setScanResponseData(scanData);
        pAdvertising->setMinInterval(32);
        pAdvertising->setMaxInterval(64);
        pAdvertising->start();

        updateStatusLed();
        Serial.printf("[BLE] Advertising started successfully as '%s'.\n", DALY_DEVICE_NAME);
        Serial.println("[BLE] Ready for connection from Daly BMS App or ESP32 Universal Monitor...");
    } else {
        // Initialize NimBLE Device for JBD
        NimBLEDevice::init(BLE_DEVICE_NAME);
        NimBLEDevice::setMTU(512);
        NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Maximum TX power (+9dBm)
        NimBLEDevice::setSecurityAuth(false, false, false);

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

        // Configure Advertising for iOS CoreBluetooth (Strict <= 31 bytes per packet)
        NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();

        // Primary Advertisement Packet (25 bytes <= 31 bytes): Flags + Complete Name + 16-bit Service UUID
        NimBLEAdvertisementData advData;
        advData.setFlags(0x06); // General Discoverable + BR_EDR_NOT_SUPPORTED
        advData.setName(BLE_DEVICE_NAME);
        advData.setCompleteServices(NimBLEUUID((uint16_t)0xFF00));

        // Scan Response Packet (24 bytes <= 31 bytes): 128-bit UUID + pure 6-byte MAC in Manufacturer Data
        NimBLEAdvertisementData scanData;
        scanData.setCompleteServices(NimBLEUUID("0000FF00-0000-1000-8000-00805F9B34FB"));
        uint8_t mfgBytes[6] = {0xA5, 0xC2, 0x3A, 0x26, 0xF2, 0xC2};
        scanData.setManufacturerData(std::string((char*)mfgBytes, 6));

        pAdvertising->setAdvertisementData(advData);
        pAdvertising->setScanResponseData(scanData);
        pAdvertising->setMinInterval(32); // 20ms advertising interval for instant discovery
        pAdvertising->setMaxInterval(64); // 40ms
        pAdvertising->start();

        // Initial state: Red LED indicates Advertising / Waiting for client
        updateStatusLed();

        Serial.println("[BLE] Advertising started successfully as 'BS-26A-072-005' with PIN 123456.");
        Serial.println("[BLE] Ready for connection from XiaoxiangBMS, JBD BMS, or ESP32 Universal Monitor...");
    }
}

// ============================================================================
// Main Loop (Physics Simulation + Telemetry + Interactive Control)
// ============================================================================
void loop() {
    unsigned long now = millis();

    // Expire double reset window after 2.5 seconds of stable running
    if (g_doubleResetPending && (now - g_bootTime > 2500)) {
        g_doubleResetPending = false;
        s_prefs.putBool("dr_flag", false);
    }

    // Hardware Button (GPIO 9 BOOT Button): Long press (> 1.5s) toggles BMS mode
    static unsigned long s_btnPressStart = 0;
    static bool s_btnWasPressed = false;
    bool btnIsPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    if (btnIsPressed && !s_btnWasPressed) {
        s_btnWasPressed = true;
        s_btnPressStart = now;
    } else if (btnIsPressed && s_btnWasPressed) {
        if (now - s_btnPressStart > 1500) {
            Serial.println("\n[BUTTON] Long press detected on BOOT button (GPIO 9)! Toggling mode...");
            toggleEmulatorMode();
        }
    } else if (!btnIsPressed && s_btnWasPressed) {
        s_btnWasPressed = false;
    }

    // High frequency physics & telemetry simulation (every 200ms)
    if (now - g_lastSimTime >= 200) {
        float dt_sec = (now - g_lastSimTime) / 1000.0f;
        g_lastSimTime = now;

        static int stepCount = 0;
        stepCount++;

        // Realistic sinusoidal inverter load ripple + small ADC noise
        float t = stepCount * 0.2f;
        float wave1 = sinf(t * 0.8f);
        float wave2 = cosf(t * 1.3f);
        float currentJitter = (wave1 * 0.35f) + (wave2 * 0.15f); // +/- 0.50A dynamic ripple

        if (g_bms.mode == MODE_DISCHARGE && (g_bms.fet_status & 0x02)) {
            // Active Discharge (-17A nominal with realistic dynamic load curve)
            float currentA = -17.0f + currentJitter;
            g_bms.current_10ma = (int16_t)(currentA * 100.0f);

            // Integrate capacity: delta_Ah = I * dt
            float delta_mah = (fabs(currentA) * 1000.0f) * (dt_sec / 3600.0f);
            g_bms.fractional_mah += delta_mah;
            while (g_bms.fractional_mah >= 10.0f) {
                if (g_bms.remain_cap_10mah > 0) g_bms.remain_cap_10mah--;
                g_bms.fractional_mah -= 10.0f;
            }

            // Cell voltages sag under 17A load (~3.28V) with realistic cell delta & live breathing (+/- 4..7 mV)
            int d1 = (int)(wave1 * 4.0f);
            int d2 = (int)(wave2 * 3.5f);
            int d3 = (int)(sinf(t * 0.5f) * 3.0f);

            g_bms.cell_mv[0] = 3280 + d1;
            g_bms.cell_mv[1] = 3284 - d2;
            g_bms.cell_mv[2] = 3279 + d2;
            g_bms.cell_mv[3] = 3283 - d1;
            g_bms.cell_mv[4] = 3278 + d3;
            g_bms.cell_mv[5] = 3285 - d3;
            g_bms.cell_mv[6] = 3281 + d1 - d2;
            g_bms.cell_mv[7] = 3282 + d2 - d3;

            g_bms.ntc1_temp_01k = 2991 + (int)(wave1 * 2.0f); // ~26.0 °C
            g_bms.ntc2_temp_01k = 2986 + (int)(wave2 * 2.0f); // ~25.5 °C
            g_bms.ntc3_temp_01k = 3011 + (int)(wave1 * 3.0f); // ~28.0 °C (MOSFET heats up under 17A discharge)

            uint32_t totalMv = 0;
            for (int i = 0; i < 8; ++i) totalMv += g_bms.cell_mv[i];
            g_bms.pack_voltage_10mv = totalMv / 10;
        } else if (g_bms.mode == MODE_CHARGE && (g_bms.fet_status & 0x01)) {
            // Active Charge (+25A nominal with dynamic charger ripple)
            float currentA = 25.0f + currentJitter;
            g_bms.current_10ma = (int16_t)(currentA * 100.0f);

            float delta_mah = (currentA * 1000.0f) * (dt_sec / 3600.0f);
            g_bms.fractional_mah += delta_mah;
            while (g_bms.fractional_mah >= 10.0f) {
                if (g_bms.remain_cap_10mah < g_bms.nominal_cap_10mah) g_bms.remain_cap_10mah++;
                g_bms.fractional_mah -= 10.0f;
            }

            // Cell voltages rise during charging (~3.45V) with dynamic ripples (+/- 4..6 mV)
            int d1 = (int)(wave1 * 4.0f);
            int d2 = (int)(wave2 * 3.5f);
            int d3 = (int)(sinf(t * 0.5f) * 3.0f);

            g_bms.cell_mv[0] = 3450 + d1;
            g_bms.cell_mv[1] = 3453 - d2;
            g_bms.cell_mv[2] = 3448 + d2;
            g_bms.cell_mv[3] = 3452 - d1;
            g_bms.cell_mv[4] = 3449 + d3;
            g_bms.cell_mv[5] = 3454 - d3;
            g_bms.cell_mv[6] = 3450 + d1 - d2;
            g_bms.cell_mv[7] = 3451 + d2 - d3;

            g_bms.ntc1_temp_01k = 2986 + (int)(wave1 * 2.0f); // 25.5 °C
            g_bms.ntc2_temp_01k = 2981 + (int)(wave2 * 2.0f); // 25.0 °C
            g_bms.ntc3_temp_01k = 3031 + (int)(wave2 * 3.0f); // ~30.0 °C (MOSFET heats up under 25A charge)

            uint32_t totalMv = 0;
            for (int i = 0; i < 8; ++i) totalMv += g_bms.cell_mv[i];
            g_bms.pack_voltage_10mv = totalMv / 10;
        } else {
            // Idle Mode (0.00 A) - realistic resting LiFePO4 cells with micro-drift
            g_bms.current_10ma = 0;
            int n = (int)(wave1 * 2.0f);
            g_bms.cell_mv[0] = 3320 + n;
            g_bms.cell_mv[1] = 3323 - n;
            g_bms.cell_mv[2] = 3319 + n;
            g_bms.cell_mv[3] = 3322 - n;
            g_bms.cell_mv[4] = 3318 + n;
            g_bms.cell_mv[5] = 3324 - n;
            g_bms.cell_mv[6] = 3320;
            g_bms.cell_mv[7] = 3321;
            g_bms.pack_voltage_10mv = 2656 + n;
            g_bms.ntc1_temp_01k = 2981; // 25.0 °C
            g_bms.ntc2_temp_01k = 2976; // 24.5 °C
            g_bms.ntc3_temp_01k = 2955; // 22.4 °C (MOSFET ambient resting)
        }

        // Calculate SOC (%)
        if (g_bms.nominal_cap_10mah > 0) {
            g_bms.soc_percent = (uint8_t)(((uint32_t)g_bms.remain_cap_10mah * 100) / g_bms.nominal_cap_10mah);
        }
    }

    // In Daly mode, periodically push Modbus D2 Status frame (every 1500ms) only if client has requested D2 Modbus
    static unsigned long s_lastDalyPush = 0;
    if (g_proto == PROTO_DALY && g_deviceConnected && s_lastDalyReadCount > 0 && (now - s_lastDalyPush >= 1500)) {
        s_lastDalyPush = now;
        sendDalyModbusResponseD2(0x0000, s_lastDalyReadCount);
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

        Serial.printf("[STATUS] Proto: %s | BLE: %s | Mode: %s | Pack: %.2fV | Current: %+.2fA | Power: %.1fW | SOC: %u%% (%.2fAh)\n",
                      (g_proto == PROTO_DALY) ? "DALY" : "JBD",
                      g_deviceConnected ? "CONNECTED" : "ADVERTISING",
                      modeStr, packV, currA, powerW, g_bms.soc_percent, g_bms.remain_cap_10mah / 100.0f);
    }

    // Interactive Serial Control
    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == 'm' || ch == 'M') {
            toggleEmulatorMode();
        } else if (ch == 't' || ch == 'T') {
            Serial.printf(">>> Manual Telemetry Burst (proto=%s, connected=%d) <<<\n",
                          (g_proto == PROTO_DALY) ? "DALY" : "JBD", g_deviceConnected);
            if (g_proto == PROTO_DALY) {
                sendDalyModbusStatusD2(false);
                sendDalyP81Cells();
                sendAllDalyTelemetry();
            }
        } else if (ch == 'j' || ch == 'J') {
            setEmulatorMode(PROTO_JBD);
        } else if (ch == 'y' || ch == 'Y') {
            setEmulatorMode(PROTO_DALY);
        } else if (ch == '1' || ch == 'd' || ch == 'D') {
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
