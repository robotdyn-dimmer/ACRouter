/**
 * @file PowerMeterADC.cpp
 * @brief Implementation of Power Meter using ESP-IDF ADC Continuous (DMA) mode
 *
 * @author ACRouter Project
 * @date 2024
 */

#include "PowerMeterADC.h"
#include "driver/gpio.h"
#include "soc/soc_caps.h"
#include <cstring>

// Logging tag
const char* PowerMeterADC::TAG = "PowerMeterADC";

// ADC format macros for ESP32
#if CONFIG_IDF_TARGET_ESP32 || CONFIG_IDF_TARGET_ESP32S2
#define ADC_OUTPUT_TYPE         ADC_DIGI_OUTPUT_FORMAT_TYPE1
#define ADC_GET_CHANNEL(p_data) ((p_data)->type1.channel)
#define ADC_GET_DATA(p_data)    ((p_data)->type1.data)
#else
#define ADC_OUTPUT_TYPE         ADC_DIGI_OUTPUT_FORMAT_TYPE2
#define ADC_GET_CHANNEL(p_data) ((p_data)->type2.channel)
#define ADC_GET_DATA(p_data)    ((p_data)->type2.data)
#endif

// ============================================================
// Singleton Instance
// ============================================================

PowerMeterADC& PowerMeterADC::getInstance() {
    static PowerMeterADC instance;
    return instance;
}

// ============================================================
// Constructor / Destructor
// ============================================================

PowerMeterADC::PowerMeterADC() :
    m_channel_count(0),
    m_is_running(false),
    m_initialized(false),
    m_adc_handle(nullptr),
    m_cali_handle(nullptr),
    m_task_handle(nullptr),
    m_frame_count(0),
    m_results_callback(nullptr),
    m_callback_user_data(nullptr),
    m_frames_processed(0),
    m_frames_dropped(0),
    m_rms_update_count(0),
    m_debug_period_sec(0)  // Debug logging disabled by default
{
    // Initialize accumulators
    memset(m_sum_squares, 0, sizeof(m_sum_squares));
    memset(m_positive_sum, 0, sizeof(m_positive_sum));
    memset(m_negative_sum, 0, sizeof(m_negative_sum));
    memset(m_dc_sum, 0, sizeof(m_dc_sum));
    memset(m_sample_count, 0, sizeof(m_sample_count));
    memset(m_adc_channels, 0, sizeof(m_adc_channels));
    memset(m_phase_same, 0, sizeof(m_phase_same));
    memset(m_phase_diff, 0, sizeof(m_phase_diff));
    m_voltage_ac = 0;

    // Create mutex for measurements
    m_measurements_mutex = xSemaphoreCreateMutex();
}

PowerMeterADC::~PowerMeterADC() {
    deinit();

    if (m_measurements_mutex) {
        vSemaphoreDelete(m_measurements_mutex);
    }
}

// ============================================================
// Initialization
// ============================================================

