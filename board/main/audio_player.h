#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t audio_player_init(void);
esp_err_t audio_player_start(uint32_t sample_rate_hz);
void audio_player_stop(void);
void audio_player_flush(void);
bool audio_player_submit_pcm(const uint8_t *data, size_t len);
bool audio_player_is_playing(void);
