#include "board_audio.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include "esp_log.h"
#include "driver/i2s_std.h"
#include "esp_timer.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"
#include "model_path.h"
#include "sdkconfig.h"

#define TAG "BD_AUDIO"

#if CONFIG_IDF_TARGET_ESP32C3
#define MIC_I2S_PORT I2S_NUM_0
#else
#define MIC_I2S_PORT I2S_NUM_1
#endif
#define MIC_I2S_WS   1
#define MIC_I2S_SCK  4
#define MIC_I2S_SD   3

#define MIC_SAMPLE_RATE 16000
#define MIC_SAMPLE_ALIGN_SHIFT 12
#define MAX_CHUNK_SAMPLES 512

struct board_audio_s {
    i2s_chan_handle_t rx_handle;
    bool mic_ready;
    
    // WakeNet
    const esp_wn_iface_t *wn_iface;
    model_iface_data_t *wn_model_data;
    srmodel_list_t *srmodels;
    
    int16_t detect_buf[MAX_CHUNK_SAMPLES]; // Persistent buffer
    int32_t raw_buffer[MAX_CHUNK_SAMPLES * 2]; // Stereo raw
};

static struct board_audio_s g_audio_instance;

static inline int16_t mic_sample_to_int16(int32_t aligned_sample)
{
    int32_t scaled = aligned_sample >> MIC_SAMPLE_ALIGN_SHIFT;
    if (scaled > INT16_MAX) scaled = INT16_MAX;
    else if (scaled < INT16_MIN) scaled = INT16_MIN;
    return (int16_t)scaled;
}

board_audio_t* board_audio_init(void)
{
    board_audio_t *audio = &g_audio_instance;
    memset(audio, 0, sizeof(struct board_audio_s));
    
    // WakeNet Init
    ESP_LOGI(TAG, "Initializing WakeNet...");
    audio->srmodels = esp_srmodel_init("model"); // Partition label
    const char *wn_name = "wn9s_hiesp";
    if (audio->srmodels) {
        if (esp_srmodel_exists(audio->srmodels, (char *)wn_name) < 0) {
            wn_name = esp_srmodel_filter(audio->srmodels, ESP_WN_PREFIX, NULL);
        }
    } else {
        ESP_LOGE(TAG, "WakeNet model partition not found");
        return NULL; // Critical fail
    }
    
    if (wn_name) {
        audio->wn_iface = esp_wn_handle_from_name(wn_name);
    }
    
    if (audio->wn_iface) {
        audio->wn_model_data = audio->wn_iface->create(wn_name, DET_MODE_90);
        if (audio->wn_model_data) {
             ESP_LOGI(TAG, "WakeNet READY. Chunk size: %d", audio->wn_iface->get_samp_chunksize(audio->wn_model_data));
        } else {
            ESP_LOGE(TAG, "WakeNet create failed");
        }
    }
    
    return audio;
}

esp_err_t board_audio_mic_start(board_audio_t *audio)
{
    if (!audio) return ESP_ERR_INVALID_ARG;
    if (audio->mic_ready && audio->rx_handle) return ESP_OK;

    // Rate limit? handled in caller or basic checks
    // Keep it simple here
    
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 6;
    chan_cfg.dma_frame_num = 256;
    chan_cfg.auto_clear = true;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &audio->rx_handle);
    if (err != ESP_OK) return err;

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(MIC_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = MIC_I2S_SCK,
            .ws = MIC_I2S_WS,
            .dout = I2S_GPIO_UNUSED,
            .din = MIC_I2S_SD,
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv = false,
            },
        },
    };

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
    if (!audio || !audio->wn_iface || !audio->wn_model_data) return res;

    if (board_audio_mic_start(audio) != ESP_OK) return res;

    int chunk_samples = audio->wn_iface->get_samp_chunksize(audio->wn_model_data);
    if (chunk_samples > MAX_CHUNK_SAMPLES) return res;

    const uint32_t chunk_wait_ticks = pdMS_TO_TICKS(50); // Timeout

    size_t need_int32 = chunk_samples * 2;
    size_t bytes_read = 0;
    
    // Read directly into persistent buffer
    esp_err_t err = i2s_channel_read(audio->rx_handle, audio->raw_buffer,
                                     need_int32 * sizeof(int32_t), &bytes_read, chunk_wait_ticks);
    
    if (err != ESP_OK || bytes_read != need_int32 * sizeof(int32_t)) return res;

    uint32_t peak = 0;
    uint64_t sum_sq = 0;

    for (size_t sample_idx = 0, out_idx = 0; out_idx < chunk_samples; ++out_idx, sample_idx += 2) {
        int16_t sample = mic_sample_to_int16(audio->raw_buffer[sample_idx]);
        audio->detect_buf[out_idx] = sample;
        
        uint32_t abs_sample = (sample < 0) ? -sample : sample;
        if (abs_sample > peak) peak = abs_sample;
        sum_sq += (uint64_t)abs_sample * abs_sample;
    }

    res.peak = peak;
    res.rms = (chunk_samples > 0) ? (uint32_t)sqrt((double)sum_sq / chunk_samples) : 0;
    res.chunk_ms = (chunk_samples * 1000) / MIC_SAMPLE_RATE;

    float detection_score = (float)peak / 32768.0f;
    if (detection_score > 1.0f) detection_score = 1.0f;
    // Note: this is "volume score", not wakenet score.
    // WakeNet score comes from detect() result logic inside wakenet libs usually?
    // Actually esp_wn_iface->detect returns integer state.
    // The "score" in original code `detection_score` was actually normalized peak volume! 
    // Wait, original code `float detection_score = (float)peak / 32768.0f;` Yes.
    
    res.score = detection_score; 

    wakenet_state_t wn_res = audio->wn_iface->detect(audio->wn_model_data, audio->detect_buf);
    
    if (wn_res == WAKENET_DETECTED) {
        res.detected = true;
    }

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
        for (size_t i = 0; i < int32_count; i += 2) {
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
