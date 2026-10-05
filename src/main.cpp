#include <Arduino.h>
#include <NimBLEDevice.h>

#define BLE_DEVICE_NAME       "DB24SF01"
#define SERVICE_UUID          "FF00"
#define NOTIFY_CHAR_UUID      "FF01"
#define WRITE_CHAR_UUID       "FF02"

// Onboard WS2812 RGB LED for WeAct Studio ESP32-C6 Mini (GPIO 8)
#ifndef RGB_LED_PIN
#define RGB_LED_PIN 8
#endif

void setRgbLed(uint8_t r, uint8_t g, uint8_t b) {
    // 50% brightness reduction for comfortable viewing
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

// BMS State Structure
struct BMSState {
    uint16_t pack_voltage_10mv = 2624;   // ~26.24 V under 17A load (8S LiFePO4)
    int16_t  current_10ma      = -1700;  // -17.00 A (17A Discharge)
    uint16_t remain_cap_10mah  = 19400;  // 194.00 Ah
    uint16_t nominal_cap_10mah = 20000;  // 200.00 Ah (8S200A)
    uint16_t cycle_count       = 15;     // 15 cycles
    uint16_t prod_date         = 0x30AA; // 2024-05-10 ((24<<9)|(5<<5)|10)
    uint16_t balance_low       = 0x0000; // Balancing bitmask
    uint16_t balance_high      = 0x0000;
    uint16_t protection_status = 0x0000; // 0x0000 = Normal / No alarms
    uint8_t  software_version  = 0x13;   // v13 (matches screenshot "BMS version 13")
    uint8_t  soc_percent       = 97;     // 97%
    uint8_t  fet_status        = 0x03;   // Bit0: Charge FET (1=ON), Bit1: Discharge FET (1=ON)
    uint8_t  cell_count        = 8;      // 8S LiFePO4 (24V)
    uint8_t  ntc_count         = 2;      // 2 NTC sensors
    uint16_t ntc1_temp_01k     = 2991;   // 26.0 °C
    uint16_t ntc2_temp_01k     = 2986;   // 25.5 °C
    uint16_t cell_mv[8]        = {3280, 3283, 3279, 3282, 3278, 3284, 3280, 3281};
    char     device_name[32]   = "DB24SF01";

    // Mode and Simulation state
    OperationMode mode         = MODE_DISCHARGE; // Default: 17A discharge
    float    target_current_a  = -17.0f;         // -17A
    float    fractional_mah    = 0.0f;           // Integration accumulator
};

BMSState g_bms;
NimBLEServer* pServer = nullptr;
NimBLECharacteristic* pNotifyChar = nullptr;
NimBLECharacteristic* pWriteChar = nullptr;
bool g_deviceConnected = false;
unsigned long g_lastLogTime = 0;
unsigned long g_lastSimTime = 0;

// Dynamic status LED function
void updateStatusLed() {
    if (!g_deviceConnected) {
        // 🔴 Red: Advertising / Waiting for Bluetooth client
        setRgbLed(40, 0, 0);
    } else {
        // Connected to Client
        if (g_bms.protection_status != 0) {
            // 🟡 Yellow: Protection / Alarm triggered
            setRgbLed(40, 32, 0);
        } else if (g_bms.soc_percent >= 100) {
            // ⚪ White: 100% Fully Charged
            setRgbLed(25, 25, 25);
        } else if (g_bms.mode == MODE_CHARGE && (g_bms.fet_status & 0x01)) {
            // 🩵 Cyan: Active Charging (+17A)
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

// Calculate JBD Checksum: 0x10000 - sum(bytes)
uint16_t calculateJbdCRC(const uint8_t* data, size_t len) {
    uint32_t sum = 0;
    for (size_t i = 0; i < len; ++i) {
        sum += data[i];
    }
    return (uint16_t)(0x10000 - sum);
}

// Server connection callbacks
class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override {
        g_deviceConnected = true;
        pServer->updateConnParams(connInfo.getConnHandle(), 12, 24, 0, 400); // 15-30ms interval for iOS
        updateStatusLed();
        Serial.printf("[BLE] Client connected! Peer address: %s\n", connInfo.getAddress().toString().c_str());
    }

    void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo, int reason) override {
        g_deviceConnected = false;
        updateStatusLed();
        Serial.printf("[BLE] Client disconnected (reason: %d). Restarting advertising...\n", reason);
        NimBLEDevice::startAdvertising();
    }
};

// Response helper functions
void sendJBDResponse(uint8_t reg, const uint8_t* payload, uint8_t payloadLen, uint8_t status = 0x00) {
    if (!pNotifyChar || !g_deviceConnected) return;

    // Response frame format:
    // [0xDD] [Register] [Status: 0x00] [Length: N] [Data (N bytes)] [CRC Hi] [CRC Lo] [0x77]
    size_t totalLen = 7 + payloadLen;
    uint8_t frame[64];
    
    frame[0] = 0xDD;
    frame[1] = reg;
    frame[2] = status;
    frame[3] = payloadLen;

    if (payload && payloadLen > 0) {
        memcpy(&frame[4], payload, payloadLen);
    }

    // JBD Response Checksum is calculated over Status (frame[2]) + Length (frame[3]) + Data (frame[4..])
    // Total bytes for CRC = 2 + payloadLen
    uint16_t crc = calculateJbdCRC(&frame[2], 2 + payloadLen);
    frame[4 + payloadLen] = (uint8_t)(crc >> 8);
    frame[5 + payloadLen] = (uint8_t)(crc & 0xFF);
    frame[6 + payloadLen] = 0x77;

    // Send BLE notification FIRST before any logging to avoid any USB latency
    pNotifyChar->setValue(frame, totalLen);
    pNotifyChar->notify();

    if (Serial) {
        Serial.printf("[JBD TX -> Reg 0x%02X] (%u bytes)\n", reg, (unsigned int)totalLen);
    }
}

// Build and send Basic Info (Register 0x03)
void sendBasicInfo() {
    // Basic info payload is 27-31 bytes:
    // 0x00-0x01: Voltage (10mV)
    // 0x02-0x03: Current (10mA, signed)
    // 0x04-0x05: Remaining Capacity (10mAh)
    // 0x06-0x07: Nominal Capacity (10mAh)
    // 0x08-0x09: Cycle Count
    // 0x0A-0x0B: Production Date
    // 0x0C-0x0D: Balance Mask Low
    // 0x0E-0x0F: Balance Mask High
    // 0x10-0x11: Protection / Alarm Status
    // 0x12:      Software Version
    // 0x13:      SOC (%)
    // 0x14:      MOSFET Status (Bit0: Charge, Bit1: Discharge)
    // 0x15:      Cell Count
    // 0x16:      NTC Sensor Count
    // 0x17-0x18: NTC 1 Temp (0.1K)
    // 0x19-0x1A: NTC 2 Temp (0.1K)
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
    // cell_count cells * 2 bytes
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
    const char* barcode = "";
    sendJBDResponse(0xA0, (const uint8_t*)barcode, strlen(barcode));
}

// Build and send Manufacturer Name (Register 0xA1)
void sendManufacturerName() {
    const char* mfg = "8S200A";
    sendJBDResponse(0xA1, (const uint8_t*)mfg, strlen(mfg));
}

// Build and send Hardware Info (Register 0xA2)
void sendHardwareVersion() {
    const char* hw = "DB24SF01 V1.0";
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
        case 0x21: val = 10000; break; // Discharge Overcurrent (100.0A)
        case 0x22: val = 3350; break;  // Balance Start Voltage (mV)
        case 0x23: val = 15; break;    // Balance Delta Voltage (mV)
        default: val = 0; break;
    }
    uint8_t data[2];
    data[0] = (uint8_t)(val >> 8);
    data[1] = (uint8_t)(val & 0xFF);
    sendJBDResponse(reg, data, sizeof(data));
}

// Write callback handler for 0xFF02
class WriteCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo) override {
        std::string rxData = pCharacteristic->getValue();
        size_t len = rxData.length();
        if (len < 7) {
            Serial.printf("[JBD RX] Received invalid short frame (%u bytes)\n", (unsigned int)len);
            return;
        }

