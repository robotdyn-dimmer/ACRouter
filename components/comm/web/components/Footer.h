/**
 * @file Footer.h
 * @brief Footer component with version info and links
 */

#ifndef FOOTER_H
#define FOOTER_H

const char HTML_FOOTER_COMPONENT[] PROGMEM = R"rawliteral(
<footer class="app-footer">
    <div class="footer-content">
        <div class="footer-section">
            <div class="footer-title">⚡ ACRouter <span id="footer-version">--</span></div>
            <div class="footer-text">Solar Power Router Controller</div>
        </div>
        <div class="footer-section">
            <div class="footer-links">
                <a href="/docs" class="footer-link">📚 Documentation</a>
                <a href="https://github.com/shuliga/acrouter-fw" class="footer-link" target="_blank">💻 GitHub</a>
                <a href="http://www.rbdimmer.com" class="footer-link" target="_blank">🛠️ rbdimmer.com</a>
            </div>
        </div>
        <div class="footer-section">
            <div class="footer-stat">
                <span class="footer-stat-label">Uptime:</span>
                <span class="footer-stat-value" id="footer-uptime">--</span>
            </div>
            <div class="footer-stat">
                <span class="footer-stat-label">Free Heap:</span>
                <span class="footer-stat-value" id="footer-heap">--</span>
            </div>
        </div>
    </div>
</footer>

<script>
// Update footer stats
async function updateFooterStats() {
    try {
        const data = await fetch('/api/info').then(r => r.json());
        if (data) {
            // Update version
            if (data.version) {
                const footerVersion = document.getElementById('footer-version');
                if (footerVersion) {
                    footerVersion.textContent = 'v' + data.version;
                }
            }

            // Update uptime
            if (data.uptime_sec !== undefined) {
                const footerUptime = document.getElementById('footer-uptime');
                if (footerUptime) {
                    footerUptime.textContent = formatUptime(data.uptime_sec);
                }
            }

            // Update heap
            if (data.free_heap !== undefined) {
                const footerHeap = document.getElementById('footer-heap');
                if (footerHeap) {
                    footerHeap.textContent = formatBytes(data.free_heap);
                }
            }
        }
    } catch (e) {
        // Silently fail
    }
}

// Helper: Format uptime
function formatUptime(seconds) {
    const days = Math.floor(seconds / 86400);
    const hours = Math.floor((seconds % 86400) / 3600);
    const minutes = Math.floor((seconds % 3600) / 60);

    if (days > 0) {
        return days + 'd ' + hours + 'h';
    } else if (hours > 0) {
        return hours + 'h ' + minutes + 'm';
    } else {
        return minutes + 'm';
    }
}

// Helper: Format bytes
function formatBytes(bytes) {
    if (bytes >= 1024 * 1024) {
        return (bytes / (1024 * 1024)).toFixed(1) + ' MB';
    } else if (bytes >= 1024) {
        return (bytes / 1024).toFixed(1) + ' KB';
    } else {
        return bytes + ' B';
    }
}

// Update footer stats on page load and every 30 seconds
window.addEventListener('DOMContentLoaded', () => {
    updateFooterStats();
    setInterval(updateFooterStats, 30000);
});
</script>
)rawliteral";

#endif // FOOTER_H
