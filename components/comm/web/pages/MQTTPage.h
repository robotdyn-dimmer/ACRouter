/**
 * @file MQTTPage.h
 * @brief MQTT Configuration Page
 *
 * Material UI styled MQTT settings page with:
 * - Enable/Disable toggle
 * - Broker URL configuration
 * - Authentication settings
 * - Device ID/Name settings
 * - Publish interval adjustment
 * - Home Assistant Discovery toggle
 * - Connection status indicator
 * - Test/Reconnect buttons
 */

#ifndef MQTT_PAGE_H
#define MQTT_PAGE_H

#include "../components/Layout.h"

// ============================================================
// MQTT Page Specific Styles
// ============================================================

const char MQTT_STYLES[] PROGMEM = R"rawliteral(
<style>
/* Connection Status Card */
.mqtt-status-card {
    background: linear-gradient(135deg, #e8f5e9 0%, #c8e6c9 100%);
    border-left: 4px solid var(--success-main);
    transition: all var(--transition-normal);
}

.mqtt-status-card.disconnected {
    background: linear-gradient(135deg, #ffebee 0%, #ffcdd2 100%);
    border-left-color: var(--error-main);
}

.mqtt-status-card.connecting {
    background: linear-gradient(135deg, #fff3e0 0%, #ffe0b2 100%);
    border-left-color: var(--warning-main);
}

.mqtt-status-card.disabled {
    background: linear-gradient(135deg, #eceff1 0%, #cfd8dc 100%);
    border-left-color: var(--grey-500);
}

.status-indicator {
    display: flex;
    align-items: center;
    gap: var(--spacing-md);
}

.status-dot {
    width: 16px;
    height: 16px;
    border-radius: 50%;
    background-color: var(--success-main);
    animation: pulse 2s infinite;
}

.status-dot.disconnected {
    background-color: var(--error-main);
    animation: none;
}

.status-dot.connecting {
    background-color: var(--warning-main);
    animation: blink 0.5s infinite;
}

.status-dot.disabled {
    background-color: var(--grey-500);
    animation: none;
}

@keyframes pulse {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.5; }
}

@keyframes blink {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.3; }
}

.status-text {
    font-size: 1.25rem;
    font-weight: 500;
}

.status-details {
    margin-top: var(--spacing-md);
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: var(--spacing-sm);
}

.status-detail {
    display: flex;
    justify-content: space-between;
    padding: var(--spacing-xs) 0;
    border-bottom: 1px solid rgba(0,0,0,0.1);
}

.status-detail .label {
    color: var(--text-secondary);
}

.status-detail .value {
    font-weight: 500;
    font-family: monospace;
}

/* Switch Toggle */
.switch-container {
    display: flex;
    align-items: center;
    gap: var(--spacing-sm);
}

.switch {
    position: relative;
    display: inline-block;
    width: 48px;
    height: 24px;
}

.switch input {
    opacity: 0;
    width: 0;
    height: 0;
}

.slider {
    position: absolute;
    cursor: pointer;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: var(--grey-400);
    transition: var(--transition-normal);
    border-radius: 24px;
}

.slider:before {
    position: absolute;
    content: "";
    height: 18px;
    width: 18px;
    left: 3px;
    bottom: 3px;
    background-color: white;
    transition: var(--transition-normal);
    border-radius: 50%;
}

input:checked + .slider {
    background-color: var(--primary-main);
}

input:checked + .slider:before {
    transform: translateX(24px);
}

/* Config Sections */
.config-section {
    margin-bottom: var(--spacing-lg);
}

.section-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: var(--spacing-md);
}

.section-title {
    font-weight: 500;
    color: var(--text-primary);
}

/* Password Toggle */
.password-wrapper {
    position: relative;
}

.password-toggle {
    position: absolute;
    right: 10px;
    top: 50%;
    transform: translateY(-50%);
    background: none;
    border: none;
    cursor: pointer;
    font-size: 1.25rem;
    color: var(--grey-500);
}

/* Info Box */
.info-box {
    background: #e3f2fd;
    border-left: 4px solid var(--info-main);
    padding: var(--spacing-md);
    margin-bottom: var(--spacing-lg);
    border-radius: var(--radius-sm);
}

.info-box p {
    margin: 0;
    color: var(--text-secondary);
    font-size: 0.875rem;
}

/* Statistics Grid */
.stats-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(120px, 1fr));
    gap: var(--spacing-md);
    margin-top: var(--spacing-md);
}

