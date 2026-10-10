#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#define DEFAULT_BACKEND_VOICE_URL "http://10.93.98.240:8000/chat/voice"

// --- Distinct Timing Configurations (in milliseconds) ---
#define RBOT_SPEECH_START_TIMEOUT_INITIAL_MS   7000   // 7.0s to start speaking after wake word/PTT
#define RBOT_SPEECH_START_TIMEOUT_FOLLOWUP_MS 10000   // 10.0s follow-up window after playback
#define RBOT_MAX_RECORDING_DURATION_MS        10000   // 10.0s maximum recording duration per turn
#define RBOT_END_OF_TURN_SILENCE_MS            1500   // 1.5s continuous silence after speech
#define RBOT_FOLLOW_UP_WINDOW_MS              10000   // 10.0s follow-up window
#define RBOT_MIN_SPEECH_DURATION_MS             400   // 400ms minimum speech to filter noise
#define RBOT_PRE_ROLL_DURATION_MS               300   // 300ms pre-roll circular buffer
#define RBOT_SPEAKER_COOLDOWN_MS                500   // 500ms speaker cooldown
#define RBOT_MAX_SESSION_TURNS                   10   // Max 10 turns per session
#define RBOT_MAX_SESSION_DURATION_MS         300000   // Max 5 minutes per session (300s)
#define RBOT_BUTTON_LONG_PRESS_MS              1500   // 1.5s button hold to end session

typedef enum {
    TRIGGER_WAKE_WORD = 0,
    TRIGGER_BUTTON,
    TRIGGER_CLI
} rbot_trigger_t;

typedef enum {
    TURN_RESULT_CONTINUE = 0,       // Turn succeeded, backend requested continue
    TURN_RESULT_SESSION_END,        // Turn succeeded, but explicit exit or backend requested end
    TURN_RESULT_NO_SPEECH_TIMEOUT,  // No speech onset detected before timeout
    TURN_RESULT_ERROR               // Network, HTTP, or recording failure
} rbot_turn_result_t;

/**
 * @brief Initialize voice chat module
 */
esp_err_t voice_chat_init(void);

/**
 * @brief Perform one turn within a session:
 *        Wait for speech onset (timeout_ms) -> Record (max 10s or 1.5s silence) -> HTTP POST -> Stream playback
 * @param speech_start_timeout_ms Max ms to wait for user to start speaking
 * @param session_id Session ID string (persistent across multi-turn session)
 * @param turn_index 1-based turn index
 * @param trigger Trigger source
 * @param out_result Pointer to store turn result
 * @return ESP_OK if HTTP turn completed (even if session end), error code otherwise
 */
esp_err_t voice_chat_execute_turn(uint32_t speech_start_timeout_ms,
                                  const char *session_id,
                                  int turn_index,
                                  rbot_trigger_t trigger,
                                  rbot_turn_result_t *out_result);

/**
 * @brief Backward-compatible single-turn trigger (wraps voice_chat_execute_turn)
 */
esp_err_t voice_chat_trigger_turn(uint32_t duration_sec, rbot_trigger_t trigger);

/**
 * @brief Set custom backend URL
 */
void voice_chat_set_backend_url(const char *url);

/**
 * @brief Explicitly notify backend to close session
 */
void voice_chat_close_backend_session(const char *session_id);

/**
 * @brief Check or signal user interruption (e.g. button press during follow-up wait)
 */
void voice_chat_signal_button_pressed(void);
bool voice_chat_consume_button_pressed(void);

/**
 * @brief Probe backend /health endpoint to update server connection icon
 */
esp_err_t voice_chat_check_server_health(void);
