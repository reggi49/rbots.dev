#include "board_audio.h"
#include "board_pins.h"          /* Single source of truth for GPIOs */
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "BD_AUDIO"

/* Mic uses dedicated I2S port (separate from speaker) */
#define MIC_I2S_PORT  MIC_I2S_PORT_NUM   /* from board_pins.h */

#define MIC_SAMPLE_RATE 16000
// INMP441: 24-bit data in 32-bit container [bits 31:8]
// First shift right 8 to extract 24-bit, then shift right 8 more to get 16-bit
#define MIC_SAMPLE_ALIGN_SHIFT 16  // Total shift: 8 (extract) + 8 (scale to 16-bit)
#define MAX_CHUNK_SAMPLES 512

struct board_audio_s {
    i2s_chan_handle_t rx_handle;
    bool mic_ready;
    
    int32_t raw_buffer[MAX_CHUNK_SAMPLES]; // Mono raw samples
};

static struct board_audio_s g_audio_instance;

// Convert INMP441 32-bit sample to 16-bit PCM
// INMP441 outputs 24-bit audio in bits [31:8] of 32-bit word
static inline int16_t mic_sample_to_int16(int32_t sample_32bit)
{
    // Extract 24-bit data from bits [31:8]
    int32_t sample_24bit = sample_32bit >> 8;
    // Sign-extend from 24-bit to 32-bit
    if (sample_24bit & 0x00800000) {
        sample_24bit |= 0xFF000000;
    }
    // Scale to 16-bit (divide by 256)
    int32_t scaled = sample_24bit >> 8;
    // Clamp to prevent overflow
    if (scaled > INT16_MAX) scaled = INT16_MAX;
    else if (scaled < INT16_MIN) scaled = INT16_MIN;
    return (int16_t)scaled;
}

board_audio_t* board_audio_init(void)
{
    board_audio_t *audio = &g_audio_instance;
    memset(audio, 0, sizeof(struct board_audio_s));
    
    ESP_LOGI(TAG, "Audio intialized (WakeNet Removed)");
    return audio;
}

esp_err_t board_audio_mic_start(board_audio_t *audio)
{
    if (!audio) return ESP_ERR_INVALID_ARG;
    if (audio->mic_ready && audio->rx_handle) return ESP_OK;

    // Rate limit? handled in caller or basic checks
    // Keep it simple here
    
    /* I2S Channel: RX-only (microphone input) on dedicated port */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(MIC_I2S_PORT, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;

    /* Create RX channel only (second parameter NULL = no TX) */
    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &audio->rx_handle);
    if (err != ESP_OK) return err;

    /* I2S Standard Configuration for INMP441
     *  - Philips standard, 24-bit data in 32-bit container
     *  - LEFT channel only (L/R pin → GND)
     *  - GPIOs from board_pins.h */
    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_MIC_I2S_BCK,           /* GPIO 4: Bit Clock        */
            .ws   = PIN_MIC_I2S_WS,            /* GPIO 1: Word Select      */
            .dout = I2S_GPIO_UNUSED,            /* No speaker on this port  */
            .din  = PIN_MIC_I2S_SD,            /* GPIO 2: Serial Data IN   */
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };
    
    // INMP441: LEFT channel only (L/R pin = GND selects LEFT)
    std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    err = i2s_channel_init_std_mode(audio->rx_handle, &std_cfg);
    if (err != ESP_OK) {
        i2s_del_channel(audio->rx_handle);
        audio->rx_handle = NULL;
        return err;
    }

    err = i2s_channel_enable(audio->rx_handle);
    if (err != ESP_OK) {
        i2s_del_channel(audio->rx_handle);
        audio->rx_handle = NULL;
        return err;
    }

    audio->mic_ready = true;
    return ESP_OK;
}

