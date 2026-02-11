/**
 * @file selftest.c
 * @brief Self-test implementations for speaker tone, mic RMS, TFT splash,
 *        and touch press logging.
 */

#include "selftest.h"
#include "board_pins.h"
#include "board_audio.h"
#include "board_display.h"
#include "audio_player.h"

#include "driver/gpio.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

#define TAG_ST "SELFTEST"

/* ──────────────────────────────────────────────────────── */
/*  1. Speaker tone test (1 kHz sine, ~2 s)                */
/* ──────────────────────────────────────────────────────── */

esp_err_t selftest_speaker_tone(void)
{
    ESP_LOGI(TAG_ST, "=== Speaker Tone Test (1 kHz, 2 s) ===");

    esp_err_t err = audio_player_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ST, "audio_player_init failed: 0x%x", err);
        return err;
    }

    const uint32_t sample_rate = 16000;
    err = audio_player_start(sample_rate);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ST, "audio_player_start failed: 0x%x", err);
        return err;
    }

    /* Generate 2 seconds of 1 kHz sine at ~50 % amplitude */
    const float freq = 1000.0f;
    const float amplitude = 16000.0f;          /* ~50 % of INT16_MAX   */
    const uint32_t total_samples = sample_rate * 2;
    const size_t chunk = 256;
    int16_t buf[256];

    for (uint32_t i = 0; i < total_samples; i += chunk) {
        uint32_t n = (total_samples - i < chunk) ? (total_samples - i) : chunk;
        for (uint32_t j = 0; j < n; j++) {
            float t = (float)(i + j) / (float)sample_rate;
            buf[j] = (int16_t)(amplitude * sinf(2.0f * M_PI * freq * t));
        }
        audio_player_submit_pcm((const uint8_t *)buf, n * sizeof(int16_t));
    }

    /* Wait for ring-buffer to drain */
    audio_player_wait_empty(4000);
    audio_player_stop();

    ESP_LOGI(TAG_ST, "Speaker tone test DONE");
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────── */
/*  2. Mic RMS / peak level log (~1 s capture)             */
/* ──────────────────────────────────────────────────────── */

esp_err_t selftest_mic_rms(void)
{
    ESP_LOGI(TAG_ST, "=== Mic Level Test (1 s) ===");

    board_audio_t *audio = board_audio_init();
    if (!audio) {
        ESP_LOGE(TAG_ST, "board_audio_init failed");
        return ESP_FAIL;
    }

    esp_err_t err = board_audio_mic_start(audio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ST, "mic_start failed: 0x%x", err);
        return err;
    }

    const uint32_t target_samples = 16000; /* 1 s @ 16 kHz */
    int32_t raw[256];
    uint32_t total = 0;
    int64_t sum_sq = 0;
    int32_t peak = 0;

    while (total < target_samples) {
        size_t bytes_read = 0;
        err = board_audio_read(audio, raw, sizeof(raw), &bytes_read, pdMS_TO_TICKS(500));
        if (err != ESP_OK || bytes_read == 0) break;

        size_t samples = bytes_read / sizeof(int32_t);
        for (size_t i = 0; i < samples; i++) {
            /* INMP441: 24-bit in bits [31:8] → scale to 16-bit */
            int32_t s24 = raw[i] >> 8;
            if (s24 & 0x00800000) s24 |= 0xFF000000;
            int32_t s16 = s24 >> 8;
            if (s16 > 32767)  s16 = 32767;
            if (s16 < -32768) s16 = -32768;

            sum_sq += (int64_t)s16 * s16;
            int32_t abs_s = (s16 < 0) ? -s16 : s16;
            if (abs_s > peak) peak = abs_s;
            total++;
        }
    }

    board_audio_mic_stop(audio);

    float rms = (total > 0) ? sqrtf((float)sum_sq / total) : 0.0f;
    ESP_LOGI(TAG_ST, "Mic test: samples=%u  RMS=%.0f  Peak=%d", total, rms, (int)peak);

    if (total == 0) {
        ESP_LOGE(TAG_ST, "No mic data captured — check wiring");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_ST, "Mic level test DONE");
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────── */
/*  3. TFT "READY" splash screen                          */
/* ──────────────────────────────────────────────────────── */

esp_err_t selftest_tft_ready(void)
{
    ESP_LOGI(TAG_ST, "=== TFT Ready Screen ===");

    board_display_t *disp = board_display_init();
    if (!disp) {
        ESP_LOGE(TAG_ST, "board_display_init failed");
        return ESP_FAIL;
    }

    board_display_clear(disp);

    /* Draw green "READY" in center of 128×160 screen              *
     * Using the existing face-draw API to show neutral + overlay. */
    board_display_draw_face(disp, FACE_HAPPY, BLINK_OPEN, 0, 0, 0);

    /* Overlay text at bottom */
    board_display_draw_overlay(disp, AI_IDLE, "READY", WIFI_OFF,
                               NET_UNKNOWN, BAT_FULL, false, 0.0f,
                               false, 0, "", 0);

    ESP_LOGI(TAG_ST, "TFT ready screen DONE");
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────── */
/*  4. Touch press log (10 s polling)                      */
/* ──────────────────────────────────────────────────────── */

esp_err_t selftest_touch_log(void)
{
    ESP_LOGI(TAG_ST, "=== Touch Sensor Log (10 s) ===");

    /* Configure touch pin as input with pull-up */
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << PIN_TOUCH),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io);

    int prev = -1;
    int64_t end_us = esp_timer_get_time() + 10LL * 1000000;

    while (esp_timer_get_time() < end_us) {
        int level = gpio_get_level(PIN_TOUCH);
        if (level != prev) {
            ESP_LOGI(TAG_ST, "Touch GPIO%d = %d  (%s)",
                     PIN_TOUCH, level, level == 0 ? "PRESSED" : "released");
            prev = level;
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }

    ESP_LOGI(TAG_ST, "Touch log DONE");
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────── */
/*  5. Run all self-tests                                  */
/* ──────────────────────────────────────────────────────── */

esp_err_t selftest_run_all(void)
{
    ESP_LOGI(TAG_ST, "======== SELF-TEST SUITE START ========");

    esp_err_t rc;

    rc = selftest_tft_ready();
    ESP_LOGI(TAG_ST, "TFT        : %s", rc == ESP_OK ? "PASS" : "FAIL");

    rc = selftest_mic_rms();
    ESP_LOGI(TAG_ST, "MIC        : %s", rc == ESP_OK ? "PASS" : "FAIL");

    rc = selftest_speaker_tone();
    ESP_LOGI(TAG_ST, "SPEAKER    : %s", rc == ESP_OK ? "PASS" : "FAIL");

    rc = selftest_touch_log();
    ESP_LOGI(TAG_ST, "TOUCH      : %s", rc == ESP_OK ? "PASS" : "FAIL");

    ESP_LOGI(TAG_ST, "======== SELF-TEST SUITE DONE ========");
    return ESP_OK;
}
