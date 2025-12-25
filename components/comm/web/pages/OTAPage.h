/**
 * @file OTAPage.h
 * @brief OTA Firmware Update Page
 *
 * Material UI styled OTA update page with:
 * - Drag & drop file upload
 * - Progress bar
 * - Status messages
 */

#ifndef OTA_PAGE_H
#define OTA_PAGE_H

#include "../components/Layout.h"

// ============================================================
// OTA Page Specific Styles
// ============================================================

const char OTA_STYLES[] PROGMEM = R"rawliteral(
<style>
/* Upload Area */
.upload-area {
    border: 3px dashed var(--grey-400);
    border-radius: var(--radius-md);
    padding: var(--spacing-xl);
    text-align: center;
    transition: all var(--transition-fast);
    cursor: pointer;
    background: var(--grey-50);
}

.upload-area:hover {
    border-color: var(--primary-main);
    background: #f5f5ff;
}

.upload-area.dragover {
    border-color: var(--primary-main);
    background: #e8e8ff;
    transform: scale(1.02);
}

.upload-icon {
    font-size: 4rem;
    margin-bottom: var(--spacing-md);
}

.upload-text {
    color: var(--text-secondary);
    font-size: 1rem;
    margin-bottom: var(--spacing-sm);
}

.file-info {
    color: var(--primary-main);
    font-weight: 500;
    margin-top: var(--spacing-md);
    font-size: 0.875rem;
}

.file-input {
    display: none;
}

/* Progress */
.progress-container {
    margin-top: var(--spacing-lg);
}

.progress-bar {
    background: var(--grey-200);
    border-radius: var(--radius-sm);
    height: 24px;
    overflow: hidden;
    margin-bottom: var(--spacing-sm);
}

.progress-fill {
    background: linear-gradient(90deg, var(--primary-main) 0%, var(--primary-dark) 100%);
    height: 100%;
    width: 0%;
    transition: width 0.3s ease;
    display: flex;
    align-items: center;
    justify-content: center;
    color: white;
    font-weight: 500;
    font-size: 0.875rem;
}

.progress-status {
    text-align: center;
    color: var(--text-secondary);
    font-size: 0.875rem;
}

/* Warning Box */
.warning-box {
    background: #fff3e0;
    border-left: 4px solid var(--warning-main);
    padding: var(--spacing-md);
    margin-bottom: var(--spacing-lg);
    border-radius: var(--radius-sm);
    display: flex;
    align-items: center;
    gap: var(--spacing-sm);
}

.warning-box .icon {
    font-size: 1.5rem;
}

.warning-box .text {
    color: #e65100;
    font-size: 0.875rem;
}

/* Result Messages */
.result-success {
    background: #e8f5e9;
    border-left: 4px solid var(--success-main);
    padding: var(--spacing-md);
    margin-top: var(--spacing-lg);
    border-radius: var(--radius-sm);
    color: var(--success-dark);
}

.result-error {
    background: #ffebee;
    border-left: 4px solid var(--error-main);
    padding: var(--spacing-md);
    margin-top: var(--spacing-lg);
    border-radius: var(--radius-sm);
    color: var(--error-dark);
}

/* Hidden */
.hidden {
    display: none !important;
}
</style>
)rawliteral";

// ============================================================
// OTA Page Content
// ============================================================

