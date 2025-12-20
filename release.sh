#!/bin/bash
# ACRouter Release Script for Linux/Mac
# Builds project and creates GitHub release with binary files

set -e

echo "========================================"
echo "ACRouter Release Builder"
echo "========================================"

# Check if version tag is provided
if [ -z "$1" ]; then
    echo "Error: Version tag required"
    echo "Usage: ./release.sh v1.0.0 [release notes]"
    exit 1
fi

VERSION=$1
RELEASE_NOTES=${2:-"ACRouter $VERSION - Binary firmware release"}

echo ""
echo "Version: $VERSION"
echo "Notes: $RELEASE_NOTES"
echo ""

# Step 1: Build the project
echo "[1/5] Building project..."
idf.py build

# Step 2: Create release directory
echo "[2/5] Preparing release files..."
RELEASE_DIR="build/release-$VERSION"
rm -rf "$RELEASE_DIR"
mkdir -p "$RELEASE_DIR"

# Step 3: Copy binary files
echo "[3/5] Copying binary files..."
cp build/bootloader/bootloader.bin "$RELEASE_DIR/"
cp build/partition_table/partition-table.bin "$RELEASE_DIR/"
cp build/ACRouter-project.bin "$RELEASE_DIR/"
cp build/ota_data_initial.bin "$RELEASE_DIR/"
cp build/flash_args "$RELEASE_DIR/"

# Step 4: Create flash script for users
echo "[4/5] Creating flash scripts..."

# Windows flash script
cat > "$RELEASE_DIR/flash.bat" << 'EOF'
@echo off
REM ACRouter Firmware Flash Script
REM Version: %VERSION%

echo ========================================
echo ACRouter Firmware Flasher %VERSION%
echo ========================================
echo.

REM Check if PORT is provided
if "%1"=="" (
    echo Error: COM port required
    echo Usage: flash.bat COM5
    exit /b 1
)

set PORT=%1

echo Flashing to %PORT%...
echo.

python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset --port %PORT% write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 bootloader.bin 0x20000 ACRouter-project.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin

if errorlevel 1 (
    echo Flash failed!
    exit /b 1
)

echo.
echo ========================================
echo Flash completed successfully!
echo ========================================
EOF

# Linux/Mac flash script
cat > "$RELEASE_DIR/flash.sh" << 'EOF'
#!/bin/bash
# ACRouter Firmware Flash Script

echo "========================================"
echo "ACRouter Firmware Flasher"
echo "========================================"
echo ""

# Check if PORT is provided
if [ -z "$1" ]; then
    echo "Error: Serial port required"
    echo "Usage: ./flash.sh /dev/ttyUSB0"
    exit 1
fi

PORT=$1

echo "Flashing to $PORT..."
echo ""

python -m esptool --chip esp32 -b 460800 \
  --before default_reset --after hard_reset \
  --port $PORT write_flash \
  --flash_mode dio --flash_size 4MB --flash_freq 40m \
  0x1000 bootloader.bin \
  0x20000 ACRouter-project.bin \
  0x8000 partition-table.bin \
  0xf000 ota_data_initial.bin

if [ $? -ne 0 ]; then
    echo "Flash failed!"
    exit 1
fi

echo ""
echo "========================================"
echo "Flash completed successfully!"
echo "========================================"
EOF

chmod +x "$RELEASE_DIR/flash.sh"

# Create README for release
cat > "$RELEASE_DIR/FLASH_README.txt" << EOF
# ACRouter Firmware $VERSION

## Binary Files

- \`bootloader.bin\` - ESP32 bootloader (26 KB)
- \`partition-table.bin\` - Partition table (3 KB)
- \`ACRouter-project.bin\` - Main application (~1.1 MB)
- \`ota_data_initial.bin\` - OTA data (8 KB)
- \`flash_args\` - Flash parameters

## Quick Flash Instructions

### Windows:
\`\`\`batch
flash.bat COM5
\`\`\`

### Linux/Mac:
\`\`\`bash
./flash.sh /dev/ttyUSB0
\`\`\`

## Manual Flash Command

\`\`\`bash
python -m esptool --chip esp32 -b 460800 \\
  --before default_reset --after hard_reset \\
  --port COM5 write_flash \\
  --flash_mode dio --flash_size 4MB --flash_freq 40m \\
  0x1000 bootloader.bin \\
  0x20000 ACRouter-project.bin \\
  0x8000 partition-table.bin \\
  0xf000 ota_data_initial.bin
\`\`\`

## Requirements

- Python 3.7+
- esptool: \`pip install esptool\`

## More Information

Visit: https://github.com/robotdyn-dimmer/ACRouter
EOF

# Step 5: Create GitHub release and upload files
echo "[5/5] Creating GitHub release..."
gh release create "$VERSION" \
    --title "ACRouter $VERSION" \
    --notes "$RELEASE_NOTES" \
    "$RELEASE_DIR/bootloader.bin#Bootloader" \
    "$RELEASE_DIR/partition-table.bin#Partition Table" \
    "$RELEASE_DIR/ACRouter-project.bin#Main Application" \
    "$RELEASE_DIR/ota_data_initial.bin#OTA Data" \
    "$RELEASE_DIR/flash.bat#Flash Script (Windows)" \
    "$RELEASE_DIR/flash.sh#Flash Script (Linux/Mac)" \
    "$RELEASE_DIR/FLASH_README.txt#Flash Instructions"

echo ""
echo "========================================"
echo "Release $VERSION created successfully!"
echo "========================================"
echo ""
echo "Release URL: https://github.com/robotdyn-dimmer/ACRouter/releases/tag/$VERSION"
echo "Files directory: $RELEASE_DIR"
echo ""
