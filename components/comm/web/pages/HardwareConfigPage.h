/**
 * @file HardwareConfigPage.h
 * @brief Hardware Configuration Page
 *
 * Complete hardware configuration interface:
 * - ADC channels (GPIO pins, sensor types, calibration)
 * - Dimmer channels
 * - Relay channels
 * - Zero-cross detector
 * - LED indicators
 * - Validation and save to NVS
 * - Device reboot
 */

#ifndef HARDWARE_CONFIG_PAGE_H
#define HARDWARE_CONFIG_PAGE_H

#include "../components/Layout.h"

// ============================================================
// Hardware Config Page Styles
// ============================================================

const char HARDWARE_CONFIG_STYLES[] PROGMEM = R"rawliteral(
<style>
.config-section {
    margin-bottom: var(--spacing-xl);
}

.channel-config {
    padding: var(--spacing-md);
    border: 1px solid var(--grey-300);
    border-radius: var(--radius-md);
    margin-bottom: var(--spacing-md);
}

.channel-config.disabled {
    opacity: 0.6;
    background-color: var(--grey-100);
}

.channel-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: var(--spacing-md);
    padding-bottom: var(--spacing-sm);
    border-bottom: 1px solid var(--grey-200);
}

.channel-title {
    font-weight: 500;
    font-size: 1rem;
    color: var(--text-primary);
}

.config-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
    gap: var(--spacing-md);
}

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

.pin-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(60px, 1fr));
    gap: var(--spacing-xs);
    margin-top: var(--spacing-sm);
}

.pin-option {
    padding: var(--spacing-sm);
    border: 1px solid var(--grey-300);
    border-radius: var(--radius-sm);
    text-align: center;
    cursor: pointer;
    transition: all var(--transition-fast);
    font-size: 0.875rem;
}

.pin-option:hover {
    border-color: var(--primary-main);
    background-color: var(--grey-100);
}

.pin-option.selected {
    border-color: var(--primary-main);
    background-color: var(--primary-main);
    color: white;
}

.pin-option.input-only {
    background-color: #fff3e0;
}

.pin-option.disabled {
    opacity: 0.4;
    cursor: not-allowed;
}

.sensor-type-select {
    display: grid;
    grid-template-columns: repeat(2, 1fr);
    gap: var(--spacing-sm);
}

.sensor-option {
    padding: var(--spacing-sm);
    border: 2px solid var(--grey-300);
    border-radius: var(--radius-md);
    cursor: pointer;
    transition: all var(--transition-fast);
    text-align: center;
}

.sensor-option:hover {
    border-color: var(--primary-main);
}

.sensor-option.selected {
    border-color: var(--primary-main);
    background-color: rgba(25, 118, 210, 0.1);
}

.sensor-icon {
    font-size: 1.5rem;
    display: block;
    margin-bottom: var(--spacing-xs);
}

.warning-box {
    background: #fff3e0;
    border-left: 4px solid var(--warning-main);
    padding: var(--spacing-md);
    border-radius: var(--radius-sm);
    margin-top: var(--spacing-md);
}
</style>
)rawliteral";

// ============================================================
// Hardware Config Page Content
// ============================================================

const char HARDWARE_CONFIG_CONTENT[] PROGMEM = R"rawliteral(
<div class="alert alert-info">
    <strong>ℹ️ Hardware Configuration</strong><br>
    Configure GPIO pins and sensor types. Changes are saved to NVS and require reboot to take effect.
</div>

