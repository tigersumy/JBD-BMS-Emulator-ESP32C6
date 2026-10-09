# 🔋 ESP32-C6 JBD / Xiaoxiang BMS BLE Emulator (BS-26A-072-005 / 8S150A)

<p align="center">
  <a href="README.md"><b>English</b></a> |
  <a href="README_UA.md"><b>Українська</b></a>
</p>

---

A full-featured, standalone hardware-in-the-loop (HIL) emulator for **JBD (Xiaoxiang / Jiabaida / BS-26A-072-005 / Overkill Solar) BMS** systems powered by **ESP32-C6** (also supports **ESP32-S3**).

This project enables safe, rapid bench testing and calibration of inverter gateways, solar setups, smart home integrations (Home Assistant, ESPHome, Node-RED), and mobile apps without needing a physical, high-voltage battery pack.

---

## 🚀 Key Features

- **Mobile App Compatibility**:
  - **JBD BMS** (Official iOS / Android App with full PIN auth `FF AA 15` and status handshake)
  - **Xiaoxiang BMS** (iOS / Android)
  - **SMART BMS** (iOS / Android)
  - **BMS Tool** / **ESPHome JBD BMS** / **ESP32 Universal Monitor**
- **Complete BLE GATT Service Emulation**:
  - Primary Service UUID: `0000FF00-0000-1000-8000-00805F9B34FB` (`0xFF00`)
  - Notify/Read UUID: `0000FF01-0000-1000-8000-00805F9B34FB` (`0xFF01`)
  - Write UUID: `0000FF02-0000-1000-8000-00805F9B34FB` (`0xFF02`)
- **Default Profile: BS-26A-072-005 (8S LiFePO4)**:
  - BLE Advertised Name: `BS-26A-072-005`
  - Exact Hardware MAC: `A5:C2:3A:26:F2:C2` (hardware-level interface mapping)
  - Manufacturer (Register `0xA0`): `Jiabaida`
  - Factory Hardware Model (Register `0xA1` & `AT^VERSION?`): `SP08S004` (8S LiFePO4 profile)
  - Barcode String (Register `0xA2`): `BS-26A-072-005`
  - Nominal Capacity: `150.00 Ah` (8S LiFePO4 24V)
  - Full Parameter EEPROM Map (`0x10`..`0x3F`, `0xAA` error logs) for seamless "About Battery" / "Parameters" navigation.
  - **Extended AFE RAM Memory Protocol (`0xFA`)**: Full support for Android JBD app direct memory reading (offset `0x58` cell voltages in mV, chip config, serial).
  - **Universal ATT MTU Chunking**: 20-byte packet streaming with inter-frame delays for 100% reliable data parsing across both Android (MTU 23) and iOS (MTU 512).
- **🍏 iOS CoreBluetooth & Official JBD BMS App Compatibility**:
  - **Embedded MAC in Manufacturer Data**: iOS CoreBluetooth hides raw hardware MACs. The emulator injects the 6-byte MAC (`A5:C2:3A:26:F2:C2`) into the `Manufacturer Data` of the **Scan Response Packet**, allowing the official JBD BMS iOS app to discover and display the device instantly.
  - **Primary 16-bit Service UUID `0xFF00`**: Included in the primary advertising payload (25 bytes $\le$ 31 bytes) to satisfy iOS hardware background scanning filters.
  - **Open Pairing Security**: Utilizes application-level PIN handshake (`123456`) without triggering OS-level SMP bonding conflicts.
- **Realistic 8S LiFePO4 Battery Physics Simulation**:
  - Load Current: **`-17.00 A`** (with dynamic $\pm 0.1\text{A}$ jitter)
  - Power Draw: **`~447 W`** ($26.24\text{ V} \times 17.0\text{ A}$)
  - Voltage Sag Under Load: **`~26.24 V`** (~`3.28 V` per cell)
  - Coulomb Counting integration: $I \times \Delta t$ updating remaining Ah and SOC %
  - Current cutoff (0A / 0W) when **Discharge FET** switch is toggled OFF
  - 8 individual cell voltages (3278–3284 mV under load) with realistic cell delta
  - NTC1 & NTC2 temperature sensors: 26.0 °C / 25.5 °C under current
- **Onboard WS2812B Addressable RGB LED (GPIO 8)**:
  - 🔴 **Red**: Advertising / Waiting for Bluetooth client.
  - 🟠 **Orange**: Active Discharge (-17.00 A).
  - 🩵 **Cyan**: Active Charge (+17.00 A).
  - 🟢 **Green**: Standby / Idle (0.00 A / FETs OFF).
  - ⚪ **White**: Full Charge (100% SOC).
  - 🟡 **Yellow**: Protection / Alarm triggered (`0x10 != 0`).
  - *Brightness scaled to 50% for comfortable viewing.*
- **Non-Blocking USB CDC Pipeline**:
  - Built with `Serial.setTxTimeoutMs(0)` and priority BLE notifications so the emulator runs autonomously at full speed regardless of whether a serial terminal is connected to your computer.

---

## 🛠 Repository Structure

```
.
├── README.md                      # English documentation
├── README_UA.md                   # Ukrainian documentation
├── JBD_PROTOCOL_SPEC.md           # Complete JBD binary protocol specification
├── platformio.ini                 # PlatformIO build configuration (ESP32-C6 / ESP32-S3)
├── src/
│   └── main.cpp                   # C++ emulator source code (NimBLE-Arduino)
└── bin/                           # Ready-to-flash precompiled binaries
    ├── jbd_emulator_esp32_c6_factory.bin    # Full factory image for ESP32-C6 (0x0000)
    ├── jbd_emulator_esp32_c6_firmware.bin   # App image for ESP32-C6 (0x10000)
    ├── jbd_emulator_esp32_s3_factory.bin    # Full factory image for ESP32-S3 (0x0000)
    └── jbd_emulator_esp32_s3_firmware.bin   # App image for ESP32-S3 (0x10000)
```

---

## ⚡ Build & Flash Guide

### 1. Compile & Flash ESP32-C6 (via PlatformIO):
```bash
pio run -e esp32-c6-supermini -t upload
```

### 2. Compile & Flash ESP32-S3 (via PlatformIO):
```bash
pio run -e esp32-s3-supermini -t upload
```

### 3. Quick Flash with Precompiled Factory Binary (via esptool at 0x0000):
```bash
esptool.py -p /dev/cu.usbmodem* -b 921600 write_flash 0x0000 bin/jbd_emulator_esp32_c6_factory.bin
```

---

## 💻 Interactive Serial Console (115200 baud)

- `1` or `d`: Switch to Active Discharge (-17.00 A) $\to$ 🟠 Orange LED.
- `2` or `g`: Switch to Active Charge (+17.00 A) $\to$ 🩵 Cyan LED.
- `0` or `i`: Switch to Standby / Idle (0.00 A) $\to$ 🟢 Green LED.
- `t`: Toggle between Charge & Discharge mode.
- `+` / `=`: Increase SOC by +1%.
- `-` / `_`: Decrease SOC by -1%.

---

## 📱 Mobile App Connection

1. Enable Bluetooth on your iPhone or Android smartphone.
2. Open **Xiaoxiang BMS** or **SMART BMS**.
3. Select **`DB24SF01`** from the discovered devices list.
4. Instantly view live 8-cell voltages, 26.24V pack voltage, 17A discharge, and control Charge/Discharge MOSFETs in real time.
