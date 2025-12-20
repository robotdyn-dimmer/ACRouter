/**
 * @file CurrentSensorDrivers.h
 * @brief Current sensor driver definitions and default calibration parameters
 *
 * Supports multiple current sensor types:
 * - SCT-013 series: Non-invasive AC current transformers (5A, 10A, 20A, 30A, 50A, 60A, 80A, 100A)
 * - ACS712 series: Hall effect current sensors (5A, 20A, 30A)
 *
 * ## SCT-013 Characteristics:
 * - AC output: 0-1V RMS at maximum current
 * - NO DC bias (pure AC signal)
 * - Multiplier = nominal current (e.g., 30A → multiplier = 30.0)
 * - Offset = 0.0V
 *
 * ## ACS712 Characteristics:
 * - DC output with 2.5V bias @ 5V supply
 * - Voltage divider to 3.3V: ratio = 3.3V / 5V = 0.66
 * - DC offset after divider: 2.5V × 0.66 = 1.65V
 * - Sensitivity after divider: Original_sensitivity × 0.66
 *
 * Example:
 * - ACS712-5A: 185 mV/A @ 5V → 122 mV/A @ 3.3V
 * - ACS712-20A: 100 mV/A @ 5V → 66 mV/A @ 3.3V
 * - ACS712-30A: 66 mV/A @ 5V → 43.56 mV/A @ 3.3V
 *
 * ## DC Offset Auto-Compensation:
 * PowerMeterADC automatically measures and subtracts DC offset for all channels.
 * The offset values defined here are nominal references. Zero-point calibration
 * (hardware-current-calibrate-zero) can compensate for trimmer drift.
 */

#ifndef CURRENT_SENSOR_DRIVERS_H
#define CURRENT_SENSOR_DRIVERS_H

#include <Arduino.h>
#include "esp_log.h"

// ============================================================
// Current Sensor Driver Types
// ============================================================

/**
 * @brief Current sensor driver types
 *
 * Enum values organized by sensor family for clarity.
 */
enum class CurrentSensorDriver : uint8_t {
    // SCT-013 series (AC current transformers, 0-1V output, no DC bias)
    SCT013_5A   = 0,    ///< SCT-013-005: 0-5A, 1V @ 5A
    SCT013_10A  = 1,    ///< SCT-013-010: 0-10A, 1V @ 10A
    SCT013_20A  = 2,    ///< SCT-013-020: 0-20A, 1V @ 20A
    SCT013_30A  = 3,    ///< SCT-013-030: 0-30A, 1V @ 30A
    SCT013_50A  = 4,    ///< SCT-013-050: 0-50A, 1V @ 50A
    SCT013_60A  = 5,    ///< SCT-013-060: 0-60A, 1V @ 60A
    SCT013_80A  = 6,    ///< SCT-013-080: 0-80A, 1V @ 80A
    SCT013_100A = 7,    ///< SCT-013-100: 0-100A, 1V @ 100A

    // ACS712 series (Hall effect, DC output with 2.5V bias @ 5V)
    // Values compensated for 3.3V operation via voltage divider
    ACS712_5A   = 10,   ///< ACS712-5A: ±5A, 185 mV/A @ 5V → 122 mV/A @ 3.3V
    ACS712_20A  = 11,   ///< ACS712-20A: ±20A, 100 mV/A @ 5V → 66 mV/A @ 3.3V
    ACS712_30A  = 12,   ///< ACS712-30A: ±30A, 66 mV/A @ 5V → 43.56 mV/A @ 3.3V

    // Custom sensor (user-defined parameters)
    CUSTOM      = 255   ///< Custom sensor with manual calibration
};

// ============================================================
// Default Calibration Parameters
// ============================================================

namespace CurrentSensorDefaults {
    // Voltage divider ratio for ACS712 (5V → 3.3V)
    constexpr float VOLTAGE_DIVIDER_RATIO = 0.66f;  // 3.3V / 5V

    // -----------------------------------------------------
    // SCT-013 Series (AC output, no DC bias)
    // -----------------------------------------------------
    // Multiplier = nominal current (1V RMS = max current)
    // Offset = 0.0V (pure AC signal)

    constexpr float SCT013_5A_NOMINAL    = 5.0f;
    constexpr float SCT013_5A_MULTIPLIER = 5.0f;
    constexpr float SCT013_5A_OFFSET     = 0.0f;