bool PowerMeterADC::begin(const ADCChannelConfig* configs, uint8_t channel_count) {
    if (m_initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return true;
    }

    if (channel_count == 0 || channel_count > PowerMeterConfig::MAX_CHANNELS) {
        ESP_LOGE(TAG, "Invalid channel count: %d (max %d)", channel_count, PowerMeterConfig::MAX_CHANNELS);
        return false;
    }

    // Filter and store only ENABLED channels with valid GPIO
    m_channel_count = 0;
    for (uint8_t i = 0; i < channel_count; i++) {
        // Skip disabled channels or channels with invalid GPIO
        if (!configs[i].enabled || configs[i].gpio == 0) {
            ESP_LOGI(TAG, "Channel %d: SKIPPED (enabled=%d, gpio=%d)",
                     i, configs[i].enabled, configs[i].gpio);
            continue;
        }

        // Convert GPIO pin to ADC channel
        adc_unit_t adc_unit;
        adc_channel_t adc_channel;
        esp_err_t err = adc_continuous_io_to_channel(configs[i].gpio, &adc_unit, &adc_channel);

        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Channel %d: GPIO %d is not a valid ADC pin, skipping", i, configs[i].gpio);
            continue;
        }

        if (adc_unit != ADC_UNIT_1) {
            ESP_LOGW(TAG, "Channel %d: GPIO %d is ADC%d (not ADC1), skipping",
                     i, configs[i].gpio, adc_unit + 1);
            continue;
        }

        // Store valid enabled channel
        m_channel_configs[m_channel_count] = configs[i];
        m_adc_channels[m_channel_count] = adc_channel;

        ESP_LOGI(TAG, "Channel %d: GPIO %d -> ADC1_CH%d (type: %s, multiplier: %.4f)",
                 m_channel_count, configs[i].gpio, adc_channel,
                 isVoltageSensor(configs[i].type) ? "Voltage" : "Current",
                 configs[i].multiplier);

        m_channel_count++;
    }

    // Verify we have at least one valid channel
    if (m_channel_count == 0) {
        ESP_LOGE(TAG, "No valid enabled ADC channels found!");
        return false;
    }

    // Create ADC continuous handle
    adc_continuous_handle_cfg_t adc_config = {
        .max_store_buf_size = PowerMeterConfig::BUFFER_SIZE_BYTES,
        .conv_frame_size = PowerMeterConfig::FRAME_SIZE_BYTES,
    };

    esp_err_t ret = adc_continuous_new_handle(&adc_config, &m_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create ADC handle: %s", esp_err_to_name(ret));
        return false;
    }

    // Configure ADC patterns
    adc_digi_pattern_config_t adc_patterns[PowerMeterConfig::MAX_CHANNELS];
    for (uint8_t i = 0; i < m_channel_count; i++) {
        adc_patterns[i].atten = PowerMeterConfig::ADC_ATTENUATION;
        adc_patterns[i].channel = m_adc_channels[i];
        adc_patterns[i].unit = ADC_UNIT_1;
        adc_patterns[i].bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
    }

    adc_continuous_config_t dig_cfg = {
        .pattern_num = m_channel_count,
        .adc_pattern = adc_patterns,
        .sample_freq_hz = PowerMeterConfig::SAMPLING_FREQ_HZ * m_channel_count,
        .conv_mode = ADC_CONV_SINGLE_UNIT_1,
        .format = ADC_OUTPUT_TYPE,
    };

    ret = adc_continuous_config(m_adc_handle, &dig_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure ADC: %s", esp_err_to_name(ret));
        adc_continuous_deinit(m_adc_handle);
        m_adc_handle = nullptr;
        return false;
    }

    // Initialize calibration
    if (!initCalibration()) {
        ESP_LOGW(TAG, "ADC calibration not available, using raw values");
    }

    // Register DMA callback
    adc_continuous_evt_cbs_t cbs = {
        .on_conv_done = dmaConversionCallback,
        .on_pool_ovf = nullptr
    };

    ret = adc_continuous_register_event_callbacks(m_adc_handle, &cbs, this);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DMA callbacks: %s", esp_err_to_name(ret));
        adc_continuous_deinit(m_adc_handle);
        m_adc_handle = nullptr;
        return false;
    }

    // Create processing task
    BaseType_t task_ret = xTaskCreate(
        processingTask,
        "PowerMeterADC",
        PowerMeterConfig::PROCESSING_TASK_STACK_SIZE,
        this,
        PowerMeterConfig::PROCESSING_TASK_PRIORITY,
        &m_task_handle
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create processing task");
        adc_continuous_deinit(m_adc_handle);
        m_adc_handle = nullptr;
        return false;
    }

    m_initialized = true;
    ESP_LOGI(TAG, "Initialized successfully (%d channels, %lu Hz per channel)",
             m_channel_count, PowerMeterConfig::SAMPLING_FREQ_HZ);

    return true;
}

bool PowerMeterADC::start() {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    if (m_is_running) {
        ESP_LOGW(TAG, "Already running");
        return true;
    }

    // Reset statistics
    m_frames_processed = 0;
    m_frames_dropped = 0;
    m_frame_count = 0;
    memset(m_sum_squares, 0, sizeof(m_sum_squares));

    // Start ADC DMA
    esp_err_t ret = adc_continuous_start(m_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start ADC DMA: %s", esp_err_to_name(ret));
        return false;
    }

    m_is_running = true;
    ESP_LOGI(TAG, "Started ADC DMA");

    return true;
}