void board_audio_mic_stop(board_audio_t *audio)
{
    if (audio && audio->rx_handle) {
        i2s_channel_disable(audio->rx_handle);
        i2s_del_channel(audio->rx_handle);
        audio->rx_handle = NULL;
    }
    if (audio) audio->mic_ready = false;
}

board_audio_wake_result_t board_audio_process_wake_word(board_audio_t *audio)
{
    board_audio_wake_result_t res = {0};
    if (!audio) return res;

    if (board_audio_mic_start(audio) != ESP_OK) return res;

    int chunk_samples = MAX_CHUNK_SAMPLES;
    const uint32_t chunk_wait_ticks = pdMS_TO_TICKS(50); // Timeout

    size_t need_int32 = chunk_samples;
    size_t bytes_read = 0;
    
    // Read directly into persistent buffer
    esp_err_t err = i2s_channel_read(audio->rx_handle, audio->raw_buffer,
                                     need_int32 * sizeof(int32_t), &bytes_read, chunk_wait_ticks);
    
    if (err != ESP_OK || bytes_read != need_int32 * sizeof(int32_t)) return res;

    uint32_t peak = 0;
    uint64_t sum_sq = 0;

    for (size_t out_idx = 0; out_idx < chunk_samples; ++out_idx) {
        int16_t sample = mic_sample_to_int16(audio->raw_buffer[out_idx]);
        // No detect_buf needed anymore
        
        uint32_t abs_sample = (sample < 0) ? -sample : sample;
        if (abs_sample > peak) peak = abs_sample;
        sum_sq += (uint64_t)abs_sample * abs_sample;
    }

    res.peak = peak;
    res.rms = (chunk_samples > 0) ? (uint32_t)sqrt((double)sum_sq / chunk_samples) : 0;
    res.chunk_ms = (chunk_samples * 1000) / MIC_SAMPLE_RATE;

    float detection_score = (float)peak / 32768.0f;
    if (detection_score > 1.0f) detection_score = 1.0f;
    
    res.score = detection_score; 
    res.detected = false; // Legacy Wakenet removed

    return res;
}

esp_err_t board_audio_capture_window(board_audio_t *audio, int16_t *buf, size_t samples, int16_t *rms_out, int16_t *peak_out)
{
    if (!audio || !buf || samples == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = board_audio_mic_start(audio);
    if (err != ESP_OK) return err;

    size_t filled = 0;
    int32_t temp_buf[64];
    size_t bytes_read = 0;
    int16_t max_peak = 0;
    double rms_sum = 0.0;
    int samples_count = 0;
    int64_t start_us = esp_timer_get_time();

    while (filled < samples) {
        err = i2s_channel_read(audio->rx_handle, temp_buf, sizeof(temp_buf),
                               &bytes_read, pdMS_TO_TICKS(10));
        if (err != ESP_OK || bytes_read == 0) {
            if ((esp_timer_get_time() - start_us) > 2000000) break; // 2s timeout
            continue;
        }

        size_t int32_count = bytes_read / sizeof(int32_t);
        for (size_t i = 0; i < int32_count; i++) {
            if (filled >= samples) break;
            int16_t s = mic_sample_to_int16(temp_buf[i]);
            buf[filled++] = s;
            
            int32_t abs_s = s < 0 ? -s : s;
            if (abs_s > max_peak) max_peak = abs_s;
            rms_sum += (double)s * s;
            samples_count++;
        }
    }

    if (rms_out) *rms_out = (samples_count > 0) ? (int16_t)sqrt(rms_sum / samples_count) : 0;
    if (peak_out) *peak_out = max_peak;

    return (filled > 0) ? ESP_OK : ESP_FAIL;
}

esp_err_t board_audio_read(board_audio_t *audio, void *dest, size_t bytes, size_t *bytes_read, uint32_t timeout_ticks)
{
    if (!audio || !audio->rx_handle) return ESP_ERR_INVALID_STATE;
    return i2s_channel_read(audio->rx_handle, dest, bytes, bytes_read, timeout_ticks);
}