    constexpr float SCT013_10A_NOMINAL    = 10.0f;
    constexpr float SCT013_10A_MULTIPLIER = 10.0f;
    constexpr float SCT013_10A_OFFSET     = 0.0f;

    constexpr float SCT013_20A_NOMINAL    = 20.0f;
    constexpr float SCT013_20A_MULTIPLIER = 20.0f;
    constexpr float SCT013_20A_OFFSET     = 0.0f;

    constexpr float SCT013_30A_NOMINAL    = 30.0f;
    constexpr float SCT013_30A_MULTIPLIER = 30.0f;
    constexpr float SCT013_30A_OFFSET     = 0.0f;

    constexpr float SCT013_50A_NOMINAL    = 50.0f;
    constexpr float SCT013_50A_MULTIPLIER = 50.0f;
    constexpr float SCT013_50A_OFFSET     = 0.0f;

    constexpr float SCT013_60A_NOMINAL    = 60.0f;
    constexpr float SCT013_60A_MULTIPLIER = 60.0f;
    constexpr float SCT013_60A_OFFSET     = 0.0f;

    constexpr float SCT013_80A_NOMINAL    = 80.0f;
    constexpr float SCT013_80A_MULTIPLIER = 80.0f;
    constexpr float SCT013_80A_OFFSET     = 0.0f;

    constexpr float SCT013_100A_NOMINAL    = 100.0f;
    constexpr float SCT013_100A_MULTIPLIER = 100.0f;
    constexpr float SCT013_100A_OFFSET     = 0.0f;

    // -----------------------------------------------------
    // ACS712 Series (DC output with 2.5V bias @ 5V)
    // -----------------------------------------------------
    // DC offset after voltage divider: 2.5V × 0.66 = 1.65V
    // Sensitivity after divider: Original_sensitivity × 0.66
    //
    // Note: PowerMeterADC auto-compensates DC offset, so these values
    // are nominal references. Zero-point calibration can fine-tune.

    // ACS712-5A: 185 mV/A @ 5V
    constexpr float ACS712_5A_SENSITIVITY_5V  = 0.185f;  // V/A
    constexpr float ACS712_5A_SENSITIVITY_3V3 = ACS712_5A_SENSITIVITY_5V * VOLTAGE_DIVIDER_RATIO;  // 0.122 V/A
    constexpr float ACS712_5A_NOMINAL         = 5.0f;    // Amperes
    constexpr float ACS712_5A_MULTIPLIER      = 1.0f / ACS712_5A_SENSITIVITY_3V3;  // ≈ 8.2 A/V
    constexpr float ACS712_5A_DC_OFFSET_3V3   = 2.5f * VOLTAGE_DIVIDER_RATIO;  // 1.65V

    // ACS712-20A: 100 mV/A @ 5V
    constexpr float ACS712_20A_SENSITIVITY_5V  = 0.100f;  // V/A
    constexpr float ACS712_20A_SENSITIVITY_3V3 = ACS712_20A_SENSITIVITY_5V * VOLTAGE_DIVIDER_RATIO;  // 0.066 V/A
    constexpr float ACS712_20A_NOMINAL         = 20.0f;   // Amperes
    constexpr float ACS712_20A_MULTIPLIER      = 1.0f / ACS712_20A_SENSITIVITY_3V3;  // ≈ 15.15 A/V
    constexpr float ACS712_20A_DC_OFFSET_3V3   = 2.5f * VOLTAGE_DIVIDER_RATIO;  // 1.65V

    // ACS712-30A: 66 mV/A @ 5V
    constexpr float ACS712_30A_SENSITIVITY_5V  = 0.066f;  // V/A
    constexpr float ACS712_30A_SENSITIVITY_3V3 = ACS712_30A_SENSITIVITY_5V * VOLTAGE_DIVIDER_RATIO;  // 0.04356 V/A
    constexpr float ACS712_30A_NOMINAL         = 30.0f;   // Amperes
    constexpr float ACS712_30A_MULTIPLIER      = 1.0f / ACS712_30A_SENSITIVITY_3V3;  // ≈ 22.96 A/V
    constexpr float ACS712_30A_DC_OFFSET_3V3   = 2.5f * VOLTAGE_DIVIDER_RATIO;  // 1.65V
}

// ============================================================
// Helper Functions
// ============================================================

