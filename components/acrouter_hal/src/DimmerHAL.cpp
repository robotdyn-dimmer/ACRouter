/**
 * @file DimmerHAL.cpp
 * @brief Implementation of AC Dimmer Hardware Abstraction Layer
 *
 * @author ACRouter Project
 * @date 2024
 */

#include "DimmerHAL.h"
#include "esp_log.h"

static const char* TAG = "DimmerHAL";

// Singleton instance access
DimmerHAL& DimmerHAL::getInstance() {
    static DimmerHAL instance;
    return instance;
}

// Private constructor
DimmerHAL::DimmerHAL() :
    m_initialized(false),
    m_default_curve(DimmerCurve::RMS)
{
    // Initialize channel pointers to NULL
    for (uint8_t i = 0; i < DimmerConfig::MAX_CHANNELS; i++) {
        m_channels[i] = nullptr;
        m_status[i] = DimmerStatus();
    }
}

bool DimmerHAL::begin(DimmerCurve curve) {
    if (m_initialized) {
        ESP_LOGI(TAG, "Already initialized");
        return true;
    }

    ESP_LOGI(TAG, "Initializing dimmer control system...");

    m_default_curve = curve;

    // Step 1: Initialize RBDimmer library
    rbdimmer_err_t err = rbdimmer_init();
    if (err != RBDIMMER_OK) {
        ESP_LOGE(TAG, "Failed to initialize RBDimmer library (error: %d)", err);
        return false;
    }
    ESP_LOGI(TAG, "RBDimmer library initialized");

    // Step 2: Register zero-cross detector
    ESP_LOGI(TAG, "Registering zero-cross detector on GPIO %d...", PIN_ZEROCROSS);
    err = rbdimmer_register_zero_cross(
        PIN_ZEROCROSS,
        DimmerConfig::PHASE_NUM,
        DimmerConfig::MAINS_FREQUENCY
    );
    if (err != RBDIMMER_OK) {
        ESP_LOGE(TAG, "Failed to register zero-cross detector (error: %d)", err);
        return false;
    }
    ESP_LOGI(TAG, "Zero-cross detector registered");

    // Step 3: Create dimmer channels
    for (uint8_t i = 0; i < DimmerConfig::MAX_CHANNELS; i++) {
        uint8_t gpio_pin = CHANNEL_PINS[i];

        ESP_LOGI(TAG, "Creating channel %d on GPIO %d...", i, gpio_pin);

        // Configure channel
        rbdimmer_config_t config = {
            .gpio_pin = gpio_pin,
            .phase = DimmerConfig::PHASE_NUM,
            .initial_level = DimmerConfig::DEFAULT_POWER_PERCENT,
            .curve_type = static_cast<rbdimmer_curve_t>(curve)
        };

        // Create channel
        err = rbdimmer_create_channel(&config, &m_channels[i]);
        if (err != RBDIMMER_OK) {
            ESP_LOGE(TAG, "Failed to create channel %d (error: %d)", i, err);

            // Cleanup previously created channels
            for (uint8_t j = 0; j < i; j++) {
                if (m_channels[j] != nullptr) {
                    rbdimmer_delete_channel(m_channels[j]);
                    m_channels[j] = nullptr;
                }
            }
            return false;
        }

        // Update status
        m_status[i].initialized = true;
        m_status[i].active = false;
        m_status[i].power_percent = DimmerConfig::DEFAULT_POWER_PERCENT;
        m_status[i].target_percent = DimmerConfig::DEFAULT_POWER_PERCENT;
        m_status[i].curve = curve;

        ESP_LOGI(TAG, "Channel %d created successfully", i);
    }

    // Step 4: Allow system to stabilize
    delay(DimmerConfig::INIT_STABILIZATION_MS);

    // Step 5: Check frequency detection
    uint16_t frequency = rbdimmer_get_frequency(DimmerConfig::PHASE_NUM);
    if (frequency > 0) {
        ESP_LOGI(TAG, "Detected mains frequency: %d Hz", frequency);
    } else {
        ESP_LOGI(TAG, "Frequency detection in progress...");
    }

    m_initialized = true;
    ESP_LOGI(TAG, "Initialization complete");

    return true;
}

bool DimmerHAL::setPower(DimmerChannel channel, uint8_t power_percent) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    if (!isValidChannel(channel)) {
        ESP_LOGE(TAG, "Invalid channel %d", static_cast<uint8_t>(channel));
        return false;
    }

    uint8_t ch_idx = static_cast<uint8_t>(channel);
    uint8_t clamped_power = clampPower(power_percent);

    // Set power level
    rbdimmer_err_t err = rbdimmer_set_level(m_channels[ch_idx], clamped_power);
    if (err != RBDIMMER_OK) {
        ESP_LOGE(TAG, "Failed to set power on channel %d (error: %d)", ch_idx, err);
        return false;
    }

    // Update status
    m_status[ch_idx].power_percent = clamped_power;
    m_status[ch_idx].target_percent = clamped_power;
    m_status[ch_idx].active = (clamped_power > 0);

    ESP_LOGI(TAG, "Channel %d power set to %d%%", ch_idx, clamped_power);

    return true;
}

