/**
 * @file WiFiConfigPage.h
 * @brief WiFi Configuration Web Page (Material UI version)
 *
 * Refactored WiFi configuration page using Material UI layout.
 * Includes network scanning, connection management, and NVS credential storage.
 */

#ifndef WIFI_CONFIG_PAGE_H
#define WIFI_CONFIG_PAGE_H

#include "../components/Layout.h"

// ============================================================
// WiFi Page Specific Styles
// ============================================================

const char WIFI_PAGE_STYLES[] PROGMEM = R"rawliteral(
<style>
.network-list {
    max-height: 400px;
    overflow-y: auto;
    border: 1px solid var(--grey-300);
    border-radius: var(--radius-md);
}

.network-item {
    padding: var(--spacing-md);
    border-bottom: 1px solid var(--grey-200);
    cursor: pointer;
    transition: background-color var(--transition-fast);
    display: flex;
    justify-content: space-between;
    align-items: center;
}

.network-item:last-child {
    border-bottom: none;
}

.network-item:hover {
    background-color: var(--grey-100);
}

.network-item.selected {
    background-color: rgba(25, 118, 210, 0.12);
    border-left: 4px solid var(--primary-main);
}

.network-name {
    font-weight: 500;
    color: var(--text-primary);
}

.network-info {
    display: flex;
    gap: var(--spacing-md);
    font-size: 0.875rem;
    color: var(--text-secondary);
    align-items: center;
}

.signal-bars {
    font-size: 1rem;
}

.ip-box {
    background: linear-gradient(135deg, #e8f5e9 0%, #c8e6c9 100%);
    padding: var(--spacing-lg);
    border-radius: var(--radius-md);
    text-align: center;
    margin-top: var(--spacing-md);
}

.ip-label {
    font-size: 0.875rem;
    color: var(--text-secondary);
    margin-bottom: var(--spacing-sm);
}

.ip-address {
    font-size: 1.5rem;
    font-weight: 500;
    font-family: 'Courier New', monospace;
    color: var(--success-main);
}

.ip-address a {
    color: var(--success-main);
    text-decoration: none;
}

.ip-address a:hover {
    text-decoration: underline;
}

.status-row {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: var(--spacing-sm) 0;
}

.status-row:not(:last-child) {
    border-bottom: 1px solid var(--grey-200);
}

.status-label {
    font-weight: 500;
    color: var(--text-secondary);
}

.status-value {
    font-weight: 500;
    color: var(--text-primary);
}
</style>
)rawliteral";

// ============================================================
// WiFi Configuration Page Content
// ============================================================