<!-- ADC Channels Configuration -->
<div class="card config-section">
    <div class="card-header">
        <h2 class="card-title">📊 ADC Channels Configuration</h2>
        <p class="card-subtitle">Configure voltage and current sensor inputs</p>
    </div>
    <div class="card-content">
        <!-- ADC Channel 0 -->
        <div class="channel-config" id="adc-ch0-container">
            <div class="channel-header">
                <span class="channel-title">ADC Channel 0</span>
                <div class="switch-container">
                    <label class="switch">
                        <input type="checkbox" id="adc0-enabled" checked onchange="toggleADCChannel(0, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span>Enabled</span>
                </div>
            </div>

            <div class="config-grid">
                <div class="form-group">
                    <label class="form-label">GPIO Pin</label>
                    <select class="form-control" id="adc0-gpio">
                        <option value="32">GPIO 32</option>
                        <option value="33">GPIO 33</option>
                        <option value="34">GPIO 34 (input-only)</option>
                        <option value="35" selected>GPIO 35 (input-only)</option>
                        <option value="36">GPIO 36 (input-only)</option>
                        <option value="39">GPIO 39 (input-only)</option>
                    </select>
                    <div class="form-helper">Valid ADC1 pins only</div>
                </div>

                <div class="form-group">
                    <label class="form-label">Sensor Type</label>
                    <select class="form-control" id="adc0-type">
                        <option value="0">NONE</option>
                        <option value="1" selected>VOLTAGE_AC</option>
                        <option value="2">CURRENT_LOAD</option>
                        <option value="3">CURRENT_GRID</option>
                        <option value="4">CURRENT_SOLAR</option>
                    </select>
                </div>

                <div class="form-group">
                    <label class="form-label">Multiplier</label>
                    <input type="number" class="form-control" id="adc0-mult" value="230.0" step="0.1">
                    <div class="form-helper">Calibration coefficient</div>
                </div>

                <div class="form-group">
                    <label class="form-label">Offset</label>
                    <input type="number" class="form-control" id="adc0-offset" value="0.0" step="0.01">
                    <div class="form-helper">Calibration offset</div>
                </div>
            </div>
        </div>

        <!-- ADC Channel 1 -->
        <div class="channel-config" id="adc-ch1-container">
            <div class="channel-header">
                <span class="channel-title">ADC Channel 1</span>
                <div class="switch-container">
                    <label class="switch">
                        <input type="checkbox" id="adc1-enabled" checked onchange="toggleADCChannel(1, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span>Enabled</span>
                </div>
            </div>

            <div class="config-grid">
                <div class="form-group">
                    <label class="form-label">GPIO Pin</label>
                    <select class="form-control" id="adc1-gpio">
                        <option value="32">GPIO 32</option>
                        <option value="33">GPIO 33</option>
                        <option value="34">GPIO 34 (input-only)</option>
                        <option value="35">GPIO 35 (input-only)</option>
                        <option value="36">GPIO 36 (input-only)</option>
                        <option value="39" selected>GPIO 39 (input-only)</option>
                    </select>
                </div>

                <div class="form-group">
                    <label class="form-label">Sensor Type</label>
                    <select class="form-control" id="adc1-type">
                        <option value="0">NONE</option>
                        <option value="1">VOLTAGE_AC</option>
                        <option value="2" selected>CURRENT_LOAD</option>
                        <option value="3">CURRENT_GRID</option>
                        <option value="4">CURRENT_SOLAR</option>
                    </select>
                </div>

                <div class="form-group">
                    <label class="form-label">Multiplier</label>
                    <input type="number" class="form-control" id="adc1-mult" value="30.0" step="0.1">
                </div>

                <div class="form-group">
                    <label class="form-label">Offset</label>
                    <input type="number" class="form-control" id="adc1-offset" value="0.0" step="0.01">
                </div>
            </div>
        </div>

        <!-- ADC Channel 2 -->
        <div class="channel-config" id="adc-ch2-container">
            <div class="channel-header">
                <span class="channel-title">ADC Channel 2</span>
                <div class="switch-container">
                    <label class="switch">
                        <input type="checkbox" id="adc2-enabled" checked onchange="toggleADCChannel(2, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span>Enabled</span>
                </div>
            </div>

            <div class="config-grid">
                <div class="form-group">
                    <label class="form-label">GPIO Pin</label>
                    <select class="form-control" id="adc2-gpio">
                        <option value="32">GPIO 32</option>
                        <option value="33">GPIO 33</option>
                        <option value="34">GPIO 34 (input-only)</option>
                        <option value="35">GPIO 35 (input-only)</option>
                        <option value="36" selected>GPIO 36 (input-only)</option>
                        <option value="39">GPIO 39 (input-only)</option>
                    </select>
                </div>

                <div class="form-group">
                    <label class="form-label">Sensor Type</label>
                    <select class="form-control" id="adc2-type">
                        <option value="0">NONE</option>
                        <option value="1">VOLTAGE_AC</option>
                        <option value="2">CURRENT_LOAD</option>
                        <option value="3" selected>CURRENT_GRID</option>
                        <option value="4">CURRENT_SOLAR</option>
                    </select>
                </div>

                <div class="form-group">
                    <label class="form-label">Multiplier</label>
                    <input type="number" class="form-control" id="adc2-mult" value="30.0" step="0.1">
                </div>

                <div class="form-group">
                    <label class="form-label">Offset</label>
                    <input type="number" class="form-control" id="adc2-offset" value="0.0" step="0.01">
                </div>
            </div>
        </div>

        <!-- ADC Channel 3 -->
        <div class="channel-config" id="adc-ch3-container">
            <div class="channel-header">
                <span class="channel-title">ADC Channel 3</span>
                <div class="switch-container">
                    <label class="switch">
                        <input type="checkbox" id="adc3-enabled" checked onchange="toggleADCChannel(3, this.checked)">
                        <span class="slider"></span>
                    </label>
                    <span>Enabled</span>
                </div>
            </div>

            <div class="config-grid">
                <div class="form-group">
                    <label class="form-label">GPIO Pin</label>
                    <select class="form-control" id="adc3-gpio">
                        <option value="32">GPIO 32</option>
                        <option value="33">GPIO 33</option>
                        <option value="34" selected>GPIO 34 (input-only)</option>
                        <option value="35">GPIO 35 (input-only)</option>
                        <option value="36">GPIO 36 (input-only)</option>
                        <option value="39">GPIO 39 (input-only)</option>
                    </select>
                </div>

                <div class="form-group">
                    <label class="form-label">Sensor Type</label>
                    <select class="form-control" id="adc3-type">
                        <option value="0">NONE</option>
                        <option value="1">VOLTAGE_AC</option>
                        <option value="2">CURRENT_LOAD</option>
                        <option value="3">CURRENT_GRID</option>
                        <option value="4" selected>CURRENT_SOLAR</option>
                    </select>
                </div>

                <div class="form-group">
                    <label class="form-label">Multiplier</label>
                    <input type="number" class="form-control" id="adc3-mult" value="30.0" step="0.1">
                </div>

                <div class="form-group">
                    <label class="form-label">Offset</label>
                    <input type="number" class="form-control" id="adc3-offset" value="0.0" step="0.01">
                </div>
            </div>
        </div>
    </div>
