#pragma once

#include <Arduino.h>

// Favicon SVG stored in PROGMEM (Gradient Battery with Lightning Badge)
static const char FAVICON_SVG[] PROGMEM = 
  "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 100 100'>"
  "<defs><linearGradient id='g' x1='0%' y1='0%' x2='100%' y2='100%'>"
  "<stop offset='0%' stop-color='#00d2ff'/><stop offset='100%' stop-color='#00e676'/>"
  "</linearGradient></defs>"
  "<rect width='100' height='100' rx='22' fill='#0d1117'/>"
  "<rect x='18' y='28' width='64' height='54' rx='8' fill='none' stroke='url(#g)' stroke-width='6'/>"
  "<rect x='40' y='18' width='20' height='8' rx='3' fill='url(#g)'/>"
  "<path d='M52 38 L38 56 L49 56 L47 72 L62 52 L51 52 Z' fill='#00e676'/>"
  "</svg>";

// Main Dashboard HTML Template (PROGMEM)
static const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>BMS Smart Emulator Web Dashboard</title>
  <link rel="icon" type="image/svg+xml" href="/favicon.svg">
  <style>
    :root {
      --bg: #0b0f14;
      --card-bg: rgba(18, 24, 33, 0.88);
      --card-border: rgba(56, 139, 253, 0.22);
      --text: #f0f6fc;
      --text-mut: #8b949e;
      --accent-blue: #58a6ff;
      --accent-green: #3fb950;
      --accent-orange: #d29922;
      --accent-red: #f85149;
      --accent-cyan: #39c5bb;
      --accent-purple: #bc8cff;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, Helvetica, Arial, sans-serif; }
    body { background: var(--bg); color: var(--text); min-height: 100vh; padding: 16px; background-attachment: fixed; }
    .container { max-width: 960px; margin: 0 auto; display: flex; flex-direction: column; gap: 16px; }

    /* Header */
    .header {
      display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 12px;
      background: var(--card-bg); border: 1px solid var(--card-border);
      border-radius: 16px; padding: 16px 20px; backdrop-filter: blur(12px);
      box-shadow: 0 8px 24px rgba(0,0,0,0.4);
    }
    .header-left { display: flex; align-items: center; gap: 14px; }
    .logo-box {
      width: 44px; height: 44px; border-radius: 12px;
      background: linear-gradient(135deg, #1f6feb, #238636);
      display: flex; align-items: center; justify-content: center;
      font-size: 22px; box-shadow: 0 0 16px rgba(35, 134, 54, 0.4);
    }
    .header-titles h1 { font-size: 1.25rem; font-weight: 700; display: flex; align-items: center; gap: 8px; }
    .header-titles p { font-size: 0.8rem; color: var(--text-mut); }
    .header-right { display: flex; align-items: center; gap: 10px; flex-wrap: wrap; }
    
    .badge {
      display: inline-flex; align-items: center; gap: 6px; font-size: 0.8rem;
      padding: 6px 12px; border-radius: 20px; background: rgba(255,255,255,0.06);
      border: 1px solid rgba(255,255,255,0.1); font-weight: 500;
    }
    .badge-proto {
      background: rgba(88, 166, 255, 0.15); border-color: rgba(88, 166, 255, 0.35);
      color: var(--accent-blue); font-weight: 700; text-transform: uppercase;
    }
    .badge-cells {
      background: rgba(63, 185, 80, 0.15); border-color: rgba(63, 185, 80, 0.35);
      color: var(--accent-green); font-weight: 700;
    }
    .dot { width: 9px; height: 9px; border-radius: 50%; background: var(--accent-red); transition: all 0.3s; }
    .dot.online { background: var(--accent-green); box-shadow: 0 0 8px var(--accent-green); }

    .btn {
      background: rgba(255,255,255,0.07); border: 1px solid rgba(255,255,255,0.15);
      color: var(--text); padding: 7px 14px; border-radius: 8px; font-size: 0.85rem;
      font-weight: 600; cursor: pointer; text-decoration: none; display: inline-flex;
      align-items: center; gap: 6px; transition: 0.2s;
    }
    .btn:hover { background: rgba(255,255,255,0.15); border-color: var(--accent-blue); }
    .btn-active { background: rgba(56,139,253,0.25); border-color: var(--accent-blue); color: #fff; }

    /* KPI Grid */
    .kpi-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(180px, 1fr)); gap: 12px; }
    .kpi-card {
      background: var(--card-bg); border: 1px solid var(--card-border);
      border-radius: 14px; padding: 16px; display: flex; flex-direction: column; gap: 6px;
      backdrop-filter: blur(10px); position: relative; overflow: hidden;
    }
    .kpi-label { font-size: 0.75rem; color: var(--text-mut); text-transform: uppercase; letter-spacing: 0.5px; font-weight: 600; }
    .kpi-val { font-size: 1.7rem; font-weight: 800; }
    .val-green { color: var(--accent-green); }
    .val-blue { color: var(--accent-blue); }
    .val-orange { color: var(--accent-orange); }
    .val-cyan { color: var(--accent-cyan); }
    .val-purple { color: var(--accent-purple); }
    .kpi-sub { font-size: 0.78rem; color: var(--text-mut); }

    /* Control Panel Card */
    .card {
      background: var(--card-bg); border: 1px solid var(--card-border);
      border-radius: 16px; padding: 20px; backdrop-filter: blur(12px);
      display: flex; flex-direction: column; gap: 18px;
    }
    .card-title {
      font-size: 1.1rem; font-weight: 700; display: flex;
      align-items: center; justify-content: space-between; gap: 8px;
      border-bottom: 1px solid rgba(255,255,255,0.06); padding-bottom: 12px;
    }
    .controls-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 16px; }
    .control-group {
      background: rgba(0,0,0,0.25); border: 1px solid rgba(255,255,255,0.06);
      border-radius: 12px; padding: 14px; display: flex; flex-direction: column; gap: 10px;
    }
    .control-label { font-size: 0.85rem; font-weight: 600; color: var(--text-mut); display: flex; justify-content: space-between; }
    .btn-row { display: flex; gap: 8px; flex-wrap: wrap; }
    .btn-toggle {
      flex: 1; min-width: 75px; padding: 10px; border-radius: 8px;
      font-size: 0.85rem; font-weight: 700; text-align: center; cursor: pointer;
      border: 1px solid rgba(255,255,255,0.12); background: rgba(255,255,255,0.04);
      color: var(--text); transition: all 0.2s;
    }
    .btn-toggle:hover { border-color: var(--accent-blue); background: rgba(56,139,253,0.1); }
    .btn-toggle.active {
      background: linear-gradient(135deg, rgba(31,111,235,0.4), rgba(35,134,54,0.4));
      border-color: var(--accent-green); color: #fff; box-shadow: 0 0 12px rgba(63,185,80,0.3);
    }
    .btn-proto.active-jbd {
      background: rgba(88,166,255,0.25); border-color: var(--accent-blue); color: var(--accent-blue);
    }
    .btn-proto.active-daly {
      background: rgba(188,140,255,0.25); border-color: var(--accent-purple); color: var(--accent-purple);
    }
    .btn-proto.active-ant {
      background: rgba(57,197,187,0.25); border-color: var(--accent-cyan); color: var(--accent-cyan);
    }
    .btn-proto.active-jk {
      background: rgba(240,136,62,0.25); border-color: var(--accent-orange); color: var(--accent-orange);
    }

    /* Slider row */
    .slider-row { display: flex; align-items: center; gap: 12px; }
    .slider {
      flex: 1; -webkit-appearance: none; height: 8px; border-radius: 4px;
      background: rgba(255,255,255,0.15); outline: none; transition: background 0.2s;
    }
    .slider::-webkit-slider-thumb {
      -webkit-appearance: none; appearance: none; width: 20px; height: 20px;
      border-radius: 50%; background: var(--accent-green); cursor: pointer;
      box-shadow: 0 0 8px var(--accent-green);
    }
    .slider-val { font-size: 1.1rem; font-weight: 800; min-width: 48px; text-align: right; }

    /* Switches */
    .switch-row { display: flex; justify-content: space-between; align-items: center; padding: 4px 0; }
    .switch {
      position: relative; display: inline-block; width: 44px; height: 24px;
    }
    .switch input { opacity: 0; width: 0; height: 0; }
    .slider-sw {
      position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0;
      background-color: rgba(255,255,255,0.2); transition: .3s; border-radius: 24px;
    }
    .slider-sw:before {
      position: absolute; content: ""; height: 18px; width: 18px; left: 3px; bottom: 3px;
      background-color: white; transition: .3s; border-radius: 50%;
    }
    input:checked + .slider-sw { background-color: var(--accent-green); }
    input:checked + .slider-sw:before { transform: translateX(20px); }

    /* Cells Grid */
    .cells-grid {
      display: grid; grid-template-columns: repeat(auto-fill, minmax(130px, 1fr)); gap: 10px;
    }
    .cell-box {
      background: rgba(0,0,0,0.35); border: 1px solid rgba(255,255,255,0.08);
      border-radius: 12px; padding: 12px 10px; text-align: center;
      position: relative; overflow: hidden; transition: 0.2s;
    }
    .cell-box:hover { border-color: rgba(255,255,255,0.2); transform: translateY(-2px); }
    .cell-box.min-cell {
      border-color: var(--accent-cyan); box-shadow: inset 0 0 10px rgba(57,197,187,0.25);
    }
    .cell-box.max-cell {
      border-color: var(--accent-green); box-shadow: inset 0 0 10px rgba(63,185,80,0.25);
    }
    .cell-tag {
      position: absolute; top: 4px; right: 6px; font-size: 0.6rem; font-weight: 800;
      padding: 2px 4px; border-radius: 4px; text-transform: uppercase;
    }
    .tag-min { background: rgba(57,197,187,0.25); color: var(--accent-cyan); }
    .tag-max { background: rgba(63,185,80,0.25); color: var(--accent-green); }
    .cell-num { font-size: 0.72rem; color: var(--text-mut); margin-bottom: 2px; font-weight: 600; }
    .cell-mv { font-size: 1.25rem; font-weight: 800; color: #fff; margin-bottom: 6px; }
    .cell-bar-bg { width: 100%; height: 5px; background: rgba(255,255,255,0.08); border-radius: 3px; overflow: hidden; }
    .cell-bar-fill { height: 100%; width: 50%; background: var(--accent-green); transition: width 0.3s ease; }

    /* Footer Info */
    .footer-bar {
      display: flex; justify-content: space-between; align-items: center; flex-wrap: wrap; gap: 10px;
      font-size: 0.8rem; color: var(--text-mut); padding: 8px 12px;
    }
  </style>
</head>
<body>
  <div class="container">
    <!-- Header -->
    <header class="header">
      <div class="header-left">
        <div class="logo-box">⚡</div>
        <div class="header-titles">
          <h1 id="app-title">BMS Smart Emulator <span id="proto-badge" class="badge badge-proto">JBD</span></h1>
          <p id="app-subtitle">ESP32-C6 / ESP32-S3 Dual-Protocol Hardware Emulator</p>
        </div>
      </div>
      <div class="header-right">
        <div class="badge"><span id="live-dot" class="dot online"></span> <span id="live-text">ONLINE</span></div>
        <span id="cells-badge" class="badge badge-cells">8S (24V)</span>
        <span id="mac-badge" class="badge" style="font-family:monospace; color:var(--accent-cyan); border-color:rgba(57,197,187,0.35); font-weight:700;">MAC: ...</span>
        <button id="lang-btn" class="btn" onclick="toggleLang()">🇬🇧 EN</button>
        <a href="/setup" class="btn">⚙️ <span data-i18n="settings">Налаштування</span></a>
      </div>
    </header>

    <!-- KPI Grid -->
    <div class="kpi-grid">
      <div class="kpi-card">
        <div class="kpi-label" data-i18n="pack_volts">Напруга батареї</div>
        <div id="kpi-volts" class="kpi-val val-green">26.24 V</div>
        <div id="kpi-volts-sub" class="kpi-sub">8S LiFePO4 · ~3.28V/cell</div>
      </div>
      <div class="kpi-card">
        <div class="kpi-label" data-i18n="current">Струм (I)</div>
        <div id="kpi-current" class="kpi-val val-orange">-17.02 A</div>
        <div id="kpi-power" class="kpi-sub">Потужність: 446.4 W</div>
      </div>
      <div class="kpi-card">
        <div class="kpi-label" data-i18n="soc">Заряд (SOC)</div>
        <div id="kpi-soc" class="kpi-val val-cyan">96 %</div>
        <div id="kpi-capacity" class="kpi-sub">192.8 / 200.0 Ah</div>
      </div>
      <div class="kpi-card">
        <div class="kpi-label" data-i18n="temperatures">Температури</div>
        <div id="kpi-temps" class="kpi-val val-purple">26.0 °C</div>
        <div id="kpi-temps-sub" class="kpi-sub">NTC2: 25.5° | MOS: 28.0°</div>
      </div>
    </div>

    <!-- Interactive Control Panel -->
    <div class="card">
      <div class="card-title">
        <span>🎮 <span data-i18n="control_panel">Панель керування емулятором</span></span>
        <span id="ble-status" class="badge" style="font-size:0.75rem;">BLE: ADVERTISING</span>
      </div>

      <div class="controls-grid">
        <!-- Series Configuration -->
        <div class="control-group">
          <div class="control-label">
            <span data-i18n="cell_config">Конфігурація батареї (Кількість комірок)</span>
            <span id="label-cells-curr">8S (24V)</span>
          </div>
          <div class="btn-row">
            <button class="btn-toggle" onclick="setCells(4)">4S (12V)</button>
            <button class="btn-toggle" onclick="setCells(8)">8S (24V)</button>
            <button class="btn-toggle" onclick="setCells(16)">16S (48V)</button>
          </div>
          <div style="display:flex; align-items:center; gap:8px; margin-top:4px;">
            <span style="font-size:0.8rem; color:var(--text-mut);" data-i18n="custom_s">Довільна (3..32S):</span>
            <select id="select-custom-cells" onchange="setCells(this.value)" style="flex:1; background:#161b22; color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:6px; font-weight:700;">
              <!-- 3 to 32 options populated in JS -->
            </select>
          </div>
        </div>

        <!-- Mode & Protocol Switcher -->
        <div class="control-group">
          <div class="control-label">
            <span data-i18n="protocol_select">Протокол емуляції BMS</span>
            <span id="label-proto-name">JBD / Xiaoxiang</span>
          </div>
          <div class="btn-row">
            <button id="btn-proto-jbd" class="btn-toggle btn-proto active-jbd" onclick="setProto('jbd')">JBD BMS (BS-26A)</button>
            <button id="btn-proto-daly" class="btn-toggle btn-proto" onclick="setProto('daly')">Daly BMS (DL-4018)</button>
            <button id="btn-proto-ant" class="btn-toggle btn-proto" onclick="setProto('ant')">Ant BMS (ANT-BMS)</button>
            <button id="btn-proto-jk" class="btn-toggle btn-proto" onclick="setProto('jk')">JKBMS (JK-B2A)</button>
          </div>
        </div>

        <!-- Load simulation -->
        <div class="control-group">
          <div class="control-label">
            <span data-i18n="sim_mode">Режим навантаження</span>
            <span id="label-sim-mode">Розряд (-17A)</span>
          </div>
          <div class="btn-row">
            <button id="btn-mode-dis" class="btn-toggle active" onclick="setMode('dis')">⚡ <span data-i18n="discharge">Розряд</span> (-17A)</button>
            <button id="btn-mode-chg" class="btn-toggle" onclick="setMode('chg')">🔌 <span data-i18n="charge">Заряд</span> (+25A)</button>
            <button id="btn-mode-idle" class="btn-toggle" onclick="setMode('idle')">⏸️ <span data-i18n="standby">Спокій</span> (0A)</button>
          </div>
        </div>

        <!-- Switches & SOC -->
        <div class="control-group">
          <div class="control-label">
            <span data-i18n="switches_soc">Ключі захисту та рівень SOC</span>
          </div>
          <div class="switch-row">
            <span data-i18n="chg_mos">Charge MOSFET:</span>
            <label class="switch"><input type="checkbox" id="sw-chg" onchange="toggleFet('charge', this.checked)"><span class="slider-sw"></span></label>
          </div>
          <div class="switch-row">
            <span data-i18n="dsg_mos">Discharge MOSFET:</span>
            <label class="switch"><input type="checkbox" id="sw-dsg" onchange="toggleFet('discharge', this.checked)"><span class="slider-sw"></span></label>
          </div>
          <div class="slider-row" style="margin-top:6px;">
            <span style="font-size:0.8rem; color:var(--text-mut);">SOC:</span>
            <input type="range" id="soc-slider" min="0" max="100" class="slider" onchange="setSoc(this.value)" oninput="document.getElementById('soc-slider-val').textContent = this.value + '%'">
            <span id="soc-slider-val" class="slider-val">96%</span>
          </div>
        </div>

        <!-- BLE Adapter MAC Address Selector -->
        <div class="control-group">
          <div class="control-label">
            <span data-i18n="mac_management">📡 BLE MAC-адреса адаптера</span>
            <span id="label-active-mac" style="font-family:monospace; color:var(--accent-cyan); font-weight:700;">...</span>
          </div>
          <div style="display:flex; gap:8px; align-items:center;">
            <select id="select-mac-profile" style="flex:1; background:#161b22; color:#fff; border:1px solid rgba(255,255,255,0.15); border-radius:6px; padding:7px; font-weight:600; font-family:monospace; font-size:0.85rem;">
            </select>
            <button class="btn btn-active" onclick="switchMacFromSelect()" style="padding:7px 12px; white-space:nowrap;" data-i18n="apply_btn">⚡ Застосувати</button>
          </div>
          <div style="display:flex; justify-content:space-between; align-items:center; font-size:0.75rem; color:var(--text-mut);">
            <span data-i18n="mac_hint">Перезавантажує ESP32 із вибраним MAC</span>
            <a href="/setup" style="color:var(--accent-blue); text-decoration:none;" data-i18n="manage_mac">Всі налаштування ⚙️</a>
          </div>
        </div>
      </div>
    </div>

    <!-- Cell Voltages Matrix -->
    <div class="card">
      <div class="card-title">
        <span>🔋 <span data-i18n="cell_matrix">Напруги комірок акумулятора</span> (<span id="cells-count-title">8S</span>)</span>
        <div style="font-size:0.85rem; font-weight:600; color:var(--text-mut);">
          <span data-i18n="delta">Дельта</span>: <span id="delta-val" style="color:var(--accent-green); font-weight:800;">7 mV</span>
        </div>
      </div>
      <div id="cells-grid" class="cells-grid">
        <!-- Rendered dynamically -->
      </div>
    </div>

    <!-- Footer System Info -->
    <div class="footer-bar">
      <div>📶 Wi-Fi: <span id="sys-wifi">STA (192.168.1.100)</span> · RSSI: <span id="sys-rssi">-60 dBm</span></div>
      <div>💾 Free RAM: <span id="sys-heap">180 KB</span> · Uptime: <span id="sys-uptime">0h 15m</span></div>
    </div>
  </div>

  <script>
    const i18n = {
      uk: {
        settings: "Налаштування",
        pack_volts: "Напруга батареї",
        current: "Струм (I)",
        soc: "Заряд (SOC)",
        temperatures: "Температури",
        control_panel: "Панель керування емулятором",
        cell_config: "Конфігурація батареї (Кількість комірок)",
        custom_s: "Довільна (3..32S):",
        protocol_select: "Протокол емуляції BMS",
        sim_mode: "Режим навантаження",
        discharge: "Розряд",
        charge: "Заряд",
        standby: "Спокій",
        switches_soc: "Ключі захисту та рівень SOC",
        chg_mos: "Charge MOSFET (Заряд):",
        dsg_mos: "Discharge MOSFET (Розряд):",
        cell_matrix: "Напруги комірок акумулятора",
        delta: "Дельта розбалансу",
        cell: "Комірка",
        mac_management: "📡 BLE MAC-адреса адаптера",
        apply_btn: "⚡ Застосувати",
        mac_hint: "Перезавантажує ESP32 із вибраним MAC",
        manage_mac: "Всі налаштування ⚙️"
      },
      en: {
        settings: "Settings",
        pack_volts: "Battery Voltage",
        current: "Current (I)",
        soc: "State of Charge (SOC)",
        temperatures: "Temperatures",
        control_panel: "Emulator Control Panel",
        cell_config: "Battery Configuration (Cell Series)",
        custom_s: "Custom (3..32S):",
        protocol_select: "BMS Protocol Engine",
        sim_mode: "Load Simulation Mode",
        discharge: "Discharge",
        charge: "Charge",
        standby: "Standby",
        switches_soc: "Protection Switches & SOC",
        chg_mos: "Charge MOSFET:",
        dsg_mos: "Discharge MOSFET:",
        cell_matrix: "Individual Cell Voltages",
        delta: "Cell Delta",
        cell: "Cell",
        mac_management: "📡 BLE Adapter MAC Address",
        apply_btn: "⚡ Apply",
        mac_hint: "Reboots ESP32 with selected MAC",
        manage_mac: "All Settings ⚙️"
      }
    };

    let curLang = localStorage.getItem('bms_lang') || 'uk';
    function toggleLang() {
      curLang = (curLang === 'uk') ? 'en' : 'uk';
      localStorage.setItem('bms_lang', curLang);
      applyLang();
    }

    function applyLang() {
      document.getElementById('lang-btn').textContent = (curLang === 'uk') ? '🇬🇧 EN' : '🇺🇦 UA';
      document.querySelectorAll('[data-i18n]').forEach(el => {
        const k = el.getAttribute('data-i18n');
        if (i18n[curLang][k]) el.textContent = i18n[curLang][k];
      });
    }

    // Populate custom cell options 3..32
    const sel = document.getElementById('select-custom-cells');
    for (let i = 3; i <= 32; i++) {
      const opt = document.createElement('option');
      opt.value = i;
      opt.textContent = `${i}S (${(i * 3.2).toFixed(1)}V nominal)`;
      sel.appendChild(opt);
    }

    let isUpdating = false;

    async function fetchData() {
      try {
        const res = await fetch('/api/data');
        if (!res.ok) throw new Error();
        const data = await res.json();
        updateUI(data);
        document.getElementById('live-dot').className = 'dot online';
        document.getElementById('live-text').textContent = 'ONLINE';
      } catch (e) {
        document.getElementById('live-dot').className = 'dot';
        document.getElementById('live-text').textContent = 'OFFLINE';
      }
    }

    function updateUI(d) {
      // Protocol badges
      const pBadge = document.getElementById('proto-badge');
      pBadge.textContent = d.proto;
      ['jbd', 'daly', 'ant', 'jk'].forEach(p => {
        const b = document.getElementById('btn-proto-' + p);
        if (b) b.className = 'btn-toggle btn-proto';
      });

      if (d.proto === 'DALY') {
        pBadge.className = 'badge badge-proto active-daly';
        const b = document.getElementById('btn-proto-daly'); if (b) b.className = 'btn-toggle btn-proto active-daly';
        document.getElementById('label-proto-name').textContent = 'Daly Smart BMS (DL-4018)';
      } else if (d.proto === 'ANT') {
        pBadge.className = 'badge badge-proto active-ant';
        const b = document.getElementById('btn-proto-ant'); if (b) b.className = 'btn-toggle btn-proto active-ant';
        document.getElementById('label-proto-name').textContent = 'Ant BMS (ANT-BMS)';
      } else if (d.proto === 'JK') {
        pBadge.className = 'badge badge-proto active-jk';
        const b = document.getElementById('btn-proto-jk'); if (b) b.className = 'btn-toggle btn-proto active-jk';
        document.getElementById('label-proto-name').textContent = 'JKBMS (JK-B2A)';
      } else {
        pBadge.className = 'badge badge-proto active-jbd';
        const b = document.getElementById('btn-proto-jbd'); if (b) b.className = 'btn-toggle btn-proto active-jbd';
        document.getElementById('label-proto-name').textContent = 'JBD BMS (BS-26A)';
      }

      // Cells badge
      const nominalV = (d.cell_count * 3.2).toFixed(1);
      document.getElementById('cells-badge').textContent = `${d.cell_count}S (${nominalV}V)`;
      document.getElementById('label-cells-curr').textContent = `${d.cell_count}S (${nominalV}V)`;
      document.getElementById('cells-count-title').textContent = `${d.cell_count}S`;
      sel.value = d.cell_count;

      // KPIs
      document.getElementById('kpi-volts').textContent = d.pack_voltage.toFixed(2) + ' V';
      document.getElementById('kpi-volts-sub').textContent = `${d.cell_count}S LiFePO4 · avg ${(d.pack_voltage / (d.cell_count || 1)).toFixed(3)}V`;

      const currEl = document.getElementById('kpi-current');
      currEl.textContent = (d.current >= 0 ? '+' : '') + d.current.toFixed(2) + ' A';
      if (d.current > 0.5) currEl.className = 'kpi-val val-green';
      else if (d.current < -0.5) currEl.className = 'kpi-val val-orange';
      else currEl.className = 'kpi-val';
      document.getElementById('kpi-power').textContent = `${(curLang==='uk'?'Потужність:':'Power:')} ${Math.abs(d.power).toFixed(1)} W`;

      document.getElementById('kpi-soc').textContent = d.soc + ' %';
      document.getElementById('kpi-capacity').textContent = `${d.remain_ah.toFixed(1)} / ${d.nominal_ah.toFixed(1)} Ah`;

      document.getElementById('kpi-temps').textContent = d.t1.toFixed(1) + ' °C';
      document.getElementById('kpi-temps-sub').textContent = `NTC2: ${d.t2.toFixed(1)}° · MOS: ${d.t_mos.toFixed(1)}°`;

      // Sim mode buttons
      document.getElementById('btn-mode-dis').className = (d.mode === 'discharge') ? 'btn-toggle active' : 'btn-toggle';
      document.getElementById('btn-mode-chg').className = (d.mode === 'charge') ? 'btn-toggle active' : 'btn-toggle';
      document.getElementById('btn-mode-idle').className = (d.mode === 'idle') ? 'btn-toggle active' : 'btn-toggle';
      document.getElementById('label-sim-mode').textContent = 
        (d.mode === 'discharge') ? (curLang==='uk'?'Розряд (-17A)':'Discharge (-17A)') :
        (d.mode === 'charge') ? (curLang==='uk'?'Заряд (+25A)':'Charge (+25A)') : (curLang==='uk'?'Спокій (0A)':'Standby (0A)');

      // Switches & slider
      if (!isUpdating) {
        document.getElementById('sw-chg').checked = d.fet_charge;
        document.getElementById('sw-dsg').checked = d.fet_discharge;
        document.getElementById('soc-slider').value = d.soc;
        document.getElementById('soc-slider-val').textContent = d.soc + '%';
      }

      // Delta
      document.getElementById('delta-val').textContent = d.delta_mv + ' mV';

      // Cell voltages matrix
      renderCellsGrid(d);

      // System info
      document.getElementById('sys-wifi').textContent = `${d.wifi_mode} (${d.ip})`;
      document.getElementById('sys-rssi').textContent = d.rssi ? (d.rssi + ' dBm') : 'N/A';
      document.getElementById('sys-heap').textContent = Math.round(d.free_heap / 1024) + ' KB';
      const m = Math.floor(d.uptime_sec / 60);
      const h = Math.floor(m / 60);
      document.getElementById('sys-uptime').textContent = `${h}h ${m % 60}m`;

      // MAC Address & Profiles
      if (d.mac_active) {
        const mb = document.getElementById('mac-badge');
        if (mb) mb.textContent = 'MAC: ' + d.mac_active;
        const ml = document.getElementById('label-active-mac');
        if (ml) ml.textContent = d.mac_active;

        const selMac = document.getElementById('select-mac-profile');
        if (selMac && d.mac_profiles) {
          const profHash = JSON.stringify(d.mac_profiles);
          if (selMac.dataset.hash !== profHash) {
            selMac.innerHTML = '';
            d.mac_profiles.forEach(p => {
              const opt = document.createElement('option');
              opt.value = p.index;
              opt.textContent = `[${p.index + 1}] ${p.label} (${p.mac})` + (p.active ? ' ★' : '');
              if (p.active) opt.selected = true;
              selMac.appendChild(opt);
            });
            selMac.dataset.hash = profHash;
          }
        }
      }
    }

    function renderCellsGrid(d) {
      const grid = document.getElementById('cells-grid');
      const cells = d.cells || [];
      grid.innerHTML = '';
      cells.forEach((mv, idx) => {
        const cNum = idx + 1;
        const box = document.createElement('div');
        box.className = 'cell-box';
        const isMin = (idx === d.min_cell_idx);
        const isMax = (idx === d.max_cell_idx);
        if (isMin) box.classList.add('min-cell');
        if (isMax) box.classList.add('max-cell');

        let tagHtml = '';
        if (isMin) tagHtml = '<span class="cell-tag tag-min">MIN</span>';
        else if (isMax) tagHtml = '<span class="cell-tag tag-max">MAX</span>';

        const pct = Math.min(100, Math.max(0, ((mv - 2800) / (3650 - 2800)) * 100));
        const cellWord = (curLang === 'uk') ? 'Комірка' : 'Cell';

        box.innerHTML = `
          ${tagHtml}
          <div class="cell-num">${cellWord} #${cNum}</div>
          <div class="cell-mv">${(mv / 1000).toFixed(3)} <span style="font-size:0.75rem; color:var(--text-mut);">V</span></div>
          <div class="cell-bar-bg"><div class="cell-bar-fill" style="width:${pct}%;"></div></div>
        `;
        grid.appendChild(box);
      });
    }

    // API Post actions
    async function setCells(num) {
      await fetch('/api/set-cells', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ cells: parseInt(num) })
      });
      fetchData();
    }

    async function setProto(proto) {
      await fetch('/api/set-proto', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ proto: proto })
      });
      setTimeout(fetchData, 1000);
    }

    async function setMode(mode) {
      await fetch('/api/set-mode', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ mode: mode })
      });
      fetchData();
    }

    async function toggleFet(fetType, state) {
      isUpdating = true;
      await fetch('/api/set-fet', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ fet: fetType, state: state })
      });
      setTimeout(() => { isUpdating = false; fetchData(); }, 500);
    }

    async function setSoc(socVal) {
      isUpdating = true;
      await fetch('/api/set-soc', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ soc: parseInt(socVal) })
      });
      setTimeout(() => { isUpdating = false; fetchData(); }, 500);
    }

    async function switchMacFromSelect() {
      const selMac = document.getElementById('select-mac-profile');
      const idx = parseInt(selMac.value);
      if (isNaN(idx)) return;
      const text = selMac.options[selMac.selectedIndex].text;
      const msg = (curLang === 'uk') ? `Перемкнути MAC на ${text} та перезавантажити емулятор?` : `Switch MAC to ${text} and reboot emulator?`;
      if (!confirm(msg)) return;
      try {
        await fetch('/api/mac/select', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ index: idx })
        });
        alert(curLang === 'uk' ? 'MAC змінено! Емулятор перезавантажується...' : 'MAC updated! Emulator is rebooting...');
        setTimeout(() => { location.reload(); }, 3500);
      } catch (e) {
        alert('Помилка перемикання MAC!');
      }
    }

    applyLang();
    fetchData();
    setInterval(fetchData, 1500);
  </script>
