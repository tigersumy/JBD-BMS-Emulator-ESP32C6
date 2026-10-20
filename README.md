# 🔋 ESP32-C6 Dual BMS BLE Emulator: JBD & Daly Smart BMS

<p align="center">
  <a href="README.md"><b>English</b></a> |
  <a href="README_UA.md"><b>Українська</b></a> |
  <a href="PROJECT_CAPABILITIES.md"><b>📋 Complete Capabilities Spec (MD)</b></a>
</p>

---

A full-featured, standalone hardware-in-the-loop (HIL) battery management system (BMS) emulator powered by the **ESP32-C6** microcontroller (also compatible with **ESP32-S3**).

The emulator implements a **Dual-Protocol Engine** and can dynamically emulate the two most popular smart BMS standards in the world:
1. **JBD / Xiaoxiang / Jiabaida / Overkill Solar BMS**
2. **Daly Smart BMS (DL-xxxx series)**

This project allows safe, rapid bench testing, debugging, and calibration of inverter gateways (Growatt, Deye, Datouboss, Sandisolar, Victron), Home Assistant / ESPHome setups, and official mobile apps without connecting to a physical, hazardous high-voltage battery pack.

> 📘 **For a complete technical breakdown of registers, packet layouts, and BLE timings, see [PROJECT_CAPABILITIES.md](PROJECT_CAPABILITIES.md).**

---

## 🚀 Key Features

### 1. Dual-Protocol Engine (JBD ↔ Daly)
- **On-the-Fly Switching**:
  - **Hardware Double Reset**: Double-clicking the **RST** button on the ESP32-C6 board within 1.5 seconds toggles between JBD and Daly modes. Mode is persisted across reboots in NVS Flash.
  - **Interactive Serial CLI**: Enter `m` (toggle), `j` (JBD), or `y` (Daly) in the serial terminal (115200 baud).
  - **Bash Script**: Run `./switch_mode.sh daly` or `./switch_mode.sh jbd`.
- **Dynamic BLE Identity**:
  - Automatically swaps BLE device name, GATT services, characteristics, and hardware MAC address on mode change.

---

### 2. Daly Smart BMS Emulation (DL-401804015491)
- **Device Name**: `DL-401804015491` | **MAC**: `A5:C2:3A:26:F2:D1`.
- **Dual Communication Standards**:
  1. **Modbus RTU over BLE (0xD2)**: Official **Smart BMS** mobile app support (iOS & Android).
     - Full 8-cell voltage breakdown (3.275V–3.288V) with dynamic min/max highlights.
     - NTC temperatures (T1, T2, T3) and power MOS temperature (28°C).
     - Complete device identity: Firmware `24_231116_001T`, Hardware `DL-8S-200A`, SN `207JD160400278`, Production Date `20240516` (0x00CC), PID `NoPID` (0x00CE).
     - Function 0x06 write control: Charge MOS (`0x00A5`), Discharge MOS (`0x00A6`), Active Balancer (`0x00A7`).
  2. **UART-over-BLE (0xA5)**: 13-byte framed protocol for **Universal BMS Diagnostic Monitor (ESP32-S3 Probe / Tailscale)**, ESPHome, and inverter gateways.
     - Full support for commands `0x90`–`0x98`, `0xD9`/`0xDA`.
     - Multi-frame 0x95 cell streaming (3 sequential frames) with 40ms inter-frame pacing to prevent BLE buffer drops.

---

### 3. JBD / Xiaoxiang BMS Emulation (BS-26A-072-005)
- **Device Name**: `BS-26A-072-005` | **MAC**: `A5:C2:3A:26:F2:C2`.
- **App Compatibility**: Official **JBD BMS** (iOS & Android) and **Xiaoxiang BMS**.
- **Complete BLE GATT Profile**:
  - Primary Service `0xFF00` (Notify: `0xFF01`, Write: `0xFF02`) and mirror service `0xFFF0`.
  - **iOS CoreBluetooth Discovery Fix**: Injects hardware MAC into `Manufacturer Data` of the Scan Response packet for instant discovery on iPhone.
  - **Authentication**: Supports `FF AA 15` handshake and default PIN `123456`.
  - **Full EEPROM Map**: Registers `0x10`..`0x3F`, `0xAA` error logs, and direct AFE RAM memory reading (`0xFA`, offset `0x58`) for Android.
  - **20-byte ATT MTU Chunking**: Smooth 20-byte packet streaming with 25ms delay for 100% stability across Android (MTU 23) and iOS (MTU 512).

