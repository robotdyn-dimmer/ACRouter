#!/bin/bash
# Build script for AC Power Router Controller
# Skips Arduino ESP-IDF version check for compatibility with ESP-IDF 5.4.1

export ARDUINO_SKIP_IDF_VERSION_CHECK=1

echo "Building with ARDUINO_SKIP_IDF_VERSION_CHECK=1"
idf.py build "$@"
