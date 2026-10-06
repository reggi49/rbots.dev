#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define DEFAULT_BACKEND_VOICE_URL "http://10.93.98.240:8000/chat/voice"

/**
 * @brief Initialize voice chat module
 */
esp_err_t voice_chat_init(void);

/**
 * @brief Perform one complete conversational turn:
 *        Countdown -> Record Mic (duration_sec) -> HTTP POST -> Stream Audio Playback
 * @param duration_sec Recording duration in seconds (default 5)
 * @return ESP_OK on success
 */
esp_err_t voice_chat_trigger_turn(uint32_t duration_sec);

/**
 * @brief Set custom backend URL
 */
void voice_chat_set_backend_url(const char *url);