const char WIFI_PAGE_CONTENT[] PROGMEM = R"rawliteral(
<div class="grid">
    <!-- Current Status Card -->
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">📡 WiFi Status</h2>
            <p class="card-subtitle">Current connection status</p>
        </div>
        <div class="card-content">
            <div class="status-row">
                <span class="status-label">State:</span>
                <span class="status-value" id="status-state">Loading...</span>
            </div>
            <div class="status-row" id="status-sta" style="display:none;">
                <span class="status-label">Connected to:</span>
                <span class="status-value" id="status-sta-ssid"></span>
            </div>
            <div class="status-row" id="status-rssi" style="display:none;">
                <span class="status-label">Signal Strength:</span>
                <span class="status-value" id="status-sta-rssi"></span>
            </div>

            <!-- STA IP Address Display -->
            <div id="sta-ip-box" class="ip-box" style="display:none;">
                <div class="ip-label">Access router via STA network:</div>
                <div class="ip-address">
                    <a href="#" id="status-sta-ip-link" target="_blank">
                        <span id="status-sta-ip"></span> 🔗
                    </a>
                </div>
            </div>
        </div>
        <div class="card-actions">
            <button class="btn btn-primary" onclick="loadStatus()">
                🔄 Refresh Status
            </button>
        </div>
    </div>

    <!-- Network Scanner Card -->
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">🔍 Available Networks</h2>
            <p class="card-subtitle">Scan for WiFi networks in range</p>
        </div>
        <div class="card-content">
            <button class="btn btn-primary" onclick="scanNetworks()" style="width:100%;">
                <span>Scan for Networks</span>
                <div id="scan-spinner" class="spinner spinner-sm" style="display:none;"></div>
            </button>

            <div id="networks" class="network-list mt-md" style="display:none;">
                <!-- Networks will be populated here -->
            </div>
        </div>
    </div>

    <!-- Connection Form Card -->
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">🔐 Connect to Network</h2>
            <p class="card-subtitle">Enter credentials to connect and save to NVS</p>
        </div>
        <div class="card-content">
            <div class="form-group">
                <label class="form-label" for="ssid">Network Name (SSID)</label>
                <input type="text" id="ssid" class="form-control" placeholder="Enter network SSID">
                <div class="form-helper">Select from scanned networks or enter manually</div>
            </div>

            <div class="form-group">
                <label class="form-label" for="password">Password</label>
                <input type="password" id="password" class="form-control" placeholder="Enter network password">
                <div class="form-helper">Leave empty for open networks</div>
            </div>
        </div>
        <div class="card-actions">
            <button class="btn btn-success" onclick="connectWiFi()">
                ✓ Connect & Save to NVS
            </button>
        </div>
    </div>

    <!-- Network Management Card -->
    <div class="card">
        <div class="card-header">
            <h2 class="card-title">⚙️ Network Management</h2>
            <p class="card-subtitle">Disconnect or forget saved credentials</p>
        </div>
        <div class="card-content">
            <p style="color: var(--text-secondary); font-size: 0.875rem;">
                Use these controls to disconnect from the current network or clear saved credentials from NVS.
            </p>
        </div>
        <div class="card-actions">
            <button class="btn btn-outlined btn-primary" onclick="disconnect()">
                ⏸ Disconnect
            </button>
            <button class="btn btn-error" onclick="forgetNetwork()">
                🗑 Forget Saved Network
            </button>
        </div>
    </div>
</div>

<script>
let selectedSSID = '';
let statusUpdateInterval;

// ============================================================
// Status Loading
// ============================================================
function loadStatus() {
    apiGet('wifi/status').then(data => {
        if (!data) return;

        document.getElementById('status-state').textContent = data.state;

        if (data.sta_connected) {
            // Show STA connection info
            document.getElementById('status-sta').style.display = 'flex';
            document.getElementById('status-rssi').style.display = 'flex';
            document.getElementById('status-sta-ssid').textContent = data.sta_ssid;

            // Format signal strength with visual indicator
            const rssi = data.rssi;
            let signalQuality = '📶📶📶 Excellent';
            if (rssi < -85) signalQuality = '📶 Poor';
            else if (rssi < -70) signalQuality = '📶📶 Fair';
            else if (rssi < -50) signalQuality = '📶📶📶 Good';

            document.getElementById('status-sta-rssi').textContent = `${rssi} dBm (${signalQuality})`;

            // Show STA IP address with link
            if (data.sta_ip) {
                document.getElementById('sta-ip-box').style.display = 'block';
                const ipUrl = 'http://' + data.sta_ip;
                document.getElementById('status-sta-ip').textContent = ipUrl;
                document.getElementById('status-sta-ip-link').href = ipUrl;
            }

            // Update header status badge
            updateStatusBadge('CONNECTED', 'IDLE');
        } else {
            document.getElementById('status-sta').style.display = 'none';
            document.getElementById('status-rssi').style.display = 'none';
            document.getElementById('sta-ip-box').style.display = 'none';

            updateStatusBadge('DISCONNECTED', 'IDLE');
        }
    });
}

