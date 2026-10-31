# 🔋 ESP32-C6 5-in-1 Universal BMS BLE Emulator: JBD, Daly, Ant BMS, JKBMS & Redodo / LiTime

<p align="center">
  <a href="README.md"><b>English README</b></a> |
  <a href="README_UA.md"><b>Українська README</b></a> |
  <a href="PROJECT_CAPABILITIES.md"><b>📋 Capabilities Spec (EN)</b></a> |
  <a href="PROJECT_CAPABILITIES_UA.md"><b>📋 Опис можливостей (UA)</b></a>
</p>

---

A full-featured, standalone hardware-in-the-loop (HIL) battery management system (BMS) emulator powered by the **ESP32-C6** microcontroller (also compatible with **ESP32-S3**).

The emulator implements a **5-Protocol Multi-Engine** and dynamically emulates the 5 most popular smart BMS standards in the world:
1. **JBD / Xiaoxiang / Jiabaida / Overkill Solar BMS** (`BS-26A-072-005`)
2. **Daly Smart BMS (DL-xxxx series)** (`DL-401804015491`)
3. **Ant BMS (ANT-BMS series)** (dynamic `ANT-BMS-08S`, `ANT-BMS-16S`, `ANT-BMS-24S`)
4. **JKBMS (JiKong JK02 protocol)** (dynamic `JK-B2A8S20P`, `JK-B2A16S20P`, `JK-B2A24S20P`)
5. **Redodo / LiTime Monolithic BMS** (dynamic `Redodo-12V100A`, `Redodo-24V100A`, `Redodo-48V100A`)

This project allows safe, rapid bench testing, debugging, and calibration of inverter gateways (Growatt, Deye, Datouboss, Sandisolar, Victron), Home Assistant / ESPHome setups, and official mobile apps without connecting to a physical, hazardous high-voltage battery pack.

> 📘 **For a complete technical breakdown of registers, packet layouts, and BLE timings, see [PROJECT_CAPABILITIES.md](PROJECT_CAPABILITIES.md).**

---

## 🚀 Key Features

### 📱 Mobile App & Client Compatibility
- **JBD BMS (Official Jiabaida, Xiaoxiang, PowerQueen & Overkill Solar Apps for iOS & Android)**:
  - **100% native compatibility in JBD BMS mode** with selectable manufacturer sub-profiles:
    - **Standard JBD / Xiaoxiang**: (`BS-26A-072-005` / `JBD-SP04S034`, MAC `A5:C2:3A:26:F2:C2`)
    - **PowerQueen BMS**: (`P-12100B` 4S, `P-24100B` 8S, `P-48100B` 16S, MAC `A5:C2:3A:26:F2:C3`) — test native **PowerQueen BMS App**
    - **Overkill Solar**: (`Overkill Solar 4S` / `8S` / `16S`, MAC `A5:C2:3A:26:F2:C4`) — test **Overkill Solar BMS App**
  - Proprietary PIN authentication handshake `FF AA 15` (default password `123456`).
  - Full access to "Parameters" and "About Battery" views (EEPROM read/write).
- **Smart BMS (Official Daly BMS App for iOS & Android)**:
  - **100% native compatibility in Daly BMS mode (`DL-401804015491`)**.
  - Complete cell voltage matrix with dynamic min/max highlights.
  - Identification: Firmware `24_231116_001T`, Hardware `DL-8S-200A`, SN `207JD160400278`.
  - Full Charge & Discharge MOS switch toggling and Active Balancer control.
- **Ant BMS App (Official MAYA / AntBMS for iOS & Android)**:
  - **100% native compatibility in Ant BMS mode (`ANT-BMS-XXS`)**.
  - 140-byte full telemetry frames via Service `0xFFE0` / Notify `0xFFE1`.
  - Polling negotiation (`0xDB 0xDB` / `0x5A 0x5A`) and switch controls (`0xA5 0xA5`).
- **JKBMS (Official JiKong App for iOS & Android & ESPHome)**:
  - **100% native compatibility in JKBMS mode (`JK-B2A<N>S20P`)**.
  - JK02 protocol with 300-byte telemetry frames via Service `0xFFE0` / Notify `0xFFE1` / Write `0xFFE2`.
  - Device Information Service `0x180A`, CellInfo (`0x96`), Settings (`0x97`), and register control (`0x1D`/`0x1E`/`0x1F`).
- **Redodo / LiTime BMS (Official LiTime / Redodo Apps & Web Bluetooth)**:
  - **100% native compatibility in Redodo mode (`Redodo-12V100A`, `Redodo-24V100A`, `Redodo-48V100A`)**.
  - 105-byte Little-Endian telemetry frames via Service `0xFFE0` / Characteristic `0xFFE1`.
  - Polling command handling (`00 00 04 01 13 55 AA 17`), individual cell voltages (up to 16 cells), terminal and cell sum voltages, signed current, 3 temperatures (Cells, MOS, Ambient), capacity, and SOC.
  - Fully compatible with HTML5 Web Bluetooth dashboard (`redodo_monitor.html`).
