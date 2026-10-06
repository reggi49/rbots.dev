#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    WAKE_STATE_UNINITIALIZED = 0,
    WAKE_STATE_LISTENING,
    WAKE_STATE_SUSPENDED,
    WAKE_STATE_DETECTED
} wake_word_state_t;

typedef void (*wake_word_detected_cb_t)(const char *word_name, int word_index);

/**
 * @brief Initialize ESP-SR WakeNet engine with model discovery & fallback hierarchy:
 *        1. wn10_en_heybuddy (WakeNet10, primary - checked & reported)
 *        2. wn10_hiesp (WakeNet10, fallback 1 - checked & reported)
 *        3. wn9_hiesp (WakeNet9, fallback 2 - active & operational)
 * @param callback Function to call immediately when wake word is detected
 * @return ESP_OK on success
 */
esp_err_t wake_word_sr_init(wake_word_detected_cb_t callback);

/**
 * @brief Start wake word detection task
 */
esp_err_t wake_word_sr_start(void);

/**
 * @brief Suspend wake word listening (during recording, HTTP upload, and audio playback)
 */
void wake_word_sr_suspend(void);

/**
 * @brief Resume wake word listening (after audio playback cooldown)
 */
void wake_word_sr_resume(void);

/**
 * @brief Check if wake word engine is actively listening
 */
bool wake_word_sr_is_listening(void);

/**
 * @brief Get currently active model name and wake word string
 */
const char *wake_word_sr_get_model_name(void);
const char *wake_word_sr_get_word_name(void);

#ifdef __cplusplus
}
#endif