bool PowerMeterADC::stop() {
    if (!m_is_running) {
        return true;
    }

    esp_err_t ret = adc_continuous_stop(m_adc_handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop ADC DMA: %s", esp_err_to_name(ret));
        return false;
    }

    m_is_running = false;
    ESP_LOGI(TAG, "Stopped ADC DMA");

    return true;
}

void PowerMeterADC::deinit() {
    if (!m_initialized) {
        return;
    }

    // Stop if running
    stop();

    // Delete task
    if (m_task_handle) {
        vTaskDelete(m_task_handle);
        m_task_handle = nullptr;
    }

    // Cleanup ADC
    if (m_adc_handle) {
        adc_continuous_deinit(m_adc_handle);
        m_adc_handle = nullptr;
    }

    // Cleanup calibration
    if (m_cali_handle) {
#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
        adc_cali_delete_scheme_curve_fitting(m_cali_handle);
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_delete_scheme_line_fitting(m_cali_handle);
#endif
        m_cali_handle = nullptr;
    }

    m_initialized = false;
    ESP_LOGI(TAG, "Deinitialized");
}

// ============================================================
// Calibration
// ============================================================

bool PowerMeterADC::initCalibration() {
    esp_err_t ret;

#if ADC_CALI_SCHEME_CURVE_FITTING_SUPPORTED
    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = PowerMeterConfig::ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &m_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: curve fitting");
        return true;
    }
#elif ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
    adc_cali_line_fitting_config_t cali_config = {
        .unit_id = ADC_UNIT_1,
        .atten = PowerMeterConfig::ADC_ATTENUATION,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };

    ret = adc_cali_create_scheme_line_fitting(&cali_config, &m_cali_handle);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC calibration: line fitting");
        return true;
    }
#endif

    ESP_LOGW(TAG, "ADC calibration not supported");
    return false;
}

int IRAM_ATTR PowerMeterADC::rawToMillivolts(uint16_t raw_value) {
    // Linear conversion: 12-bit ADC (0-4095) → millivolts
    // IMPORTANT: Calibrated for actual ESP32 power supply voltage (~2.85V) and sensor power (3.26V)
    // Calibration factor determined empirically: 0.247V measured vs 0.343V calculated = 0.720 correction
    // Effective reference voltage: 3300mV * 0.720 = 2376mV
    return (raw_value * 2376) / 4095;
}

float PowerMeterADC::applyChannelCalibration(int millivolts, uint8_t channel_idx) {
    if (channel_idx >= m_channel_count) {
        return 0.0f;
    }

    // Convert mV to V, then apply sensor-specific multiplier
    float volts = millivolts / 1000.0f;
    return volts * m_channel_configs[channel_idx].multiplier;
}

// ============================================================
// Callback Registration
// ============================================================

void PowerMeterADC::setResultsCallback(RMSResultsCallback callback, void* user_data) {
    m_results_callback = callback;
    m_callback_user_data = user_data;
}

// ============================================================
// Get Results
// ============================================================

PowerMeterADC::Measurements PowerMeterADC::getMeasurements() const {
    Measurements result;

    if (m_measurements_mutex && xSemaphoreTake(m_measurements_mutex, pdMS_TO_TICKS(10))) {
        result = m_measurements;
        xSemaphoreGive(m_measurements_mutex);
    }

    return result;
}

float PowerMeterADC::getVoltageRMS() const {
    return getMeasurements().voltage_rms;
}

float PowerMeterADC::getCurrentRMS(CurrentChannel channel) const {
    if (channel < CURRENT_COUNT) {
        return getMeasurements().current_rms[channel];
    }
    return 0.0f;
}

uint64_t PowerMeterADC::getLastUpdateTime() const {
    return getMeasurements().timestamp_us;
}

bool PowerMeterADC::isDataValid() const {
    return getMeasurements().valid;
}

float PowerMeterADC::getRawVoltageVDC() const {
    return getMeasurements().voltage_vdc_raw;
}

// ============================================================
// DMA Callback (IRAM) - MINIMAL PROCESSING
// ============================================================

