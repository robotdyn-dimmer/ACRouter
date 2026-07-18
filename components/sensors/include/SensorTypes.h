/**
 * @file SensorTypes.h
 * @brief Sensor type definitions and ADC channel configuration
 */

#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <Arduino.h>
#include <cstdint>
#include "VoltageSensorDrivers.h"
#include "CurrentSensorDrivers.h"

// ============================================================
// Sensor Type Enumeration
// ============================================================

/**
 * @brief Type of sensor connected to ADC channel
 *
 * Allows flexible configuration - any ADC channel can be
 * assigned to any sensor type or disabled
 */
enum class SensorType : uint8_t {
    NONE = 0,           ///< Channel not used
    VOLTAGE_AC = 1,     ///< AC voltage sensor (ZMPT107/ZMPT101B)

    // Primary current sensors
    CURRENT_GRID = 10,  ///< Current sensor for grid (+ import, - export)
    CURRENT_SOLAR = 11, ///< Current sensor for solar inverter

    // Load current sensors (expandable to 8 channels)
    CURRENT_LOAD_1 = 20, ///< Load current sensor 1 (SCT-013/ACS712)
    CURRENT_LOAD_2 = 21, ///< Load current sensor 2
    CURRENT_LOAD_3 = 22, ///< Load current sensor 3
    CURRENT_LOAD_4 = 23, ///< Load current sensor 4
    CURRENT_LOAD_5 = 24, ///< Load current sensor 5
    CURRENT_LOAD_6 = 25, ///< Load current sensor 6
    CURRENT_LOAD_7 = 26, ///< Load current sensor 7
    CURRENT_LOAD_8 = 27, ///< Load current sensor 8

    // Legacy aliases for backward compatibility
    CURRENT_LOAD = CURRENT_LOAD_1,  ///< Alias for LOAD_1
    CURRENT_AUX1 = 30,              ///< Auxiliary current sensor 1
    CURRENT_AUX2 = 31,              ///< Auxiliary current sensor 2

    // Temperature sensors
    TEMP_NTC = 40,      ///< NTC thermistor temperature sensor
    TEMP_ANALOG = 41    ///< Other analog temperature sensor
};

/**
 * Convert SensorType to string
 */
inline const char* sensorTypeToString(SensorType type) {
    switch (type) {
        case SensorType::NONE:           return "NONE";
        case SensorType::VOLTAGE_AC:     return "VOLTAGE_AC";
        case SensorType::CURRENT_GRID:   return "CURRENT_GRID";
        case SensorType::CURRENT_SOLAR:  return "CURRENT_SOLAR";
        case SensorType::CURRENT_LOAD_1: return "CURRENT_LOAD_1";
        case SensorType::CURRENT_LOAD_2: return "CURRENT_LOAD_2";
        case SensorType::CURRENT_LOAD_3: return "CURRENT_LOAD_3";
        case SensorType::CURRENT_LOAD_4: return "CURRENT_LOAD_4";
        case SensorType::CURRENT_LOAD_5: return "CURRENT_LOAD_5";
        case SensorType::CURRENT_LOAD_6: return "CURRENT_LOAD_6";
        case SensorType::CURRENT_LOAD_7: return "CURRENT_LOAD_7";
        case SensorType::CURRENT_LOAD_8: return "CURRENT_LOAD_8";
        case SensorType::CURRENT_AUX1:   return "CURRENT_AUX1";
        case SensorType::CURRENT_AUX2:   return "CURRENT_AUX2";
        case SensorType::TEMP_NTC:       return "TEMP_NTC";
        case SensorType::TEMP_ANALOG:    return "TEMP_ANALOG";
        default:                         return "UNKNOWN";
    }
}

/**
 * Check if sensor type is a current sensor
 */
inline bool isCurrentSensor(SensorType type) {
    return (type == SensorType::CURRENT_GRID ||
            type == SensorType::CURRENT_SOLAR ||
            type == SensorType::CURRENT_LOAD_1 ||
            type == SensorType::CURRENT_LOAD_2 ||
            type == SensorType::CURRENT_LOAD_3 ||
            type == SensorType::CURRENT_LOAD_4 ||
            type == SensorType::CURRENT_LOAD_5 ||
            type == SensorType::CURRENT_LOAD_6 ||
            type == SensorType::CURRENT_LOAD_7 ||
            type == SensorType::CURRENT_LOAD_8 ||
            type == SensorType::CURRENT_AUX1 ||
            type == SensorType::CURRENT_AUX2);
}

