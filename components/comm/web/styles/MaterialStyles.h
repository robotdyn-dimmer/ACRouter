/**
 * @file MaterialStyles.h
 * @brief Material UI inspired styles for ACRouter web interface
 *
 * Enhanced CSS with Material Design principles:
 * - Material UI color palette
 * - Card-based layout
 * - Responsive design
 * - Modern typography
 * - Smooth animations
 */

#ifndef MATERIAL_STYLES_H
#define MATERIAL_STYLES_H

const char MATERIAL_CSS[] PROGMEM = R"rawliteral(
/* ============================================================
   CSS Variables - Material UI Color Palette
   ============================================================ */
:root {
    /* Primary Colors (Blue) */
    --primary-main: #1976d2;
    --primary-light: #42a5f5;
    --primary-dark: #1565c0;
    --primary-contrast: #ffffff;

    /* Secondary Colors (Pink) */
    --secondary-main: #dc004e;
    --secondary-light: #f50057;
    --secondary-dark: #c51162;
    --secondary-contrast: #ffffff;

    /* Success/Error/Warning/Info */
    --success-main: #2e7d32;
    --success-light: #4caf50;
    --error-main: #d32f2f;
    --error-light: #f44336;
    --warning-main: #ed6c02;
    --warning-light: #ff9800;
    --info-main: #0288d1;
    --info-light: #03a9f4;

    /* Neutral Colors */
    --grey-50: #fafafa;
    --grey-100: #f5f5f5;
    --grey-200: #eeeeee;
    --grey-300: #e0e0e0;
    --grey-400: #bdbdbd;
    --grey-500: #9e9e9e;
    --grey-600: #757575;
    --grey-700: #616161;
    --grey-800: #424242;
    --grey-900: #212121;

    /* Background */
    --bg-default: #fafafa;
    --bg-paper: #ffffff;

    /* Text */
    --text-primary: rgba(0, 0, 0, 0.87);
    --text-secondary: rgba(0, 0, 0, 0.6);
    --text-disabled: rgba(0, 0, 0, 0.38);

    /* Shadows */
    --shadow-1: 0 2px 4px rgba(0,0,0,0.1);
    --shadow-2: 0 4px 8px rgba(0,0,0,0.12);
    --shadow-4: 0 8px 16px rgba(0,0,0,0.15);
    --shadow-8: 0 16px 32px rgba(0,0,0,0.2);

    /* Spacing */
    --spacing-xs: 4px;
    --spacing-sm: 8px;
    --spacing-md: 16px;
    --spacing-lg: 24px;
    --spacing-xl: 32px;

    /* Border Radius */
    --radius-sm: 4px;
    --radius-md: 8px;
    --radius-lg: 12px;
    --radius-xl: 16px;

    /* Transitions */
    --transition-fast: 150ms cubic-bezier(0.4, 0, 0.2, 1);
    --transition-normal: 250ms cubic-bezier(0.4, 0, 0.2, 1);
    --transition-slow: 350ms cubic-bezier(0.4, 0, 0.2, 1);
}

/* ============================================================
   Base Styles
   ============================================================ */
* {
    margin: 0;
    padding: 0;
    box-sizing: border-box;
}

body {
    font-family: 'Roboto', 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background-color: var(--bg-default);
    color: var(--text-primary);
    line-height: 1.5;
    -webkit-font-smoothing: antialiased;
    -moz-osx-font-smoothing: grayscale;
}

/* ============================================================
   Layout Components
   ============================================================ */

/* App Bar (Header) */
.app-bar {
    background-color: var(--primary-main);
    color: var(--primary-contrast);
    box-shadow: var(--shadow-4);
    position: sticky;
    top: 0;
    z-index: 1100;
}

.app-bar-content {
    max-width: 1280px;
    margin: 0 auto;
    padding: var(--spacing-md) var(--spacing-lg);
    display: flex;
    align-items: center;
    justify-content: space-between;
}

.app-bar-left {
    display: flex;
    align-items: center;
    gap: var(--spacing-sm);
}

