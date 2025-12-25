/**
 * @file DashboardPage.h
 * @brief Main Dashboard Page - Real-time router status monitoring
 *
 * Material UI inspired dashboard with real-time metrics display:
 * - Block 1: Power measurement (Grid Power)
 * - Block 2: Load devices (Dimmers) - shown in AUTO/ECO/OFFGRID/BOOST modes
 * - Block 3: Manual Dimmer Control - shown only in MANUAL mode
 */

#ifndef DASHBOARD_PAGE_H
#define DASHBOARD_PAGE_H

#include "../components/Layout.h"

// ============================================================
// Dashboard Page Specific Styles
// ============================================================

const char DASHBOARD_STYLES[] PROGMEM = R"rawliteral(
<style>
/* Power Metric Cards */
.power-metric {
    text-align: center;
    padding: var(--spacing-lg);
    border-radius: var(--radius-md);
    transition: transform var(--transition-fast);
}

.power-metric:hover {
    transform: translateY(-4px);
}

.power-metric.grid {
    background: linear-gradient(135deg, #e3f2fd 0%, #bbdefb 100%);
}

.power-metric.solar {
    background: linear-gradient(135deg, #fff3e0 0%, #ffe0b2 100%);
}

.power-metric.load {
    background: linear-gradient(135deg, #f3e5f5 0%, #e1bee7 100%);
}

.power-metric.dimmer {
    background: linear-gradient(135deg, #e8f5e9 0%, #c8e6c9 100%);
}

.power-value {
    font-size: 3rem;
    font-weight: 300;
    line-height: 1;
    margin: var(--spacing-sm) 0;
    color: var(--text-primary);
}

.power-unit {
    font-size: 1.5rem;
    color: var(--text-secondary);
    margin-left: var(--spacing-xs);
}

.power-label {
    font-size: 0.875rem;
    text-transform: uppercase;
    letter-spacing: 1px;
    color: var(--text-secondary);
    font-weight: 500;
}

.power-icon {
    font-size: 2rem;
    margin-bottom: var(--spacing-sm);
}

/* Section Headers */
.section-header {
    display: flex;
    align-items: center;
    gap: var(--spacing-sm);
    margin-bottom: var(--spacing-md);
    padding-bottom: var(--spacing-sm);
    border-bottom: 1px solid var(--grey-200);
}

.section-title {
    font-size: 1rem;
    font-weight: 500;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 1px;
    margin: 0;
}

/* Load Devices Section */
.devices-section {
    margin-bottom: var(--spacing-lg);
}

.devices-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: var(--spacing-md);
}

.device-card {
    text-align: center;
    padding: var(--spacing-md);
    border-radius: var(--radius-md);
    transition: transform var(--transition-fast);
    position: relative;
}

.device-card:hover {
    transform: translateY(-2px);
}

.device-number {
    position: absolute;
    top: var(--spacing-xs);
    right: var(--spacing-xs);
    background: rgba(0,0,0,0.1);
    color: var(--text-secondary);
    font-size: 0.75rem;
    padding: 2px 6px;
    border-radius: var(--radius-sm);
}

.device-value {
    font-size: 2rem;
    font-weight: 300;
    line-height: 1;
    margin: var(--spacing-xs) 0;
    color: var(--text-primary);
}

.device-label {
    font-size: 0.75rem;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    color: var(--text-secondary);
    font-weight: 500;
}

/* Mode Selection */
.mode-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(140px, 1fr));
    gap: var(--spacing-sm);
}

.mode-btn {
    padding: var(--spacing-md);
    border: 2px solid var(--grey-300);
    border-radius: var(--radius-md);
    background: white;
    cursor: pointer;
    transition: all var(--transition-fast);
    text-align: center;
}

.mode-btn:hover {
    border-color: var(--primary-main);
    box-shadow: var(--shadow-2);
}

.mode-btn.active {
    border-color: var(--primary-main);
    background: var(--primary-main);
    color: white;
    box-shadow: var(--shadow-4);
}

.mode-btn .mode-icon {
    font-size: 2rem;
    display: block;
    margin-bottom: var(--spacing-xs);
}

.mode-btn .mode-name {
    font-weight: 500;
    font-size: 0.875rem;
}

/* Dimmer Slider */
.dimmer-control {
    padding: var(--spacing-lg);
    text-align: center;
}

.dimmer-slider {
    width: 100%;
    height: 8px;
    border-radius: 4px;
    background: linear-gradient(to right, #e0e0e0 0%, var(--primary-main) 100%);
    outline: none;
    -webkit-appearance: none;
    margin: var(--spacing-md) 0;
}

.dimmer-slider::-webkit-slider-thumb {
    -webkit-appearance: none;
    appearance: none;
    width: 24px;
    height: 24px;
    border-radius: 50%;
    background: var(--primary-main);
    cursor: pointer;
    box-shadow: var(--shadow-2);
}

.dimmer-slider::-moz-range-thumb {
    width: 24px;
    height: 24px;
    border-radius: 50%;
    background: var(--primary-main);
    cursor: pointer;
    box-shadow: var(--shadow-2);
    border: none;
}

.dimmer-value-display {
    font-size: 2.5rem;
    font-weight: 300;
    color: var(--text-primary);
}

/* State Indicator */
.state-indicator {
    display: inline-flex;
    align-items: center;
    gap: var(--spacing-sm);
    padding: var(--spacing-sm) var(--spacing-md);
    border-radius: var(--radius-sm);
    font-size: 0.875rem;
    font-weight: 500;
}

.state-indicator.idle {
    background: var(--grey-200);
    color: var(--text-secondary);
}

.state-indicator.increasing {
    background: #e8f5e9;
    color: var(--success-main);
}

.state-indicator.decreasing {
    background: #fff3e0;
    color: var(--warning-main);
}

.state-indicator.maximum {
    background: #ffebee;
    color: var(--error-main);
}

.state-indicator.minimum {
    background: #e3f2fd;
    color: var(--info-main);
}

/* Status Pulse Animation */
@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
}

.status-pulse {
    animation: pulse 2s ease-in-out infinite;
}

/* Last Update Timer */
.update-time {
    font-size: 0.75rem;
    color: var(--text-secondary);
    text-align: center;
    margin-top: var(--spacing-sm);
}

/* Hidden class */
.hidden {
    display: none !important;
}

/* Manual Mode Section */
.manual-section {
    border: 2px dashed var(--warning-main);
    border-radius: var(--radius-md);
    padding: var(--spacing-md);
    margin-bottom: var(--spacing-lg);
    background: #fffde7;
}

.manual-section .section-header {
    border-bottom-color: var(--warning-main);
}

.manual-section .section-title {
    color: var(--warning-dark);
}

/* Dimmer Progress Bar */
.dimmer-progress {
    width: 100%;
    height: 6px;
    background: var(--grey-300);
    border-radius: 3px;
    margin-top: var(--spacing-sm);
    overflow: hidden;
}

.dimmer-progress-fill {
    height: 100%;
    background: linear-gradient(90deg, var(--success-main) 0%, var(--primary-main) 100%);
    transition: width var(--transition-normal);
    border-radius: 3px;
}

/* Enhanced Power Values */
.power-value {
    font-size: 3.5rem;
}

/* Grid Direction Indicator - More Prominent */
#grid-direction {
    font-size: 0.875rem;
    font-weight: 500;
    padding: var(--spacing-xs) var(--spacing-sm);
    border-radius: var(--radius-sm);
    margin-top: var(--spacing-xs);
    display: inline-block;
}

