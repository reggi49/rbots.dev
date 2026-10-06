#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define DEFAULT_BACKEND_VOICE_URL "http://10.93.98.240:8000/chat/voice"

typedef enum {
    TRIGGER_WAKE_WORD = 0,
    TRIGGER_BUTTON,
    TRIGGER_CLI
} rbot_trigger_t;

/**
 * @brief Initialize voice chat module
 */
esp_err_t voice_chat_init(void);

/**
 * @brief Perform one complete conversational turn:
 *        Record Mic (duration_sec) -> HTTP POST -> Stream Audio Playback
 * @param duration_sec Maximum recording duration in seconds (default 5)
 * @param trigger Trigger source (WAKE_WORD, BUTTON, or CLI)
 * @return ESP_OK on success
 */
esp_err_t voice_chat_trigger_turn(uint32_t duration_sec, rbot_trigger_t trigger);

/**
 * @brief Set custom backend URL
 */
void voice_chat_set_backend_url(const char *url);
