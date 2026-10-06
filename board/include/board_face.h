#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "board_types.h"
#include "board_display.h"

#ifdef __cplusplus
extern "C" {
#endif

// Error sub-types for display
typedef enum {
    FACE_ERR_NONE = 0,
    FACE_ERR_WIFI,
    FACE_ERR_SERVER,
    FACE_ERR_STT,
    FACE_ERR_LLM,
    FACE_ERR_TTS,
    FACE_ERR_AUDIO,
    FACE_ERR_GENERIC
} face_error_type_t;

/**
 * @brief Initialize the face animation engine and display task
 * @param disp Pointer to initialized board_display_t
 * @return ESP_OK on success
 */
esp_err_t board_face_init(board_display_t *disp);

/**
 * @brief Set current face expression state
 * @param state FACE_BOOT, FACE_IDLE, FACE_LISTENING, FACE_THINKING, FACE_SPEAKING, FACE_HAPPY, FACE_ERROR, FACE_DISCONNECTED
 */
void board_face_set_state(face_state_t state);

/**
 * @brief Get current face expression state
 */
face_state_t board_face_get_state(void);

/**
 * @brief Set error expression with category
 */
void board_face_set_error(face_error_type_t err_type, const char *short_msg);

/**
 * @brief Trigger happy expression for specified duration (ms), then return to IDLE
 */
void board_face_trigger_happy(uint32_t duration_ms);

/**
 * @brief Update bottom status text manually
 */
void board_face_set_status_text(const char *text);

/**
 * @brief Update live audio level for speaking mouth modulation (0.0 to 1.0)
 */
void board_face_set_audio_level(float level);

/**
 * @brief Update WiFi connection status for top-bar indicator
 */
void board_face_set_wifi_status(wifi_status_t status);

/**
 * @brief Update Battery level for top-bar indicator
 */
void board_face_set_battery_state(battery_state_t bat);

/**
 * @brief Trigger the standalone 12-state expression demonstration mode
 */
void board_face_trigger_demo(void);

/**
 * @brief Check if demo mode is currently running
 */
bool board_face_is_demo_active(void);

/**
 * @brief Query live rendering metrics
 * @param out_fps Pointer to store calculated FPS
 * @param out_avg_render_ms Pointer to store average frame render time in ms
 * @param out_max_render_ms Pointer to store maximum frame render time in ms
 */
void board_face_get_metrics(float *out_fps, float *out_avg_render_ms, float *out_max_render_ms);

#ifdef __cplusplus
}
#endif
