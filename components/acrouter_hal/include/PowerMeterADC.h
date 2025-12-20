/**
 * @file PowerMeterADC.h
 * @brief Power meter using ESP-IDF ADC Continuous (DMA) mode for AC power measurement
 *
 * Implements continuous ADC DMA sampling for real-time AC voltage and current
 * measurement using ESP-IDF's native adc_continuous API for maximum performance.
 *
 * Architecture:
 * - DMA ADC at 20 kHz (80 kHz total for 4 channels)
 * - ISR callback every 10 ms (minimal processing)
 * - Processing task: channel extraction + RMS accumulation
 * - RMS calculation every 200 ms → triggers main system callback
 *
 * Features:
 * - Direct DMA access for optimal performance
 * - RMS calculation over 200ms (10 AC cycles at 50Hz)
 * - Support for voltage sensor (ZMPT107) and current sensors (SCT-013)
 * - Callback-driven architecture (RMS results as system driver)
 * - Thread-safe operation with FreeRTOS task
 * - Configurable ADC channels via SensorTypes.h
 */

#ifndef POWER_METER_ADC_H
#define POWER_METER_ADC_H

#include <Arduino.h>
#include "SensorTypes.h"
#include "PinDefinitions.h"
#include "DataTypes.h"
#include "esp_log.h"
#include "esp_adc/adc_continuous.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <cmath>

// ============================================================
// Configuration Constants
// ============================================================

namespace PowerMeterConfig {
    // DMA ADC settings
    constexpr uint32_t SAMPLING_FREQ_HZ = 20000;       // Per-channel sampling frequency
    constexpr uint8_t MAX_CHANNELS = 4;                // Voltage + 3 current sensors

    // Frame configuration (10 ms per frame)
    constexpr uint32_t FRAME_TIME_MS = 10;             // DMA callback interval
    constexpr uint32_t SAMPLES_PER_FRAME = 200;        // Samples per channel per frame
    constexpr uint32_t TOTAL_SAMPLES_PER_FRAME = SAMPLES_PER_FRAME * MAX_CHANNELS;  // 800
    constexpr uint32_t FRAME_SIZE_BYTES = TOTAL_SAMPLES_PER_FRAME * 2;  // 1600 bytes
    constexpr uint32_t BUFFER_SIZE_BYTES = FRAME_SIZE_BYTES * 2;        // 3200 bytes (DMA buffer)

    // RMS calculation (200 ms period)
    constexpr uint16_t RMS_FRAMES_COUNT = 20;          // 20 frames × 10 ms = 200 ms
    constexpr uint16_t RMS_SAMPLES_PER_CHANNEL = SAMPLES_PER_FRAME * RMS_FRAMES_COUNT;  // 4000
    constexpr uint32_t RMS_UPDATE_INTERVAL_MS = 200;   // Update RMS every 200ms

    // ADC Configuration
    constexpr uint8_t ADC_WIDTH_BITS = 12;             // 12-bit resolution
    constexpr adc_atten_t ADC_ATTENUATION = ADC_ATTEN_DB_12;  // 0-3.3V range

    // Task configuration
    constexpr uint32_t PROCESSING_TASK_STACK_SIZE = 4096;
    constexpr UBaseType_t PROCESSING_TASK_PRIORITY = 10;  // High priority
}

// ============================================================
// PowerMeterADC Class
// ============================================================

/**
 * @brief Power meter using ESP-IDF ADC Continuous (DMA) mode
 *
 * Singleton class that manages DMA-based continuous ADC sampling for AC power measurement.
 * Provides callback-driven architecture where RMS results every 200ms drive the system.
 */
class PowerMeterADC {
public:
    // ============================================================
    // Public Types
    // ============================================================

    /**
     * @brief Channel index for accessing current sensor data
     */
    enum CurrentChannel : uint8_t {
        CURRENT_LOAD = 0,    ///< Load current sensor
        CURRENT_GRID = 1,    ///< Grid current sensor
        CURRENT_SOLAR = 2,   ///< Solar current sensor
        CURRENT_COUNT = 3    ///< Total number of current channels
    };

    /**
     * @brief Phase sign for halfperiod analysis
     *
     * Indicates the predominant polarity of a signal during a measurement period.
     * Used to determine current direction (consuming vs supplying).
     */
    enum class PhaseSign : uint8_t {
        POSITIVE = 0,    ///< Positive halfperiod (above DC offset)
        NEGATIVE = 1,    ///< Negative halfperiod (below DC offset)
        BALANCED = 2     ///< Balanced (rare, indicates zero power)
    };

    /**
     * @brief Current direction indicator
     */
    enum class CurrentDirection : uint8_t {
        CONSUMING = 0,   ///< Consuming power from grid
        SUPPLYING = 1,   ///< Supplying power to grid
        ZERO = 2,        ///< Zero or negligible power
        UNKNOWN = 3      ///< Unknown (not enough data)
    };