#grid-direction.importing {
    background-color: rgba(211, 47, 47, 0.1);
    color: var(--error-main);
}

#grid-direction.exporting {
    background-color: rgba(46, 125, 50, 0.1);
    color: var(--success-main);
}

#grid-direction.balanced {
    background-color: rgba(0, 0, 0, 0.05);
    color: var(--text-secondary);
}

/* Responsive - smaller power values on mobile */
@media (max-width: 600px) {
    .power-value {
        font-size: 2rem;
    }

    .device-value {
        font-size: 1.5rem;
    }
}
</style>
)rawliteral";

// ============================================================
// Dashboard Page Content
// ============================================================

const char DASHBOARD_CONTENT[] PROGMEM = R"rawliteral(
<!-- Block 1: Power Metrics -->
<div class="section-header">
    <span>⚡</span>
    <h3 class="section-title">Power Monitoring</h3>
</div>
<div id="power-grid-container" class="grid grid-3 mb-lg">
    <!-- Grid Power -->
    <div class="card power-metric grid">
        <div class="power-icon">⚡</div>
        <div class="power-label">Grid Power</div>
        <div class="power-value">
            <span id="power-grid">--</span>
            <span class="power-unit">W</span>
        </div>
        <div id="grid-direction" class="form-helper"></div>
    </div>

    <!-- Solar Power -->
    <div class="card power-metric solar" id="solar-card">
        <div class="power-icon">☀️</div>
        <div class="power-label">Solar Power</div>
        <div class="power-value">
            <span id="power-solar">--</span>
            <span class="power-unit">W</span>
        </div>
    </div>

    <!-- Load Power -->
    <div class="card power-metric load" id="load-card">
        <div class="power-icon">🔌</div>
        <div class="power-label">Load Power</div>
        <div class="power-value">
            <span id="power-load">--</span>
            <span class="power-unit">W</span>
        </div>
    </div>