bool IRAM_ATTR PowerMeterADC::dmaConversionCallback(
    adc_continuous_handle_t handle,
    const adc_continuous_evt_data_t* edata,
    void* user_data)
{
    PowerMeterADC* instance = static_cast<PowerMeterADC*>(user_data);

    // Check size
    if (edata->size > PowerMeterConfig::FRAME_SIZE_BYTES) {
        instance->m_frames_dropped++;
        return false;
    }

    // Signal processing task
    BaseType_t taskWoken = pdFALSE;
    vTaskNotifyGiveFromISR(instance->m_task_handle, &taskWoken);

    instance->m_frames_processed++;

    return (taskWoken == pdTRUE);
}

// ============================================================
// Processing Task
// ============================================================

void PowerMeterADC::processingTask(void* param) {
    PowerMeterADC* instance = static_cast<PowerMeterADC*>(param);

    ESP_LOGI(TAG, "Processing task started");

    while (true) {
        // Wait for DMA callback notification
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        // Read data from DMA
        uint32_t bytes_read = 0;
        esp_err_t ret = adc_continuous_read(
            instance->m_adc_handle,
            instance->m_dma_buffer,
            PowerMeterConfig::FRAME_SIZE_BYTES,
            &bytes_read,
            100  // 100 ms timeout
        );

        if (ret == ESP_OK && bytes_read > 0) {
            instance->processFrame(instance->m_dma_buffer, bytes_read);
        } else if (ret == ESP_ERR_TIMEOUT) {
            ESP_LOGW(TAG, "ADC read timeout");
        } else {
            ESP_LOGE(TAG, "ADC read error: %s", esp_err_to_name(ret));
        }
    }
}

// ============================================================
// Frame Processing - Channel Extraction + RMS Accumulation
// ============================================================

void IRAM_ATTR PowerMeterADC::processFrame(uint8_t* buffer, uint32_t size) {
    uint16_t* data_16bit = reinterpret_cast<uint16_t*>(buffer);
    uint32_t elements = size / sizeof(uint16_t);
    uint32_t samples_per_channel = elements / m_channel_count;

    // Process interleaved channel data
    // Buffer format: channels are SORTED by channel number [CH0, CH3, CH6, CH7, ...]
    // NOT in configuration order!
    for (uint32_t sample = 0; sample < samples_per_channel; sample++) {
        for (uint8_t ch_idx = 0; ch_idx < m_channel_count; ch_idx++) {
            uint32_t buffer_idx = sample * m_channel_count + ch_idx;

            if (buffer_idx >= elements) {
                break;
            }

            // Extract data: [15:12] = channel, [11:0] = data
            uint16_t raw_data = data_16bit[buffer_idx];
            uint16_t adc_value = raw_data & 0x0FFF;
            uint8_t detected_channel = (raw_data >> 12) & 0x0F;

            // Find which configured channel this data belongs to
            int8_t config_idx = -1;
            for (uint8_t i = 0; i < m_channel_count; i++) {
                if (m_adc_channels[i] == detected_channel) {
                    config_idx = i;
                    break;
                }
            }

            if (config_idx >= 0) {
                // === WORK WITH RAW ADC VALUES (0-4095) - NO CALIBRATION ===

                // Accumulate raw ADC for DC offset calculation
                m_dc_sum[config_idx] += adc_value;
                m_sample_count[config_idx]++;

                // Calculate adaptive DC offset (average DC value over current measurement period)
                // This compensates for power supply voltage variations
                int32_t dc_offset = (m_sample_count[config_idx] > 0) ?
                                   (m_dc_sum[config_idx] / m_sample_count[config_idx]) : 2048;

                // Calculate AC component (raw ADC units)
                // ac_component = sample - measured_dc_offset
                int32_t ac_component = static_cast<int32_t>(adc_value) - dc_offset;

                // Accumulate square for RMS (integer math, no float!)
                int64_t ac_squared = static_cast<int64_t>(ac_component) * ac_component;
                m_sum_squares[config_idx] += ac_squared;

                // Accumulate positive and negative sums (for debug/legacy)
                if (ac_component > 0) {
                    m_positive_sum[config_idx] += ac_component;
                } else if (ac_component < 0) {
                    m_negative_sum[config_idx] += ac_component;
                }

                // === PHASE CORRELATION FOR DIRECTION DETECTION ===
                // Check if this is voltage channel (config_idx 0) or current channel
                if (isVoltageSensor(m_channel_configs[config_idx].type)) {
                    // Save voltage AC component for comparison with current channels
                    m_voltage_ac = ac_component;
                } else if (isCurrentSensor(m_channel_configs[config_idx].type)) {
                    // Compare current phase with voltage phase
                    // Same sign = in phase = CONSUMING
                    // Different sign = out of phase = SUPPLYING
                    bool voltage_positive = (m_voltage_ac > 0);
                    bool current_positive = (ac_component > 0);

                    if (voltage_positive == current_positive) {
                        m_phase_same[config_idx]++;   // Same sign → CONSUMING
                    } else {
                        m_phase_diff[config_idx]++;   // Different sign → SUPPLYING
                    }
                }
            } else {
                // Unknown channel - log occasionally
                static uint32_t mismatch_count = 0;
                if (++mismatch_count % 10000 == 0) {
                    ESP_LOGW(TAG, "Unknown channel detected: %d (not in configuration)", detected_channel);
                }
            }
        }
    }

    m_frame_count++;

    // Every 20 frames (200 ms) - calculate RMS
    if (m_frame_count >= PowerMeterConfig::RMS_FRAMES_COUNT) {
        calculateAndPublishRMS();
    }
}