const char OTA_CONTENT[] PROGMEM = R"rawliteral(
<!-- GitHub Update Card -->
<div class="card">
    <div class="card-header">
        <h2 class="card-title">🔄 Check for Updates</h2>
        <p class="card-subtitle">Automatically check for new firmware releases from GitHub</p>
    </div>
    <div class="card-content">
        <div style="display: flex; gap: var(--spacing-md); align-items: center; margin-bottom: var(--spacing-md);">
            <button class="btn btn-primary" id="checkUpdateBtn" onclick="checkForUpdates()">
                🔍 Check for Updates
            </button>
            <span id="checkStatus" style="color: var(--text-secondary); font-size: 0.875rem;"></span>
        </div>

        <!-- Update Available Box -->
        <div id="updateAvailableBox" class="hidden" style="background: #e3f2fd; border-left: 4px solid var(--info-main); padding: var(--spacing-md); margin-top: var(--spacing-md); border-radius: var(--radius-sm);">
            <div style="display: flex; justify-content: space-between; align-items: start; margin-bottom: var(--spacing-sm);">
                <div>
                    <h3 style="margin: 0 0 var(--spacing-xs) 0; color: var(--info-dark);">
                        <span id="updateVersionTitle">New Version Available</span>
                    </h3>
                    <p style="margin: 0; color: var(--text-secondary); font-size: 0.875rem;">
                        <strong>Current:</strong> <span id="currentVersionText">--</span> →
                        <strong>Latest:</strong> <span id="latestVersionText">--</span>
                    </p>
                </div>
            </div>

            <!-- Changelog -->
            <div style="margin: var(--spacing-md) 0;">
                <strong style="color: var(--text-primary);">What's new:</strong>
                <div id="changelogContent" style="background: white; padding: var(--spacing-sm); margin-top: var(--spacing-xs); border-radius: var(--radius-sm); max-height: 200px; overflow-y: auto; font-size: 0.875rem; white-space: pre-wrap; font-family: monospace;"></div>
            </div>

            <!-- Release Info -->
            <div style="display: flex; gap: var(--spacing-lg); margin: var(--spacing-md) 0; font-size: 0.875rem; color: var(--text-secondary);">
                <div>
                    <strong>Release:</strong> <span id="releaseName">--</span>
                </div>
                <div>
                    <strong>Published:</strong> <span id="publishedDate">--</span>
                </div>
                <div>
                    <strong>Size:</strong> <span id="assetSize">--</span>
                </div>
            </div>

            <!-- Update Button -->
            <button class="btn btn-primary" id="updateFromGitHubBtn" onclick="updateFromGitHub()" style="width: 100%;">
                🚀 Download and Install Update
            </button>
        </div>

        <!-- Up-to-date Box -->
        <div id="upToDateBox" class="hidden" style="background: #e8f5e9; border-left: 4px solid var(--success-main); padding: var(--spacing-md); margin-top: var(--spacing-md); border-radius: var(--radius-sm);">
            <div style="display: flex; align-items: center; gap: var(--spacing-sm);">
                <span style="font-size: 1.5rem;">✅</span>
                <div>
                    <strong style="color: var(--success-dark);">You're up to date!</strong>
                    <p style="margin: var(--spacing-xs) 0 0 0; color: var(--text-secondary); font-size: 0.875rem;">
                        Running version <span id="currentVersionUpToDate">--</span>
                    </p>
                </div>
            </div>
        </div>
    </div>
</div>

<!-- Local File Upload Card -->
<div class="card mt-lg">
    <div class="card-header">
        <h2 class="card-title">📦 Local File Update</h2>
        <p class="card-subtitle">Upload firmware file from your computer</p>
    </div>
    <div class="card-content">
        <div class="warning-box">
            <span class="icon">⚠️</span>
            <span class="text">Do not disconnect power or close this page during the update process!</span>
        </div>

        <div class="upload-area" id="uploadArea" onclick="document.getElementById('fileInput').click()">
            <div class="upload-icon">📁</div>
            <div class="upload-text">Click to select or drag & drop firmware file here</div>
            <div class="upload-text" style="font-size: 0.75rem; color: var(--grey-500);">Accepts .bin files only</div>
            <div class="file-info" id="fileInfo"></div>
        </div>

        <input type="file" id="fileInput" class="file-input" accept=".bin" onchange="handleFileSelect(event)">

        <div class="mt-lg">
            <button class="btn btn-primary" id="uploadBtn" onclick="uploadFirmware()" disabled style="width: 100%;">
                🚀 Upload Firmware
            </button>
        </div>

        <div class="progress-container hidden" id="progressContainer">
            <div class="progress-bar">
                <div class="progress-fill" id="progressFill">0%</div>
            </div>
            <div class="progress-status" id="progressStatus">Preparing...</div>
        </div>

        <div id="resultMessage"></div>
    </div>
</div>

<div class="card mt-lg">
    <div class="card-header">
        <h2 class="card-title">ℹ️ Current Firmware</h2>
    </div>
    <div class="card-content">
        <div class="status-row">
            <span class="status-label">ACRouter — Open Source Solar Router Controller</span>
        </div>     
        <div class="status-row">
            <span class="status-label">Version:</span>
            <span class="status-value" id="fw-version">--</span>
        </div>
        <div class="status-row">
            <span class="status-label">Support and hardware:</span>
            <span class="status-value">www.rbdimmer.com</span>
        </div>        
        <div class="status-row">
            <span class="status-label">Download:</span>
            <span class="status-value">https://github.com/robotdyn-dimmer/ACRouter</span>
        </div>                
        <div class="status-row">
            <span class="status-label">Uptime:</span>
            <span class="status-value" id="fw-uptime">--</span>
        </div>
        <div class="status-row">
            <span class="status-label">Free Heap:</span>
            <span class="status-value" id="fw-heap">--</span>
        </div>
    </div>