</div>

<!-- Block 2: Load Devices (shown in AUTO/ECO/OFFGRID/BOOST modes) -->
<div id="devices-section" class="devices-section hidden">
    <div class="section-header">
        <span>🎚️</span>
        <h3 class="section-title">Load Devices Output</h3>
    </div>
    <div id="devices-grid" class="devices-grid">
        <!-- Dimmer 1 -->
        <div class="card device-card dimmer" id="dimmer-card-1">
            <span class="device-number">#1</span>
            <div class="power-icon">🎚️</div>
            <div class="device-label">Dimmer</div>
            <div class="device-value">
                <span id="dimmer-level-1">--</span>
                <span class="power-unit">%</span>
            </div>
            <div class="dimmer-progress">
                <div class="dimmer-progress-fill" id="dimmer-progress-1" style="width: 0%"></div>
            </div>
        </div>
    </div>
</div>

<!-- Block 3: Manual Dimmer Control (only in MANUAL mode) -->
<div id="manual-section" class="manual-section hidden">
    <div class="section-header">
        <span>✋</span>
        <h3 class="section-title">Manual Control Mode</h3>
    </div>
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">🎚️ Manual Dimmer Control</h2>
            <p class="card-subtitle">Direct control of dimmer output</p>
        </div>
        <div class="card-content dimmer-control">
            <div class="dimmer-value-display">
                <span id="dimmer-slider-value">0</span>%
            </div>
            <input
                type="range"
                min="0"
                max="100"
                value="0"
                class="dimmer-slider"
                id="dimmer-slider"
                oninput="updateDimmerDisplay(this.value)"
                onchange="setDimmer(this.value)">
            <div class="form-helper">
                Move slider to adjust dimmer output level
            </div>
        </div>
    </div>
</div>

<div class="grid grid-2">
    <!-- Mode Selection Card -->
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">⚙️ Operating Mode</h2>
            <p class="card-subtitle">Select router control mode</p>
        </div>
        <div class="card-content">
            <div class="mode-grid">
                <button class="mode-btn" data-mode="off" onclick="setMode('off')">
                    <span class="mode-icon">⭕</span>
                    <span class="mode-name">OFF</span>
                </button>
                <button class="mode-btn" data-mode="auto" onclick="setMode('auto')">
                    <span class="mode-icon">🤖</span>
                    <span class="mode-name">AUTO</span>
                </button>
                <button class="mode-btn" data-mode="eco" onclick="setMode('eco')">
                    <span class="mode-icon">🌱</span>
                    <span class="mode-name">ECO</span>
                </button>
                <button class="mode-btn" data-mode="offgrid" onclick="setMode('offgrid')">
                    <span class="mode-icon">🔋</span>
                    <span class="mode-name">OFFGRID</span>
                </button>
                <button class="mode-btn" data-mode="manual" onclick="setMode('manual')">
                    <span class="mode-icon">✋</span>
                    <span class="mode-name">MANUAL</span>
                </button>
                <button class="mode-btn" data-mode="boost" onclick="setMode('boost')">
                    <span class="mode-icon">🚀</span>
                    <span class="mode-name">BOOST</span>
                </button>
            </div>

            <div class="mt-md" style="text-align: center;">
                <span class="state-indicator" id="state-indicator">
                    <span id="state-text">IDLE</span>
                </span>
            </div>
        </div>
    </div>

    <!-- System Information -->
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">ℹ️ System Information</h2>
            <p class="card-subtitle">Device status and configuration</p>
        </div>
        <div class="card-content">
            <div class="status-row">
                <span class="status-label">Firmware:</span>
                <span class="status-value" id="info-version">--</span>
            </div>
            <div class="status-row">
                <span class="status-label">Support & Hardware:</span>
                <span class="status-value">www.rbdimmer.com</span>
            </div>            
            <div class="status-row">
                <span class="status-label">Uptime:</span>
                <span class="status-value" id="info-uptime">--</span>
            </div>
            <div class="status-row">
                <span class="status-label">Free Heap:</span>
                <span class="status-value" id="info-heap">--</span>
            </div>
            <div class="status-row">
                <span class="status-label">WiFi RSSI:</span>
                <span class="status-value" id="info-rssi">--</span>
            </div>
        </div>
        <div class="card-actions">
            <button class="btn btn-outlined btn-primary" onclick="loadSystemInfo()">
                🔄 Refresh
            </button>
        </div>
    </div>

    <!-- Algorithm Parameters -->
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">🔧 Control Parameters</h2>
            <p class="card-subtitle">Current algorithm settings</p>
        </div>
        <div class="card-content">
            <div class="status-row">
                <span class="status-label">Control Gain:</span>
                <span class="status-value" id="param-gain">--</span>
            </div>
            <div class="status-row">
                <span class="status-label">Balance Threshold:</span>
                <span class="status-value" id="param-threshold">--</span>
            </div>
        </div>
    </div>
