/**
 * @file selftest.h
 * @brief Self-test helpers for verifying speaker, mic, TFT, and touch hardware.
 *
 * Each test is a standalone function that can be called from app_main()
 * or from a dedicated "selftest" build target.
 */

#pragma once

#include "esp_err.h"

/**
 * @brief Play a 1 kHz sine tone on the speaker for ~2 seconds.
 *        Verifies MAX98357A I2S TX path.
 */
esp_err_t selftest_speaker_tone(void);

/**
 * @brief Capture ~1 second of mic audio and log RMS / peak levels.
 *        Verifies INMP441 I2S RX path.
 */
esp_err_t selftest_mic_rms(void);

/**
 * @brief Draw a "READY" splash screen on the ST7735 TFT.
 *        Verifies SPI display path.
 */
esp_err_t selftest_tft_ready(void);

/**
 * @brief Poll the touch sensor for 10 seconds and log press events.
 *        Verifies GPIO touch input.
 */
esp_err_t selftest_touch_log(void);

/**
 * @brief Run ALL self-tests in sequence.
 */
esp_err_t selftest_run_all(void);