---

### 4. 8S LiFePO4 Battery Physics Simulation (24V)
- **Dynamic Cell Voltage Breathing**: Cell voltages continuously oscillate in the $3.275\text{–}3.288\text{ V}$ range (3–12 mV delta) using sine-wave variations and pseudorandom micro-jitter.
- **Load Modes**:
  - **Discharge**: $-17.00\text{ A}$, pack voltage $\sim 26.24\text{ V}$, power draw $\sim 445\text{ W}$.
  - **Charge**: $+17.00\text{ A}$, pack voltage $\sim 27.20\text{ V}$.
  - **Standby**: $0.00\text{ A}$, power draw $0\text{ W}$.
- **Coulomb Counting**: Real-time integration of current ($I \times \Delta t$) updating remaining Ah and SOC %.
- **FET Switch Responsiveness**: Turning off the Discharge FET immediately drops current to $0.00\text{ A}$ and power to $0\text{ W}$.

---

### 5. WS2812B RGB Status LED (GPIO 8)
- 🔴 **Red**: Advertising / Waiting for connection.
- 🟠 **Orange**: Active Discharge (-17.00 A).
- 🩵 **Cyan**: Active Charge (+17.00 A).
- 🟢 **Green**: Standby (0.00 A / FETs OFF).
- ⚪ **White**: Full Charge (100% SOC).
- 🟡 **Yellow**: Alarm / Protection triggered.

---

## 🛠 Repository Structure

```
.
├── README.md                      # English documentation
├── README_UA.md                   # Ukrainian documentation
├── PROJECT_CAPABILITIES.md        # Detailed capabilities & protocol reference
├── Daly.MD                        # Daly Modbus & BLE notes
├── JBD_PROTOCOL_SPEC.md           # JBD binary protocol specification
├── JBD_REGISTER_MAP.md            # JBD EEPROM register map
├── platformio.ini                 # PlatformIO configuration (ESP32-C6 / ESP32-S3)
├── switch_mode.sh                 # Mode switcher script (JBD / Daly)
├── src/
│   └── main.cpp                   # C++ emulator source code (NimBLE-Arduino)
└── bin/                           # Ready-to-flash precompiled binaries
    ├── jbd_emulator_esp32_c6_factory.bin    # Full factory image for ESP32-C6 (0x0000)
    ├── jbd_emulator_esp32_c6_firmware.bin   # App OTA image for ESP32-C6 (0x10000)
    ├── jbd_emulator_esp32_s3_factory.bin    # Full factory image for ESP32-S3 (0x0000)
    └── jbd_emulator_esp32_s3_firmware.bin   # App OTA image for ESP32-S3 (0x10000)
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

- `m` or `M`: Toggle protocol (**JBD** $\leftrightarrow$ **DALY**).
- `j` or `J`: Force **JBD BMS** mode.
- `y` or `Y`: Force **DALY BMS** mode.
- `1` or `d`: Active discharge mode ($-17.00\text{ A}$) $\to$ 🟠 Orange LED.
- `2` or `c`: Active charge mode ($+17.00\text{ А}$) $\to$ 🩵 Cyan LED.
- `0` or `s`: Standby mode ($0.00\text{ А}$) $\to$ 🟢 Green LED.
- `b` or `B`: Toggle active balancing simulation.
- `f` or `F`: Toggle Charge/Discharge FET switches.
- `+` / `-`: Adjust SOC by $\pm 5\%$.
- `t` or `T`: Send telemetry burst over BLE.
- `?` or `h`: Print command help.

---

## 📱 Verified Client Compatibility

1. **JBD BMS / Xiaoxiang App** (iOS & Android) — Full telemetry, EEPROM, and cell monitoring in JBD mode (`BS-26A-072-005`).
2. **Smart BMS App** (iOS & Android) — Official Daly app with full 8 cells, NTCs, and FET switch controls in Daly mode (`DL-401804015491`).
3. **Universal BMS Diagnostic Monitor** (ESP32-S3 Probe via Tailscale) — Seamless auto-detection and telemetry streaming for both JBD and Daly.
4. **ESPHome / Home Assistant** — Compatible via standard `daly_bms` and `jbd_bms` components.
