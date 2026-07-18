/**
 * @file RelaysPage.h
 * @brief Relays Control Page
 *
 * Material UI styled relay management page with:
 * - Relay status overview (4 relays)
 * - On/Off toggle controls
 * - Configuration for each relay
 * - Debounce status indicators
 * - Total power consumption display
 */

#ifndef RELAYS_PAGE_H
#define RELAYS_PAGE_H

#include "../components/Layout.h"

// ============================================================
// Relays Page Specific Styles
// ============================================================

const char RELAYS_STYLES[] PROGMEM = R"rawliteral(
<style>
/* Relay Cards Grid */
.relays-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(280px, 1fr));
    gap: var(--spacing-lg);
    margin-bottom: var(--spacing-lg);
}

/* Relay Card */
.relay-card {
    background: white;
    border-radius: var(--radius-lg);
    box-shadow: var(--shadow-2);
    overflow: hidden;
    transition: all var(--transition-normal);
}

.relay-card:hover {
    box-shadow: var(--shadow-8);
    transform: translateY(-2px);
}

.relay-card.disabled {
    opacity: 0.6;
    background: var(--grey-100);
}

/* Relay Header */
.relay-header {
    padding: var(--spacing-md);
    display: flex;
    justify-content: space-between;
    align-items: center;
    border-bottom: 1px solid var(--grey-200);
}