bool DimmerHAL::setPowerSmooth(DimmerChannel channel, uint8_t power_percent, uint32_t transition_ms) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    if (!isValidChannel(channel)) {
        ESP_LOGE(TAG, "Invalid channel %d", static_cast<uint8_t>(channel));
        return false;
    }

    uint8_t ch_idx = static_cast<uint8_t>(channel);
    uint8_t clamped_power = clampPower(power_percent);

    // Clamp transition time
    if (transition_ms > DimmerConfig::MAX_TRANSITION_MS) {
        transition_ms = DimmerConfig::MAX_TRANSITION_MS;
    }

    // Set power level with transition
    rbdimmer_err_t err = rbdimmer_set_level_transition(
        m_channels[ch_idx],
        clamped_power,
        transition_ms
    );
    if (err != RBDIMMER_OK) {
        ESP_LOGE(TAG, "Failed to set smooth power on channel %d (error: %d)", ch_idx, err);
        return false;
    }

    // Update status
    m_status[ch_idx].target_percent = clamped_power;
    m_status[ch_idx].active = (clamped_power > 0) || (m_status[ch_idx].power_percent > 0);

    ESP_LOGI(TAG, "Channel %d transitioning to %d%% over %lu ms",
                  ch_idx, clamped_power, transition_ms);

    return true;
}

uint8_t DimmerHAL::getPower(DimmerChannel channel) const {
    if (!m_initialized || !isValidChannel(channel)) {
        return 0;
    }

    uint8_t ch_idx = static_cast<uint8_t>(channel);
    return rbdimmer_get_level(m_channels[ch_idx]);
}

DimmerStatus DimmerHAL::getStatus(DimmerChannel channel) const {
    if (!m_initialized || !isValidChannel(channel)) {
        return DimmerStatus();
    }

    uint8_t ch_idx = static_cast<uint8_t>(channel);

    // Update power level from library
    DimmerStatus status = m_status[ch_idx];
    status.power_percent = rbdimmer_get_level(m_channels[ch_idx]);
    status.active = rbdimmer_is_active(m_channels[ch_idx]);

    return status;
}

uint16_t DimmerHAL::getMainsFrequency() const {
    if (!m_initialized) {
        return 0;
    }
    return rbdimmer_get_frequency(DimmerConfig::PHASE_NUM);
}

bool DimmerHAL::allOff() {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    ESP_LOGI(TAG, "Turning off all channels...");

    bool all_success = true;
    for (uint8_t i = 0; i < DimmerConfig::MAX_CHANNELS; i++) {
        rbdimmer_err_t err = rbdimmer_set_level(m_channels[i], 0);
        if (err != RBDIMMER_OK) {
            ESP_LOGW(TAG, "Failed to turn off channel %d (error: %d)", i, err);
            all_success = false;
        } else {
            m_status[i].power_percent = 0;
            m_status[i].target_percent = 0;
            m_status[i].active = false;
        }
    }

    if (all_success) {
        ESP_LOGI(TAG, "All channels turned off");
    }

    return all_success;
}

bool DimmerHAL::setCurve(DimmerChannel channel, DimmerCurve curve) {
    if (!m_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return false;
    }

    if (!isValidChannel(channel)) {
        ESP_LOGE(TAG, "Invalid channel %d", static_cast<uint8_t>(channel));
        return false;
    }

    uint8_t ch_idx = static_cast<uint8_t>(channel);

    // Set curve type
    rbdimmer_err_t err = rbdimmer_set_curve(
        m_channels[ch_idx],
        static_cast<rbdimmer_curve_t>(curve)
    );
    if (err != RBDIMMER_OK) {
        ESP_LOGE(TAG, "Failed to set curve on channel %d (error: %d)", ch_idx, err);
        return false;
    }

    // Update status
    m_status[ch_idx].curve = curve;

    const char* curve_name = "Unknown";
    switch (curve) {
        case DimmerCurve::LINEAR:      curve_name = "Linear"; break;
        case DimmerCurve::RMS:         curve_name = "RMS"; break;
        case DimmerCurve::LOGARITHMIC: curve_name = "Logarithmic"; break;
    }

    ESP_LOGI(TAG, "Channel %d curve set to %s", ch_idx, curve_name);

    return true;
}

bool DimmerHAL::isValidChannel(DimmerChannel channel) const {
    return static_cast<uint8_t>(channel) < DimmerConfig::MAX_CHANNELS;
}

uint8_t DimmerHAL::clampPower(uint8_t power_percent) const {
    if (power_percent < DimmerConfig::MIN_POWER_PERCENT) {
        return DimmerConfig::MIN_POWER_PERCENT;
    }
    if (power_percent > DimmerConfig::MAX_POWER_PERCENT) {
        return DimmerConfig::MAX_POWER_PERCENT;
    }
    return power_percent;
}
