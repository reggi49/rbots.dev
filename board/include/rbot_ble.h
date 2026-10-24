/**
 * @file rbot_ble.h
 * @brief R-bot BLE GATT Service Interface (Protocol v1).
 */

#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the R-bot BLE GATT server and start advertising.
 *
 * Implements the R-bot GATT Service (7b4a0001-9d3f-4c2e-8a61-2f0c5b7e1000):
 *  - Command Characteristic  (7b4a0002-...) [Write-with-response]
 *  - Status Characteristic   (7b4a0003-...) [Read, Notify]
 *  - Config Characteristic   (7b4a0004-...) [Read-only]
 *  - Event Characteristic    (7b4a0005-...) [Notify]
 *
 * Features in Read-Only Vertical Slice:
 *  - Protocol v1 JSON framing with version check
 *  - PING -> pong
 *  - GET_INFO -> real firmware/hardware metadata
 *  - GET_STATUS -> real status (WiFi, server, face, has_real_battery: false)
 *  - GET_CAPABILITIES -> truthful capabilities (all mutation & unsupported features: false)
 *  - Motor and mutation commands: safely rejected with error code NOT_SUPPORTED
 *  - Safe rejection of invalid JSON and unsupported versions
 *  - Disconnect callback that restarts advertising without affecting motor/audio/voice/face.
 */
esp_err_t rbot_ble_init(void);

/**
 * @brief Check if a BLE peer is currently connected.
 */
bool rbot_ble_is_connected(void);

/**
 * @brief Send a notification on the Status characteristic.
 * @param json_str JSON payload string
 */
void rbot_ble_notify_status(const char *json_str);

/**
 * @brief Send an asynchronous event notification on the Event characteristic.
 * @param json_str JSON payload string
 */
void rbot_ble_notify_event(const char *json_str);

#ifdef __cplusplus
}
#endif