.app-bar-right {
    display: flex;
    align-items: center;
    gap: var(--spacing-md);
}

.app-bar-title {
    font-size: 1.25rem;
    font-weight: 500;
    display: flex;
    align-items: baseline;
    gap: var(--spacing-xs);
    margin: 0;
}

.app-bar-version {
    font-size: 0.75rem;
    font-weight: 400;
    opacity: 0.85;
    letter-spacing: 0.5px;
}

#status-badge {
    font-size: 0.875rem;
    padding: 6px 16px;
    min-width: 80px;
    text-align: center;
    box-shadow: 0 2px 4px rgba(0,0,0,0.2);
}

/* Navigation Drawer */
.nav-drawer {
    width: 240px;
    background-color: var(--bg-paper);
    box-shadow: var(--shadow-2);
    position: fixed;
    left: 0;
    top: 64px;
    bottom: 0;
    overflow-y: auto;
    transition: transform var(--transition-normal);
}

.nav-drawer.hidden {
    transform: translateX(-100%);
}

/* Mobile Nav Overlay */
.nav-overlay {
    display: none;
    position: fixed;
    top: 64px;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: rgba(0, 0, 0, 0.5);
    z-index: 1040;
}

.nav-overlay.visible {
    display: block;
}

.nav-list {
    list-style: none;
    padding: var(--spacing-sm) 0;
}

.nav-item {
    padding: var(--spacing-sm) var(--spacing-lg);
    cursor: pointer;
    transition: background-color var(--transition-fast);
    display: flex;
    align-items: center;
    gap: var(--spacing-md);
    color: var(--text-primary);
    text-decoration: none;
}

.nav-item:hover {
    background-color: var(--grey-100);
}

.nav-item.active {
    background-color: var(--primary-light);
    color: var(--primary-contrast);
    border-left: 4px solid var(--primary-dark);
}

.nav-icon {
    font-size: 1.5rem;
    width: 24px;
    text-align: center;
}

/* Main Content Area */
.main-content {
    margin-left: 240px;
    padding: var(--spacing-lg);
    min-height: calc(100vh - 64px);
    transition: margin-left var(--transition-normal);
}

.main-content.full-width {
    margin-left: 0;
}

/* Container */
.container {
    max-width: 1280px;
    margin: 0 auto;
    padding: 0 var(--spacing-lg);
}

.container-sm {
    max-width: 600px;
}

.container-md {
    max-width: 960px;
}

/* ============================================================
   Cards
   ============================================================ */
.card {
    background-color: var(--bg-paper);
    border-radius: var(--radius-lg);
    box-shadow: var(--shadow-2);
    overflow: hidden;
    transition: box-shadow var(--transition-normal);
}

.card:hover {
    box-shadow: var(--shadow-4);
}

.card-header {
    padding: var(--spacing-md) var(--spacing-lg);
    border-bottom: 1px solid var(--grey-200);
}

.card-title {
    font-size: 1.25rem;
    font-weight: 500;
    color: var(--text-primary);
}

.card-subtitle {
    font-size: 0.875rem;
    color: var(--text-secondary);
    margin-top: var(--spacing-xs);
}

.card-content {
    padding: var(--spacing-lg);
}

.card-actions {
    padding: var(--spacing-md) var(--spacing-lg);
    display: flex;
    gap: var(--spacing-sm);
    justify-content: flex-end;
}

/* ============================================================
   Buttons
   ============================================================ */
.btn {
    padding: 8px 22px;
    border: none;
    border-radius: var(--radius-sm);
    font-size: 0.875rem;
    font-weight: 500;
    text-transform: uppercase;
    letter-spacing: 0.5px;
    cursor: pointer;
    transition: all var(--transition-fast);
    display: inline-flex;
    align-items: center;
    gap: var(--spacing-sm);
    text-decoration: none;
    white-space: nowrap;
}

.btn:disabled {
    opacity: 0.38;
    cursor: not-allowed;
}