</div>

<div class="update-time">
    Last updated: <span id="last-update">Never</span>
</div>

<script>
let metricsUpdateInterval;
let currentMode = 'off';

// ============================================================
// Update UI based on current mode
// ============================================================
function updateModeUI(mode) {
    const devicesSection = document.getElementById('devices-section');
    const manualSection = document.getElementById('manual-section');

    // Automatic modes: show devices section, hide manual control
    const autoModes = ['auto', 'eco', 'offgrid', 'boost'];

    if (mode === 'manual') {
        // Manual mode: hide devices output, show manual control
        devicesSection.classList.add('hidden');
        manualSection.classList.remove('hidden');
    } else if (autoModes.includes(mode)) {
        // Automatic modes: show devices output, hide manual control
        devicesSection.classList.remove('hidden');
        manualSection.classList.add('hidden');
    } else {
        // OFF mode: hide both
        devicesSection.classList.add('hidden');
        manualSection.classList.add('hidden');
    }
}

// ============================================================
// Load Metrics (Lightweight, for polling)
// ============================================================
async function loadMetrics() {
    const data = await apiGet('metrics');
    if (!data) return;

    // Update Grid Power
    const gridPower = data.metrics.power_grid;
    document.getElementById('power-grid').textContent = Math.round(gridPower);

    // Grid direction indicator - Enhanced with classes
    const gridDir = document.getElementById('grid-direction');
    gridDir.className = ''; // Clear classes

    if (gridPower > 10) {
        gridDir.textContent = '← Importing from grid';
        gridDir.classList.add('importing');
    } else if (gridPower < -10) {
        gridDir.textContent = '→ Exporting to grid';
        gridDir.classList.add('exporting');
    } else {
        gridDir.textContent = '⚖ Balanced';
        gridDir.classList.add('balanced');
    }

    // Update Solar Power (show absolute value, solar is typically negative = supplying)
    const solarPower = data.metrics.power_solar || 0;
    document.getElementById('power-solar').textContent = Math.round(Math.abs(solarPower));

    // Update Load Power
    const loadPower = data.metrics.power_load || 0;
    document.getElementById('power-load').textContent = Math.round(Math.abs(loadPower));

    // Update dimmer levels from array
    if (data.dimmers && data.dimmers.length > 0) {
        data.dimmers.forEach((dimmer) => {
            const levelEl = document.getElementById('dimmer-level-' + dimmer.id);
            if (levelEl) {
                levelEl.textContent = dimmer.level;
            }

            // Update progress bar
            const progressEl = document.getElementById('dimmer-progress-' + dimmer.id);
            if (progressEl) {
                progressEl.style.width = dimmer.level + '%';
            }

            // Also update manual slider if in manual mode
            if (currentMode === 'manual' && dimmer.id === 1) {
                document.getElementById('dimmer-slider').value = dimmer.level;
                document.getElementById('dimmer-slider-value').textContent = dimmer.level;
            }
        });
    }

    // Update mode from metrics if available
    if (data.mode && data.mode !== currentMode) {
        currentMode = data.mode;
        updateModeButtons(currentMode);
        updateModeUI(currentMode);
    }

    // Last update time
    const now = new Date();
    document.getElementById('last-update').textContent = now.toLocaleTimeString();
}

// ============================================================
// Load Status (Complete status with mode/state)
// ============================================================
async function loadStatus() {
    const data = await apiGet('status');
    if (!data) return;

    // Update current mode
    currentMode = data.mode.toLowerCase();
    updateModeButtons(currentMode);
    updateModeUI(currentMode);

    // Update state indicator
    updateStateIndicator(data.state);

    // Update header badge
    updateStatusBadge(data.mode, data.state);

    // Update control parameters
    document.getElementById('param-gain').textContent = data.control_gain.toFixed(1);
    document.getElementById('param-threshold').textContent = data.balance_threshold.toFixed(1) + ' W';
}