// ============================================================
// Network Scanning
// ============================================================
function scanNetworks() {
    const spinner = document.getElementById('scan-spinner');
    const networksList = document.getElementById('networks');

    spinner.style.display = 'inline-block';
    networksList.innerHTML = '<div style="padding: var(--spacing-md); text-align: center; color: var(--text-secondary);">Scanning...</div>';
    networksList.style.display = 'block';

    apiGet('wifi/scan').then(data => {
        spinner.style.display = 'none';

        if (!data) {
            networksList.innerHTML = '<div class="network-item">Scan failed</div>';
            return;
        }

        networksList.innerHTML = '';

        if (data.count === 0) {
            networksList.innerHTML = '<div class="network-item">No networks found</div>';
        } else {
            data.networks.forEach(net => {
                const item = document.createElement('div');
                item.className = 'network-item';
                item.onclick = () => selectNetwork(net.ssid, item);

                // Signal strength visualization
                const rssi = net.rssi;
                let signalBars = '📶';
                if (rssi > -50) signalBars = '📶📶📶';
                else if (rssi > -70) signalBars = '📶📶';

                // Encryption icon
                const encIcon = net.encryption === 'open' ? '🔓' : '🔒';

                item.innerHTML = `
                    <div>
                        <div class="network-name">${net.ssid || '(Hidden Network)'}</div>
                    </div>
                    <div class="network-info">
                        <span class="signal-bars">${signalBars}</span>
                        <span>${rssi} dBm</span>
                        <span>${encIcon}</span>
                    </div>
                `;
                networksList.appendChild(item);
            });

            showAlert(`Found ${data.count} network${data.count > 1 ? 's' : ''}`, 'success');
        }
    });
}

// ============================================================
// Network Selection
// ============================================================
function selectNetwork(ssid, element) {
    selectedSSID = ssid;
    document.getElementById('ssid').value = ssid;

    // Highlight selected network
    document.querySelectorAll('.network-item').forEach(item => {
        item.classList.remove('selected');
    });
    element.classList.add('selected');

    // Focus password field
    document.getElementById('password').focus();
}

// ============================================================
// WiFi Connection
// ============================================================
function connectWiFi() {
    const ssid = document.getElementById('ssid').value.trim();
    const password = document.getElementById('password').value;

    if (!ssid) {
        showAlert('Please enter network SSID', 'error');
        return;
    }

    showLoading();
    showAlert(`Connecting to "${ssid}"... Please wait`, 'info');

    apiPost('wifi/connect', { ssid, password }).then(data => {
        hideLoading();

        if (!data) return;

        if (data.success) {
            showAlert('Connecting... Credentials will be saved to NVS on success', 'success');
            // Refresh status after 5 seconds to show connection result
            setTimeout(loadStatus, 5000);
        } else {
            showAlert(data.error || 'Connection failed', 'error');
        }
    });
}

// ============================================================
// Disconnect
// ============================================================
function disconnect() {
    if (!confirm('Disconnect from WiFi network? (Saved credentials will remain in NVS)')) {
        return;
    }

    showLoading();

    apiPost('wifi/disconnect', {}).then(data => {
        hideLoading();

        if (!data) return;

        showAlert('Disconnected from WiFi network', 'success');
        setTimeout(loadStatus, 1000);
    });
}

// ============================================================
// Forget Network
// ============================================================
function forgetNetwork() {
    if (!confirm('Clear saved WiFi credentials from NVS? You will need to reconfigure on next boot.')) {
        return;
    }

    showLoading();

    apiPost('wifi/forget', {}).then(data => {
        hideLoading();

        if (!data) return;

        showAlert('WiFi credentials cleared from NVS', 'success');
        setTimeout(loadStatus, 1000);
    });
}

// ============================================================
// Page Initialization
// ============================================================
window.addEventListener('DOMContentLoaded', () => {
    // Initial status load
    loadStatus();

    // Auto-refresh every 15 seconds
    statusUpdateInterval = setInterval(loadStatus, 15000);
});

// Clean up interval when page unloads
window.addEventListener('beforeunload', () => {
    if (statusUpdateInterval) {
        clearInterval(statusUpdateInterval);
    }
});
</script>
)rawliteral";

// ============================================================
// Complete WiFi Configuration Page
// ============================================================

/**
 * @brief Generate complete WiFi configuration page with Material UI layout
 * @return Complete HTML page
 */
inline String getWiFiConfigPage() {
    String content;

    // Add WiFi-specific styles
    content += FPSTR(WIFI_PAGE_STYLES);

    // Add page content
    content += FPSTR(WIFI_PAGE_CONTENT);

    // Build complete page with layout
    return buildPageLayout("WiFi Configuration", content, "nav-wifi");
}

#endif // WIFI_CONFIG_PAGE_H
