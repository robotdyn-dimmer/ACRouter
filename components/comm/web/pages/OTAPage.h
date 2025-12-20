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
<div class="card">
    <div class="card-header">
        <h2 class="card-title">📦 Firmware Update</h2>
        <p class="card-subtitle">Upload new firmware over-the-air</p>
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