    /**
     * @brief Power meter measurement results
     */
    struct Measurements {
        // RMS values
        float voltage_rms;                      ///< RMS voltage in volts
        float current_rms[CURRENT_COUNT];       ///< RMS currents in amperes
        float voltage_vdc_raw;                  ///< Raw VDC RMS from voltage sensor (before multiplier)

        // Phase analysis
        PhaseSign voltage_phase;                ///< Voltage phase sign
        PhaseSign current_phase[CURRENT_COUNT]; ///< Current phase signs

        // Power and direction
        float power_active[CURRENT_COUNT];      ///< Active power in watts (can be negative)
        CurrentDirection direction[CURRENT_COUNT]; ///< Current direction for each channel

        // Metadata
        uint64_t timestamp_us;                  ///< Measurement timestamp (microseconds)
        uint32_t sample_count;                  ///< Number of samples used for RMS
        bool valid;                             ///< Data validity flag

        Measurements() :
            voltage_rms(0.0f),
            voltage_vdc_raw(0.0f),
            voltage_phase(PhaseSign::BALANCED),
            timestamp_us(0),
            sample_count(0),
            valid(false) {
            for (int i = 0; i < CURRENT_COUNT; i++) {
                current_rms[i] = 0.0f;
                current_phase[i] = PhaseSign::BALANCED;
                power_active[i] = 0.0f;
                direction[i] = CurrentDirection::UNKNOWN;
            }
        }
    };

    /**
     * @brief Callback function type for RMS results
     *
     * This callback is invoked every 200ms when RMS calculations complete.
     * It runs in the context of the processing task (not ISR).
     *
     * @param measurements Latest RMS measurement results
     * @param user_data User-provided data pointer
     */
    typedef void (*RMSResultsCallback)(const Measurements& measurements, void* user_data);

    // ============================================================
    // Singleton Access
    // ============================================================

    /**
     * @brief Get singleton instance
     */
    static PowerMeterADC& getInstance();

    // Prevent copying
    PowerMeterADC(const PowerMeterADC&) = delete;
    PowerMeterADC& operator=(const PowerMeterADC&) = delete;

    // ============================================================
    // Lifecycle
    // ============================================================

    /**
     * @brief Initialize power meter with ADC channel configuration
     *
     * @param configs Array of ADC channel configurations
     * @param channel_count Number of active channels (must be <= MAX_CHANNELS)
     * @return true if initialization successful, false otherwise
     */
    bool begin(const ADCChannelConfig* configs, uint8_t channel_count);

    /**
     * @brief Start ADC DMA sampling
     *
     * @return true if started successfully
     */
    bool start();

    /**
     * @brief Stop ADC DMA sampling
     *
     * @return true if stopped successfully
     */
    bool stop();

    /**
     * @brief Deinitialize and cleanup resources
     */
    void deinit();

    /**
     * @brief Check if power meter is running
     */
    bool isRunning() const { return m_is_running; }

    // ============================================================
    // Callback Registration
    // ============================================================

    /**
     * @brief Register callback for RMS results (200ms updates)
     *
     * This is the main system driver - callback is invoked every 200ms
     * with fresh RMS measurements, triggering all downstream processing.
     *
     * @param callback Callback function pointer
     * @param user_data Optional user data passed to callback
     */
    void setResultsCallback(RMSResultsCallback callback, void* user_data = nullptr);

    // ============================================================
    // Get Results
    // ============================================================

    /**
     * @brief Get latest measurement results
     *
     * @return Measurements structure with RMS values
     */
    Measurements getMeasurements() const;

    /**
     * @brief Get voltage RMS in volts
     */
    float getVoltageRMS() const;

    /**
     * @brief Get current RMS in amperes
     *
     * @param channel Current channel index (CURRENT_LOAD, CURRENT_GRID, CURRENT_SOLAR)
     * @return Current RMS value in amperes
     */
    float getCurrentRMS(CurrentChannel channel) const;

    /**
     * @brief Get timestamp of last measurement (microseconds)
     */
    uint64_t getLastUpdateTime() const;

    /**
     * @brief Check if measurements are valid
     */
    bool isDataValid() const;

    /**
     * @brief Get raw VDC RMS from voltage sensor (before multiplier applied)
     *
     * Returns the RMS voltage measured directly from the voltage sensor in volts (VDC).
     * This is the raw sensor output BEFORE the calibration multiplier is applied.
     * Useful for automatic calibration: multiplier = V_measured_grid / getRawVoltageVDC()
     *
     * @return Raw VDC RMS in volts (e.g., 0.70V for ZMPT107 at 230VAC nominal)
     */
    float getRawVoltageVDC() const;

    // ============================================================
    // Statistics
    // ============================================================

    /**
     * @brief Get total number of frames processed
     */
    uint32_t getFramesProcessed() const { return m_frames_processed; }

    /**
     * @brief Get number of frames dropped
     */
    uint32_t getFramesDropped() const { return m_frames_dropped; }

