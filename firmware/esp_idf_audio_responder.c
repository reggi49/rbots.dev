/**
 * @file esp_idf_audio_responder.c
 * @brief ESP-IDF v5.x Modern I2S STD Responder for ESP32-S3 + INMP441
 * @details Target: 16kHz, 16-bit Mono PCM, compatible with audio_audit.py
 *          Includes:
 *            1. 100ms Pre-Roll Discard (Muting Awal)
 *            2. 1st-Order IIR High-Pass Filter (@ 90 Hz)
 * 
 * Hardware Pinout (ESP32-S3):
 *   - GPIO 4 : I2S SCK / BCLK (Bit-Clock)
 *   - GPIO 1 : I2S WS  / LRCL (Word-Select)
 *   - GPIO 2 : I2S SD  / DOUT (Serial Data In from mic)
 *   - VDD    : 3.3V
 *   - GND    : GND
 *   - L/R    : GND (Left channel)
 */

#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include "driver/i2s_std.h"
#include "driver/uart.h"

#define TAG "AUDIO_AUDIT_ESP32"

#define PIN_I2S_BCLK       GPIO_NUM_4
#define PIN_I2S_WS         GPIO_NUM_1
#define PIN_I2S_DIN        GPIO_NUM_2

#define AUDIO_SAMPLE_RATE  16000
#define RECORD_DURATION_S  5
#define TOTAL_SAMPLES      (AUDIO_SAMPLE_RATE * RECORD_DURATION_S)
#define CHUNK_SAMPLES      256
#define PREROLL_SAMPLES    1600 // 100ms settling time

// 1st-Order IIR High-Pass Filter Coeff (fc ≈ 90 Hz @ fs = 16,000 Hz)
static const float HPF_ALPHA = 0.96586f;

static i2s_chan_handle_t rx_handle = NULL;

static void init_i2s_microphone(void) {
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 4;
    chan_cfg.dma_frame_num = CHUNK_SAMPLES;
    ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, NULL, &rx_handle));

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(AUDIO_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_I2S_BCLK,
            .ws   = PIN_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din  = PIN_I2S_DIN,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    ESP_ERROR_CHECK(i2s_channel_init_std_mode(rx_handle, &std_cfg));
    ESP_ERROR_CHECK(i2s_channel_enable(rx_handle));
    ESP_LOGI(TAG, "I2S RX Channel initialized on GPIO4(BCLK), GPIO1(WS), GPIO2(DIN)");
}

static void handle_audio_stream(void) {
    ESP_LOGI(TAG, "Trigger accepted. Preparing 5s audio stream with DSP...");
    vTaskDelay(pdMS_TO_TICKS(50));

    int32_t raw_32[CHUNK_SAMPLES];
    int16_t pcm_16[CHUNK_SAMPLES];

    // 1. Muting Awal (Pre-Roll Discard): buang 100 ms transien awal MEMS
    size_t preroll_discarded = 0;
    while (preroll_discarded < PREROLL_SAMPLES) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, raw_32, sizeof(raw_32), &bytes_read, pdMS_TO_TICKS(100));
        if (ret == ESP_OK && bytes_read > 0) {
            preroll_discarded += (bytes_read / sizeof(int32_t));
        }
    }

    // 2. Reset filter state
    float prev_x = 0.0f;
    float prev_y = 0.0f;

    // 3. Kirim frame marker START_RECORD
    printf("START_RECORD\n");
    fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(10));

    size_t total_samples_sent = 0;

    while (total_samples_sent < TOTAL_SAMPLES) {
        size_t bytes_read = 0;
        esp_err_t ret = i2s_channel_read(rx_handle, raw_32, sizeof(raw_32), &bytes_read, pdMS_TO_TICKS(500));
        if (ret == ESP_OK && bytes_read > 0) {
            size_t samples = bytes_read / sizeof(int32_t);

            for (size_t i = 0; i < samples; i++) {
                // Ekstraksi 24-bit MSB dalam container 32-bit ke 16-bit PCM
                int32_t sample_shifted = raw_32[i] >> 14;

                // 1st-Order IIR HPF @ 90Hz
                float x = (float)sample_shifted;
                float y = HPF_ALPHA * (prev_y + x - prev_x);
                prev_x = x;
                prev_y = y;

                // Hard limiter
                if (y > 32767.0f) y = 32767.0f;
                else if (y < -32768.0f) y = -32768.0f;

                pcm_16[i] = (int16_t)y;
            }

            // Tulis binary payload langsung ke stdout (USB CDC / UART)
            fwrite(pcm_16, sizeof(int16_t), samples, stdout);
            fflush(stdout);

            total_samples_sent += samples;
        }
    }

    // 4. Kirim frame marker END_RECORD
    printf("\nEND_RECORD\n");
    fflush(stdout);
    ESP_LOGI(TAG, "Audio stream complete (%zu samples sent).", total_samples_sent);
}

void app_main(void) {
    init_i2s_microphone();

    printf("\n==========================================\n");
    printf(" ESP32-S3 Audio Audit Responder (ESP-IDF)\n");
    printf(" DSP: 100ms Pre-Roll Discard + 90Hz HPF\n");
    printf(" Listening for 'CMD_RECORD' on serial...\n");
    printf("==========================================\n");

    char line_buf[64];
    while (1) {
        if (fgets(line_buf, sizeof(line_buf), stdin) != NULL) {
            if (strstr(line_buf, "CMD_RECORD") != NULL || strstr(line_buf, "AUDIT_START") != NULL) {
                printf("HANDSHAKE_OK\n");
                fflush(stdout);
                handle_audio_stream();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}