        const uint8_t* buf = (const uint8_t*)rxData.data();
        Serial.printf("[JBD RX <- App] (%u bytes): ", (unsigned int)len);
        for (size_t i = 0; i < len; ++i) {
            Serial.printf("%02X ", buf[i]);
        }
        Serial.println();

        // Validate Start Byte (0xDD) and Stop Byte (0x77)
        if (buf[0] != 0xDD || buf[len - 1] != 0x77) {
            Serial.println("[JBD RX] Warning: Invalid start/stop frame delimiter.");
            return;
        }

        uint8_t cmd      = buf[1]; // 0xA5 (Read), 0x5A (Write), or 0xAA (Ping/Check)
        uint8_t reg      = buf[2]; // Target Register
        uint8_t dataLen  = buf[3]; // Payload data length

        if (cmd == 0xA5) {
            // Read Command
            Serial.printf("[JBD CMD] Read Register 0x%02X\n", reg);
            switch (reg) {
                case 0x03: // Basic Info
                    sendBasicInfo();
                    break;
                case 0x04: // Cell Voltages
                    sendCellVoltages();
                    break;
                case 0x05: // Device Name
                    sendDeviceName();
                    break;
                case 0x06: // PIN Code / Password Query
                    {
                        uint8_t pinData[7] = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
                        sendJBDResponse(0x06, pinData, 7);
                    }
                    break;
                case 0xA0: // Barcode / Serial
                    sendBarcode();
                    break;
                case 0xA1: // Manufacturer Name
                    sendManufacturerName();
                    break;
                case 0xA2: // Hardware Version String
                    sendHardwareVersion();
                    break;
                case 0xAA: // Factory Mode Status Check
                    {
                        uint8_t ackData[2] = {0x00, 0x00};
                        sendJBDResponse(0xAA, ackData, 2);
                    }
                    break;
                default:
                    if (reg >= 0x10 && reg <= 0x3F) {
                        sendParameterRegister(reg);
                    } else {
                        Serial.printf("[JBD CMD] Unhandled Read Register 0x%02X -> returning default 2 bytes\n", reg);
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
                // Example payload: 0x00 (both off), 0x01 (charge on), 0x02 (discharge on), 0x03 (both on)
                if (dataLen >= 1) {
                    uint8_t newFetState = buf[4];
                    if (dataLen >= 2 && buf[4] == 0x00) {
                        newFetState = buf[5]; // Some variants send uint16_t state
                    }
                    g_bms.fet_status = newFetState & 0x03;
                    updateStatusLed();
                    Serial.printf("[JBD FET] Updated MOSFET State: 0x%02X (Charge FET: %s, Discharge FET: %s)\n",
                                  g_bms.fet_status,
                                  (g_bms.fet_status & 0x01) ? "ON" : "OFF",
                                  (g_bms.fet_status & 0x02) ? "ON" : "OFF");
                }
                // Send ACK status
                sendJBDResponse(0xE1, nullptr, 0, 0x00);
            } else if (reg == 0x00 || reg == 0x01) {
                // Factory / EEPROM Unlock commands
                Serial.printf("[JBD CMD] Unlock / Configuration command 0x%02X acknowledged.\n", reg);
                sendJBDResponse(reg, nullptr, 0, 0x00);
            } else {
                Serial.printf("[JBD CMD] General Write Register 0x%02X acknowledged.\n", reg);
                sendJBDResponse(reg, nullptr, 0, 0x00);
            }
        }
    }
};

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0); // Non-blocking USB CDC output - never block BLE thread
    delay(500);

    Serial.println("\n=======================================================");
    Serial.println("       ESP32-C6 JBD (Xiaoxiang) BMS BLE Emulator       ");
    Serial.println("=======================================================");
    Serial.printf("Device Name: %s\n", BLE_DEVICE_NAME);
    Serial.printf("Pack Voltage: %.2fV | SOC: %u%% | Cells: %uS\n", 
                  g_bms.pack_voltage_10mv / 100.0f, g_bms.soc_percent, g_bms.cell_count);
    Serial.printf("MOSFET Status: 0x%02X (Charge: %s, Discharge: %s)\n",
                  g_bms.fet_status,
                  (g_bms.fet_status & 0x01) ? "ON" : "OFF",
                  (g_bms.fet_status & 0x02) ? "ON" : "OFF");
    Serial.println("=======================================================\n");

    // Initialize NimBLE Device
    NimBLEDevice::init(BLE_DEVICE_NAME);
    NimBLEDevice::setPower(ESP_PWR_LVL_P9); // Max TX power for robust iOS connectivity

    // Create Server
    pServer = NimBLEDevice::createServer();
    pServer->setCallbacks(new ServerCallbacks());

    // Create JBD Primary Service 0xFF00
    NimBLEService* pService = pServer->createService(SERVICE_UUID);

    // Create Characteristic 0xFF01 (READ | NOTIFY)
    pNotifyChar = pService->createCharacteristic(
        NOTIFY_CHAR_UUID,
        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY
    );

    // Create Characteristic 0xFF02 (WRITE | WRITE_NR)
    pWriteChar = pService->createCharacteristic(
        WRITE_CHAR_UUID,
        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_NR
    );
    pWriteChar->setCallbacks(new WriteCallbacks());

    // Start Server & Advertising
    pServer->start();

    NimBLEAdvertising* pAdvertising = NimBLEDevice::getAdvertising();
    pAdvertising->setName(BLE_DEVICE_NAME);
    pAdvertising->addServiceUUID(pService->getUUID());
    pAdvertising->enableScanResponse(true);
    pAdvertising->start();

    // Initial state: Red LED indicates Advertising / Waiting for client
    updateStatusLed();

    Serial.println("[BLE] Advertising started successfully. Waiting for Xiaoxiang / Overkill Solar app...");
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
            g_bms.ntc1_temp_01k = 2991; // 26.0 °C
            g_bms.ntc2_temp_01k = 2986; // 25.5 °C

        } else if (g_bms.mode == MODE_CHARGE && (g_bms.fet_status & 0x01)) {
            // Active Charge (+17A)
            float currentA = 17.0f + jitter;
            g_bms.current_10ma = (int16_t)(currentA * 100.0f); // +1700 (10mA)

            // Integrate capacity: charging increases remain_cap
            float delta_mah = (17.0f * 1000.0f) * (dt_sec / 3600.0f);
            g_bms.fractional_mah += delta_mah;
            while (g_bms.fractional_mah >= 10.0f) {
                if (g_bms.remain_cap_10mah < g_bms.nominal_cap_10mah) g_bms.remain_cap_10mah++;
                g_bms.fractional_mah -= 10.0f;
            }

            // Cell voltages rise under 17A charge (~3.42V)
            g_bms.cell_mv[0] = 3420 + drift;
            g_bms.cell_mv[1] = 3424 - drift;
            g_bms.cell_mv[2] = 3418 + drift;
            g_bms.cell_mv[3] = 3422 - drift;
            g_bms.cell_mv[4] = 3419 + drift;
            g_bms.cell_mv[5] = 3425 - drift;
            g_bms.cell_mv[6] = 3421 + drift;
            g_bms.cell_mv[7] = 3423 - drift;
            g_bms.ntc1_temp_01k = 2986; // 25.5 °C
            g_bms.ntc2_temp_01k = 2981; // 25.0 °C

        } else {
            // Idle / FETs Disabled (0.00A)
            g_bms.current_10ma = 0;

            // Resting cell voltages (~3.345V)
            g_bms.cell_mv[0] = 3345 + drift;
            g_bms.cell_mv[1] = 3348 - drift;
            g_bms.cell_mv[2] = 3346 + drift;
            g_bms.cell_mv[3] = 3347 - drift;
            g_bms.cell_mv[4] = 3344 + drift;
            g_bms.cell_mv[5] = 3348 - drift;
            g_bms.cell_mv[6] = 3345 + drift;
            g_bms.cell_mv[7] = 3347 - drift;
            g_bms.ntc1_temp_01k = 2981; // 25.0 °C
            g_bms.ntc2_temp_01k = 2976; // 24.5 °C
        }

        // Calculate SOC %
        g_bms.soc_percent = (uint8_t)(((uint32_t)g_bms.remain_cap_10mah * 100) / g_bms.nominal_cap_10mah);

        // Calculate total pack voltage (10mV units)
        uint32_t totalMv = 0;
        for (int i = 0; i < g_bms.cell_count; ++i) {
            totalMv += g_bms.cell_mv[i];
        }
        g_bms.pack_voltage_10mv = totalMv / 10;
    }

    // Periodic telemetry log (every 3 seconds)
    if (now - g_lastLogTime >= 3000) {
        g_lastLogTime = now;
        updateStatusLed();

        float packV = g_bms.pack_voltage_10mv / 100.0f;
        float currA = g_bms.current_10ma / 100.0f;
        float powerW = packV * fabs(currA);

        const char* modeStr = (g_bms.current_10ma > 50) ? "CHARGING (+)" :
                              ((g_bms.current_10ma < -50) ? "DISCHARGING (-)" : "STANDBY (IDLE)");

        Serial.printf("[STATUS] BLE: %s | Mode: %s | Pack: %.2fV | Current: %+.2fA | Power: %.1fW | SOC: %u%% (%.2fAh)\n",
                      g_deviceConnected ? "CONNECTED" : "ADVERTISING",
                      modeStr,
                      packV,
                      currA,
                      powerW,
                      g_bms.soc_percent,
                      g_bms.remain_cap_10mah / 100.0f);
    }

    // Process serial input for live interactive control
    if (Serial.available()) {
        char ch = Serial.read();
        if (ch == '1' || ch == 'd' || ch == 'D') {
            g_bms.mode = MODE_DISCHARGE;
            updateStatusLed();
            Serial.println(">>> Switched to MODE_DISCHARGE (-17.00 A) <<<");
        } else if (ch == '2' || ch == 'g' || ch == 'G') {
            g_bms.mode = MODE_CHARGE;
            updateStatusLed();
            Serial.println(">>> Switched to MODE_CHARGE (+17.00 A) <<<");
        } else if (ch == '0' || ch == 'i' || ch == 'I') {
            g_bms.mode = MODE_IDLE;
            updateStatusLed();
            Serial.println(">>> Switched to MODE_IDLE (0.00 A Standby) <<<");
        } else if (ch == 't' || ch == 'T') {
            g_bms.mode = (g_bms.mode == MODE_DISCHARGE) ? MODE_CHARGE : MODE_DISCHARGE;
            updateStatusLed();
            Serial.printf(">>> Toggled Mode to %s <<<\n", (g_bms.mode == MODE_CHARGE) ? "CHARGE (+17A)" : "DISCHARGE (-17A)");
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
