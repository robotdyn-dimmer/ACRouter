/**
 * @file Layout.h
 * @brief Layout components - Header, Navigation, Footer
 *
 * Provides reusable layout templates for all web pages.
 * Includes Material UI inspired navigation drawer and app bar.
 */

#ifndef LAYOUT_H
#define LAYOUT_H

#include "Footer.h"

// ============================================================
// Navigation Menu Definition
// ============================================================

/**
 * @brief Navigation menu structure
 */
struct NavItem {
    const char* icon;
    const char* label;
    const char* url;
    bool active;
};

// ============================================================
// App Bar (Header) Component
// ============================================================

const char HTML_APP_BAR[] PROGMEM = R"rawliteral(
<div class="app-bar">
    <div class="app-bar-content">
        <div class="app-bar-left">
            <button class="btn-icon btn-text" onclick="toggleNav()" style="color: white; font-size: 1.5rem;">
                ☰
            </button>
            <h1 class="app-bar-title">
                <span class="nav-icon">⚡</span>
                ACRouter
                <span class="app-bar-version" id="header-version">--</span>
            </h1>
        </div>
        <div class="app-bar-right">
            <span id="status-badge" class="badge badge-neutral">--</span>
        </div>
    </div>
</div>
)rawliteral";

// ============================================================
// Navigation Drawer Component
// ============================================================

const char HTML_NAV_DRAWER[] PROGMEM = R"rawliteral(
<div id="navOverlay" class="nav-overlay" onclick="closeNav()"></div>
<nav id="navDrawer" class="nav-drawer">
    <ul class="nav-list">
        <li>
            <a href="/" class="nav-item" id="nav-dashboard">
                <span class="nav-icon">📊</span>
                <span>Dashboard</span>
            </a>
        </li>
        <li>
            <a href="/wifi" class="nav-item" id="nav-wifi">
                <span class="nav-icon">📡</span>
                <span>WiFi</span>
            </a>
        </li>
        <li>
            <a href="/mqtt" class="nav-item" id="nav-mqtt">
                <span class="nav-icon">🔗</span>
                <span>MQTT</span>
            </a>
        </li>
        <li>
            <a href="/settings/hardware" class="nav-item" id="nav-hardware">
                <span class="nav-icon">🔧</span>
                <span>Hardware Config</span>
            </a>
        </li>
        <li>
            <a href="/ota" class="nav-item" id="nav-ota">
                <span class="nav-icon">📦</span>
                <span>Firmware Update</span>
            </a>
        </li>
    </ul>
</nav>
)rawliteral";

// ============================================================
// Page Header Template
// ============================================================

/**
 * @brief Generate page header HTML
 * @param title Page title
 * @param includeStyles Whether to include Material CSS
 * @return Complete HTML header
 */
inline String getPageHeader(const char* title, bool includeStyles = true) {
    String html = "<!DOCTYPE html>\n<html lang=\"en\">\n<head>\n";
    html += "    <meta charset=\"UTF-8\">\n";
    html += "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n";
    html += "    <title>" + String(title) + " - ACRouter</title>\n";

    if (includeStyles) {
        html += "    <link rel=\"stylesheet\" href=\"/styles.css\">\n";
    }

    html += "</head>\n<body>\n";
    return html;
}

// ============================================================
// Page Footer Template
// ============================================================

const char HTML_FOOTER[] PROGMEM = R"rawliteral(
<script>
// ============================================================
// Navigation Toggle
// ============================================================
function toggleNav() {
    const drawer = document.getElementById('navDrawer');
    const overlay = document.getElementById('navOverlay');
    const content = document.getElementById('mainContent');
    const footer = document.querySelector('.app-footer');
    const isMobile = window.innerWidth <= 960;

    if (drawer) {
        if (isMobile) {
            // Mobile: use visible class to show/hide (CSS transform)
            const isVisible = drawer.classList.contains('visible');
            if (isVisible) {
                drawer.classList.remove('visible');
                if (overlay) overlay.classList.remove('visible');
            } else {
                drawer.classList.add('visible');
                if (overlay) overlay.classList.add('visible');
            }
        } else {
            // Desktop: use hidden class
            drawer.classList.toggle('hidden');
            if (content) {
                content.classList.toggle('full-width');
            }
            if (footer) {
                footer.classList.toggle('full-width');
            }
        }
    }
}

function closeNav() {
    const drawer = document.getElementById('navDrawer');
    const overlay = document.getElementById('navOverlay');
    if (drawer) drawer.classList.remove('visible');
    if (overlay) overlay.classList.remove('visible');
}

// ============================================================
// Active Navigation Item
// ============================================================
function setActiveNav(itemId) {
    // Remove active class from all items
    document.querySelectorAll('.nav-item').forEach(item => {
        item.classList.remove('active');
    });

    // Add active class to current item
    const activeItem = document.getElementById(itemId);
    if (activeItem) {
        activeItem.classList.add('active');
    }
}

