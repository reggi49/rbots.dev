#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "board_types.h"

// Opaque handle
typedef struct board_display_s board_display_t;

/**
 * @brief Initialize the display module
 * @return Handle to display object or NULL on failure
 */
board_display_t* board_display_init(void);

/**
 * @brief Clear screen to black
 */
void board_display_clear(board_display_t *display);

/**
 * @brief Fill the screen with a solid RGB565 color
 */
void board_display_fill_color(board_display_t *display, uint16_t rgb565);

/**
 * @brief Draw the robot face
 */
void board_display_draw_face(board_display_t *display, face_state_t face, blink_state_t blink, int8_t off_x, int8_t off_y, int8_t off_size);

/**
 * @brief Draw the AI Overlay (Net badge, Battery, AI State)
 */
void board_display_draw_overlay(board_display_t *display, ai_state_t ai_state, const char *ai_text, 
                                wifi_status_t wifi_status, net_state_t net_state, battery_state_t bat_state, 
                                bool anim_phase, float mic_level, bool wake_accepted,
                                int64_t wake_block_until_ms, const char *wake_block_msg, int64_t ai_wake_until_ms);

/**
 * @brief Draw chat answer text
 */
void board_display_draw_chat(board_display_t *display, const char *text);

/**
 * @brief Get touch button state
 */
int board_display_get_touch_level(board_display_t *display);