</div>

<div class="grid grid-2">
    <!-- Dimmer Configuration -->
    <div class="card config-section">
        <div class="card-header">
            <h2 class="card-title">🎚️ Dimmer Configuration</h2>
            <p class="card-subtitle">TRIAC dimmer outputs</p>
        </div>
        <div class="card-content">
            <!-- Dimmer 1 -->
            <div class="channel-config">
                <div class="channel-header">
                    <span class="channel-title">Dimmer Channel 1</span>
                    <div class="switch-container">
                        <label class="switch">
                            <input type="checkbox" id="dimmer1-enabled" checked>
                            <span class="slider"></span>
                        </label>
                        <span>Enabled</span>
                    </div>
                </div>

                <div class="form-group">
                    <label class="form-label">GPIO Pin</label>
                    <input type="number" class="form-control" id="dimmer1-gpio" value="19" min="0" max="39">
                    <div class="form-helper">Output-capable GPIO required</div>
                </div>
            </div>

            <!-- Dimmer 2 -->
            <div class="channel-config">
                <div class="channel-header">
                    <span class="channel-title">Dimmer Channel 2</span>
                    <div class="switch-container">
                        <label class="switch">
                            <input type="checkbox" id="dimmer2-enabled">
                            <span class="slider"></span>
                        </label>
                        <span>Enabled</span>
                    </div>
                </div>

                <div class="form-group">
                    <label class="form-label">GPIO Pin</label>
                    <input type="number" class="form-control" id="dimmer2-gpio" value="23" min="0" max="39">
                </div>
            </div>

            <!-- Zero-Cross Detector -->
            <div class="channel-config">
                <div class="channel-header">
                    <span class="channel-title">Zero-Cross Detector</span>
                    <div class="switch-container">
                        <label class="switch">
                            <input type="checkbox" id="zerocross-enabled" checked>
                            <span class="slider"></span>
                        </label>
                        <span>Enabled</span>
                    </div>
                </div>

                <div class="form-group">
                    <label class="form-label">GPIO Pin</label>
                    <input type="number" class="form-control" id="zerocross-gpio" value="18" min="0" max="39">
                    <div class="form-helper">Interrupt-capable GPIO required</div>
                </div>
            </div>
        </div>
    </div>

    <!-- Relay Configuration -->
    <div class="card config-section">
        <div class="card-header">
            <h2 class="card-title">🔌 Relay Configuration</h2>
            <p class="card-subtitle">Relay outputs (Phase 2)</p>
        </div>
        <div class="card-content">
            <!-- Relay 1 -->
            <div class="channel-config">
                <div class="channel-header">
                    <span class="channel-title">Relay Channel 1</span>
                    <div class="switch-container">
                        <label class="switch">
                            <input type="checkbox" id="relay1-enabled">
                            <span class="slider"></span>
                        </label>
                        <span>Enabled</span>
                    </div>
                </div>

                <div class="config-grid">
                    <div class="form-group">
                        <label class="form-label">GPIO Pin</label>
                        <input type="number" class="form-control" id="relay1-gpio" value="15" min="0" max="39">
                    </div>

                    <div class="form-group">
                        <label class="form-label">Polarity</label>
                        <select class="form-control" id="relay1-polarity">
                            <option value="1" selected>Active HIGH</option>
                            <option value="0">Active LOW</option>
                        </select>
                    </div>
                </div>
            </div>

            <!-- Relay 2 -->
            <div class="channel-config">
                <div class="channel-header">
                    <span class="channel-title">Relay Channel 2</span>
                    <div class="switch-container">
                        <label class="switch">
                            <input type="checkbox" id="relay2-enabled">
                            <span class="slider"></span>
                        </label>
                        <span>Enabled</span>
                    </div>
                </div>

                <div class="config-grid">
                    <div class="form-group">
                        <label class="form-label">GPIO Pin</label>
                        <input type="number" class="form-control" id="relay2-gpio" value="2" min="0" max="39">
                    </div>

                    <div class="form-group">
                        <label class="form-label">Polarity</label>
                        <select class="form-control" id="relay2-polarity">
                            <option value="1" selected>Active HIGH</option>
                            <option value="0">Active LOW</option>
                        </select>
                    </div>
                </div>
            </div>

            <!-- LED Configuration -->
            <div class="channel-config">
                <div class="channel-header">
                    <span class="channel-title">LED Indicators</span>
                </div>

                <div class="config-grid">
                    <div class="form-group">
                        <label class="form-label">Status LED</label>
                        <input type="number" class="form-control" id="led-status-gpio" value="17" min="0" max="39">
                    </div>

                    <div class="form-group">
                        <label class="form-label">Load LED</label>
                        <input type="number" class="form-control" id="led-load-gpio" value="5" min="0" max="39">
                    </div>
                </div>
            </div>
        </div>
    </div>
