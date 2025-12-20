/**
 * @file PinDefinitions.h
 * @brief GPIO Pin Definitions for AC Power Router Controller
 *
 * Hardware: AC Power Router Controller Board (rbdimmer.com)
 * Target: ESP32-WROOM-32 / ESP32-WROVER
 */

#ifndef PIN_DEFINITIONS_H
#define PIN_DEFINITIONS_H

#include <Arduino.h>

// ============================================================
// ADC1 Channels (WiFi-safe)
// ============================================================

/**
 * ADC1 Channel Assignments
 * Any pin can be assigned to any sensor type via configuration
 */
#define PIN_ADC_CH0     36      // ADC1_CHANNEL_0 (VP)   - Input only
#define PIN_ADC_CH3     39      // ADC1_CHANNEL_3 (VN)   - Input only
#define PIN_ADC_CH4     32      // ADC1_CHANNEL_4        - General purpose
#define PIN_ADC_CH5     33      // ADC1_CHANNEL_5        - General purpose
#define PIN_ADC_CH6     34      // ADC1_CHANNEL_6        - Input only
#define PIN_ADC_CH7     35      // ADC1_CHANNEL_7        - Input only

// Note: ADC1_CH1 (GPIO37) and ADC1_CH2 (GPIO38) not available on most DevKits

/**
 * Default sensor assignments (can be changed via ConfigManager)
 */
#define PIN_VOLTAGE_SENSOR      PIN_ADC_CH7     // IO35 - ZMPT107
#define PIN_CURRENT_SENSOR_1    PIN_ADC_CH3     // IO39 - SCT-013 Load
#define PIN_CURRENT_SENSOR_2    PIN_ADC_CH0     // IO36 - SCT-013 Grid
#define PIN_CURRENT_SENSOR_3    PIN_ADC_CH6     // IO34 - SCT-013 Solar

// ============================================================
// Digital Inputs
// ============================================================

/**
 * Zero-Cross Detection
 * Critical for phase-cut dimming synchronization
 * Requires external interrupt capability
 */
#define PIN_ZEROCROSS           18              // Zero-cross detector input

// ============================================================
// Dimmer Outputs (TRIAC Control)
// ============================================================

/**
 * Phase-cut dimmer outputs
 * Connected to TRIAC driver circuits
 */
#define PIN_DIMMER_1            19              // Dimmer channel 1
#define PIN_DIMMER_2            23              // Dimmer channel 2 [Phase 2]

// ============================================================
// Relay Outputs
// ============================================================

/**
 * Relay control outputs
 * Active HIGH (can be inverted in software if needed)
 */
#define PIN_RELAY_1             15              // Relay channel 1 [Phase 2]
#define PIN_RELAY_2             2               // Relay channel 2 [Phase 2]
                                                // Note: IO2 also controls onboard LED

// ============================================================
// Indicator LEDs
// ============================================================

/**
 * Status indicator LEDs
 */
#define PIN_LED_STATUS          17              // Status LED (system state)
#define PIN_LED_LOAD            5               // Load LED (dimmer activity)

// Alias for compatibility
#define LED_BUILTIN             PIN_RELAY_2     // Most ESP32 boards use IO2

// ============================================================
// Buzzer Output
// ============================================================

/**
 * Piezo buzzer for audible alerts
 * Can use digital on/off or PWM for tones
 */
#define PIN_BUZZER              4               // Buzzer output [Phase 2]

// ============================================================
// OneWire Bus (Temperature Sensors)
// ============================================================

/**
 * OneWire bus for DS18B20 temperature sensors
 */
#define PIN_ONEWIRE             27              // OneWire data [Phase 2]

// ============================================================
// ADC Constants
// ============================================================

/**
 * ADC resolution and reference voltage
 */
#define ADC_RESOLUTION          12              // 12-bit ADC (0-4095)
#define ADC_MAX_VALUE           4095            // Maximum ADC reading
#define ADC_VREF                3.3f            // Reference voltage (V)

/**
 * ADC channels enum for easier indexing
 */
enum ADCChannel : uint8_t {
    ADC_CH0 = 0,    // GPIO36
    ADC_CH3 = 1,    // GPIO39
    ADC_CH4 = 2,    // GPIO32
    ADC_CH5 = 3,    // GPIO33
    ADC_CH6 = 4,    // GPIO34
    ADC_CH7 = 5,    // GPIO35
    ADC_CHANNEL_COUNT = 6
};

// ============================================================
// Hardware Limits
// ============================================================

/**
 * Maximum number of supported channels/devices
 */
#define MAX_ADC_CHANNELS        8               // ESP32 ADC1 has 8 channels
#define MAX_DIMMER_CHANNELS     2               // 2 TRIAC dimmers supported
#define MAX_RELAY_CHANNELS      2               // 2 relays supported
#define MAX_LED_CHANNELS        2               // 2 indicator LEDs
#define MAX_TEMP_SENSORS        4               // Maximum DS18B20 sensors [Phase 2]

// ============================================================
// Pin Validation Macros
// ============================================================

/**
 * Check if GPIO is valid for ADC1
 */
#define IS_VALID_ADC1_PIN(pin) ( \
    (pin) == 36 || (pin) == 39 || (pin) == 34 || (pin) == 35 || \
    (pin) == 32 || (pin) == 33 || (pin) == 37 || (pin) == 38 \
)

/**
 * Check if GPIO is input-only
 */
#define IS_INPUT_ONLY_PIN(pin) ( \
    (pin) == 34 || (pin) == 35 || (pin) == 36 || (pin) == 39 \
)

/**
 * Check if GPIO supports pull-up/pull-down
 */
#define SUPPORTS_PULLUP(pin) (!IS_INPUT_ONLY_PIN(pin))

// ============================================================
// Boot Pin Warnings
// ============================================================

/**
 * GPIO2 must be LOW during boot for proper startup
 * If using active-LOW relay on IO2, it may activate during boot
 *
 * GPIO0, GPIO2, GPIO5, GPIO12, GPIO15 affect boot mode
 */
#define BOOT_STRAPPING_PINS     {0, 2, 5, 12, 15}

// ============================================================
// Pin Groups for Initialization
// ============================================================

/**
 * Output pins to initialize at startup
 */
#define OUTPUT_PINS { \
    PIN_DIMMER_1, \
    PIN_DIMMER_2, \
    PIN_RELAY_1, \
    PIN_RELAY_2, \
    PIN_LED_STATUS, \
    PIN_LED_LOAD, \
    PIN_BUZZER \
}

/**
 * Input pins to initialize at startup
 */
#define INPUT_PINS { \
    PIN_ZEROCROSS \
}

/**
 * Default safe state for outputs (at boot)
 * All outputs LOW except relays which depend on module type
 */
#define SAFE_OUTPUT_STATE       LOW

// ============================================================
// Debug / Test Points
// ============================================================

// Uncomment for debug output on spare GPIO
// #define DEBUG_PIN_1          25
// #define DEBUG_PIN_2          26

#endif // PIN_DEFINITIONS_H
