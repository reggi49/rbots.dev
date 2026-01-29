#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "board_types.h"

// Opaque Handle
typedef struct board_network_s board_network_t;

/**
 * @brief Initialize the network module (Wi-Fi, SNTP)
 * @return Handle or NULL
 */
board_network_t* board_network_init(void);

// [1.1.38]
void board_network_connect(board_network_t *net);

/**
 * @brief Check if network is connected and stable
 */
// Lifecycle
void board_network_tick(board_network_t *net);

bool board_network_is_stable(board_network_t *net);

/**
 * @brief Check if network is in runtime phase (not boot)
 */
bool board_network_in_runtime_phase(board_network_t *net);

/**
 * @brief Get current Wi-Fi status
 */
wifi_status_t board_network_get_status(board_network_t *net);

/**
 * @brief Check if network operations are allowed (Gatekeeper)
 */
bool board_network_is_connected(board_network_t *net);

/**
 * @brief Get current Network Logic State
 */
net_state_t board_network_get_net_state(board_network_t *net);

/**
 * @brief Manually set network logic state (e.g. from checker)
 */
void board_network_set_net_state(board_network_t *net, net_state_t state);

/**
 * @brief Wait for SNTP time sync
 */
bool board_network_wait_for_time_sync(board_network_t *net, uint32_t timeout_ms);

/**
 * @brief Get internal IP string (debug)
 */
const char* board_network_get_ip_str(board_network_t *net);