/**
 * Check if sensor type is a voltage sensor
 */
inline bool isVoltageSensor(SensorType type) {
    return (type == SensorType::VOLTAGE_AC);
}

/**
 * Check if sensor type is a temperature sensor
 */
inline bool isTemperatureSensor(SensorType type) {
    return (type == SensorType::TEMP_NTC ||
            type == SensorType::TEMP_ANALOG);
}

// ============================================================
// ADC Channel Configuration
// ============================================================

/**
 * @brief Configuration for a single ADC channel
 *
 * Stores sensor type, GPIO pin, and calibration data
 */
struct ADCChannelConfig {
    uint8_t gpio;               ///< GPIO pin number (32-39 for ADC1)
    SensorType type;            ///< Type of sensor on this channel
    float multiplier;           ///< Hardware calibration multiplier
    float offset;               ///< Hardware calibration offset
    bool enabled;               ///< Channel is active

    // Voltage sensor specific parameters (only for VOLTAGE_AC type)
    VoltageSensorDriver voltage_driver;  ///< Voltage sensor driver type
    float nominal_vdc;                   ///< Nominal VDC output at rated voltage

    // Current sensor specific parameters (only for CURRENT_* types)
    CurrentSensorDriver current_driver;  ///< Current sensor driver type

    /**
     * Default constructor - disabled channel
     */
    ADCChannelConfig() :
        gpio(0),
        type(SensorType::NONE),
        multiplier(1.0f),
        offset(0.0f),
        enabled(false),
        voltage_driver(VoltageSensorDriver::ZMPT107_ADC),
        nominal_vdc(VoltageSensorDefaults::ZMPT107_NOMINAL_VDC),
        current_driver(CurrentSensorDriver::SCT013_30A)
    {}

    /**
     * Constructor with parameters
     */
    ADCChannelConfig(uint8_t pin, SensorType sensor_type, float mult = 1.0f, float off = 0.0f, bool en = true) :
        gpio(pin),
        type(sensor_type),
        multiplier(mult),
        offset(off),
        enabled(en),
        voltage_driver(VoltageSensorDriver::ZMPT107_ADC),
        nominal_vdc(VoltageSensorDefaults::ZMPT107_NOMINAL_VDC),
        current_driver(CurrentSensorDriver::SCT013_30A)
    {}

    /**
     * Check if this channel is configured and enabled
     */
    bool isActive() const {
        return enabled && (type != SensorType::NONE);
    }

    /**
     * Check if this is a current sensor channel
     */
    bool isCurrentChannel() const {
        return isCurrentSensor(type);
    }

    /**
     * Check if this is a voltage sensor channel
     */
    bool isVoltageChannel() const {
        return isVoltageSensor(type);
    }

    /**
     * Check if this is a temperature sensor channel
     */
    bool isTemperatureChannel() const {
        return isTemperatureSensor(type);
    }
};

// ============================================================
// Sensor Calibration Constants
// ============================================================

/**
 * @brief Default calibration values for common sensors
 *
 * These are starting points - actual values should be determined
 * through calibration against known reference measurements
 */
namespace SensorCalibration {
    // ZMPT107 Voltage Sensor (230V AC, output centered at 1.65V)
    // Factory calibration: 0.70V RMS = 230V AC nominal
    // Calibration formula: multiplier = V_measured / 0.70
    // Example: 230V grid → multiplier = 230 / 0.70 = 328.57
    //          110V grid → multiplier = 110 / 0.70 = 157.14
    constexpr float ZMPT107_MULTIPLIER      = 328.57f;  // 230V / 0.70V
    constexpr float ZMPT107_OFFSET          = 0.0f;

    // SCT-013-030 Current Sensor (30A max, 1V output at 30A)
    // Calibration: multiplier = I_max (30A for SCT-013-030)
    constexpr float SCT013_030_MULTIPLIER   = 30.0f;    // 30A / 1V
    constexpr float SCT013_030_OFFSET       = 0.0f;

    // SCT-013-050 Current Sensor (50A max, 0-1V output)
    constexpr float SCT013_050_MULTIPLIER   = 50.0f;    // 50A max
    constexpr float SCT013_050_OFFSET       = 0.0f;

    // SCT-013-000 Current Sensor (100A max, 0-50mA output with burden resistor)
    // Assuming 33Ω burden resistor → 50mA × 33Ω = 1.65V max
    constexpr float SCT013_000_MULTIPLIER   = 100.0f;   // 100A max
    constexpr float SCT013_000_OFFSET       = 0.0f;