</div>

<!-- Actions -->
<div class="card">
    <div class="card-content">
        <div class="warning-box">
            <strong>⚠️ Important:</strong> Hardware configuration changes require device reboot to take effect.
            Critical tasks will be stopped before writing to NVS.
        </div>
    </div>
    <div class="card-actions">
        <button class="btn btn-outlined btn-primary" onclick="loadHardwareConfig()">
            🔄 Reload from NVS
        </button>
        <button class="btn btn-warning" onclick="validateConfig()">
            ✓ Validate Configuration
        </button>
        <button class="btn btn-success" onclick="saveConfig()">
            💾 Save to NVS
        </button>
        <button class="btn btn-error" onclick="saveAndReboot()">
            🔄 Save & Reboot
        </button>
    </div>
</div>

<script>
// ============================================================
// ADC Channel Toggle
// ============================================================
function toggleADCChannel(channel, enabled) {
    const container = document.getElementById(`adc-ch${channel}-container`);
    if (enabled) {
        container.classList.remove('disabled');
    } else {
        container.classList.add('disabled');
    }
}

// ============================================================
// Load Hardware Configuration
// ============================================================
async function loadHardwareConfig() {
    showLoading();

    const data = await apiGet('hardware/config');

    hideLoading();

    if (!data) return;

    // Load ADC channels
    for (let i = 0; i < 4; i++) {
        const ch = data.adc_channels[i];
        document.getElementById(`adc${i}-gpio`).value = ch.gpio;
        document.getElementById(`adc${i}-type`).value = ch.type;
        document.getElementById(`adc${i}-mult`).value = ch.multiplier;
        document.getElementById(`adc${i}-offset`).value = ch.offset;
        document.getElementById(`adc${i}-enabled`).checked = ch.enabled;
        toggleADCChannel(i, ch.enabled);
    }

    // Load dimmer channels
    document.getElementById('dimmer1-gpio').value = data.dimmer_ch1.gpio;
    document.getElementById('dimmer1-enabled').checked = data.dimmer_ch1.enabled;
    document.getElementById('dimmer2-gpio').value = data.dimmer_ch2.gpio;
    document.getElementById('dimmer2-enabled').checked = data.dimmer_ch2.enabled;

    // Load zero-cross
    document.getElementById('zerocross-gpio').value = data.zerocross_gpio;
    document.getElementById('zerocross-enabled').checked = data.zerocross_enabled;

    // Load relays
    document.getElementById('relay1-gpio').value = data.relay_ch1.gpio;
    document.getElementById('relay1-polarity').value = data.relay_ch1.active_high ? '1' : '0';
    document.getElementById('relay1-enabled').checked = data.relay_ch1.enabled;

    document.getElementById('relay2-gpio').value = data.relay_ch2.gpio;
    document.getElementById('relay2-polarity').value = data.relay_ch2.active_high ? '1' : '0';
    document.getElementById('relay2-enabled').checked = data.relay_ch2.enabled;

    // Load LEDs
    document.getElementById('led-status-gpio').value = data.led_status_gpio;
    document.getElementById('led-load-gpio').value = data.led_load_gpio;

    showAlert('Configuration loaded from NVS', 'success');
}

