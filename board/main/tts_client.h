#pragma once

#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

typedef enum {
	TTS_FAIL_NONE,
	TTS_FAIL_HTTP,
	TTS_FAIL_WAV,
	TTS_FAIL_I2S
} tts_fail_stage_t;

typedef struct {
	int status_code;
	size_t bytes;         // total bytes received
	size_t data_bytes;    // bytes sent to I2S (if known)
	tts_fail_stage_t fail_stage;
	int fail_errno;
	char fail_msg[64];
} tts_stream_result_t;

esp_err_t tts_client_stream_play(const char *text, tts_stream_result_t *result);
esp_err_t tts_client_speak_async(const char *text);
bool tts_client_is_busy(void);
