@echo off
REM ACRouter Release Script for Windows
REM Builds project and creates GitHub release with binary files

setlocal enabledelayedexpansion

echo ========================================
echo ACRouter Release Builder
echo ========================================

REM Check if version tag is provided
if "%1"=="" (
    echo Error: Version tag required
    echo Usage: release.bat v1.0.0 [release notes] [--skip-build]
    exit /b 1
)

set VERSION=%1
set "RELEASE_NOTES=%~2"
set SKIP_BUILD=0

REM Check for --skip-build flag
if "%~3"=="--skip-build" set SKIP_BUILD=1
if "%~2"=="--skip-build" (
    set SKIP_BUILD=1
    set "RELEASE_NOTES="
)

REM Default release notes if not provided
if "!RELEASE_NOTES!"=="" (
    set "RELEASE_NOTES=ACRouter !VERSION! - Binary firmware release"
)

echo.
echo Version: !VERSION!
echo Notes: !RELEASE_NOTES!
if !SKIP_BUILD!==1 echo Skip build: YES
echo.

REM Step 1: Build the project
if !SKIP_BUILD!==0 (
    echo [1/5] Building project...
    call idf.py build
    if errorlevel 1 (
        echo Build failed!
        exit /b 1
    )
) else (
    echo [1/5] Skipping build - using existing binaries...
    if not exist "build\ACRouter-project.bin" (
        echo Error: Binary files not found! Run without --skip-build first.
        exit /b 1
    )
)

REM Step 2: Create release directory
echo [2/5] Preparing release files...
set "RELEASE_DIR=build\release-!VERSION!"
if exist "!RELEASE_DIR!" rmdir /s /q "!RELEASE_DIR!"
mkdir "!RELEASE_DIR!"

REM Step 3: Copy binary files
echo [3/5] Copying binary files...
copy build\bootloader\bootloader.bin "!RELEASE_DIR!\" >nul
copy build\partition_table\partition-table.bin "!RELEASE_DIR!\" >nul
copy build\ACRouter-project.bin "!RELEASE_DIR!\" >nul
copy build\ota_data_initial.bin "!RELEASE_DIR!\" >nul
copy build\flash_args "!RELEASE_DIR!\" >nul

REM Step 4: Create flash script for users
echo [4/5] Creating flash script...
(
echo @echo off
echo REM ACRouter Firmware Flash Script
echo REM Version: %VERSION%
echo.
echo echo ========================================
echo echo ACRouter Firmware Flasher %VERSION%
echo echo ========================================
echo echo.
echo.
echo REM Check if PORT is provided
echo if "%%1"=="" ^(
echo     echo Error: COM port required
echo     echo Usage: flash.bat COM5
echo     exit /b 1
echo ^)
echo.
echo set PORT=%%1
echo.
echo echo Flashing to %%PORT%%...
echo echo.
echo.
echo python -m esptool --chip esp32 -b 460800 --before default_reset --after hard_reset --port %%PORT%% write_flash --flash_mode dio --flash_size 4MB --flash_freq 40m 0x1000 bootloader.bin 0x20000 ACRouter-project.bin 0x8000 partition-table.bin 0xf000 ota_data_initial.bin
echo.
echo if errorlevel 1 ^(
echo     echo Flash failed!
echo     exit /b 1
echo ^)
echo.
echo echo.
echo echo ========================================
echo echo Flash completed successfully!
echo echo ========================================
) > "!RELEASE_DIR!\flash.bat"

REM Create README for release
(
echo # ACRouter Firmware %VERSION%
echo.
echo ## Binary Files
echo.
echo - `bootloader.bin` - ESP32 bootloader ^(26 KB^)
echo - `partition-table.bin` - Partition table ^(3 KB^)
echo - `ACRouter-project.bin` - Main application ^(~1.1 MB^)
echo - `ota_data_initial.bin` - OTA data ^(8 KB^)
echo - `flash_args` - Flash parameters
echo.
echo ## Quick Flash Instructions
echo.
echo ### Windows:
echo ```batch
echo flash.bat COM5
echo ```
echo.
echo ### Linux/Mac:
echo ```bash
echo ./flash.sh /dev/ttyUSB0
echo ```
echo.
echo ## Manual Flash Command
echo.
echo ```bash
echo python -m esptool --chip esp32 -b 460800 \
echo   --before default_reset --after hard_reset \
echo   --port COM5 write_flash \
echo   --flash_mode dio --flash_size 4MB --flash_freq 40m \
echo   0x1000 bootloader.bin \
echo   0x20000 ACRouter-project.bin \
echo   0x8000 partition-table.bin \
echo   0xf000 ota_data_initial.bin
echo ```
echo.
echo ## Requirements
echo.
echo - Python 3.7+
echo - esptool: `pip install esptool`
echo.
echo ## More Information
echo.
echo Visit: https://github.com/robotdyn-dimmer/ACRouter
) > "!RELEASE_DIR!\FLASH_README.txt"

REM Create Linux/Mac flash script
(
echo #!/bin/bash
echo # ACRouter Firmware Flash Script
echo # Version: %VERSION%
echo.
echo echo "========================================"
echo echo "ACRouter Firmware Flasher %VERSION%"
echo echo "========================================"
echo echo ""
echo.
echo # Check if PORT is provided
echo if [ -z "$1" ]; then
echo     echo "Error: Serial port required"
echo     echo "Usage: ./flash.sh /dev/ttyUSB0"
echo     exit 1
echo fi
echo.
echo PORT=$1
echo.
echo echo "Flashing to $PORT..."
echo echo ""
echo.
echo python -m esptool --chip esp32 -b 460800 \
echo   --before default_reset --after hard_reset \
echo   --port $PORT write_flash \
echo   --flash_mode dio --flash_size 4MB --flash_freq 40m \
echo   0x1000 bootloader.bin \
echo   0x20000 ACRouter-project.bin \
echo   0x8000 partition-table.bin \
echo   0xf000 ota_data_initial.bin
echo.
echo if [ $? -ne 0 ]; then
echo     echo "Flash failed!"
echo     exit 1
echo fi
echo.
echo echo ""
echo echo "========================================"
echo echo "Flash completed successfully!"
echo echo "========================================"
) > "!RELEASE_DIR!\flash.sh"

REM Step 5: Create GitHub release and upload files
echo [5/5] Creating GitHub release...
gh release create !VERSION! ^
    --title "ACRouter !VERSION!" ^
    --notes "!RELEASE_NOTES!" ^
    "!RELEASE_DIR!\bootloader.bin#Bootloader" ^
    "!RELEASE_DIR!\partition-table.bin#Partition Table" ^
    "!RELEASE_DIR!\ACRouter-project.bin#Main Application" ^
    "!RELEASE_DIR!\ota_data_initial.bin#OTA Data" ^
    "!RELEASE_DIR!\flash.bat#Flash Script (Windows)" ^
    "!RELEASE_DIR!\flash.sh#Flash Script (Linux/Mac)" ^
    "!RELEASE_DIR!\FLASH_README.txt#Flash Instructions"

if errorlevel 1 (
    echo Failed to create GitHub release!
    echo Files are available in: !RELEASE_DIR!
    exit /b 1
)

echo.
echo ========================================
echo Release !VERSION! created successfully!
echo ========================================
echo.
echo Release URL: https://github.com/robotdyn-dimmer/ACRouter/releases/tag/!VERSION!
echo Files directory: !RELEASE_DIR!
echo.

endlocal