.relay-header.on {
    background: linear-gradient(135deg, #e8f5e9 0%, #c8e6c9 100%);
    border-bottom-color: var(--success-main);
}

.relay-header.off {
    background: linear-gradient(135deg, #fafafa 0%, #f5f5f5 100%);
}

.relay-header.debounce {
    background: linear-gradient(135deg, #fff3e0 0%, #ffe0b2 100%);
    border-bottom-color: var(--warning-main);
}

.relay-id {
    font-size: 0.75rem;
    color: var(--text-secondary);
    background: rgba(0,0,0,0.1);
    padding: 2px 8px;
    border-radius: var(--radius-sm);
}

.relay-name {
    font-size: 1.25rem;
    font-weight: 500;
    color: var(--text-primary);
    flex: 1;
    margin-left: var(--spacing-sm);
}

/* Relay Status */
.relay-status {
    display: flex;
    align-items: center;
    gap: var(--spacing-xs);
}

.status-dot {
    width: 12px;
    height: 12px;
    border-radius: 50%;
    background: var(--grey-400);
}

.status-dot.on {
    background: var(--success-main);
    animation: pulse 2s infinite;
}

.status-dot.off {
    background: var(--grey-400);
}

.status-dot.debounce {
    background: var(--warning-main);
    animation: blink 0.5s infinite;
}

@keyframes pulse {
    0%, 100% { opacity: 1; box-shadow: 0 0 0 0 rgba(76, 175, 80, 0.4); }
    50% { opacity: 0.8; box-shadow: 0 0 0 8px rgba(76, 175, 80, 0); }
}

@keyframes blink {
    0%, 100% { opacity: 1; }
    50% { opacity: 0.4; }
}

/* Relay Body */
.relay-body {
    padding: var(--spacing-md);
}

.relay-info {
    display: grid;
    grid-template-columns: 1fr 1fr;
    gap: var(--spacing-sm);
    margin-bottom: var(--spacing-md);
}

.info-item {
    display: flex;
    flex-direction: column;
}

.info-label {
    font-size: 0.75rem;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

.info-value {
    font-size: 1rem;
    font-weight: 500;
    color: var(--text-primary);
}

/* Power Badge */
.power-badge {
    background: var(--primary-light);
    color: var(--primary-dark);
    padding: var(--spacing-xs) var(--spacing-sm);
    border-radius: var(--radius-sm);
    font-weight: 600;
    font-size: 0.875rem;
    display: inline-block;
}

/* Debounce Timer */
.debounce-timer {
    background: var(--warning-light);
    color: var(--warning-dark);
    padding: var(--spacing-xs) var(--spacing-sm);
    border-radius: var(--radius-sm);
    font-size: 0.875rem;
    display: flex;
    align-items: center;
    gap: var(--spacing-xs);
    margin-top: var(--spacing-sm);
}

/* Relay Actions */
.relay-actions {
    padding: var(--spacing-md);
    border-top: 1px solid var(--grey-200);
    display: flex;
    gap: var(--spacing-sm);
    justify-content: center;
}

/* Toggle Switch - Large */
.toggle-switch {
    position: relative;
    width: 80px;
    height: 40px;
    display: inline-block;
}

.toggle-switch input {
    opacity: 0;
    width: 0;
    height: 0;
}

.toggle-slider {
    position: absolute;
    cursor: pointer;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: var(--grey-400);
    transition: var(--transition-normal);
    border-radius: 40px;
}

.toggle-slider:before {
    position: absolute;
    content: "";
    height: 32px;
    width: 32px;
    left: 4px;
    bottom: 4px;
    background-color: white;
    transition: var(--transition-normal);
    border-radius: 50%;
    box-shadow: var(--shadow-2);
}

.toggle-switch input:checked + .toggle-slider {
    background-color: var(--success-main);
}

.toggle-switch input:checked + .toggle-slider:before {
    transform: translateX(40px);
}

.toggle-switch input:disabled + .toggle-slider {
    opacity: 0.5;
    cursor: not-allowed;
}

/* Summary Card */
.summary-card {
    background: linear-gradient(135deg, #e3f2fd 0%, #bbdefb 100%);
    border-radius: var(--radius-lg);
    padding: var(--spacing-lg);
    margin-bottom: var(--spacing-lg);
}

.summary-grid {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(150px, 1fr));
    gap: var(--spacing-lg);
    text-align: center;
}

.summary-item {
    display: flex;
    flex-direction: column;
    align-items: center;
}

.summary-value {
    font-size: 2.5rem;
    font-weight: 300;
    color: var(--primary-dark);
}

.summary-label {
    font-size: 0.875rem;
    color: var(--text-secondary);
    text-transform: uppercase;
    letter-spacing: 1px;
}

/* Config Modal */
.config-modal {
    display: none;
    position: fixed;
    top: 0;
    left: 0;
    width: 100%;
    height: 100%;
    background: rgba(0,0,0,0.5);
    z-index: 1000;
    align-items: center;
    justify-content: center;
}

.config-modal.active {
    display: flex;
}

.config-content {
    background: white;
    border-radius: var(--radius-lg);
    padding: var(--spacing-lg);
    width: 90%;
    max-width: 500px;
    max-height: 90vh;
    overflow-y: auto;
}

.config-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: var(--spacing-lg);
    padding-bottom: var(--spacing-md);
    border-bottom: 1px solid var(--grey-200);
}

.config-title {
    font-size: 1.25rem;
    font-weight: 500;
}

.close-btn {
    background: none;
    border: none;
    font-size: 1.5rem;
    cursor: pointer;
    color: var(--grey-500);
}

.close-btn:hover {
    color: var(--text-primary);
}

/* Responsive */
@media (max-width: 600px) {
    .relays-grid {
        grid-template-columns: 1fr;
    }

    .summary-value {
        font-size: 2rem;
    }
}
</style>
)rawliteral";

// ============================================================
// Relays Page Content
// ============================================================

const char RELAYS_CONTENT[] PROGMEM = R"rawliteral(
<!-- Summary Card -->
<div class="summary-card">
    <div class="summary-grid">
        <div class="summary-item">
            <span class="summary-value" id="enabledCount">0</span>
            <span class="summary-label">Enabled</span>
        </div>
        <div class="summary-item">
            <span class="summary-value" id="onCount">0</span>
            <span class="summary-label">Active (ON)</span>
        </div>
        <div class="summary-item">
            <span class="summary-value" id="totalPower">0</span>
            <span class="summary-label">Total Power (W)</span>
        </div>
    </div>
</div>

<!-- Relays Grid -->
<div class="relays-grid" id="relaysGrid">
    <!-- Relay cards will be generated dynamically -->
</div>

<!-- All Off Button -->
<div class="card">
    <div class="card-content" style="text-align: center;">
        <button class="btn btn-error" onclick="allRelaysOff()">
            <span style="font-size: 1.5rem;">&#9724;</span> Emergency: All Relays OFF
        </button>
        <p style="margin-top: var(--spacing-sm); color: var(--text-secondary); font-size: 0.875rem;">
            Forces all relays to OFF state immediately (ignores debounce)
        </p>
    </div>
</div>

<!-- Configuration Modal -->
<div class="config-modal" id="configModal">
    <div class="config-content">
        <div class="config-header">
            <h3 class="config-title">Configure Relay <span id="configRelayId">0</span></h3>
            <button class="close-btn" onclick="closeConfigModal()">&times;</button>
        </div>

        <div class="form-group">
            <label class="form-label">Relay Name</label>
            <input type="text" class="form-control" id="cfgName" maxlength="15">
        </div>

        <div class="grid grid-2">
            <div class="form-group">
                <label class="form-label">GPIO Pin</label>
                <input type="number" class="form-control" id="cfgGpio" min="-1" max="39">
                <div class="form-helper">-1 to disable</div>
            </div>
            <div class="form-group">
                <label class="form-label">Nominal Power (W)</label>
                <input type="number" class="form-control" id="cfgPower" min="0" max="10000">
            </div>
        </div>

        <div class="grid grid-2">
            <div class="form-group">
                <label class="form-label">Min ON Time (sec)</label>
                <input type="number" class="form-control" id="cfgMinOn" min="0" max="3600">
                <div class="form-helper">Debounce protection</div>
            </div>
            <div class="form-group">
                <label class="form-label">Min OFF Time (sec)</label>
                <input type="number" class="form-control" id="cfgMinOff" min="0" max="3600">
                <div class="form-helper">Debounce protection</div>
            </div>
        </div>

        <div class="form-group">
            <label class="form-label">Active Level</label>
            <select class="form-control" id="cfgActiveHigh">
                <option value="1">Active HIGH (relay ON when GPIO HIGH)</option>
                <option value="0">Active LOW (relay ON when GPIO LOW)</option>
            </select>
        </div>

        <div class="form-group">
            <label style="display: flex; align-items: center; gap: var(--spacing-sm); cursor: pointer;">
                <input type="checkbox" id="cfgEnabled">
                <span>Relay Enabled</span>
            </label>
        </div>

        <div class="form-group">
            <label for="cfgPriority">Priority (0-255, 0=highest)</label>
            <input type="number" id="cfgPriority" min="0" max="255" value="0" class="form-control">
            <div class="form-helper">Devices with priority 0 activate first, then 1, 2, etc.</div>
        </div>

        <div style="display: flex; gap: var(--spacing-md); justify-content: flex-end; margin-top: var(--spacing-lg);">
            <button class="btn btn-outlined" onclick="closeConfigModal()">Cancel</button>
            <button class="btn btn-primary" onclick="saveRelayConfig()">Save</button>
        </div>
    </div>
</div>

<script>
// ============================================================
// Relays Page JavaScript
// ============================================================

let relaysData = [];
let refreshInterval = null;
let currentConfigId = -1;

// Generate relay card HTML
function createRelayCard(relay) {
    const isOn = relay.is_on;
    const isDebounce = relay.debounce_active;
    const isDisabled = !relay.enabled;

    let headerClass = isDebounce ? 'debounce' : (isOn ? 'on' : 'off');
    let dotClass = isDebounce ? 'debounce' : (isOn ? 'on' : 'off');
    let statusText = isDebounce ? 'Debounce' : (isOn ? 'ON' : 'OFF');

    return `
        <div class="relay-card ${isDisabled ? 'disabled' : ''}" id="relay-card-${relay.id}">
            <div class="relay-header ${headerClass}">
                <span class="relay-id">#${relay.id}</span>
                <span class="relay-name">${relay.name || 'Relay ' + relay.id}</span>
                <div class="relay-status">
                    <span class="status-dot ${dotClass}"></span>
                    <span>${statusText}</span>
                </div>
            </div>
            <div class="relay-body">
                <div class="relay-info">
                    <div class="info-item">
                        <span class="info-label">GPIO</span>
                        <span class="info-value">${relay.gpio >= 0 ? 'GPIO ' + relay.gpio : 'Not set'}</span>
                    </div>
                    <div class="info-item">
                        <span class="info-label">Power</span>
                        <span class="info-value power-badge">${relay.power_w} W</span>
                    </div>
                    <div class="info-item">
                        <span class="info-label">Active</span>
                        <span class="info-value">${relay.active_high ? 'HIGH' : 'LOW'}</span>
                    </div>
                    <div class="info-item">
                        <span class="info-label">Debounce</span>
                        <span class="info-value">${relay.min_on}s / ${relay.min_off}s</span>
                    </div>
                </div>
                ${isDebounce ? `
                <div class="debounce-timer">
                    <span>&#9202;</span>
                    <span>Wait ${relay.debounce_remaining}s before switching</span>
                </div>
                ` : ''}
            </div>
            <div class="relay-actions">
                <label class="toggle-switch">
                    <input type="checkbox" ${isOn ? 'checked' : ''} ${isDisabled ? 'disabled' : ''}
                           onchange="toggleRelay(${relay.id}, this.checked)">
                    <span class="toggle-slider"></span>
                </label>
                <button class="btn btn-outlined btn-sm" onclick="openConfigModal(${relay.id})" ${isDisabled ? '' : ''}>
                    &#9881; Config
                </button>
            </div>
        </div>
    `;
}

// Load relays status
async function loadRelays() {
    const data = await apiGet('relays/status');

    if (!data || !data.relays) {
        console.error('Failed to load relays');
        return;
    }

    relaysData = data.relays;

    // Update summary
    document.getElementById('enabledCount').textContent = data.enabled_count || 0;
    document.getElementById('onCount').textContent = data.on_count || 0;
    document.getElementById('totalPower').textContent = data.total_power_w || 0;

    // Update relay cards
    const grid = document.getElementById('relaysGrid');
    grid.innerHTML = relaysData.map(r => createRelayCard(r)).join('');
}

// Toggle relay state
async function toggleRelay(id, turnOn) {
    const action = turnOn ? 'on' : 'off';
    const result = await apiPost(`relays/${id}/${action}`, {});

    if (result && result.success) {
        showAlert(`Relay ${id} turned ${action.toUpperCase()}`, 'success');
        setTimeout(loadRelays, 500);
    } else {
        showAlert(result?.error || `Failed to ${action} relay`, 'error');
        // Revert checkbox state
        setTimeout(loadRelays, 100);
    }
}

// All relays OFF
async function allRelaysOff() {
    if (!confirm('Turn OFF all relays immediately?')) {
        return;
    }

    const result = await apiPost('relays/all-off', {});

    if (result && result.success) {
        showAlert('All relays turned OFF', 'success');
        setTimeout(loadRelays, 500);
    } else {
        showAlert('Failed to turn off all relays', 'error');
    }
}

// Open config modal
function openConfigModal(id) {
    const relay = relaysData.find(r => r.id === id);
    if (!relay) return;

    currentConfigId = id;

    document.getElementById('configRelayId').textContent = id;
    document.getElementById('cfgName').value = relay.name || '';
    document.getElementById('cfgGpio').value = relay.gpio;
    document.getElementById('cfgPower').value = relay.power_w || 0;
    document.getElementById('cfgMinOn').value = relay.min_on || 60;
    document.getElementById('cfgMinOff').value = relay.min_off || 60;
    document.getElementById('cfgActiveHigh').value = relay.active_high ? '1' : '0';
    document.getElementById('cfgEnabled').checked = relay.enabled;
    document.getElementById('cfgPriority').value = relay.priority || 0;

    document.getElementById('configModal').classList.add('active');
}

// Close config modal
function closeConfigModal() {
    document.getElementById('configModal').classList.remove('active');
    currentConfigId = -1;
}

// Save relay configuration
async function saveRelayConfig() {
    if (currentConfigId < 0) return;

    const config = {
        name: document.getElementById('cfgName').value.trim(),
        gpio: parseInt(document.getElementById('cfgGpio').value),
        power_w: parseInt(document.getElementById('cfgPower').value),
        min_on: parseInt(document.getElementById('cfgMinOn').value),
        min_off: parseInt(document.getElementById('cfgMinOff').value),
        active_high: document.getElementById('cfgActiveHigh').value === '1',
        enabled: document.getElementById('cfgEnabled').checked,
        priority: parseInt(document.getElementById('cfgPriority').value)
    };

    showLoading();

    const result = await apiPost(`relays/${currentConfigId}/config`, config);

    hideLoading();

    if (result && result.success) {
        showAlert('Relay configuration saved', 'success');
        closeConfigModal();
        setTimeout(loadRelays, 500);
    } else {
        showAlert(result?.error || 'Failed to save configuration', 'error');
    }
}

// Close modal on outside click
document.addEventListener('click', (e) => {
    const modal = document.getElementById('configModal');
    if (e.target === modal) {
        closeConfigModal();
    }
});

// Page initialization
window.addEventListener('DOMContentLoaded', () => {
    loadRelays();

    // Auto-refresh every 2 seconds
    refreshInterval = setInterval(loadRelays, 2000);
});

// Cleanup on page unload
window.addEventListener('beforeunload', () => {
    if (refreshInterval) {
        clearInterval(refreshInterval);
    }
});
</script>
)rawliteral";

// ============================================================
// Complete Relays Page
// ============================================================

/**
 * @brief Generate complete relays page with Material UI layout
 * @return Complete HTML page
 */
inline String getRelaysPage() {
    String content;

    // Add relays-specific styles
    content += FPSTR(RELAYS_STYLES);

    // Add page content
    content += FPSTR(RELAYS_CONTENT);

    // Build complete page with layout
    return buildPageLayout("Relays Control", content, "nav-relays");
}

#endif // RELAYS_PAGE_H