.stat-item {
    text-align: center;
    padding: var(--spacing-md);
    background: var(--grey-50);
    border-radius: var(--radius-md);
}

.stat-value {
    font-size: 1.5rem;
    font-weight: 600;
    color: var(--primary-main);
}

.stat-label {
    font-size: 0.75rem;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 0.5px;
}
</style>
)rawliteral";

// ============================================================
// MQTT Page Content
// ============================================================

const char MQTT_CONTENT[] PROGMEM = R"rawliteral(
<!-- Connection Status Card -->
<div class="card mqtt-status-card disabled" id="statusCard">
    <div class="card-content">
        <div class="status-indicator">
            <div class="status-dot disabled" id="statusDot"></div>
            <span class="status-text" id="statusText">MQTT Disabled</span>
        </div>
        <div class="status-details" id="statusDetails" style="display: none;">
            <div class="status-detail">
                <span class="label">Broker:</span>
                <span class="value" id="connectedBroker">--</span>
            </div>
            <div class="status-detail">
                <span class="label">Device ID:</span>
                <span class="value" id="connectedDeviceId">--</span>
            </div>
            <div class="status-detail">
                <span class="label">Uptime:</span>
                <span class="value" id="connectionUptime">--</span>
            </div>
        </div>

        <!-- Statistics -->
        <div class="stats-grid" id="statsGrid" style="display: none;">
            <div class="stat-item">
                <div class="stat-value" id="msgPublished">0</div>
                <div class="stat-label">Published</div>
            </div>
            <div class="stat-item">
                <div class="stat-value" id="msgReceived">0</div>
                <div class="stat-label">Received</div>
            </div>
        </div>
    </div>
    <div class="card-actions">
        <button class="btn btn-outlined btn-primary" id="reconnectBtn" onclick="mqttReconnect()" disabled>
            🔄 Reconnect
        </button>
        <button class="btn btn-outlined btn-primary" id="publishBtn" onclick="mqttPublish()" disabled>
            📤 Publish Now
        </button>
    </div>
</div>

