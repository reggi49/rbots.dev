#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "board_status.h"
#include "tts_client.h"

typedef enum {
    CLOUD_CMD_NONE,
    CLOUD_CMD_SET_MOOD,
    CLOUD_CMD_SHOW_ICON,
    CLOUD_CMD_RESET
} cloud_command_type_t;

typedef struct {
    cloud_command_type_t type;
    char value[16];
} cloud_command_t;

typedef enum {
    CHAT_FAIL_NONE,
    CHAT_FAIL_DNS,
    CHAT_FAIL_TCP,
    CHAT_FAIL_TLS,
    CHAT_FAIL_HTTP,
    CHAT_FAIL_JSON
} chat_fail_stage_t;

typedef struct {
    int status_code;
    int64_t bytes;
    chat_fail_stage_t fail_stage;
    int fail_errno;
    char fail_msg[64];
} chat_request_result_t;

void cloud_client_init(void);
bool cloud_client_send_heartbeat(cloud_command_t *out);
esp_err_t cloud_client_chat_gradient(const char *prompt, char *answer_out, size_t max_len, chat_request_result_t *result);
esp_err_t cloud_client_tts_azure(const char *text);
esp_err_t cloud_client_tts_azure_stream_play(const char *text, tts_stream_result_t *result);

bool net_http_lock_take(uint32_t timeout_ms);
void net_http_lock_give(void);