// ============================================================
// Gather Configuration
// ============================================================
function gatherConfig() {
    const config = {
        adc_channels: [],
        dimmer_ch1: {},
        dimmer_ch2: {},
        relay_ch1: {},
        relay_ch2: {},
        zerocross_gpio: 0,
        zerocross_enabled: false,
        led_status_gpio: 0,
        led_load_gpio: 0
    };

    // Gather ADC channels
    for (let i = 0; i < 4; i++) {
        config.adc_channels.push({
            gpio: parseInt(document.getElementById(`adc${i}-gpio`).value),
            type: parseInt(document.getElementById(`adc${i}-type`).value),
            multiplier: parseFloat(document.getElementById(`adc${i}-mult`).value),
            offset: parseFloat(document.getElementById(`adc${i}-offset`).value),
            enabled: document.getElementById(`adc${i}-enabled`).checked
        });
    }

    // Gather dimmer channels
    config.dimmer_ch1 = {
        gpio: parseInt(document.getElementById('dimmer1-gpio').value),
        enabled: document.getElementById('dimmer1-enabled').checked
    };

    config.dimmer_ch2 = {
        gpio: parseInt(document.getElementById('dimmer2-gpio').value),
        enabled: document.getElementById('dimmer2-enabled').checked
    };

    // Gather zero-cross
    config.zerocross_gpio = parseInt(document.getElementById('zerocross-gpio').value);
    config.zerocross_enabled = document.getElementById('zerocross-enabled').checked;

    // Gather relays
    config.relay_ch1 = {
        gpio: parseInt(document.getElementById('relay1-gpio').value),
        active_high: document.getElementById('relay1-polarity').value === '1',
        enabled: document.getElementById('relay1-enabled').checked
    };

    config.relay_ch2 = {
        gpio: parseInt(document.getElementById('relay2-gpio').value),
        active_high: document.getElementById('relay2-polarity').value === '1',
        enabled: document.getElementById('relay2-enabled').checked
    };

    // Gather LEDs
    config.led_status_gpio = parseInt(document.getElementById('led-status-gpio').value);
    config.led_load_gpio = parseInt(document.getElementById('led-load-gpio').value);

    return config;
}