/**
 * @brief Get default calibration parameters for a current sensor
 *
 * @param driver Current sensor driver type
 * @param[out] nominal_current Nominal maximum current in amperes
 * @param[out] multiplier Calibration multiplier (A/V)
 * @param[out] offset DC offset in volts (0.0 for SCT-013, 1.65V for ACS712)
 */
inline void getCurrentSensorDefaults(CurrentSensorDriver driver,
                                     float& nominal_current,
                                     float& multiplier,
                                     float& offset) {
    using namespace CurrentSensorDefaults;

    switch (driver) {
        // SCT-013 series
        case CurrentSensorDriver::SCT013_5A:
            nominal_current = SCT013_5A_NOMINAL;
            multiplier = SCT013_5A_MULTIPLIER;
            offset = SCT013_5A_OFFSET;
            break;

        case CurrentSensorDriver::SCT013_10A:
            nominal_current = SCT013_10A_NOMINAL;
            multiplier = SCT013_10A_MULTIPLIER;
            offset = SCT013_10A_OFFSET;
            break;

        case CurrentSensorDriver::SCT013_20A:
            nominal_current = SCT013_20A_NOMINAL;
            multiplier = SCT013_20A_MULTIPLIER;
            offset = SCT013_20A_OFFSET;
            break;

        case CurrentSensorDriver::SCT013_30A:
            nominal_current = SCT013_30A_NOMINAL;
            multiplier = SCT013_30A_MULTIPLIER;
            offset = SCT013_30A_OFFSET;
            break;

        case CurrentSensorDriver::SCT013_50A:
            nominal_current = SCT013_50A_NOMINAL;
            multiplier = SCT013_50A_MULTIPLIER;
            offset = SCT013_50A_OFFSET;
            break;

        case CurrentSensorDriver::SCT013_60A:
            nominal_current = SCT013_60A_NOMINAL;
            multiplier = SCT013_60A_MULTIPLIER;
            offset = SCT013_60A_OFFSET;
            break;

        case CurrentSensorDriver::SCT013_80A:
            nominal_current = SCT013_80A_NOMINAL;
            multiplier = SCT013_80A_MULTIPLIER;
            offset = SCT013_80A_OFFSET;
            break;

        case CurrentSensorDriver::SCT013_100A:
            nominal_current = SCT013_100A_NOMINAL;
            multiplier = SCT013_100A_MULTIPLIER;
            offset = SCT013_100A_OFFSET;
            break;

        // ACS712 series
        case CurrentSensorDriver::ACS712_5A:
            nominal_current = ACS712_5A_NOMINAL;
            multiplier = ACS712_5A_MULTIPLIER;
            offset = ACS712_5A_DC_OFFSET_3V3;
            break;

        case CurrentSensorDriver::ACS712_20A:
            nominal_current = ACS712_20A_NOMINAL;
            multiplier = ACS712_20A_MULTIPLIER;
            offset = ACS712_20A_DC_OFFSET_3V3;
            break;

        case CurrentSensorDriver::ACS712_30A:
            nominal_current = ACS712_30A_NOMINAL;
            multiplier = ACS712_30A_MULTIPLIER;
            offset = ACS712_30A_DC_OFFSET_3V3;
            break;

        // Custom sensor (no defaults)
        case CurrentSensorDriver::CUSTOM:
        default:
            nominal_current = 0.0f;
            multiplier = 1.0f;
            offset = 0.0f;
            break;
    }
}

/**
 * @brief Parse current sensor type string to CurrentSensorDriver enum
 *
 * Supported formats:
 * - "SCT013-5A", "SCT013-10A", "SCT013-20A", "SCT013-30A", "SCT013-50A", "SCT013-60A", "SCT013-80A", "SCT013-100A"
 * - "ACS712-5A", "ACS712-20A", "ACS712-30A"
 * - "CUSTOM"
 *
 * @param type_str Type string (case-insensitive)
 * @param[out] driver Parsed driver type
 * @return true if parsing successful, false otherwise
 */