    /**
     * @brief Get number of RMS calculations completed
     */
    uint32_t getRMSUpdateCount() const { return m_rms_update_count; }

    // ============================================================
    // Debug Control
    // ============================================================

    /**
     * @brief Set debug logging period
     *
     * Controls debug logging output from calculateAndPublishRMS().
     * Can be enabled/disabled via console command.
     *
     * @param period_sec Period in seconds (0 = disable debug logging, >0 = enable with period)
     */
    void setDebugPeriod(uint32_t period_sec);

    /**
     * @brief Get current debug period setting
     * @return Debug period in seconds (0 = disabled)
     */
    uint32_t getDebugPeriod() const { return m_debug_period_sec; }

private:
    // ============================================================
    // Private Constructor
    // ============================================================

    PowerMeterADC();
    ~PowerMeterADC();

    // ============================================================
    // DMA Callback (IRAM)
    // ============================================================

    /**
     * @brief DMA conversion complete callback (called from ISR)
     *
     * Minimal processing: copy data and notify processing task
     */
    static bool IRAM_ATTR dmaConversionCallback(
        adc_continuous_handle_t handle,
        const adc_continuous_evt_data_t* edata,
        void* user_data);

    // ============================================================
    // Processing Task
    // ============================================================

    /**
     * @brief FreeRTOS task for data processing
     */
    static void processingTask(void* param);

    /**
     * @brief Process one DMA frame
     *
     * @param buffer DMA buffer with interleaved channel data
     * @param size Buffer size in bytes
     */
    void IRAM_ATTR processFrame(uint8_t* buffer, uint32_t size);

    /**
     * @brief Calculate and publish RMS results
     */
    void calculateAndPublishRMS();

    /**
     * @brief Output debug logging information
     *
     * Logs detailed ADC and phase information for debugging purposes.
     * Called from calculateAndPublishRMS() based on m_debug_period_sec setting.
     *
     * @param new_measurements Latest measurements for debug display
     */
    void logDebugInfo(const Measurements& new_measurements);

    // ============================================================
    // Calibration
    // ============================================================

    /**
     * @brief Initialize ADC calibration
     */
    bool initCalibration();

    /**
     * @brief Convert raw ADC value to calibrated millivolts
     */
    int IRAM_ATTR rawToMillivolts(uint16_t raw_value);

    /**
     * @brief Apply channel-specific calibration multiplier
     */
    float applyChannelCalibration(int millivolts, uint8_t channel_idx);

    // ============================================================
    // State Variables
    // ============================================================

    // Configuration
    ADCChannelConfig m_channel_configs[PowerMeterConfig::MAX_CHANNELS];
    adc_channel_t m_adc_channels[PowerMeterConfig::MAX_CHANNELS];
    uint8_t m_channel_count;
    bool m_is_running;
    bool m_initialized;

    // DMA handles
    adc_continuous_handle_t m_adc_handle;
    adc_cali_handle_t m_cali_handle;

    // Processing task
    TaskHandle_t m_task_handle;

    // DMA buffer
    uint8_t m_dma_buffer[PowerMeterConfig::FRAME_SIZE_BYTES];

    // RMS accumulation (raw ADC values, integer math)
    uint64_t m_sum_squares[PowerMeterConfig::MAX_CHANNELS];  ///< Sum of squared AC components (raw ADC)
    uint16_t m_frame_count;

    // Phase analysis accumulators (raw ADC values)
    int64_t m_positive_sum[PowerMeterConfig::MAX_CHANNELS];  ///< Sum of positive AC components (raw ADC)
    int64_t m_negative_sum[PowerMeterConfig::MAX_CHANNELS];  ///< Sum of negative AC components (raw ADC)
    uint64_t m_dc_sum[PowerMeterConfig::MAX_CHANNELS];       ///< Sum for DC offset calculation (raw ADC)
    uint32_t m_sample_count[PowerMeterConfig::MAX_CHANNELS]; ///< Sample count per channel

    // Phase correlation counters (for direction detection)
    // Compares voltage and current phase at each sample
    uint32_t m_phase_same[PowerMeterConfig::MAX_CHANNELS];   ///< Count: V and I have same sign (CONSUMING)
    uint32_t m_phase_diff[PowerMeterConfig::MAX_CHANNELS];   ///< Count: V and I have different sign (SUPPLYING)

    // Voltage AC component cache for phase correlation
    int32_t m_voltage_ac;  ///< Current voltage AC component (updated each sample)

    // Results
    Measurements m_measurements;
    mutable SemaphoreHandle_t m_measurements_mutex;

    // Callback
    RMSResultsCallback m_results_callback;
    void* m_callback_user_data;

    // Statistics
    volatile uint32_t m_frames_processed;
    volatile uint32_t m_frames_dropped;
    uint32_t m_rms_update_count;

    // Debug control
    uint32_t m_debug_period_sec;  ///< Debug logging period in seconds (0 = disabled)

    // Logging
    static const char* TAG;
};

#endif // POWER_METER_ADC_H