// ============================================================
// Debug Logging
// ============================================================

void PowerMeterADC::logDebugInfo(const Measurements& new_measurements) {
    // Check if debug logging is enabled and if it's time to log
    if (m_debug_period_sec == 0) {
        return;  // Debug logging disabled
    }

    // Calculate how many RMS updates correspond to the debug period
    // RMS updates every 200ms, so 5 updates = 1 second
    uint32_t updates_per_period = m_debug_period_sec * 5;

    static uint32_t log_counter = 0;
    if (++log_counter % updates_per_period != 0) {
        return;  // Not time to log yet
    }

    // Log voltage sensor debug info
    int voltage_ch_idx = -1;
    for (uint8_t i = 0; i < m_channel_count; i++) {
        if (m_channel_configs[i].type == SensorType::VOLTAGE_AC) {
            voltage_ch_idx = i;
            break;
        }
    }

    if (voltage_ch_idx >= 0) {
        int64_t voltage_balance = m_positive_sum[0] + m_negative_sum[0];
        const char* v_phase_str = (voltage_balance > 0) ? "POSITIVE" :
                                 (voltage_balance < 0) ? "NEGATIVE" : "BALANCED";
        ESP_LOGI(TAG, "VOLTAGE CH0: rms=%.1fV, phase=%s (pos=%lld, neg=%lld)",
                 new_measurements.voltage_rms,
                 v_phase_str,
                 m_positive_sum[0],
                 m_negative_sum[0]);
    }

    // Log current sensors debug info
    for (uint8_t ch = 0; ch < m_channel_count; ch++) {
        if (isCurrentSensor(m_channel_configs[ch].type)) {
            // Determine array index based on sensor type (same logic as calculateAndPublishRMS)
            int current_idx = -1;
            if (m_channel_configs[ch].type == SensorType::CURRENT_GRID) {
                current_idx = CURRENT_GRID;
            } else if (m_channel_configs[ch].type == SensorType::CURRENT_SOLAR) {
                current_idx = CURRENT_SOLAR;
            } else if (m_channel_configs[ch].type >= SensorType::CURRENT_LOAD_1 &&
                       m_channel_configs[ch].type <= SensorType::CURRENT_LOAD_8) {
                current_idx = CURRENT_LOAD;
            }

            if (current_idx < 0 || current_idx >= CURRENT_COUNT) {
                continue;  // Skip unknown sensor types
            }

            // Calculate DC offset for debug display
            double dc_avg = (m_sample_count[ch] > 0) ?
                           (static_cast<double>(m_dc_sum[ch]) / m_sample_count[ch]) : 2048.0;

            // Calculate RMS ADC value for debug display
            double mean_square_adc = static_cast<double>(m_sum_squares[ch]) / PowerMeterConfig::RMS_SAMPLES_PER_CHANNEL;
            double rms_adc = sqrt(mean_square_adc);
            int rms_mv = rawToMillivolts(static_cast<uint16_t>(rms_adc));
            float vdc = rms_mv / 1000.0f;

            // Current phase relative to its own zero
            int64_t current_balance = m_positive_sum[ch] + m_negative_sum[ch];
            const char* phase_str = (current_balance > 0) ? "POSITIVE" :
                                   (current_balance < 0) ? "NEGATIVE" : "BALANCED";

            // Phase correlation with voltage (direction indicator)
            uint32_t same = m_phase_same[ch];
            uint32_t diff = m_phase_diff[ch];
            const char* direction_str = (same > diff) ? "CONSUMING" :
                                       (diff > same) ? "SUPPLYING" : "UNKNOWN";

            // Output debug lines
            ESP_LOGI(TAG, "DEBUG CH%d [GPIO%d] %s: dc_avg=%.1f, rms_adc=%.2f, vdc=%.3fV, amps=%.2f",
                     ch,
                     m_channel_configs[ch].gpio,
                     sensorTypeToString(m_channel_configs[ch].type),
                     dc_avg,
                     rms_adc,
                     vdc,
                     new_measurements.current_rms[current_idx]);
            ESP_LOGI(TAG, "  Phase: current=%s (pos=%lld, neg=%lld), correlation: same=%lu, diff=%lu -> %s",
                     phase_str,
                     m_positive_sum[ch],
                     m_negative_sum[ch],
                     same,
                     diff,
                     direction_str);
        }
    }
}

