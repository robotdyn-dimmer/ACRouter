/**
 * @file WiFiConfigPage.h
 * @brief WiFi Configuration Web Page
 *
 * HTML page for WiFi network configuration via web interface
 */

#pragma once

// WiFi-specific CSS styles
const char WIFI_CSS[] PROGMEM = R"rawliteral(
.network-list {
    max-height: 300px;
    overflow-y: auto;
    margin-bottom: 20px;
    border: 1px solid #e0e0e0;
    border-radius: 8px;
}
.network-item {
    padding: 15px;
    border-bottom: 1px solid #e0e0e0;
    cursor: pointer;
    transition: background 0.2s;
    display: flex;
    justify-content: space-between;
    align-items: center;
}
.network-item:hover { background: #f5f5f5; }
.network-item.selected { background: #e8eaf6; }
.network-name { font-weight: 600; color: #333; }
.network-info {
    display: flex;
    gap: 15px;
    font-size: 13px;
    color: #666;
}
.ip-address {
    background: #e8f5e9;
    color: #2e7d32;
    padding: 10px 15px;
    border-radius: 8px;
    margin-top: 15px;
    text-align: center;
    font-weight: 600;
}
)rawliteral";

// WiFi Configuration HTML Page
const char WIFI_CONFIG_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WiFi Configuration - ACRouter</title>
    <link rel="stylesheet" href="/styles.css">
    <style>
        .network-list {
            max-height: 300px;
            overflow-y: auto;
            margin-bottom: 20px;
            border: 1px solid #e0e0e0;
            border-radius: 8px;
        }
        .network-item {
            padding: 15px;
            border-bottom: 1px solid #e0e0e0;
            cursor: pointer;
            transition: background 0.2s;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }
        .network-item:hover { background: #f5f5f5; }
        .network-item.selected { background: #e8eaf6; }
        .network-name { font-weight: 600; color: #333; }
        .network-info {
            display: flex;
            gap: 15px;
            font-size: 13px;
            color: #666;
        }
        .ip-address {
            background: #e8f5e9;
            color: #2e7d32;
            padding: 10px 15px;
            border-radius: 8px;
            margin-top: 15px;
            text-align: center;
            font-weight: 600;
            font-family: 'Courier New', monospace;
        }
        .ip-label {
            font-size: 12px;
            color: #666;
            margin-bottom: 5px;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🌐 WiFi Configuration</h1>
        <p class="subtitle">Configure your ACRouter WiFi connection</p>

        <div id="message" class="message"></div>

        <div class="status-box">
            <h3>Current Status</h3>
            <div class="status-item">
                <span class="status-label">State:</span>
                <span class="status-value" id="status-state">Loading...</span>
            </div>
            <div class="status-item" id="status-sta" style="display:none;">
                <span class="status-label">Connected to:</span>
                <span class="status-value" id="status-sta-ssid"></span>
            </div>
            <div class="status-item" id="status-rssi" style="display:none;">
                <span class="status-label">Signal:</span>
                <span class="status-value" id="status-sta-rssi"></span>
            </div>
        </div>

        <!-- STA IP Address Display -->
        <div id="sta-ip-box" style="display:none;">
            <div class="ip-label">Access via STA network:</div>
            <div class="ip-address">
                <span id="status-sta-ip"></span>
            </div>
        </div>

        <button class="btn btn-primary" onclick="scanNetworks()">
            🔍 Scan for Networks<span id="scan-spinner" style="display:none;" class="spinner"></span>
        </button>

        <div id="networks" class="network-list" style="display:none; margin-top:20px;">
        </div>

        <div class="form-group">
            <label for="ssid">Network Name (SSID)</label>
            <input type="text" id="ssid" placeholder="Enter network SSID">
        </div>

        <div class="form-group">
            <label for="password">Password</label>
            <input type="password" id="password" placeholder="Enter network password">
        </div>

        <button class="btn btn-primary" onclick="connectWiFi()">
            ✓ Connect & Save to NVS
        </button>

        <button class="btn btn-secondary" onclick="disconnect()">
            ⏸ Disconnect
        </button>

        <button class="btn btn-danger" onclick="forgetNetwork()">
            🗑 Forget Saved Network
        </button>
    </div>

    <script>
        let selectedSSID = '';
        let statusUpdateInterval;

        // Load status on page load and start auto-refresh
        window.onload = function() {
            loadStatus();
            // Auto-refresh every 15 seconds
            statusUpdateInterval = setInterval(loadStatus, 15000);
        };

        function showMessage(text, type = 'success') {
            const msg = document.getElementById('message');
            msg.textContent = text;
            msg.className = 'message ' + type;
            msg.style.display = 'block';
            setTimeout(() => { msg.style.display = 'none'; }, 5000);
        }

        function loadStatus() {
            fetch('/api/wifi/status')
                .then(r => r.json())
                .then(data => {
                    document.getElementById('status-state').textContent = data.state;

                    if (data.sta_connected) {
                        // Show STA connection info
                        document.getElementById('status-sta').style.display = 'flex';
                        document.getElementById('status-rssi').style.display = 'flex';
                        document.getElementById('status-sta-ssid').textContent = data.sta_ssid;
                        document.getElementById('status-sta-rssi').textContent = data.rssi + ' dBm';

                        // Show STA IP address with copy button
                        if (data.sta_ip) {
                            document.getElementById('sta-ip-box').style.display = 'block';
                            const ipUrl = 'http://' + data.sta_ip;
                            document.getElementById('status-sta-ip').innerHTML =
                                '<a href="' + ipUrl + '" target="_blank" style="color:#2e7d32; text-decoration:none;">' +
                                ipUrl + ' 🔗</a>';
                        }
                    } else {
                        document.getElementById('status-sta').style.display = 'none';
                        document.getElementById('status-rssi').style.display = 'none';
                        document.getElementById('sta-ip-box').style.display = 'none';
                    }
                })
                .catch(e => {
                    console.error('Failed to load status:', e);
                });
        }

        function scanNetworks() {
            const spinner = document.getElementById('scan-spinner');
            spinner.style.display = 'inline-block';

            fetch('/api/wifi/scan')
                .then(r => r.json())
                .then(data => {
                    spinner.style.display = 'none';
                    const list = document.getElementById('networks');
                    list.innerHTML = '';

                    if (data.count === 0) {
                        list.innerHTML = '<div class="network-item">No networks found</div>';
                    } else {
                        data.networks.forEach(net => {
                            const item = document.createElement('div');
                            item.className = 'network-item';
                            item.onclick = () => selectNetwork(net.ssid, item);

                            const signalBars = net.rssi > -50 ? '📶📶📶' :
                                             net.rssi > -70 ? '📶📶' : '📶';

                            item.innerHTML = `
                                <span class="network-name">${net.ssid}</span>
                                <div class="network-info">
                                    <span>${signalBars} ${net.rssi} dBm</span>
                                    <span>${net.encryption === 'open' ? '🔓 Open' : '🔒 Secured'}</span>
                                </div>
                            `;
                            list.appendChild(item);
                        });
                    }

                    list.style.display = 'block';
                    showMessage(`Found ${data.count} networks`, 'success');
                })
                .catch(e => {
                    spinner.style.display = 'none';
                    showMessage('Scan failed', 'error');
                });
        }

        function selectNetwork(ssid, element) {
            selectedSSID = ssid;
            document.getElementById('ssid').value = ssid;

            // Highlight selected
            document.querySelectorAll('.network-item').forEach(item => {
                item.classList.remove('selected');
            });
            element.classList.add('selected');
        }

        function connectWiFi() {
            const ssid = document.getElementById('ssid').value;
            const password = document.getElementById('password').value;

            if (!ssid) {
                showMessage('Please enter SSID', 'error');
                return;
            }

            showMessage('Connecting to ' + ssid + '... Please wait', 'info');

            fetch('/api/wifi/connect', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify({ ssid, password })
            })
            .then(r => r.json())
            .then(data => {
                if (data.success) {
                    showMessage('Connecting... Will save to NVS on success', 'success');
                    // Refresh status after 5 seconds to show connection result
                    setTimeout(loadStatus, 5000);
                } else {
                    showMessage(data.error || 'Connection failed', 'error');
                }
            })
            .catch(e => showMessage('Request failed', 'error'));
        }

        function disconnect() {
            if (!confirm('Disconnect from WiFi network?')) return;

            fetch('/api/wifi/disconnect', { method: 'POST' })
                .then(r => r.json())
                .then(data => {
                    showMessage('Disconnected from WiFi', 'success');
                    setTimeout(loadStatus, 1000);
                })
                .catch(e => showMessage('Disconnect failed', 'error'));
        }

        function forgetNetwork() {
            if (!confirm('Forget saved WiFi credentials from NVS?')) return;

            fetch('/api/wifi/forget', { method: 'POST' })
                .then(r => r.json())
                .then(data => {
                    showMessage('WiFi credentials cleared from NVS', 'success');
                    setTimeout(loadStatus, 1000);
                })
                .catch(e => showMessage('Failed to clear credentials', 'error'));
        }

        // Clean up interval when page unloads
        window.onbeforeunload = function() {
            if (statusUpdateInterval) {
                clearInterval(statusUpdateInterval);
            }
        };
    </script>
</body>
</html>
)rawliteral";