// ============================================================
// Status Badge Update
// ============================================================
function updateStatusBadge(mode) {
    const badge = document.getElementById('status-badge');
    if (!badge) return;

    // Normalize mode to uppercase
    const modeUpper = (mode || '').toUpperCase();

    // Remove all classes
    badge.className = 'badge';

    // Set badge based on mode
    switch(modeUpper) {
        case 'OFF':
            badge.className = 'badge badge-neutral';
            badge.textContent = 'OFF';
            break;
        case 'AUTO':
            badge.className = 'badge badge-success';
            badge.textContent = 'AUTO';
            break;
        case 'ECO':
            badge.className = 'badge badge-info';
            badge.textContent = 'ECO';
            break;
        case 'OFFGRID':
            badge.className = 'badge badge-warning';
            badge.textContent = 'OFFGRID';
            break;
        case 'MANUAL':
            badge.className = 'badge badge-info';
            badge.textContent = 'MANUAL';
            break;
        case 'BOOST':
            badge.className = 'badge badge-error';
            badge.textContent = 'BOOST';
            break;
        default:
            badge.className = 'badge badge-neutral';
            badge.textContent = '--';
    }
}

// Load and update status badge (called on all pages)
async function loadStatusBadge() {
    try {
        const [statusData, infoData] = await Promise.all([
            fetch('/api/status').then(r => r.json()),
            fetch('/api/info').then(r => r.json())
        ]);

        // Update status badge
        if (statusData) {
            updateStatusBadge(statusData.mode);
        }

        // Update version in header
        if (infoData && infoData.version) {
            const headerVersion = document.getElementById('header-version');
            if (headerVersion) {
                headerVersion.textContent = 'v' + infoData.version;
            }
        }
    } catch (e) {
        // Silently fail - badge will show default
    }
}

// Auto-detect current page and set active nav
window.addEventListener('DOMContentLoaded', () => {
    const path = window.location.pathname;

    if (path === '/' || path === '/index.html') {
        setActiveNav('nav-dashboard');
    } else if (path.startsWith('/wifi')) {
        setActiveNav('nav-wifi');
    } else if (path.startsWith('/mqtt')) {
        setActiveNav('nav-mqtt');
    } else if (path.startsWith('/settings/hardware')) {
        setActiveNav('nav-hardware');
    } else if (path.startsWith('/ota')) {
        setActiveNav('nav-ota');
    }

    // Load status badge on all pages
    loadStatusBadge();

    // Refresh badge every 5 seconds
    setInterval(loadStatusBadge, 5000);
});

// ============================================================
// API Helper Functions
// ============================================================

async function apiGet(endpoint) {
    try {
        const response = await fetch('/api/' + endpoint);
        if (!response.ok) {
            throw new Error('HTTP ' + response.status);
        }
        return await response.json();
    } catch (error) {
        console.error('API GET error:', error);
        showAlert('Network error: ' + error.message, 'error');
        return null;
    }
}

async function apiPost(endpoint, data) {
    try {
        const response = await fetch('/api/' + endpoint, {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify(data)
        });

        if (!response.ok) {
            throw new Error('HTTP ' + response.status);
        }

        return await response.json();
    } catch (error) {
        console.error('API POST error:', error);
        showAlert('Network error: ' + error.message, 'error');
        return null;
    }
}

// ============================================================
// Alert Helper
// ============================================================
function showAlert(message, type = 'info') {
    // Remove existing alerts
    const existingAlerts = document.querySelectorAll('.alert');
    existingAlerts.forEach(alert => alert.remove());

    // Create new alert
    const alert = document.createElement('div');
    alert.className = 'alert alert-' + type;
    alert.innerHTML = '<span>' + message + '</span>';

    // Insert at top of main content
    const mainContent = document.getElementById('mainContent');
    if (mainContent) {
        mainContent.insertBefore(alert, mainContent.firstChild);

        // Auto-remove after 5 seconds
        setTimeout(() => {
            alert.remove();
        }, 5000);
    }
}

// ============================================================
// Loading Spinner
// ============================================================
function showLoading() {
    const overlay = document.createElement('div');
    overlay.id = 'loadingOverlay';
    overlay.className = 'loading-overlay';
    overlay.innerHTML = '<div class="spinner"></div>';
    document.body.appendChild(overlay);
}

function hideLoading() {
    const overlay = document.getElementById('loadingOverlay');
    if (overlay) {
        overlay.remove();
    }
}

// ============================================================
// Format Helpers
// ============================================================
function formatPower(watts) {
    if (Math.abs(watts) >= 1000) {
        return (watts / 1000).toFixed(2) + ' kW';
    }
    return watts.toFixed(1) + ' W';
}

function formatVoltage(volts) {
    return volts.toFixed(1) + ' V';
}

function formatCurrent(amps) {
    return amps.toFixed(2) + ' A';
}

function formatPercent(value) {
    return value.toFixed(0) + '%';
}
</script>
</body>
</html>
)rawliteral";

// ============================================================
// Complete Page Layout Helper
// ============================================================

/**
 * @brief Build complete page layout with header, nav, content, footer
 * @param title Page title
 * @param content Main page content HTML
 * @param activeNavId Active navigation item ID (e.g., "nav-dashboard")
 * @return Complete HTML page
 */
inline String buildPageLayout(const char* title, const String& content, const char* activeNavId = nullptr) {
    String html;

    // Header
    html += getPageHeader(title, true);

    // App Bar
    html += FPSTR(HTML_APP_BAR);

    // Navigation Drawer
    html += FPSTR(HTML_NAV_DRAWER);

    // Main Content
    html += "<main id=\"mainContent\" class=\"main-content\">\n";
    html += "    <div class=\"container\">\n";
    html += content;
    html += "    </div>\n";
    html += "</main>\n";

    // Footer Component
    html += FPSTR(HTML_FOOTER_COMPONENT);

    // Scripts & Close Tags
    html += FPSTR(HTML_FOOTER);

    return html;
}

#endif // LAYOUT_H