- **Universal BMS Diagnostic Monitor (ESP32-S3 Probe / Tailscale Web Dashboard)**:
  - Automatic detection and telemetry streaming for all 5 BMS protocols over Tailscale.
- **ESPHome / Home Assistant / Node-RED / Inverter Gateways**:
  - Native integration with standard `jbd_bms`, `daly_bms`, `ant_bms`, and `jk_bms_ble` components.

---

### 🌐 Embedded Wi-Fi Web Management Dashboard
- **Dual Network Access Modes**:
  - **Mode A (Standalone Wi-Fi AP & Captive Portal)**:
    - Automatically hosts an open Wi-Fi network: **`BMS-Emulator-Setup`** (IP `192.168.4.1`).
    - Integrated DNS Captive Portal prompts phones/laptops to open the dashboard immediately upon connection.
  - **Mode B (Home / Lab Wi-Fi Station)**:
    - Connects to your local Wi-Fi router via the `/setup` page.
    - Discoverable on the network via mDNS: **`http://bms-emulator.local`** (or device local IP).
    - Automatic AP fallback if local Wi-Fi is unreachable.
- **Web UI Features (Dark Glassmorphic UI, UA / EN bilingual)**:
  - 📊 **Live Telemetry Monitoring**: Pack voltage, charge/discharge current, output Watts, battery SOC %, remaining Ah, temperatures, and cell voltage delta $\Delta V$ (mV).
  - 🔋 **Instant Battery Series Configuration**: 1-click preset buttons for **4S (12V)**, **8S (24V)**, and **16S (48V)**, plus dynamic selection from **3S up to 32S** persisted in NVS Flash.
  - 🔄 **4-in-1 Protocol Switcher**: Seamless 1-click toggling between **JBD BMS**, **Daly BMS**, **Ant BMS**, and **JKBMS**.
  - 📡 **BLE MAC Management**: Quick selector and custom MAC address manager with persistent profiles.
  - ⚡ **Load & Physics Simulation**: Select between **Discharge** (-17A), **Charge** (+25A), and **Standby** (0A), with live SOC slider (0–100%).
  - 🛡️ **MOSFET Switch Control**: Independent toggling of Charge MOS and Discharge MOS.
  - 📈 **Cell Voltages Matrix**: Individual cell voltage cards with color-coded battery level indicators and automated Min/Max cell identification.
  - 🚀 **Over-The-Air Firmware Updates (Web OTA)**: Upload compiled `.bin` firmware directly via the browser without USB.

---

### 🔄 Quad-Protocol Switching Architecture
- **Instant Mode Switching**:
  - **Hardware Button (BOOT / GPIO 9)**: Long press (> 1.5s) cycles to the next protocol with visual feedback. Hold at power-on also cycles modes.
  - **Hardware Double Reset**: Double-clicking the **RST** button within 2.5 seconds cycles through the 4 protocols (JBD → Daly → Ant → JK → JBD).
  - **Interactive Serial CLI**: Enter `j` (JBD), `y` (Daly), `a` (Ant), `k` (JK), or `m` (cycle) in the serial terminal (115200 baud).
  - **Web Dashboard**: Click any of the 4 protocol buttons on the dashboard or use `/api/set-proto`.
- **Dynamic BLE Identity**:
  - Automatically swaps BLE device name, GATT services, characteristics, and hardware MAC address on mode change.

---

### 🎨 WS2812B RGB Status LED (GPIO 8)
- **Advertising (Waiting for Client)**:
  - 🔴 **Red**: JBD BMS mode
  - 🟣 **Magenta**: Daly Smart BMS mode
  - 🔵 **Blue**: Ant BMS mode
  - 🟠 **Orange**: JKBMS mode
- **Connected**:
  - 🟢 **Green**: Standby (0.00 A / Idle).
  - 🩵 **Cyan**: Active Charge (+25.00 A).
  - 🔵 / 🟠: Active Discharge (-17.00 A).
  - ⚪ **White**: Full Charge (100% SOC).
  - 🟡 **Yellow**: Alarm / Protection triggered.

---

## 🛠 Repository Structure