// ============================================================
// RMS Calculation and Callback Invocation
// ============================================================

void PowerMeterADC::calculateAndPublishRMS() {
    // Calculate RMS for all channels
    Measurements new_measurements;
    new_measurements.timestamp_us = esp_timer_get_time();
    new_measurements.sample_count = PowerMeterConfig::RMS_SAMPLES_PER_CHANNEL;
    new_measurements.valid = true;

    // === Sensor profile coefficients ===
    // ZMPT107 voltage sensor: Calibrated to 0.70 VDC = Nominal AC voltage
    // User calibrates by measuring actual grid voltage and setting:
    // multiplier = V_measured / 0.70
    // Example: If grid is 230V and ADC reads 0.70V RMS → multiplier = 230/0.70 = 328.57
    //          If grid is 110V and ADC reads 0.70V RMS → multiplier = 110/0.70 = 157.14

    // SCT-013 current sensor: Typical 30A/1V or 50A/1V or 100A/1V models
    // User calibrates by measuring actual load current with clamp meter:
    // multiplier = I_measured / V_adc_rms
    // Example: SCT-013-030 (30A/1V): multiplier = 30
    //          SCT-013-050 (50A/1V): multiplier = 50
    //          SCT-013-100 (100A/1V): multiplier = 100
    // ESP32 ADC has nonlinear offset on small signals (~0.15V at 0A)
    // Using threshold to filter noise
    constexpr float CURRENT_MIN_THRESHOLD_A = 1.0f; // Below this = 0A (noise)

    // === STEP 1: Calculate RMS and Phase Sign for all channels ===
    // Convert raw ADC RMS to voltage (millivolts → volts)

    // Voltage channel (first channel assumed to be voltage)
    uint8_t voltage_ch_idx = 0;
    if (isVoltageSensor(m_channel_configs[0].type)) {
        // Calculate RMS in raw ADC units
        double mean_square_adc = static_cast<double>(m_sum_squares[0]) / PowerMeterConfig::RMS_SAMPLES_PER_CHANNEL;
        double rms_adc = sqrt(mean_square_adc);

        // Convert raw ADC to millivolts using calibration
        int rms_mv = rawToMillivolts(static_cast<uint16_t>(rms_adc));

        // Convert millivolts to volts and apply sensor multiplier from config
        // For ZMPT107: vdc is RMS around 1.65V center (AC component only)
        // Calibration: User measures grid voltage, then multiplier = V_measured / 0.70
        float vdc = rms_mv / 1000.0f;
        float multiplier = m_channel_configs[voltage_ch_idx].multiplier;
        new_measurements.voltage_vdc_raw = vdc;  // Store raw VDC for calibration
        new_measurements.voltage_rms = vdc * multiplier;

        // Determine voltage phase sign
        int64_t voltage_balance = m_positive_sum[0] + m_negative_sum[0];  // negative_sum is < 0
        if (voltage_balance > 0) {
            new_measurements.voltage_phase = PhaseSign::POSITIVE;
        } else if (voltage_balance < 0) {
            new_measurements.voltage_phase = PhaseSign::NEGATIVE;
        } else {
            new_measurements.voltage_phase = PhaseSign::BALANCED;
        }
    }
    // Current channels: Calculate RMS and Phase Sign
    // Note: Use sensor type to determine correct index in current_rms[] array
    for (uint8_t ch = 0; ch < m_channel_count; ch++) {
        if (isCurrentSensor(m_channel_configs[ch].type)) {
            // Determine array index based on sensor type
            int current_idx = -1;
            if (m_channel_configs[ch].type == SensorType::CURRENT_GRID) {
                current_idx = CURRENT_GRID;
            } else if (m_channel_configs[ch].type == SensorType::CURRENT_SOLAR) {
                current_idx = CURRENT_SOLAR;
            } else if (m_channel_configs[ch].type >= SensorType::CURRENT_LOAD_1 &&
                       m_channel_configs[ch].type <= SensorType::CURRENT_LOAD_8) {
                current_idx = CURRENT_LOAD;
            }

            if (current_idx < 0 || current_idx >= CURRENT_COUNT) {
                continue;  // Skip unknown sensor types
            }

            // Calculate RMS in raw ADC units
            double mean_square_adc = static_cast<double>(m_sum_squares[ch]) / PowerMeterConfig::RMS_SAMPLES_PER_CHANNEL;
            double rms_adc = sqrt(mean_square_adc);

            // Convert raw ADC to millivolts using calibration
            int rms_mv = rawToMillivolts(static_cast<uint16_t>(rms_adc));

            // Convert millivolts to volts and apply sensor multiplier from config
            // For SCT-013: vdc is RMS voltage output from transformer
            // Calibration: User measures load current with clamp meter, then multiplier = I_measured / vdc
            float vdc = rms_mv / 1000.0f;
            float multiplier = m_channel_configs[ch].multiplier;
            float current_amps = vdc * multiplier;
            // Apply threshold to filter noise (small signals have ADC offset)
            if (current_amps < CURRENT_MIN_THRESHOLD_A) current_amps = 0.0f;
            new_measurements.current_rms[current_idx] = current_amps;

            // Determine current phase sign
            int64_t current_balance = m_positive_sum[ch] + m_negative_sum[ch];
            if (current_balance > 0) {
                new_measurements.current_phase[current_idx] = PhaseSign::POSITIVE;
            } else if (current_balance < 0) {
                new_measurements.current_phase[current_idx] = PhaseSign::NEGATIVE;
            } else {
                new_measurements.current_phase[current_idx] = PhaseSign::BALANCED;
            }
        }
    }

    // === STEP 2: Determine Current Direction and Calculate Power ===
    // Using phase correlation counters: m_phase_same vs m_phase_diff

    constexpr float POWER_THRESHOLD_W = 5.0f;  // Minimum power to determine direction

    for (uint8_t ch = 0; ch < m_channel_count; ch++) {
        if (isCurrentSensor(m_channel_configs[ch].type)) {
            // Determine array index based on sensor type (same logic as STEP 1)
            int current_idx = -1;
            if (m_channel_configs[ch].type == SensorType::CURRENT_GRID) {
                current_idx = CURRENT_GRID;
            } else if (m_channel_configs[ch].type == SensorType::CURRENT_SOLAR) {
                current_idx = CURRENT_SOLAR;
            } else if (m_channel_configs[ch].type >= SensorType::CURRENT_LOAD_1 &&
                       m_channel_configs[ch].type <= SensorType::CURRENT_LOAD_8) {
                current_idx = CURRENT_LOAD;
            }

            if (current_idx < 0 || current_idx >= CURRENT_COUNT) {
                continue;  // Skip unknown sensor types
            }

            float voltage_rms = new_measurements.voltage_rms;
            float current_rms = new_measurements.current_rms[current_idx];

            // Simple power calculation (assumes unity power factor for direction)
            float apparent_power = voltage_rms * current_rms;

            // Determine direction based on phase correlation
            // m_phase_same[ch]: count of samples where V and I have same sign (in phase)
            // m_phase_diff[ch]: count of samples where V and I have different sign (out of phase)
            uint32_t same_count = m_phase_same[ch];
            uint32_t diff_count = m_phase_diff[ch];
            uint32_t total_count = same_count + diff_count;

            if (total_count == 0 || current_rms < 0.1f) {
                // No data or negligible current
                new_measurements.direction[current_idx] = CurrentDirection::ZERO;
                new_measurements.power_active[current_idx] = 0.0f;

            } else if (same_count > diff_count) {
                // More in-phase samples → CONSUMING (current flows into load)
                new_measurements.direction[current_idx] = CurrentDirection::CONSUMING;
                new_measurements.power_active[current_idx] = apparent_power;

            } else if (diff_count > same_count) {
                // More out-of-phase samples → SUPPLYING (current flows to grid)
                new_measurements.direction[current_idx] = CurrentDirection::SUPPLYING;
                new_measurements.power_active[current_idx] = -apparent_power;

            } else {
                // Equal - undefined direction
                new_measurements.direction[current_idx] = CurrentDirection::ZERO;
                new_measurements.power_active[current_idx] = 0.0f;
            }

            // Apply power threshold
            if (fabs(new_measurements.power_active[current_idx]) < POWER_THRESHOLD_W) {
                new_measurements.direction[current_idx] = CurrentDirection::ZERO;
                new_measurements.power_active[current_idx] = 0.0f;
            }
        }
    }

    // Update measurements (thread-safe)
    if (m_measurements_mutex && xSemaphoreTake(m_measurements_mutex, pdMS_TO_TICKS(10))) {
        m_measurements = new_measurements;
        xSemaphoreGive(m_measurements_mutex);
    }

    // Log periodically with phase correlation debug info (BEFORE reset!)
    // Find current channel indices for debug output
    uint8_t grid_ch = 0;
    for (uint8_t ch = 0; ch < m_channel_count; ch++) {
        if (m_channel_configs[ch].type == SensorType::CURRENT_GRID) {
            grid_ch = ch;
            break;
        }
    }

    m_rms_update_count++;
    /*
        if (m_rms_update_count % 100 == 0) {  // Every 20 seconds
            ESP_LOGI(TAG, "RMS #%lu: V=%.1fV, Grid: %.1fA %s (same=%lu, diff=%lu)",
                    m_rms_update_count,
                    new_measurements.voltage_rms,
                    new_measurements.current_rms[CURRENT_GRID],
                    (new_measurements.direction[CURRENT_GRID] == CurrentDirection::CONSUMING) ? "CONSUMING" :
                    (new_measurements.direction[CURRENT_GRID] == CurrentDirection::SUPPLYING) ? "SUPPLYING" : "ZERO",
                    static_cast<uint32_t>(m_phase_same[grid_ch]),
                    static_cast<uint32_t>(m_phase_diff[grid_ch]));
        }
    */
    // Call debug logging (if enabled via console command)
    logDebugInfo(new_measurements);

    // Reset accumulators
    memset(m_sum_squares, 0, sizeof(m_sum_squares));
    memset(m_positive_sum, 0, sizeof(m_positive_sum));
    memset(m_negative_sum, 0, sizeof(m_negative_sum));
    memset(m_dc_sum, 0, sizeof(m_dc_sum));
    memset(m_sample_count, 0, sizeof(m_sample_count));
    memset(m_phase_same, 0, sizeof(m_phase_same));
    memset(m_phase_diff, 0, sizeof(m_phase_diff));
    m_voltage_ac = 0;
    m_frame_count = 0;

    // === INVOKE CALLBACK - MAIN SYSTEM DRIVER ===
    if (m_results_callback) {
        m_results_callback(new_measurements, m_callback_user_data);
    }
}

// ============================================================
// Debug Control
// ============================================================

void PowerMeterADC::setDebugPeriod(uint32_t period_sec) {
    m_debug_period_sec = period_sec;

    if (period_sec == 0) {
        ESP_LOGI(TAG, "Debug logging DISABLED");
    } else {
        ESP_LOGI(TAG, "Debug logging ENABLED: period=%lu seconds", period_sec);
    }
}