<!-- MQTT Configuration -->
<div class="card mt-lg">
    <div class="card-header">
        <h2 class="card-title">⚙️ MQTT Configuration</h2>
        <p class="card-subtitle">Configure MQTT broker connection settings</p>
    </div>
    <div class="card-content">
        <!-- Enable Toggle -->
        <div class="config-section">
            <div class="section-header">
                <span class="section-title">MQTT Client</span>
                <div class="switch-container">
                    <label class="switch">
                        <input type="checkbox" id="mqttEnabled" onchange="toggleMqttEnabled(this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span id="enabledLabel">Disabled</span>
                </div>
            </div>
        </div>

        <!-- Broker Settings -->
        <div class="config-section">
            <h3 style="margin-bottom: var(--spacing-md); font-size: 1rem;">Broker Connection</h3>

            <div class="form-group">
                <label class="form-label">Broker URL</label>
                <input type="text" class="form-control" id="brokerUrl" placeholder="mqtt://192.168.1.10:1883">
                <div class="form-helper">Format: mqtt://host:port or mqtts://host:port for TLS</div>
            </div>

            <div class="grid grid-2">
                <div class="form-group">
                    <label class="form-label">Username (optional)</label>
                    <input type="text" class="form-control" id="mqttUser" placeholder="Leave empty if not required">
                </div>
                <div class="form-group">
                    <label class="form-label">Password (optional)</label>
                    <div class="password-wrapper">
                        <input type="password" class="form-control" id="mqttPass" placeholder="Leave empty if not required">
                        <button type="button" class="password-toggle" onclick="togglePassword()">👁️</button>
                    </div>
                </div>
            </div>
        </div>

        <!-- Device Settings -->
        <div class="config-section">
            <h3 style="margin-bottom: var(--spacing-md); font-size: 1rem;">Device Identification</h3>

            <div class="grid grid-2">
                <div class="form-group">
                    <label class="form-label">Device ID</label>
                    <input type="text" class="form-control" id="deviceId" placeholder="Auto-generated from MAC">
                    <div class="form-helper">Used in MQTT topics: acrouter/{device_id}/...</div>
                </div>
                <div class="form-group">
                    <label class="form-label">Device Name</label>
                    <input type="text" class="form-control" id="deviceName" placeholder="ACRouter Solar">
                    <div class="form-helper">Friendly name shown in Home Assistant</div>
                </div>
            </div>
        </div>

        <!-- Publish Settings -->
        <div class="config-section">
            <h3 style="margin-bottom: var(--spacing-md); font-size: 1rem;">Publish Settings</h3>

            <div class="form-group">
                <label class="form-label">Publish Interval: <span id="intervalValue">5000</span> ms</label>
                <input type="range" class="form-control" id="publishInterval" min="1000" max="60000" step="1000" value="5000"
                       oninput="document.getElementById('intervalValue').textContent = this.value">
                <div class="form-helper">How often to publish metrics (1-60 seconds)</div>
            </div>
        </div>

        <!-- Home Assistant -->
        <div class="config-section">
            <div class="section-header">
                <div>
                    <span class="section-title">Home Assistant Auto-Discovery</span>
                    <p style="margin: var(--spacing-xs) 0 0; color: var(--text-secondary); font-size: 0.875rem;">
                        Automatically creates entities in Home Assistant
                    </p>
                </div>
                <div class="switch-container">
                    <label class="switch">
                        <input type="checkbox" id="haDiscovery" checked>
                        <span class="slider"></span>
                    </label>
                    <span>Enabled</span>
                </div>
            </div>
        </div>
    </div>

    <div class="card-actions">
        <button class="btn btn-outlined btn-primary" onclick="loadMqttConfig()">
            🔄 Reload
        </button>
        <button class="btn btn-primary" onclick="saveMqttConfig()">
            💾 Save Configuration
        </button>
    </div>
</div>

<!-- Topic Information -->
<div class="card mt-lg">
    <div class="card-header">
        <h2 class="card-title">📋 MQTT Topics</h2>
        <p class="card-subtitle">Available topics for this device</p>
    </div>
    <div class="card-content">
        <div class="info-box">
            <p><strong>Base Topic:</strong> <code id="baseTopic">acrouter/{device_id}/</code></p>
        </div>

        <table style="width: 100%; border-collapse: collapse; font-size: 0.875rem;">
            <thead>
                <tr style="background: var(--grey-100);">
                    <th style="padding: var(--spacing-sm); text-align: left;">Topic</th>
                    <th style="padding: var(--spacing-sm); text-align: left;">Description</th>
                    <th style="padding: var(--spacing-sm); text-align: center;">R/W</th>
                </tr>
            </thead>
            <tbody>
                <tr><td style="padding: var(--spacing-xs); font-family: monospace;">status/online</td><td style="padding: var(--spacing-xs);">Device availability (LWT)</td><td style="text-align: center;">R</td></tr>
                <tr><td style="padding: var(--spacing-xs); font-family: monospace;">status/mode</td><td style="padding: var(--spacing-xs);">Current router mode</td><td style="text-align: center;">R</td></tr>
                <tr><td style="padding: var(--spacing-xs); font-family: monospace;">status/dimmer</td><td style="padding: var(--spacing-xs);">Current dimmer level (%)</td><td style="text-align: center;">R</td></tr>
                <tr style="background: var(--grey-50);"><td style="padding: var(--spacing-xs); font-family: monospace;">metrics/power_grid</td><td style="padding: var(--spacing-xs);">Grid power (W)</td><td style="text-align: center;">R</td></tr>
                <tr style="background: var(--grey-50);"><td style="padding: var(--spacing-xs); font-family: monospace;">metrics/power_solar</td><td style="padding: var(--spacing-xs);">Solar power (W)</td><td style="text-align: center;">R</td></tr>
                <tr style="background: var(--grey-50);"><td style="padding: var(--spacing-xs); font-family: monospace;">metrics/voltage</td><td style="padding: var(--spacing-xs);">AC Voltage (V)</td><td style="text-align: center;">R</td></tr>
                <tr><td style="padding: var(--spacing-xs); font-family: monospace;">command/mode</td><td style="padding: var(--spacing-xs);">Set router mode</td><td style="text-align: center;">W</td></tr>
                <tr><td style="padding: var(--spacing-xs); font-family: monospace;">command/dimmer</td><td style="padding: var(--spacing-xs);">Set dimmer level (manual mode)</td><td style="text-align: center;">W</td></tr>
                <tr><td style="padding: var(--spacing-xs); font-family: monospace;">command/emergency_stop</td><td style="padding: var(--spacing-xs);">Emergency stop</td><td style="text-align: center;">W</td></tr>
            </tbody>
        </table>

        <p style="margin-top: var(--spacing-md); color: var(--text-secondary); font-size: 0.875rem;">
            See <a href="https://github.com/robotdyn-dimmer/ACRouter/blob/main/docs/12_MQTT_GUIDE.md" target="_blank">MQTT Guide</a> for complete topic reference.
        </p>
    </div>
