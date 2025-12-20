/**
 * @file WebServerStyles.h
 * @brief Common CSS styles for web interface
 *
 * Shared styles across all web pages to enable browser caching
 */

#pragma once

// Common CSS styles for all web pages
const char COMMON_CSS[] PROGMEM = R"rawliteral(
* { margin: 0; padding: 0; box-sizing: border-box; }
body {
    font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    min-height: 100vh;
    display: flex;
    align-items: center;
    justify-content: center;
    padding: 20px;
}
.container {
    background: white;
    border-radius: 20px;
    box-shadow: 0 20px 60px rgba(0,0,0,0.3);
    max-width: 600px;
    width: 100%;
    padding: 40px;
}
h1 {
    color: #333;
    margin-bottom: 10px;
    font-size: 28px;
}
.subtitle {
    color: #666;
    margin-bottom: 30px;
    font-size: 14px;
}
.status-box {
    background: #f8f9fa;
    border-left: 4px solid #667eea;
    padding: 15px;
    border-radius: 8px;
    margin-bottom: 25px;
}
.status-box h3 {
    color: #667eea;
    margin-bottom: 10px;
    font-size: 16px;
}
.status-item {
    display: flex;
    justify-content: space-between;
    padding: 8px 0;
    border-bottom: 1px solid #e0e0e0;
}
.status-item:last-child { border-bottom: none; }
.status-label { font-weight: 600; color: #555; }
.status-value { color: #333; }
.form-group {
    margin-bottom: 20px;
}
label {
    display: block;
    margin-bottom: 8px;
    font-weight: 600;
    color: #555;
}
input[type="text"],
input[type="password"] {
    width: 100%;
    padding: 12px;
    border: 2px solid #e0e0e0;
    border-radius: 8px;
    font-size: 15px;
    transition: border-color 0.3s;
}
input:focus {
    outline: none;
    border-color: #667eea;
}
.btn {
    padding: 12px 24px;
    border: none;
    border-radius: 8px;
    font-size: 15px;
    font-weight: 600;
    cursor: pointer;
    transition: all 0.3s;
    width: 100%;
}
.btn-primary {
    background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
    color: white;
}
.btn-primary:hover {
    transform: translateY(-2px);
    box-shadow: 0 5px 15px rgba(102, 126, 234, 0.4);
}
.btn-secondary {
    background: #6c757d;
    color: white;
    margin-top: 10px;
}
.btn-danger {
    background: #dc3545;
    color: white;
    margin-top: 10px;
}
.btn:disabled {
    opacity: 0.5;
    cursor: not-allowed;
}
.message {
    padding: 12px;
    border-radius: 8px;
    margin-bottom: 20px;
    display: none;
}
.message.success {
    background: #d4edda;
    color: #155724;
    border: 1px solid #c3e6cb;
}
.message.error {
    background: #f8d7da;
    color: #721c24;
    border: 1px solid #f5c6cb;
}
.message.info {
    background: #d1ecf1;
    color: #0c5460;
    border: 1px solid #bee5eb;
}
.spinner {
    border: 3px solid #f3f3f3;
    border-top: 3px solid #667eea;
    border-radius: 50%;
    width: 20px;
    height: 20px;
    animation: spin 1s linear infinite;
    display: inline-block;
    margin-left: 10px;
}
@keyframes spin {
    0% { transform: rotate(0deg); }
    100% { transform: rotate(360deg); }
}
)rawliteral";