/* Contained Buttons */
.btn-primary {
    background-color: var(--primary-main);
    color: var(--primary-contrast);
    box-shadow: var(--shadow-2);
}

.btn-primary:hover:not(:disabled) {
    background-color: var(--primary-dark);
    box-shadow: var(--shadow-4);
}

.btn-secondary {
    background-color: var(--secondary-main);
    color: var(--secondary-contrast);
    box-shadow: var(--shadow-2);
}

.btn-secondary:hover:not(:disabled) {
    background-color: var(--secondary-dark);
    box-shadow: var(--shadow-4);
}

.btn-success {
    background-color: var(--success-main);
    color: white;
    box-shadow: var(--shadow-2);
}

.btn-success:hover:not(:disabled) {
    background-color: #1b5e20;
    box-shadow: var(--shadow-4);
}

.btn-error {
    background-color: var(--error-main);
    color: white;
    box-shadow: var(--shadow-2);
}

.btn-error:hover:not(:disabled) {
    background-color: #b71c1c;
    box-shadow: var(--shadow-4);
}

.btn-warning {
    background-color: var(--warning-main);
    color: white;
    box-shadow: var(--shadow-2);
}

.btn-warning:hover:not(:disabled) {
    background-color: #e65100;
    box-shadow: var(--shadow-4);
}

/* Outlined Buttons */
.btn-outlined {
    background-color: transparent;
    border: 1px solid currentColor;
    box-shadow: none;
}

.btn-outlined.btn-primary {
    color: var(--primary-main);
}

.btn-outlined.btn-primary:hover:not(:disabled) {
    background-color: rgba(25, 118, 210, 0.08);
}

/* Text Buttons */
.btn-text {
    background-color: transparent;
    box-shadow: none;
    padding: 6px 12px;
}

.btn-text:hover:not(:disabled) {
    background-color: rgba(0, 0, 0, 0.04);
}

/* Icon Buttons */
.btn-icon {
    padding: 8px;
    border-radius: 50%;
    min-width: 40px;
    min-height: 40px;
    display: inline-flex;
    align-items: center;
    justify-content: center;
}

/* ============================================================
   Form Controls
   ============================================================ */
.form-group {
    margin-bottom: var(--spacing-lg);
}

.form-label {
    display: block;
    margin-bottom: var(--spacing-sm);
    font-size: 0.875rem;
    font-weight: 500;
    color: var(--text-primary);
}

.form-control {
    width: 100%;
    padding: 12px 16px;
    border: 1px solid var(--grey-300);
    border-radius: var(--radius-sm);
    font-size: 1rem;
    font-family: inherit;
    transition: all var(--transition-fast);
    background-color: var(--bg-paper);
    color: var(--text-primary);
}

.form-control:focus {
    outline: none;
    border-color: var(--primary-main);
    box-shadow: 0 0 0 2px rgba(25, 118, 210, 0.2);
}

.form-control:disabled {
    background-color: var(--grey-100);
    color: var(--text-disabled);
    cursor: not-allowed;
}

.form-control.error {
    border-color: var(--error-main);
}

.form-control.error:focus {
    box-shadow: 0 0 0 2px rgba(211, 47, 47, 0.2);
}

.form-helper {
    margin-top: var(--spacing-xs);
    font-size: 0.75rem;
    color: var(--text-secondary);
}

.form-helper.error {
    color: var(--error-main);
}

select.form-control {
    cursor: pointer;
}

/* ============================================================
   Status & Alerts
   ============================================================ */
.alert {
    padding: var(--spacing-md);
    border-radius: var(--radius-sm);
    margin-bottom: var(--spacing-md);
    display: flex;
    align-items: flex-start;
    gap: var(--spacing-md);
}

.alert-success {
    background-color: #e8f5e9;
    color: #1b5e20;
    border-left: 4px solid var(--success-main);
}

.alert-error {
    background-color: #ffebee;
    color: #b71c1c;
    border-left: 4px solid var(--error-main);
}

.alert-warning {
    background-color: #fff3e0;
    color: #e65100;
    border-left: 4px solid var(--warning-main);
}