</body>
</html>
)rawliteral";

// Setup / Wi-Fi Configuration HTML Template (PROGMEM)
static const char SETUP_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="uk">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>BMS Emulator Wi-Fi Setup</title>
  <link rel="icon" type="image/svg+xml" href="/favicon.svg">
  <style>
    :root {
      --bg: #0b0f14; --card-bg: #121821; --border: rgba(56, 139, 253, 0.25);
      --text: #f0f6fc; --text-mut: #8b949e; --accent: #58a6ff; --green: #3fb950; --red: #f85149;
    }
    * { box-sizing: border-box; margin: 0; padding: 0; font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif; }
    body { background: var(--bg); color: var(--text); padding: 20px; display: flex; justify-content: center; align-items: flex-start; min-height: 100vh; }
    .card {
      background: var(--card-bg); border: 1px solid var(--border); border-radius: 16px;
      max-width: 520px; width: 100%; padding: 24px; box-shadow: 0 12px 36px rgba(0,0,0,0.5);
    }
    .header-bar { display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; }
    h2 { font-size: 1.3rem; font-weight: 700; display: flex; align-items: center; gap: 10px; }
    p.sub { font-size: 0.85rem; color: var(--text-mut); margin-bottom: 20px; line-height: 1.4; }
    .form-group { margin-bottom: 16px; }
    label { display: block; font-size: 0.85rem; font-weight: 600; margin-bottom: 6px; color: var(--text); }
    input[type="text"], input[type="password"] {
      width: 100%; background: #0b0f14; border: 1px solid rgba(255,255,255,0.15); border-radius: 8px;
      padding: 10px 14px; color: #fff; font-size: 0.95rem; outline: none; transition: 0.2s;
    }
    input:focus { border-color: var(--accent); }
    .btn {
      background: rgba(255,255,255,0.08); border: 1px solid rgba(255,255,255,0.15); color: #fff;
      padding: 10px 14px; border-radius: 8px; cursor: pointer; font-size: 0.9rem; font-weight: 600;
      display: inline-flex; align-items: center; justify-content: center; gap: 6px; transition: 0.2s; text-decoration: none;
    }
    .btn:hover { background: rgba(255,255,255,0.16); border-color: var(--accent); }
    .btn-primary { background: #238636; border-color: rgba(240,246,252,0.1); width: 100%; padding: 12px; font-size: 1rem; margin-top: 8px; }
    .btn-primary:hover { background: #2ea043; }
    .btn-danger { background: rgba(248,81,73,0.15); border-color: rgba(248,81,73,0.3); color: var(--red); }
    .btn-danger:hover { background: rgba(248,81,73,0.3); }

    .wifi-list { margin-top: 8px; display: flex; flex-direction: column; gap: 6px; max-height: 150px; overflow-y: auto; }
    .wifi-item {
      background: #0b0f14; border: 1px solid rgba(255,255,255,0.08); padding: 8px 12px;
      border-radius: 6px; font-size: 0.85rem; display: flex; justify-content: space-between; cursor: pointer;
    }
    .wifi-item:hover { border-color: var(--accent); }
    .ota-section { margin-top: 24px; padding-top: 20px; border-top: 1px solid rgba(255,255,255,0.08); }
  </style>
</head>
<body>
  <div class="card">
    <div class="header-bar">
      <h2>📶 Wi-Fi Налаштування</h2>
      <a href="/" class="btn">⬅️ До панелі</a>
    </div>
    <p class="sub">Підключіть емулятор до домашньої Wi-Fi мережі (Варіант Б) або використовуйте аварійну точку доступу <b>BMS-Emulator-Setup</b> (Варіант А).</p>

    <!-- Wi-Fi Form -->
    <div class="form-group">
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:6px;">
        <label>Доступні Wi-Fi мережі:</label>
        <button type="button" class="btn" onclick="scanWifi()" style="padding:4px 8px; font-size:0.75rem;">🔍 Сканувати</button>
      </div>
      <div id="wifi-scan-list" class="wifi-list" style="display:none;"></div>
    </div>

    <form onsubmit="saveWifi(event)">
      <div class="form-group">
        <label for="ssid">Назва мережі (SSID):</label>
        <input type="text" id="ssid" required placeholder="Наприклад: MyHomeWiFi">
      </div>
      <div class="form-group">
        <label for="pass">Пароль Wi-Fi:</label>
        <input type="password" id="pass" placeholder="Введіть пароль">
      </div>
      <button type="submit" class="btn btn-primary">💾 Зберегти та підключитися</button>
    </form>

    <!-- Actions -->
    <div style="display:flex; gap:10px; margin-top:16px;">
      <button class="btn btn-danger" style="flex:1;" onclick="resetWifi()">🗑️ Скинути Wi-Fi (Тільки AP)</button>
      <button class="btn" style="flex:1;" onclick="restartDevice()">🔄 Перезавантажити</button>
    </div>

    <!-- BLE MAC Address Management Section -->
    <div class="ota-section">
      <div style="display:flex; justify-content:space-between; align-items:center; margin-bottom:8px; flex-wrap:wrap; gap:6px;">
        <label style="font-size:1.05rem; font-weight:700;">📡 BLE MAC-адреса адаптера:</label>
        <span id="cur-mac-badge" style="background:rgba(88,166,255,0.2); color:var(--accent); padding:4px 10px; border-radius:12px; font-family:monospace; font-size:0.85rem; font-weight:700;">...</span>
      </div>
      <p class="sub" style="margin-bottom:14px;">Керування апаратним MAC-адресом для зв'язку з додатками JBD / Daly / Ant / JK BMS. Виберіть збережений або введіть власний.</p>

      <label style="margin-bottom:8px;">Збережені профілі MAC:</label>
      <div id="mac-profiles-list" style="display:flex; flex-direction:column; gap:8px; margin-bottom:14px;">
        <div style="color:var(--text-mut); font-size:0.85rem;">Завантаження...</div>
      </div>

      <!-- Add or Set custom MAC -->
      <div style="background:#0b0f14; border:1px solid rgba(255,255,255,0.1); border-radius:10px; padding:14px; margin-bottom:12px;">
        <div style="font-size:0.85rem; font-weight:600; margin-bottom:10px;">➕ Ввести власну MAC-адресу:</div>
        <div style="display:flex; flex-direction:column; gap:8px;">
          <input type="text" id="custom-mac-addr" placeholder="Наприклад: A5:C2:37:2D:5C:A9" maxlength="17" style="font-family:monospace; font-weight:600; text-transform:uppercase;">
          <input type="text" id="custom-mac-label" placeholder="Назва профілю (наприклад: Мій адаптер)">
          <div style="display:flex; gap:8px; margin-top:4px;">
            <button type="button" class="btn btn-primary" onclick="applyCustomMac(true)" style="flex:1; margin-top:0;">⚡ Застосувати та Reboot</button>
            <button type="button" class="btn" onclick="applyCustomMac(false)" style="flex:1;">💾 Додати в список</button>
          </div>
        </div>
      </div>

      <div style="display:flex; justify-content:flex-end;">
        <button type="button" class="btn" onclick="resetMacDefaults()" style="font-size:0.75rem; color:var(--text-mut); padding:6px 10px;">🔄 Відновити фабричні профілі</button>
      </div>
    </div>

    <!-- OTA Firmware Update -->
    <div class="ota-section">
      <label>📦 Оновлення прошивки по повітрю (OTA Update):</label>
      <form method="POST" action="/update" enctype="multipart/form-data" style="margin-top:8px; display:flex; flex-direction:column; gap:8px;">
        <input type="file" name="update" accept=".bin" style="color:var(--text-mut); font-size:0.85rem;" required>
        <button type="submit" class="btn">🚀 Завантажити прошивку (.bin)</button>
      </form>
    </div>
  </div>

  <script>
    async function scanWifi() {
      const listEl = document.getElementById('wifi-scan-list');
      listEl.style.display = 'flex';
      listEl.innerHTML = '<div style="padding:8px; color:var(--text-mut);">Пошук мереж...</div>';
      try {
        const res = await fetch('/api/scan-wifi');
        const list = await res.json();
        listEl.innerHTML = '';
        if (list.length === 0) {
          listEl.innerHTML = '<div style="padding:8px; color:var(--text-mut);">Мереж не знайдено</div>';
          return;
        }
        list.forEach(net => {
          const div = document.createElement('div');
          div.className = 'wifi-item';
          div.innerHTML = `<span><b>${net.ssid}</b> ${net.secure ? '🔒' : ''}</span><span style="color:var(--text-mut);">${net.rssi} dBm</span>`;
          div.onclick = () => { document.getElementById('ssid').value = net.ssid; document.getElementById('pass').focus(); };
          listEl.appendChild(div);
        });
      } catch (e) {
        listEl.innerHTML = '<div style="padding:8px; color:var(--red);">Помилка сканування</div>';
      }
    }

    async function saveWifi(e) {
      e.preventDefault();
      const ssid = document.getElementById('ssid').value;
      const pass = document.getElementById('pass').value;
      if (!confirm(`Зберегти Wi-Fi мережу "${ssid}" та перезавантажитись для підключення?`)) return;

      try {
        await fetch('/api/save-wifi', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ ssid, pass })
        });
        alert('Налаштування збережено! Емулятор підключається до Wi-Fi...');
        setTimeout(() => { window.location.href = '/'; }, 3000);
      } catch (err) {
        alert('Помилка збереження!');
      }
    }

    async function resetWifi() {
      if (!confirm('Скинути налаштування Wi-Fi та повернутися в режим точки доступу (AP)?')) return;
      await fetch('/api/reset-wifi', { method: 'POST' });
      alert('Wi-Fi скинуто. Емулятор повертається в режим BMS-Emulator-Setup.');
      setTimeout(() => { window.location.href = '/'; }, 2000);
    }

    async function restartDevice() {
      if (!confirm('Перезавантажити мікроконтролер?')) return;
      await fetch('/api/restart', { method: 'POST' });
      alert('Перезавантаження...');
      setTimeout(() => { window.location.href = '/'; }, 3000);
    }

    async function loadMacProfiles() {
      try {
        const res = await fetch('/api/mac');
        const data = await res.json();
        const badge = document.getElementById('cur-mac-badge');
        if (badge) badge.textContent = data.active_mac;
        const listEl = document.getElementById('mac-profiles-list');
        listEl.innerHTML = '';
        (data.profiles || []).forEach(p => {
          const div = document.createElement('div');
          div.style = 'background:rgba(255,255,255,0.03); border:1px solid ' + (p.active ? 'var(--accent)' : 'rgba(255,255,255,0.08)') + '; border-radius:8px; padding:10px 12px; display:flex; justify-content:space-between; align-items:center; gap:8px; flex-wrap:wrap;';
          div.innerHTML = `
            <div style="display:flex; flex-direction:column; gap:2px;">
              <div style="display:flex; align-items:center; gap:8px;">
                <span style="font-weight:700; font-size:0.9rem;">${p.label}</span>
                ${p.active ? '<span style="font-size:0.7rem; background:rgba(63,185,80,0.2); color:var(--green); padding:2px 6px; border-radius:10px; font-weight:700;">АКТИВНИЙ</span>' : ''}
              </div>
              <span style="font-family:monospace; font-size:0.85rem; color:var(--text-mut);">${p.mac}</span>
            </div>
            <div style="display:flex; gap:6px;">
              ${!p.active ? `<button class="btn" style="padding:6px 10px; font-size:0.8rem; background:rgba(88,166,255,0.15); border-color:var(--accent); color:var(--accent);" onclick="selectMacProfile(${p.index}, '${p.mac}')">⚡ Вибрати</button>` : ''}
              ${data.profiles.length > 1 ? `<button class="btn btn-danger" style="padding:6px 10px; font-size:0.8rem;" onclick="deleteMacProfile(${p.index}, '${p.label}')">🗑️</button>` : ''}
            </div>
          `;
          listEl.appendChild(div);
        });
      } catch (e) {
        console.error(e);
      }
    }

    async function selectMacProfile(idx, mac) {
      if (!confirm(`Перемкнути MAC-адресу на ${mac} та перезавантажити емулятор?`)) return;
      try {
        await fetch('/api/mac/select', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ index: idx })
        });
        alert(`MAC-адресу перемкнуто на ${mac}! Емулятор перезавантажується...`);
        setTimeout(() => { window.location.href = '/'; }, 3000);
      } catch (err) { alert('Помилка перемикання!'); }
    }

    async function applyCustomMac(rebootNow) {
      const macInput = document.getElementById('custom-mac-addr');
      const labelInput = document.getElementById('custom-mac-label');
      const mac = macInput.value.trim().toUpperCase();
      const label = labelInput.value.trim();
      const macRegex = /^([0-9A-Fa-f]{2}[:-]){5}([0-9A-Fa-f]{2})$/;
      if (!macRegex.test(mac)) {
        alert('Некоректний формат MAC-адреси! Приклад: A5:C2:37:2D:5C:A9');
        return;
      }
      if (rebootNow) {
        if (!confirm(`Встановити активний MAC ${mac} та перезавантажити?`)) return;
        try {
          await fetch('/api/mac/set', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mac, label })
          });
          alert('Новий MAC встановлено! Перезавантаження...');
          setTimeout(() => { window.location.href = '/'; }, 3000);
        } catch (e) { alert('Помилка збереження!'); }
      } else {
        try {
          await fetch('/api/mac/add', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ mac, label })
          });
          macInput.value = '';
          labelInput.value = '';
          loadMacProfiles();
          alert('Профіль успішно додано до списку!');
        } catch (e) { alert('Помилка додавання!'); }
      }
    }

    async function deleteMacProfile(idx, label) {
      if (!confirm(`Видалити профіль "${label}"?`)) return;
      try {
        await fetch('/api/mac/del', {
          method: 'POST',
          headers: { 'Content-Type': 'application/json' },
          body: JSON.stringify({ index: idx })
        });
        loadMacProfiles();
      } catch (e) { alert('Помилка видалення!'); }
    }

    async function resetMacDefaults() {
      if (!confirm('Скинути всі збережені профілі MAC до початкових за замовчуванням?')) return;
      try {
        await fetch('/api/mac/reset', { method: 'POST' });
        loadMacProfiles();
        alert('Список профілів відновлено до фабричних значень.');
      } catch (e) { alert('Помилка скидання!'); }
    }

    loadMacProfiles();
  </script>
</body>
</html>
)rawliteral";