</div>

<script>
// ============================================================
// MQTT Page JavaScript
// ============================================================

let refreshInterval = null;

// Load MQTT configuration on page load
async function loadMqttConfig() {
    showLoading();

    const data = await apiGet('mqtt/config');

    hideLoading();

    if (!data) {
        showAlert('Failed to load MQTT configuration', 'error');
        return;
    }

    // Populate form fields
    document.getElementById('mqttEnabled').checked = data.enabled || false;
    document.getElementById('brokerUrl').value = data.broker || '';
    document.getElementById('mqttUser').value = data.username || '';
    document.getElementById('mqttPass').value = ''; // Don't show password
    document.getElementById('deviceId').value = data.device_id || '';
    document.getElementById('deviceName').value = data.device_name || '';
    document.getElementById('publishInterval').value = data.publish_interval || 5000;
    document.getElementById('intervalValue').textContent = data.publish_interval || 5000;
    document.getElementById('haDiscovery').checked = data.ha_discovery !== false;

    // Update enabled label
    updateEnabledLabel(data.enabled);

    // Update base topic display
    updateBaseTopic(data.device_id);

    // Load status
    loadMqttStatus();
}

// Load MQTT status
async function loadMqttStatus() {
    const data = await apiGet('mqtt/status');

    if (!data) return;

    const statusCard = document.getElementById('statusCard');
    const statusDot = document.getElementById('statusDot');
    const statusText = document.getElementById('statusText');
    const statusDetails = document.getElementById('statusDetails');
    const statsGrid = document.getElementById('statsGrid');
    const reconnectBtn = document.getElementById('reconnectBtn');
    const publishBtn = document.getElementById('publishBtn');

    // Update status indicator
    statusCard.className = 'card mqtt-status-card';
    statusDot.className = 'status-dot';

    if (!data.enabled) {
        statusCard.classList.add('disabled');
        statusDot.classList.add('disabled');
        statusText.textContent = 'MQTT Disabled';
        statusDetails.style.display = 'none';
        statsGrid.style.display = 'none';
        reconnectBtn.disabled = true;
        publishBtn.disabled = true;
    } else if (data.connected) {
        statusCard.classList.add('connected');
        statusText.textContent = 'Connected';
        statusDetails.style.display = 'grid';
        statsGrid.style.display = 'grid';
        reconnectBtn.disabled = false;
        publishBtn.disabled = false;

        // Update details
        document.getElementById('connectedBroker').textContent = data.broker || '--';
        document.getElementById('connectedDeviceId').textContent = data.device_id || '--';
        document.getElementById('connectionUptime').textContent = formatUptime(data.uptime || 0);

        // Update statistics
        document.getElementById('msgPublished').textContent = data.messages_published || 0;
        document.getElementById('msgReceived').textContent = data.messages_received || 0;
    } else {
        statusCard.classList.add('disconnected');
        statusDot.classList.add('disconnected');
        statusText.textContent = 'Disconnected';
        statusDetails.style.display = 'none';
        statsGrid.style.display = 'none';
        reconnectBtn.disabled = false;
        publishBtn.disabled = true;

        if (data.last_error) {
            statusText.textContent = 'Disconnected: ' + data.last_error;
        }
    }
}