</div>

<script>
let selectedFile = null;
let latestReleaseUrl = null;

const uploadArea = document.getElementById('uploadArea');
const fileInput = document.getElementById('fileInput');
const uploadBtn = document.getElementById('uploadBtn');
const fileInfo = document.getElementById('fileInfo');
const progressContainer = document.getElementById('progressContainer');
const progressFill = document.getElementById('progressFill');
const progressStatus = document.getElementById('progressStatus');
const resultMessage = document.getElementById('resultMessage');

// Load firmware info
async function loadFirmwareInfo() {
    const data = await apiGet('info');
    if (data) {
        document.getElementById('fw-version').textContent = data.version || 'Unknown';
        document.getElementById('fw-uptime').textContent = formatUptime(data.uptime_sec || 0);
        document.getElementById('fw-heap').textContent = formatBytes(data.free_heap || 0);
    }
}

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

// Drag and drop handlers
uploadArea.addEventListener('dragover', (e) => {
    e.preventDefault();
    uploadArea.classList.add('dragover');
});

uploadArea.addEventListener('dragleave', () => {
    uploadArea.classList.remove('dragover');
});

uploadArea.addEventListener('drop', (e) => {
    e.preventDefault();
    uploadArea.classList.remove('dragover');
    const files = e.dataTransfer.files;
    if (files.length > 0) {
        handleFile(files[0]);
    }
});

function handleFileSelect(event) {
    const file = event.target.files[0];
    if (file) {
        handleFile(file);
    }
}

function handleFile(file) {
    if (!file.name.endsWith('.bin')) {
        showAlert('Please select a .bin firmware file', 'error');
        return;
    }
    selectedFile = file;
    fileInfo.textContent = `📦 ${file.name} (${(file.size / 1024 / 1024).toFixed(2)} MB)`;
    uploadBtn.disabled = false;
}

function uploadFirmware() {
    if (!selectedFile) return;

    uploadBtn.disabled = true;
    progressContainer.classList.remove('hidden');
    resultMessage.innerHTML = '';

    const formData = new FormData();
    formData.append('firmware', selectedFile);

    const xhr = new XMLHttpRequest();

    xhr.upload.addEventListener('progress', (e) => {
        if (e.lengthComputable) {
            const percent = Math.round((e.loaded / e.total) * 100);
            progressFill.style.width = percent + '%';
            progressFill.textContent = percent + '%';
            progressStatus.textContent = `Uploading... ${(e.loaded / 1024 / 1024).toFixed(2)} MB / ${(e.total / 1024 / 1024).toFixed(2)} MB`;
        }
    });

    xhr.addEventListener('load', () => {
        if (xhr.status === 200) {
            progressFill.style.width = '100%';
            progressFill.textContent = '100%';
            progressStatus.textContent = 'Update complete!';
            resultMessage.innerHTML = '<div class="result-success">✅ Firmware updated successfully! Device will reboot in 3 seconds...</div>';
            setTimeout(() => {
                window.location.href = '/';
            }, 5000);
        } else {
            resultMessage.innerHTML = '<div class="result-error">❌ Update failed: ' + xhr.statusText + '</div>';
            uploadBtn.disabled = false;
        }
    });

    xhr.addEventListener('error', () => {
        resultMessage.innerHTML = '<div class="result-error">❌ Upload error occurred. Please try again.</div>';
        uploadBtn.disabled = false;
    });

    xhr.open('POST', '/ota/upload');
    xhr.send(formData);
}

// ============================================================
// GitHub Update Functions
// ============================================================

