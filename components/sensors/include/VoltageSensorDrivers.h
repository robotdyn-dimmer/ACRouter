/**
 * @file VoltageSensorDrivers.h
 * @brief Voltage sensor driver definitions and configuration
 *
 * Defines supported voltage sensor types, their default calibration parameters,
 * and driver selection logic.
 */

#ifndef VOLTAGE_SENSOR_DRIVERS_H
#define VOLTAGE_SENSOR_DRIVERS_H

#include <Arduino.h>
#include <cstring>

// ============================================================
// Voltage Sensor Driver Types
// ============================================================

/**
 * @brief Supported voltage sensor driver types
 */
enum class VoltageSensorDriver : uint8_t {
    ZMPT107_ADC = 0,    ///< ZMPT107 via ADC (0.70V nominal, 230V default)
    ZMPT101B_ADC = 1,   ///< ZMPT101B via ADC (1.0V nominal, 230V default)
    CUSTOM_ADC = 255    ///< Custom sensor via ADC (user-defined parameters)
};

// ============================================================
// Default Calibration Parameters
// ============================================================

namespace VoltageSensorDefaults {
    // ZMPT107: Output 0-3.3V, center 1.65V, calibration 0.70V RMS = nominal grid voltage
    // Circuit: voltage divider with 1.65V DC bias
    // Peak voltage at 230V: 1.65V ± 1.41V = 0.24V to 3.06V (close to ADC limit!)
    constexpr float ZMPT107_NOMINAL_VDC = 0.70f;          // 0.70V RMS at nominal voltage
    constexpr float ZMPT107_DEFAULT_MULT_230V = 328.57f;  // 230V / 0.70V
    constexpr float ZMPT107_OFFSET = 0.0f;

    // ZMPT101B: Output 0-5V (using voltage divider to 3.3V), center 2.5V (1.65V after divider)
    // Calibration: 1.0V RMS = nominal grid voltage
    constexpr float ZMPT101B_NOMINAL_VDC = 1.0f;          // 1.0V RMS at nominal voltage
    constexpr float ZMPT101B_DEFAULT_MULT_230V = 230.0f;  // 230V / 1.0V
    constexpr float ZMPT101B_OFFSET = 0.0f;

    // Custom sensor: user must provide all parameters
    constexpr float CUSTOM_NOMINAL_VDC = 1.0f;    // Default nominal VDC
    constexpr float CUSTOM_DEFAULT_MULT = 1.0f;   // Default multiplier (1:1)
    constexpr float CUSTOM_OFFSET = 0.0f;
}

// ============================================================
// Driver Helper Functions
// ============================================================

/**
 * @brief Get default calibration parameters for a voltage sensor driver
 *
 * @param driver Voltage sensor driver type
 * @param nominal_vdc Output: Nominal VDC at rated grid voltage
 * @param default_mult Output: Default multiplier for 230V grid
 * @param offset Output: Calibration offset
 */
inline void getVoltageSensorDefaults(VoltageSensorDriver driver,
                                     float& nominal_vdc,
                                     float& default_mult,
                                     float& offset) {
    switch (driver) {
        case VoltageSensorDriver::ZMPT107_ADC:
            nominal_vdc = VoltageSensorDefaults::ZMPT107_NOMINAL_VDC;
            default_mult = VoltageSensorDefaults::ZMPT107_DEFAULT_MULT_230V;
            offset = VoltageSensorDefaults::ZMPT107_OFFSET;
            break;

        case VoltageSensorDriver::ZMPT101B_ADC:
            nominal_vdc = VoltageSensorDefaults::ZMPT101B_NOMINAL_VDC;
            default_mult = VoltageSensorDefaults::ZMPT101B_DEFAULT_MULT_230V;
            offset = VoltageSensorDefaults::ZMPT101B_OFFSET;
            break;

        case VoltageSensorDriver::CUSTOM_ADC:
        default:
            nominal_vdc = VoltageSensorDefaults::CUSTOM_NOMINAL_VDC;
            default_mult = VoltageSensorDefaults::CUSTOM_DEFAULT_MULT;
            offset = VoltageSensorDefaults::CUSTOM_OFFSET;
            break;
    }
}

/**
 * @brief Parse voltage sensor type string to driver enum
 *
 * @param type_str Sensor type string (e.g., "ZMPT107", "ZMPT101B", "CUSTOM")
 * @return VoltageSensorDriver Driver enum value
 */
inline VoltageSensorDriver parseVoltageSensorType(const char* type_str) {
    if (!type_str) return VoltageSensorDriver::CUSTOM_ADC;

    if (strcasecmp(type_str, "ZMPT107") == 0) {
        return VoltageSensorDriver::ZMPT107_ADC;
    } else if (strcasecmp(type_str, "ZMPT101B") == 0) {
        return VoltageSensorDriver::ZMPT101B_ADC;
    } else if (strcasecmp(type_str, "CUSTOM") == 0) {
        return VoltageSensorDriver::CUSTOM_ADC;
    }

    return VoltageSensorDriver::CUSTOM_ADC;  // Fallback to custom
}

/**
 * @brief Get human-readable name for voltage sensor driver
 *
 * @param driver Voltage sensor driver type
 * @return const char* Driver name string
 */
inline const char* getVoltageSensorDriverName(VoltageSensorDriver driver) {
    switch (driver) {
        case VoltageSensorDriver::ZMPT107_ADC:
            return "ZMPT107";
        case VoltageSensorDriver::ZMPT101B_ADC:
            return "ZMPT101B";
        case VoltageSensorDriver::CUSTOM_ADC:
            return "CUSTOM";
        default:
            return "UNKNOWN";
    }
}

/**
 * @brief Calculate multiplier from measured grid voltage and sensor nominal VDC
 *
 * @param measured_vac Measured grid voltage (VAC RMS)
 * @param nominal_vdc Sensor nominal VDC output at rated voltage
 * @return float Calculated multiplier
 */
inline float calculateVoltageMultiplier(float measured_vac, float nominal_vdc) {
    if (nominal_vdc <= 0.0f) return 1.0f;  // Avoid division by zero
    return measured_vac / nominal_vdc;
}

#endif // VOLTAGE_SENSOR_DRIVERS_H