// Save MQTT configuration
async function saveMqttConfig() {
    const config = {
        enabled: document.getElementById('mqttEnabled').checked,
        broker: document.getElementById('brokerUrl').value.trim(),
        username: document.getElementById('mqttUser').value.trim(),
        password: document.getElementById('mqttPass').value, // Send even if empty
        device_id: document.getElementById('deviceId').value.trim(),
        device_name: document.getElementById('deviceName').value.trim(),
        publish_interval: parseInt(document.getElementById('publishInterval').value),
        ha_discovery: document.getElementById('haDiscovery').checked
    };

    // Validate broker URL if enabled
    if (config.enabled && !config.broker) {
        showAlert('Please enter a broker URL', 'error');
        return;
    }

    showLoading();

    const result = await apiPost('mqtt/config', config);

    hideLoading();

    if (result && result.success) {
        showAlert('MQTT configuration saved!', 'success');
        // Reload to show updated status
        setTimeout(loadMqttStatus, 1000);
    } else {
        showAlert('Failed to save configuration: ' + (result?.error || 'Unknown error'), 'error');
    }
}

// Toggle MQTT enabled
function toggleMqttEnabled(enabled) {
    updateEnabledLabel(enabled);
}

function updateEnabledLabel(enabled) {
    document.getElementById('enabledLabel').textContent = enabled ? 'Enabled' : 'Disabled';
}

// Update base topic display
function updateBaseTopic(deviceId) {
    const topic = 'acrouter/' + (deviceId || '{device_id}') + '/';
    document.getElementById('baseTopic').textContent = topic;
}

// Reconnect MQTT
async function mqttReconnect() {
    showLoading();

    const result = await apiPost('mqtt/reconnect', {});

    hideLoading();

    if (result && result.success) {
        showAlert('Reconnection initiated', 'success');
        setTimeout(loadMqttStatus, 2000);
    } else {
        showAlert('Reconnection failed: ' + (result?.error || 'Unknown error'), 'error');
    }
}

// Force publish
async function mqttPublish() {
    showLoading();

    const result = await apiPost('mqtt/publish', {});

    hideLoading();

    if (result && result.success) {
        showAlert('Data published successfully', 'success');
    } else {
        showAlert('Publish failed: ' + (result?.error || 'Unknown error'), 'error');
    }
}

// Toggle password visibility
function togglePassword() {
    const input = document.getElementById('mqttPass');
    input.type = input.type === 'password' ? 'text' : 'password';
}

// Format uptime
function formatUptime(seconds) {
    if (seconds < 60) return seconds + 's';
    if (seconds < 3600) return Math.floor(seconds / 60) + 'm ' + (seconds % 60) + 's';
    if (seconds < 86400) return Math.floor(seconds / 3600) + 'h ' + Math.floor((seconds % 3600) / 60) + 'm';
    return Math.floor(seconds / 86400) + 'd ' + Math.floor((seconds % 86400) / 3600) + 'h';
}

// Page initialization
window.addEventListener('DOMContentLoaded', () => {
    loadMqttConfig();

    // Auto-refresh status every 5 seconds
    refreshInterval = setInterval(loadMqttStatus, 5000);
});

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
    if (refreshInterval) {
        clearInterval(refreshInterval);
    }
});

// Update device ID input listener
document.addEventListener('DOMContentLoaded', () => {
    const deviceIdInput = document.getElementById('deviceId');
    if (deviceIdInput) {
        deviceIdInput.addEventListener('input', (e) => {
            updateBaseTopic(e.target.value);
        });
    }
});
</script>
)rawliteral";

// ============================================================
// Complete MQTT Page
// ============================================================

/**
 * @brief Generate complete MQTT configuration page with Material UI layout
 * @return Complete HTML page
 */
inline String getMQTTPage() {
    String content;

    // Add MQTT-specific styles
    content += FPSTR(MQTT_STYLES);

    // Add page content
    content += FPSTR(MQTT_CONTENT);

    // Build complete page with layout
    return buildPageLayout("MQTT Configuration", content, "nav-mqtt");
}

#endif // MQTT_PAGE_H
