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

// Five Eye Shape Families
typedef enum {
    EYE_SHAPE_ROUNDED = 0, // Rounded rectangle with corner radius
    EYE_SHAPE_OVAL,        // Ellipse / smooth oval
    EYE_SHAPE_SQUINT,      // Slit / narrow squint
    EYE_SHAPE_ARC,         // Upward curving happy crescent
    EYE_SHAPE_HALF_LID     // Rounded rect or oval masked by top or bottom eyelid
} eye_shape_family_t;

// Independent Eye Parameter Model
typedef struct {
    float center_x;
    float center_y;
    float width;
    float height;
    float corner_radius;
    float tilt_deg;       // -15.0 to +15.0 degrees (slanted brows / expressions)
    float openness;       // 0.0 (closed) to 1.0 (fully open)
    float top_lid;        // 0.0 (retracted) to 1.0 (covers top)
    float bottom_lid;     // 0.0 (retracted) to 1.0 (covers bottom)
    float gaze_x;         // Local gaze horizontal offset (-6.0 to +6.0)
    float gaze_y;         // Local gaze vertical offset (-5.0 to +5.0)
    float squash;         // Aspect ratio horizontal modifier
    float stretch;        // Vertical stretch
    float curvature;      // Arc or bottom curve factor
    float highlight_int;  // Specular gleam intensity (0.0 to 1.0)
    uint16_t color;       // RGB565 big-endian color
    eye_shape_family_t shape;
} eye_params_t;

// Blinking Policy
typedef enum {
    BLINK_POLICY_NORMAL = 0,
    BLINK_POLICY_CURIOUS,  // Small eye blinks first, large eye follows
    BLINK_POLICY_SLEEPY,   // Slow close, long hold, slow open
    BLINK_POLICY_NONE      // In ARC / wink / special poses, suppress generic blink
} blink_policy_t;

// Mouth Policy
typedef enum {
    MOUTH_POLICY_NONE = 0,
    MOUTH_POLICY_SPEAKING, // Dynamic PCM soundwave or time-based oscillation
    MOUTH_POLICY_SMILE,    // Sweet little smile curve
    MOUTH_POLICY_SAD,      // Soft downward curve
    MOUTH_POLICY_SURPRISE  // Small open 'O'
} mouth_policy_t;

// Full Expression Pose
typedef struct {
    eye_params_t left_eye;
    eye_params_t right_eye;
    float global_gaze_x;
    float global_gaze_y;
    float mouth_openness;
    blink_policy_t blink_policy;
    mouth_policy_t mouth_policy;
    char status_label[24];
} face_pose_t;

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
 * @brief Trigger happy big (crescent arcs) expression for specified duration (ms)
 */
void board_face_trigger_happy_big(uint32_t duration_ms);

/**
 * @brief Trigger curious pose (with controlled asymmetry)
 * @param left_larger If true, left eye is enlarged; if false, right eye is enlarged
 * @param duration_ms Duration to hold curious pose
 */
void board_face_trigger_curious(bool left_larger, uint32_t duration_ms);

/**
 * @brief Trigger confused / skeptical expression
 */
void board_face_trigger_confused(uint32_t duration_ms);

/**
 * @brief Trigger excited expression with squash/stretch
 */
void board_face_trigger_excited(uint32_t duration_ms);

/**
 * @brief Trigger surprised expression with tall ovals
 */
void board_face_trigger_surprised(uint32_t duration_ms);

/**
 * @brief Trigger playful wink expression
 * @param left_eye If true, wink left eye; if false, wink right eye
 */
void board_face_trigger_wink(bool left_eye, uint32_t duration_ms);

/**
 * @brief Trigger sleepy expression
 */
void board_face_trigger_sleepy(void);

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
 * @brief Trigger the standalone 27-step expression demonstration mode
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