// ============================================================
// Validate Configuration
// ============================================================
async function validateConfig() {
    const config = gatherConfig();

    showLoading();

    const result = await apiPost('hardware/validate', config);

    hideLoading();

    if (!result) return;

    if (result.valid) {
        showAlert('✓ Configuration is valid!', 'success');
    } else {
        showAlert('✗ Invalid configuration: ' + result.error, 'error');
    }
}

// ============================================================
// Save Configuration
// ============================================================
async function saveConfig() {
    if (!confirm('Save hardware configuration to NVS?\n\nNote: Changes require reboot to take effect.')) {
        return;
    }

    const config = gatherConfig();

    showLoading();

    const result = await apiPost('hardware/config', config);

    hideLoading();

    if (!result) return;

    if (result.success) {
        showAlert('Configuration saved to NVS successfully!', 'success');
    } else {
        showAlert('Failed to save: ' + (result.error || 'Unknown error'), 'error');
    }
}

// ============================================================
// Save and Reboot
// ============================================================
async function saveAndReboot() {
    if (!confirm('Save configuration and reboot device?\n\n⚠️ Critical tasks will be stopped.\n⚠️ Device will restart in 3 seconds.')) {
        return;
    }

    const config = gatherConfig();

    showLoading();

    // First save configuration
    const saveResult = await apiPost('hardware/config', config);

    if (!saveResult || !saveResult.success) {
        hideLoading();
        showAlert('Failed to save configuration!', 'error');
        return;
    }

    // Then request reboot
    const rebootResult = await apiPost('system/reboot', {});

    hideLoading();

    if (rebootResult && rebootResult.success) {
        showAlert('Device is rebooting... Page will reload in 10 seconds.', 'info');

        // Auto-reload page after reboot
        setTimeout(() => {
            window.location.reload();
        }, 10000);
    }
}

// ============================================================
// Page Initialization
// ============================================================
window.addEventListener('DOMContentLoaded', () => {
    loadHardwareConfig();
});
</script>
)rawliteral";

// ============================================================
// Complete Hardware Config Page
// ============================================================

/**
 * @brief Generate complete hardware configuration page
 * @return Complete HTML page
 */
inline String getHardwareConfigPage() {
    String content;

    // Add page-specific styles
    content += FPSTR(HARDWARE_CONFIG_STYLES);

    // Add page content
    content += FPSTR(HARDWARE_CONFIG_CONTENT);

    // Build complete page with layout
    return buildPageLayout("Hardware Configuration", content, "nav-hardware");
}

#endif // HARDWARE_CONFIG_PAGE_H