.alert-info {
    background-color: #e3f2fd;
    color: #01579b;
    border-left: 4px solid var(--info-main);
}

/* Status Badges */
.badge {
    display: inline-block;
    padding: 4px 12px;
    border-radius: 12px;
    font-size: 0.75rem;
    font-weight: 500;
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

.badge-success {
    background-color: var(--success-light);
    color: white;
}

.badge-error {
    background-color: var(--error-light);
    color: white;
}

.badge-warning {
    background-color: var(--warning-light);
    color: white;
}

.badge-info {
    background-color: var(--info-light);
    color: white;
}

.badge-neutral {
    background-color: var(--grey-400);
    color: white;
}

/* ============================================================
   Metrics Display
   ============================================================ */
.metric-card {
    text-align: center;
    padding: var(--spacing-lg);
}

.metric-value {
    font-size: 2.5rem;
    font-weight: 300;
    color: var(--text-primary);
    line-height: 1;
}

.metric-label {
    font-size: 0.875rem;
    color: var(--text-secondary);
    margin-top: var(--spacing-sm);
    text-transform: uppercase;
    letter-spacing: 0.5px;
}

.metric-unit {
    font-size: 1.25rem;
    color: var(--text-secondary);
    margin-left: var(--spacing-xs);
}

/* ============================================================
   Grid System
   ============================================================ */
.grid {
    display: grid;
    gap: var(--spacing-lg);
}

.grid-2 {
    grid-template-columns: repeat(2, 1fr);
}

.grid-3 {
    grid-template-columns: repeat(3, 1fr);
}

.grid-4 {
    grid-template-columns: repeat(4, 1fr);
}

@media (max-width: 1024px) {
    .grid-4 {
        grid-template-columns: repeat(2, 1fr);
    }
}

@media (max-width: 768px) {
    .grid-2, .grid-3, .grid-4 {
        grid-template-columns: 1fr;
    }
}

/* ============================================================
   Loading & Spinner
   ============================================================ */
.spinner {
    border: 3px solid var(--grey-200);
    border-top-color: var(--primary-main);
    border-radius: 50%;
    width: 40px;
    height: 40px;
    animation: spin 0.8s linear infinite;
}

.spinner-sm {
    width: 20px;
    height: 20px;
    border-width: 2px;
}

@keyframes spin {
    to { transform: rotate(360deg); }
}

/* Loading Overlay */
.loading-overlay {
    position: fixed;
    top: 0;
    left: 0;
    right: 0;
    bottom: 0;
    background-color: rgba(0, 0, 0, 0.5);
    display: flex;
    align-items: center;
    justify-content: center;
    z-index: 9999;
}

/* ============================================================
   Utilities
   ============================================================ */
.text-center { text-align: center; }
.text-left { text-align: left; }
.text-right { text-align: right; }

.mt-xs { margin-top: var(--spacing-xs); }
.mt-sm { margin-top: var(--spacing-sm); }
.mt-md { margin-top: var(--spacing-md); }
.mt-lg { margin-top: var(--spacing-lg); }
.mt-xl { margin-top: var(--spacing-xl); }

.mb-xs { margin-bottom: var(--spacing-xs); }
.mb-sm { margin-bottom: var(--spacing-sm); }
.mb-md { margin-bottom: var(--spacing-md); }
.mb-lg { margin-bottom: var(--spacing-lg); }
.mb-xl { margin-bottom: var(--spacing-xl); }

.hidden { display: none !important; }
.visible { display: block !important; }

/* ============================================================
   Enhanced Responsive Design
   ============================================================ */

/* Large Tablets (1024px - 1280px) */
@media (max-width: 1280px) and (min-width: 1025px) {
    .container {
        padding: 0 var(--spacing-md);
    }
}

/* Tablet Portrait (768px - 1024px) */
@media (max-width: 1024px) and (min-width: 769px) {
    .grid-3 {
        grid-template-columns: repeat(2, 1fr);
    }

    .grid-4 {
        grid-template-columns: repeat(2, 1fr);
    }
}

/* Mobile & Tablet General (< 960px) */
@media (max-width: 960px) {
    .nav-drawer {
        transform: translateX(-100%);
        z-index: 1050;
    }

    .nav-drawer.visible {
        transform: translateX(0);
    }

    .main-content {
        margin-left: 0;
    }

    .app-footer {
        margin-left: 0;
    }

    .app-bar-content {
        padding: var(--spacing-md);
    }
}

/* Mobile Large (600px - 768px) */
@media (max-width: 768px) and (min-width: 601px) {
    .grid-2 {
        grid-template-columns: 1fr;
    }

    .grid-3 {
        grid-template-columns: 1fr;
    }
}

/* Mobile (< 600px) */
@media (max-width: 600px) {
    .app-bar-version {
        display: none;
    }

    .container {
        padding: 0 var(--spacing-md);
    }

    .card-content {
        padding: var(--spacing-md);
    }

    .btn {
        font-size: 0.8125rem;
        padding: 10px 18px;
        /* DO NOT set width: 100% here - breaks layout */
    }

    /* Stack all grids */
    .grid-2, .grid-3, .grid-4 {
        grid-template-columns: 1fr;
    }

    /* Touch-friendly form controls */
    .form-control,
    select.form-control,
    input[type="number"],
    input[type="text"],
    input[type="password"] {
        min-height: 44px; /* iOS recommended touch target */
        font-size: 16px; /* Prevent iOS zoom on focus */
    }

    .btn {
        min-height: 44px;
    }

    .card-actions .btn {
        width: 100%; /* Full width only in card-actions on mobile */
    }

    .card-actions {
        flex-direction: column;
    }
}

/* Very Small Mobile (< 375px) */
@media (max-width: 374px) {
    .app-bar-content {
        padding: var(--spacing-sm);
    }

    .footer-content {
        padding: 0 var(--spacing-sm);
    }
}

/* ============================================================
   Footer Styles
   ============================================================ */
.app-footer {
    background-color: var(--grey-100);
    border-top: 1px solid var(--grey-300);
    padding: var(--spacing-xl) 0;
    margin-top: var(--spacing-xl);
    margin-left: 240px; /* Match nav drawer width */
    transition: margin-left var(--transition-normal);
}

.app-footer.full-width {
    margin-left: 0;
}

.footer-content {
    max-width: 1280px;
    margin: 0 auto;
    padding: 0 var(--spacing-lg);
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
    gap: var(--spacing-lg);
}

.footer-section {
    display: flex;
    flex-direction: column;
    gap: var(--spacing-xs);
}

.footer-title {
    font-size: 1rem;
    font-weight: 500;
    color: var(--text-primary);
    margin-bottom: var(--spacing-xs);
}

.footer-text {
    font-size: 0.875rem;
    color: var(--text-secondary);
}

.footer-links {
    display: flex;
    flex-direction: column;
    gap: var(--spacing-xs);
}

.footer-link {
    color: var(--primary-main);
    text-decoration: none;
    font-size: 0.875rem;
    transition: color var(--transition-fast);
}

.footer-link:hover {
    color: var(--primary-dark);
    text-decoration: underline;
}

.footer-stat {
    display: flex;
    justify-content: space-between;
    align-items: center;
    font-size: 0.875rem;
}

.footer-stat-label {
    color: var(--text-secondary);
}

.footer-stat-value {
    color: var(--text-primary);
    font-family: 'Courier New', monospace;
    font-weight: 500;
}

@media (max-width: 768px) {
    .footer-content {
        grid-template-columns: 1fr;
        text-align: center;
    }

    .footer-links {
        align-items: center;
    }

    .footer-stat {
        justify-content: center;
        gap: var(--spacing-sm);
    }
}

/* ============================================================
   Dark Mode Support (Future)
   ============================================================ */
@media (prefers-color-scheme: dark) {
    /* Will be implemented in Phase 2 */
}
)rawliteral";

#endif // MATERIAL_STYLES_H