    // NTC Thermistor (10kΩ at 25°C, B=3950)
    constexpr float NTC_BETA                = 3950.0f;  // Beta coefficient
    constexpr float NTC_R25                 = 10000.0f; // Resistance at 25°C
    constexpr float NTC_SERIES_R            = 10000.0f; // Series resistor value
}

// ============================================================
// ADC Configuration Constants
// ============================================================

/**
 * @brief ADC-specific constants for ESP32
 */
namespace ADCConfig {
    // ADC resolution
    constexpr uint8_t RESOLUTION_BITS       = 12;      // 12-bit ADC
    constexpr uint16_t MAX_VALUE            = 4095;    // 2^12 - 1

    // Reference voltage
    constexpr float VREF                    = 3.3f;    // 3.3V reference

    // AC signal characteristics
    constexpr float AC_BIAS_VOLTAGE         = 1.65f;   // VCC/2 for AC signals
    constexpr uint16_t AC_BIAS_ADC          = 2048;    // ADC value at AC bias

    // Sampling for RMS calculation
    constexpr uint16_t SAMPLES_PER_CYCLE    = 200;     // Samples per AC half-cycle
    constexpr uint16_t MIN_SAMPLES_RMS      = 100;     // Minimum samples for valid RMS

    // ADC attenuation settings (ESP32-specific)
    // ADC_ATTEN_DB_11 allows full 0-3.3V range
    constexpr float ATTENUATION_11DB_MIN    = 0.0f;    // Min voltage (V)
    constexpr float ATTENUATION_11DB_MAX    = 3.3f;    // Max voltage (V)

    // Calibration
    constexpr bool USE_ADC_CALIBRATION      = true;    // Use ESP32 ADC calibration API
}

// ============================================================
// Sensor Value Limits
// ============================================================

/**
 * @brief Sanity check limits for sensor readings
 */
namespace SensorLimits {
    // Voltage limits
    constexpr float VOLTAGE_MIN             = 180.0f;  // V (brown-out level)
    constexpr float VOLTAGE_MAX             = 260.0f;  // V (over-voltage)
    constexpr float VOLTAGE_NOMINAL         = 230.0f;  // V (nominal European)

    // Current limits
    constexpr float CURRENT_MIN             = 0.0f;    // A
    constexpr float CURRENT_MAX             = 100.0f;  // A (sensor dependent)

    // Temperature limits
    constexpr float TEMP_MIN                = -40.0f;  // °C
    constexpr float TEMP_MAX                = 125.0f;  // °C

    // Frequency limits
    constexpr float FREQUENCY_MIN           = 45.0f;   // Hz
    constexpr float FREQUENCY_MAX           = 65.0f;   // Hz
    constexpr float FREQUENCY_NOMINAL_50    = 50.0f;   // Hz
    constexpr float FREQUENCY_NOMINAL_60    = 60.0f;   // Hz
}

// ============================================================
// Default ADC Channel Configuration
// ============================================================

/**
 * @brief Standard configuration for typical hardware setup
 *
 * Can be overridden via ConfigManager
 */
namespace DefaultADCConfig {
    /**
     * Standard 4-channel configuration:
     * - CH7 (GPIO35): Voltage sensor
     * - CH3 (GPIO39): Load current
     * - CH0 (GPIO36): Grid current
     * - CH6 (GPIO34): Solar current
     */
    inline ADCChannelConfig getStandardConfig(uint8_t channel_index) {
        switch (channel_index) {
            case 0:
                return ADCChannelConfig(35, SensorType::VOLTAGE_AC,
                                      SensorCalibration::ZMPT107_MULTIPLIER,
                                      SensorCalibration::ZMPT107_OFFSET, true);
            case 1:
                return ADCChannelConfig(39, SensorType::CURRENT_LOAD,
                                      SensorCalibration::SCT013_030_MULTIPLIER,
                                      SensorCalibration::SCT013_030_OFFSET, true);
            case 2:
                return ADCChannelConfig(36, SensorType::CURRENT_GRID,
                                      SensorCalibration::SCT013_030_MULTIPLIER,
                                      SensorCalibration::SCT013_030_OFFSET, true);
            case 3:
                return ADCChannelConfig(34, SensorType::CURRENT_SOLAR,
                                      SensorCalibration::SCT013_030_MULTIPLIER,
                                      SensorCalibration::SCT013_030_OFFSET, true);
            default:
                return ADCChannelConfig(); // Disabled channel
        }
    }