inline bool parseCurrentSensorType(const char* type_str, CurrentSensorDriver& driver) {
    if (!type_str) return false;

    String type = String(type_str);
    type.toUpperCase();
    type.trim();

    // SCT-013 series
    if (type == "SCT013-5A" || type == "SCT013_5A") {
        driver = CurrentSensorDriver::SCT013_5A;
        return true;
    }
    if (type == "SCT013-10A" || type == "SCT013_10A") {
        driver = CurrentSensorDriver::SCT013_10A;
        return true;
    }
    if (type == "SCT013-20A" || type == "SCT013_20A") {
        driver = CurrentSensorDriver::SCT013_20A;
        return true;
    }
    if (type == "SCT013-30A" || type == "SCT013_30A") {
        driver = CurrentSensorDriver::SCT013_30A;
        return true;
    }
    if (type == "SCT013-50A" || type == "SCT013_50A") {
        driver = CurrentSensorDriver::SCT013_50A;
        return true;
    }
    if (type == "SCT013-60A" || type == "SCT013_60A") {
        driver = CurrentSensorDriver::SCT013_60A;
        return true;
    }
    if (type == "SCT013-80A" || type == "SCT013_80A") {
        driver = CurrentSensorDriver::SCT013_80A;
        return true;
    }
    if (type == "SCT013-100A" || type == "SCT013_100A") {
        driver = CurrentSensorDriver::SCT013_100A;
        return true;
    }

    // ACS712 series
    if (type == "ACS712-5A" || type == "ACS712_5A") {
        driver = CurrentSensorDriver::ACS712_5A;
        return true;
    }
    if (type == "ACS712-20A" || type == "ACS712_20A") {
        driver = CurrentSensorDriver::ACS712_20A;
        return true;
    }
    if (type == "ACS712-30A" || type == "ACS712_30A") {
        driver = CurrentSensorDriver::ACS712_30A;
        return true;
    }

    // Custom
    if (type == "CUSTOM") {
        driver = CurrentSensorDriver::CUSTOM;
        return true;
    }

    return false;
}

/**
 * @brief Get human-readable name for current sensor driver
 *
 * @param driver Current sensor driver type
 * @return Driver name string
 */
inline const char* getCurrentSensorDriverName(CurrentSensorDriver driver) {
    switch (driver) {
        case CurrentSensorDriver::SCT013_5A:    return "SCT013-5A";
        case CurrentSensorDriver::SCT013_10A:   return "SCT013-10A";
        case CurrentSensorDriver::SCT013_20A:   return "SCT013-20A";
        case CurrentSensorDriver::SCT013_30A:   return "SCT013-30A";
        case CurrentSensorDriver::SCT013_50A:   return "SCT013-50A";
        case CurrentSensorDriver::SCT013_60A:   return "SCT013-60A";
        case CurrentSensorDriver::SCT013_80A:   return "SCT013-80A";
        case CurrentSensorDriver::SCT013_100A:  return "SCT013-100A";
        case CurrentSensorDriver::ACS712_5A:    return "ACS712-5A";
        case CurrentSensorDriver::ACS712_20A:   return "ACS712-20A";
        case CurrentSensorDriver::ACS712_30A:   return "ACS712-30A";
        case CurrentSensorDriver::CUSTOM:       return "CUSTOM";
        default:                                return "UNKNOWN";
    }
}

/**
 * @brief Check if sensor is ACS712 type (requires DC offset handling)
 *
 * @param driver Current sensor driver type
 * @return true if sensor is ACS712, false otherwise
 */
inline bool isACS712Sensor(CurrentSensorDriver driver) {
    return (driver == CurrentSensorDriver::ACS712_5A ||
            driver == CurrentSensorDriver::ACS712_20A ||
            driver == CurrentSensorDriver::ACS712_30A);
}

/**
 * @brief Check if sensor is SCT-013 type (AC output, no DC bias)
 *
 * @param driver Current sensor driver type
 * @return true if sensor is SCT-013, false otherwise
 */
inline bool isSCT013Sensor(CurrentSensorDriver driver) {
    return (driver == CurrentSensorDriver::SCT013_5A ||
            driver == CurrentSensorDriver::SCT013_10A ||
            driver == CurrentSensorDriver::SCT013_20A ||
            driver == CurrentSensorDriver::SCT013_30A ||
            driver == CurrentSensorDriver::SCT013_50A ||
            driver == CurrentSensorDriver::SCT013_60A ||
            driver == CurrentSensorDriver::SCT013_80A ||
            driver == CurrentSensorDriver::SCT013_100A);
}

#endif // CURRENT_SENSOR_DRIVERS_H