// ============================================================
// Load System Information
// ============================================================
async function loadSystemInfo() {
    const data = await apiGet('info');
    if (!data) return;

    document.getElementById('info-version').textContent = data.version || 'Unknown';
    document.getElementById('info-uptime').textContent = formatUptime(data.uptime_sec || 0);
    document.getElementById('info-heap').textContent = formatBytes(data.free_heap || 0);

    // Get WiFi RSSI from WiFi status
    const wifiData = await apiGet('wifi/status');
    if (wifiData && wifiData.sta_connected) {
        document.getElementById('info-rssi').textContent = wifiData.rssi + ' dBm';
    } else {
        document.getElementById('info-rssi').textContent = 'N/A (AP mode)';
    }
}

// ============================================================
// Mode Selection
// ============================================================
function updateModeButtons(mode) {
    document.querySelectorAll('.mode-btn').forEach(btn => {
        btn.classList.remove('active');
    });

    const activeBtn = document.querySelector(`.mode-btn[data-mode="${mode}"]`);
    if (activeBtn) {
        activeBtn.classList.add('active');
    }
}

async function setMode(mode) {
    const result = await apiPost('mode', { mode: mode });

    if (result && result.success) {
        showAlert(`Mode changed to ${mode.toUpperCase()}`, 'success');
        currentMode = mode;
        updateModeButtons(mode);
        updateModeUI(mode);

        // Reload status after short delay
        setTimeout(loadStatus, 500);
    }
}

// ============================================================
// State Indicator
// ============================================================
function updateStateIndicator(state) {
    const indicator = document.getElementById('state-indicator');
    const text = document.getElementById('state-text');

    // Remove all state classes
    indicator.className = 'state-indicator';

    // Map state to class and text
    const stateMap = {
        'idle': { class: 'idle', text: '⏸ IDLE' },
        'increasing': { class: 'increasing', text: '⬆ INCREASING' },
        'decreasing': { class: 'decreasing', text: '⬇ DECREASING' },
        'at_max': { class: 'maximum', text: '🔴 AT MAXIMUM' },
        'at_min': { class: 'minimum', text: '🔵 AT MINIMUM' },
        'error': { class: 'error', text: '⚠ ERROR' }
    };

    const stateKey = state.toLowerCase();
    const stateInfo = stateMap[stateKey] || stateMap['idle'];
    indicator.classList.add(stateInfo.class);
    text.textContent = stateInfo.text;

    // Add pulse animation for active states
    if (stateKey === 'increasing' || stateKey === 'decreasing') {
        indicator.classList.add('status-pulse');
    }
}

// ============================================================
// Manual Dimmer Control
// ============================================================
function updateDimmerDisplay(value) {
    document.getElementById('dimmer-slider-value').textContent = value;
}

async function setDimmer(value) {
    const result = await apiPost('dimmer', { value: parseInt(value) });

    if (result && result.success) {
        showAlert(`Dimmer set to ${value}%`, 'info');

        // Reload metrics after short delay
        setTimeout(loadMetrics, 500);
    }
}

// ============================================================
// Format Helpers
// ============================================================
function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const mins = Math.floor((seconds % 3600) / 60);

    if (days > 0) return `${days}d ${hours}h`;
    if (hours > 0) return `${hours}h ${mins}m`;
    return `${mins}m`;
}

function formatBytes(bytes) {
    if (bytes < 1024) return bytes + ' B';
    if (bytes < 1048576) return (bytes / 1024).toFixed(1) + ' KB';
    return (bytes / 1048576).toFixed(1) + ' MB';
}

// ============================================================
// Page Initialization
// ============================================================
window.addEventListener('DOMContentLoaded', () => {
    // Initial load
    loadStatus();
    loadMetrics();
    loadSystemInfo();

    // Start metrics polling (every 2 seconds)
    metricsUpdateInterval = setInterval(loadMetrics, 2000);

    // Reload full status every 10 seconds
    setInterval(loadStatus, 10000);
});

// Clean up on page unload
window.addEventListener('beforeunload', () => {
    if (metricsUpdateInterval) {
        clearInterval(metricsUpdateInterval);
    }
});
</script>
)rawliteral";

// ============================================================
// Complete Dashboard Page
// ============================================================

/**
 * @brief Generate complete dashboard page with Material UI layout
 * @return Complete HTML page
 */
inline String getDashboardPage() {
    String content;

    // Add dashboard-specific styles
    content += FPSTR(DASHBOARD_STYLES);

    // Add page content
    content += FPSTR(DASHBOARD_CONTENT);

    // Build complete page with layout
    return buildPageLayout("Dashboard", content, "nav-dashboard");
}

#endif // DASHBOARD_PAGE_H
