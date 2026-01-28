#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

// Opaque Handle
typedef struct board_audio_s board_audio_t;

// Wake Word Result
typedef struct {
    bool detected;
    float score;
    int rms;
    int peak;
    uint32_t chunk_ms;
} board_audio_wake_result_t;

/**
 * @brief Initialize Audio (Mic + WakeNet)
 */
board_audio_t* board_audio_init(void);

/**
 * @brief Initialize/Ensure Mic is running
 */
esp_err_t board_audio_mic_start(board_audio_t *audio);

/**
 * @brief Stop Mic/I2S
 */
void board_audio_mic_stop(board_audio_t *audio);

/**
 * @brief Process one chunk of audio for wake word detection
 * @return Result of detection
 */
board_audio_wake_result_t board_audio_process_wake_word(board_audio_t *audio);

/**
 * @brief Capture a window of audio (for level/calibration)
 */
esp_err_t board_audio_capture_window(board_audio_t *audio, int16_t *buf, size_t samples, int16_t *rms_out, int16_t *peak_out);

/**
 * @brief Read raw samples from Mic (blocking)
 */
esp_err_t board_audio_read(board_audio_t *audio, void *dest, size_t bytes, size_t *bytes_read, uint32_t timeout_ticks);
