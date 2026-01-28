#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "board_types.h"

// Opaque handle
typedef struct board_power_s board_power_t;

/**
 * @brief Initialize power management
 */
board_power_t* board_power_init(void);

/**
 * @brief Get current battery voltage
 */
float board_power_get_voltage(board_power_t *power);

/**
 * @brief Get current battery state (charging, full, low, etc)
 */
battery_state_t board_power_get_state(board_power_t *power);

/**
 * @brief Check if charging
 */
bool board_power_is_charging(board_power_t *power);