```
.
├── README.md                      # English documentation
├── README_UA.md                   # Ukrainian documentation
├── PROJECT_CAPABILITIES.md        # Technical capabilities specification (English)
├── PROJECT_CAPABILITIES_UA.md     # Технічний опис можливостей проєкту (Українська)
├── Daly.MD                        # Daly Modbus register reference & specification
├── JBD_PROTOCOL_SPEC.md           # JBD binary protocol specification
├── JBD_REGISTER_MAP.md            # JBD EEPROM register map
├── 4s standard LiFePO4 Alf-20210608.fig   # 4S (12V) LiFePO4 configuration profile
├── 8s standard LiFePO4 Alf-20210608.fig   # 8S (24V) LiFePO4 configuration profile
├── 16s standard LiFePO4 Alf-20210608.fig  # 16S (48V) LiFePO4 configuration profile
├── platformio.ini                 # PlatformIO configuration (ESP32-C6 / ESP32-S3)
├── switch_mode.sh                 # Mode switcher script (JBD / Daly)
├── include/
│   └── WebDashboard.h             # Embedded Web Management Dashboard (HTML/CSS/JS)
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

- `mac` / `macs`: Open the BLE Adapter MAC address management menu and list stored profiles.
- `mac <1-N>`: Select stored MAC profile by index (e.g. `mac 2`).
- `mac set <MAC>`: Set and activate a MAC address manually (e.g. `mac set a5:c2:37:2d:5c:a9`).
- `<MAC>`: Direct entry of any MAC address (e.g. `a5:c2:37:2d:5c:a9`).
- `mac add <MAC> [label]`: Add a new MAC address to the profile list without rebooting.
- `mac del <1-N>`: Delete a MAC profile from the stored list.
- `mac reset`: Reset stored MAC profiles to factory defaults.
- `m` or `M`: Toggle protocol (**JBD** $\leftrightarrow$ **DALY**).
- `j` or `jbd`: Force **JBD BMS** mode (current profile).
- `pq` or `powerqueen`: Switch to **PowerQueen JBD** profile (`P-12100B` / `P-24100B`).
- `os` or `overkill`: Switch to **Overkill Solar JBD** profile (`Overkill Solar BMS`).
- `std` or `xiaoxiang`: Switch to **Standard JBD / Xiaoxiang** profile (`BS-26A`).
- `y` or `daly`: Force **DALY BMS** mode.
- `a` or `ant`: Force **ANT BMS** mode.
- `k` or `jk`: Force **JKBMS** mode.
- `r` or `redodo`: Force **Redodo / LiTime BMS** mode.
- `m` or `toggle`: Cycle through all 5 BMS emulator modes.
- `4s` / `8s` / `16s`: Set battery series count (or `cells <N>`, 3..32).
- `1` or `dis`: Active discharge mode ($-17.00\text{ A}$) $\to$ 🟠 Orange LED.
- `2` or `chg`: Active charge mode ($+25.00\text{ A}$) $\to$ 🩵 Cyan LED.
- `0` or `idle`: Standby mode ($0.00\text{ A}$) $\to$ 🟢 Green LED.
- `f` or `fet`: Toggle Charge/Discharge FET switches.
- `+` / `-`: Adjust SOC by $\pm 1\%$.
- `t` or `telemetry`: Send telemetry burst over BLE.
- `?` or `help`: Print command help.

---

## 📱 Verified Client Compatibility

1. **JBD BMS / Xiaoxiang / PowerQueen / Overkill Solar Apps** (iOS & Android) — Full telemetry, EEPROM configuration, PIN authentication (`123456`), and individual cell monitoring in JBD mode across all 3 sub-profiles (`BS-26A-072-005`, `P-12100B`, `Overkill Solar 8S`). Native compatibility with the official **PowerQueen BMS App** and **Overkill Solar App**.
2. **Smart BMS App** (iOS & Android) — Official Daly app with full 8/16 cells, NTCs, Modbus D2 telemetry, hardware SN, firmware version, and FET switch controls in Daly mode (`DL-401804015491`).
3. **Ant BMS App / MAYA / AntBMS v2** (iOS & Android) — 100% verified compatibility with modern `7E A1` protocol and legacy 140-byte telemetry. Full cell matrix, SOC ring gauge, temperatures (MOS, balancer, T1–T4), capacity (193.6/200Ah), and FET control in Ant mode (`ANT-BMS-08S`, `ANT-BMS-16S`).
4. **JKBMS App (JiKong App)** (iOS & Android) — Official JiKong app tested and verified with complete 300-byte JK02 frames. Individual cell voltages (mV), wire resistance ($0.100\,\Omega$), active balancer telemetry, accurate SOC/current, and passcode unlock (`123456` or `1234`) in JK mode (`JK-B2A8S20P`, `JK-B2A16S20P`).
5. **Redodo / LiTime Monolithic BMS & Web Bluetooth** (Chrome / Web Bluetooth & native apps) — Full 105-byte frame telemetry via Service `0xFFE0` / Characteristic `0xFFE1`, verified with HTML5 Web Bluetooth dashboard (`redodo_monitor.html`), cell voltages 1..16, pack terminal and sum voltages, signed current, 3 temperature probes, remaining/full capacity, and SOC (`Redodo-12V100A`, `Redodo-24V100A`, `Redodo-48V100A`).
6. **Universal BMS Diagnostic Monitor** (ESP32-S3 Probe via Tailscale) — Seamless automatic protocol detection and telemetry streaming across all 5 BMS standards (JBD, Daly, Ant, JK, Redodo).
7. **ESPHome / Home Assistant** — Native integration via standard components: `jbd_bms`, `daly_bms`, `ant_bms`, and `jk_bms_ble`.