    /**
     * Minimal configuration: voltage + grid current only
     */
    inline ADCChannelConfig getMinimalConfig(uint8_t channel_index) {
        switch (channel_index) {
            case 0:
                return ADCChannelConfig(35, SensorType::VOLTAGE_AC,
                                      SensorCalibration::ZMPT107_MULTIPLIER,
                                      SensorCalibration::ZMPT107_OFFSET, true);
            case 1:
                return ADCChannelConfig(36, SensorType::CURRENT_GRID,
                                      SensorCalibration::SCT013_030_MULTIPLIER,
                                      SensorCalibration::SCT013_030_OFFSET, true);
            default:
                return ADCChannelConfig(); // Disabled channel
        }
    }
}

// ============================================================
// Sensor Binding Helpers
// ============================================================

/**
 * @brief Parse sensor binding string to SensorType
 *
 * Supported bindings:
 * - "GRID" → CURRENT_GRID
 * - "SOLAR" → CURRENT_SOLAR
 * - "LOAD_1" → CURRENT_LOAD_1
 * - "LOAD_2" → CURRENT_LOAD_2
 * - ... up to "LOAD_8"
 * - "LOAD" → CURRENT_LOAD_1 (backward compatibility)
 *
 * @param binding_str Binding string (case-insensitive)
 * @param[out] type Parsed SensorType
 * @return true if parsing successful, false otherwise
 */
inline bool parseCurrentSensorBinding(const char* binding_str, SensorType& type) {
    if (!binding_str) return false;

    String binding = String(binding_str);
    binding.toUpperCase();
    binding.trim();

    // Primary sensors
    if (binding == "GRID") {
        type = SensorType::CURRENT_GRID;
        return true;
    }
    if (binding == "SOLAR") {
        type = SensorType::CURRENT_SOLAR;
        return true;
    }

    // Load sensors
    if (binding == "LOAD_1" || binding == "LOAD" || binding == "LOAD1") {
        type = SensorType::CURRENT_LOAD_1;
        return true;
    }
    if (binding == "LOAD_2" || binding == "LOAD2") {
        type = SensorType::CURRENT_LOAD_2;
        return true;
    }
    if (binding == "LOAD_3" || binding == "LOAD3") {
        type = SensorType::CURRENT_LOAD_3;
        return true;
    }
    if (binding == "LOAD_4" || binding == "LOAD4") {
        type = SensorType::CURRENT_LOAD_4;
        return true;
    }
    if (binding == "LOAD_5" || binding == "LOAD5") {
        type = SensorType::CURRENT_LOAD_5;
        return true;
    }
    if (binding == "LOAD_6" || binding == "LOAD6") {
        type = SensorType::CURRENT_LOAD_6;
        return true;
    }
    if (binding == "LOAD_7" || binding == "LOAD7") {
        type = SensorType::CURRENT_LOAD_7;
        return true;
    }
    if (binding == "LOAD_8" || binding == "LOAD8") {
        type = SensorType::CURRENT_LOAD_8;
        return true;
    }

    return false;
}

/**
 * @brief Get human-readable binding name for current sensor type
 *
 * @param type SensorType
 * @return Binding name string (e.g., "GRID", "SOLAR", "LOAD_1")
 */
inline const char* getCurrentSensorBindingName(SensorType type) {
    switch (type) {
        case SensorType::CURRENT_GRID:   return "GRID";
        case SensorType::CURRENT_SOLAR:  return "SOLAR";
        case SensorType::CURRENT_LOAD_1: return "LOAD_1";
        case SensorType::CURRENT_LOAD_2: return "LOAD_2";
        case SensorType::CURRENT_LOAD_3: return "LOAD_3";
        case SensorType::CURRENT_LOAD_4: return "LOAD_4";
        case SensorType::CURRENT_LOAD_5: return "LOAD_5";
        case SensorType::CURRENT_LOAD_6: return "LOAD_6";
        case SensorType::CURRENT_LOAD_7: return "LOAD_7";
        case SensorType::CURRENT_LOAD_8: return "LOAD_8";
        case SensorType::CURRENT_AUX1:   return "AUX1";
        case SensorType::CURRENT_AUX2:   return "AUX2";
        default:                         return "UNKNOWN";
    }
}

#endif // SENSOR_TYPES_H