async function checkForUpdates() {
    const checkBtn = document.getElementById('checkUpdateBtn');
    const checkStatus = document.getElementById('checkStatus');
    const updateBox = document.getElementById('updateAvailableBox');
    const upToDateBox = document.getElementById('upToDateBox');

    // Disable button and show loading
    checkBtn.disabled = true;
    checkStatus.textContent = 'Checking...';
    updateBox.classList.add('hidden');
    upToDateBox.classList.add('hidden');

    try {
        const response = await fetch('/api/ota/check-github');
        const data = await response.json();

        if (data.success) {
            if (data.update_available) {
                // New version available
                document.getElementById('currentVersionText').textContent = data.current_version;
                document.getElementById('latestVersionText').textContent = data.latest_version;
                document.getElementById('updateVersionTitle').textContent = data.release_name || 'New Version Available';
                document.getElementById('releaseName').textContent = data.release_name || '--';
                document.getElementById('publishedDate').textContent = formatDate(data.published_at);
                document.getElementById('assetSize').textContent = formatBytes(data.asset_size);
                document.getElementById('changelogContent').textContent = data.changelog || 'No changelog available.';

                // Store URL for update
                latestReleaseUrl = data.asset_url;

                // Show update box
                updateBox.classList.remove('hidden');
                checkStatus.textContent = 'Update available!';
                checkStatus.style.color = 'var(--info-main)';
            } else {
                // Up to date
                document.getElementById('currentVersionUpToDate').textContent = data.current_version;
                upToDateBox.classList.remove('hidden');
                checkStatus.textContent = 'You are up to date';
                checkStatus.style.color = 'var(--success-main)';
            }
        } else {
            // Error checking
            showAlert(data.error || 'Failed to check for updates', 'error');
            checkStatus.textContent = 'Check failed';
            checkStatus.style.color = 'var(--error-main)';
        }
    } catch (error) {
        showAlert('Network error: ' + error.message, 'error');
        checkStatus.textContent = 'Network error';
        checkStatus.style.color = 'var(--error-main)';
    } finally {
        checkBtn.disabled = false;
    }
}

async function updateFromGitHub() {
    if (!latestReleaseUrl) {
        showAlert('No update URL available. Please check for updates first.', 'error');
        return;
    }

    const updateBtn = document.getElementById('updateFromGitHubBtn');
    const checkBtn = document.getElementById('checkUpdateBtn');

    // Confirm with user
    if (!confirm('This will download and install the new firmware. The device will reboot. Continue?')) {
        return;
    }

    // Disable buttons
    updateBtn.disabled = true;
    checkBtn.disabled = true;

    // Show progress
    progressContainer.classList.remove('hidden');
    progressFill.style.width = '0%';
    progressFill.textContent = '0%';
    progressStatus.textContent = 'Downloading firmware from GitHub...';

    try {
        const response = await fetch('/api/ota/update-github', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                url: latestReleaseUrl
            })
        });

        const data = await response.json();

        if (data.success) {
            // Simulate progress (actual progress happens on backend)
            let progress = 0;
            const progressInterval = setInterval(() => {
                progress += 5;
                if (progress <= 90) {
                    progressFill.style.width = progress + '%';
                    progressFill.textContent = progress + '%';
                    progressStatus.textContent = 'Downloading and installing...';
                } else {
                    clearInterval(progressInterval);
                }
            }, 500);

            // Wait for device reboot (will lose connection)
            setTimeout(() => {
                clearInterval(progressInterval);
                progressFill.style.width = '100%';
                progressFill.textContent = '100%';
                progressStatus.textContent = 'Update complete! Device is rebooting...';
                resultMessage.innerHTML = '<div class="result-success">✅ Firmware updated successfully! Device will reconnect shortly...</div>';

                // Try to reconnect after 10 seconds
                setTimeout(() => {
                    window.location.href = '/';
                }, 10000);
            }, 3000);
        } else {
            resultMessage.innerHTML = '<div class="result-error">❌ Update failed: ' + (data.error || 'Unknown error') + '</div>';
            updateBtn.disabled = false;
            checkBtn.disabled = false;
        }
    } catch (error) {
        resultMessage.innerHTML = '<div class="result-error">❌ Network error: ' + error.message + '</div>';
        updateBtn.disabled = false;
        checkBtn.disabled = false;
    }
}

function formatDate(dateString) {
    if (!dateString) return '--';
    const date = new Date(dateString);
    return date.toLocaleDateString() + ' ' + date.toLocaleTimeString();
}

// Initialize
window.addEventListener('DOMContentLoaded', () => {
    loadFirmwareInfo();
});
</script>
)rawliteral";

// ============================================================
// Complete OTA Page
// ============================================================

/**
 * @brief Generate complete OTA page with Material UI layout
 * @return Complete HTML page
 */
inline String getOTAPage() {
    String content;

    // Add OTA-specific styles
    content += FPSTR(OTA_STYLES);

    // Add page content
    content += FPSTR(OTA_CONTENT);

    // Build complete page with layout
    return buildPageLayout("Firmware Update", content, "nav-ota");
}

#endif // OTA_PAGE_H
