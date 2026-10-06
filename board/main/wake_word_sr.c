#include "wake_word_sr.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"

// ESP-SR Headers
#include "model_path.h"
#include "esp_afe_sr_models.h"
#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"
#include "esp_wn_iface.h"
#include "esp_wn_models.h"

// Board Audio
#include "board_audio.h"

static const char *TAG_WW = "WAKE_WORD_SR";

// Digital gain shift for INMP441 (24-bit in 32-bit container)
#define INMP441_DIGITAL_GAIN_SHIFT 6

static inline int16_t inmp441_to_pcm16(int32_t raw_sample)
{
    int32_t s24 = raw_sample >> 8;
    if (s24 & 0x00800000) {
        s24 |= 0xFF000000;
    }
    int32_t s16 = s24 >> INMP441_DIGITAL_GAIN_SHIFT;
    if (s16 > 32767)  s16 = 32767;
    if (s16 < -32768) s16 = -32768;
    return (int16_t)s16;
}

// Module State
static srmodel_list_t *s_models = NULL;
static esp_afe_sr_iface_t *s_afe_handle = NULL;
static esp_afe_sr_data_t *s_afe_data = NULL;
static TaskHandle_t s_ww_task_handle = NULL;
static wake_word_detected_cb_t s_detected_cb = NULL;

static volatile bool s_listening = false;
static char s_active_model_name[32] = "wn9_hiesp";
static char s_active_word_name[32] = "Hi ESP";

static void wake_word_task(void *arg);

esp_err_t wake_word_sr_init(wake_word_detected_cb_t callback)
{
    s_detected_cb = callback;
    ESP_LOGI(TAG_WW, "=== Initializing ESP-SR Local Wake Word Engine ===");

    // 1. Initialize models from Flash partition 'model'
    s_models = esp_srmodel_init("model");
    if (!s_models || s_models->num == 0) {
        ESP_LOGE(TAG_WW, "Failed to load models from 'model' partition!");
        return ESP_ERR_NOT_FOUND;
    }

    ESP_LOGI(TAG_WW, "Discovered %d models in partition 'model':", s_models->num);
    for (int i = 0; i < s_models->num; i++) {
        ESP_LOGI(TAG_WW, "  [%d] Name: %s | Info: %s",
                 i, s_models->model_name[i],
                 s_models->model_info[i] ? s_models->model_info[i] : "(none)");
    }

    // 2. Fallback hierarchy & diagnosis
    // Primary: wn10_en_heybuddy (WakeNet10, "Hey Buddy")
    int idx_heybuddy = esp_srmodel_exists(s_models, "wn10_en_heybuddy");
    // Fallback 1: wn10_hiesp (WakeNet10, "Hi ESP")
    int idx_wn10_hiesp = esp_srmodel_exists(s_models, "wn10_hiesp");
    // Fallback 2: wn9_hiesp (WakeNet9, "Hi ESP")
    int idx_wn9_hiesp = esp_srmodel_exists(s_models, "wn9_hiesp");

    if (idx_heybuddy >= 0) {
        strncpy(s_active_model_name, "wn10_en_heybuddy", sizeof(s_active_model_name) - 1);
        strncpy(s_active_word_name, "Hey Buddy", sizeof(s_active_word_name) - 1);
        ESP_LOGI(TAG_WW, "Selected PRIMARY Model: %s (Wake Word: '%s')", s_active_model_name, s_active_word_name);
    } else if (idx_wn10_hiesp >= 0) {
        ESP_LOGW(TAG_WW, "Primary 'wn10_en_heybuddy' not found. Selected Fallback 1: %s", "wn10_hiesp");
        strncpy(s_active_model_name, "wn10_hiesp", sizeof(s_active_model_name) - 1);
        strncpy(s_active_word_name, "Hi ESP", sizeof(s_active_word_name) - 1);
    } else if (idx_wn9_hiesp >= 0) {
        ESP_LOGW(TAG_WW, "Primary 'wn10_en_heybuddy' and 'wn10_hiesp' (WakeNet10) not present in ESP-SR 2.1.1.");
        ESP_LOGI(TAG_WW, "Selected TECHNICAL FALLBACK 2: %s (WakeNet9, Wake Word: '%s')", "wn9_hiesp", "Hi ESP");
        strncpy(s_active_model_name, "wn9_hiesp", sizeof(s_active_model_name) - 1);
        strncpy(s_active_word_name, "Hi ESP", sizeof(s_active_word_name) - 1);
    } else {
        // Fallback to first available wake model
        char *filt = esp_srmodel_filter(s_models, ESP_WN_PREFIX, NULL);
        if (filt) {
            strncpy(s_active_model_name, filt, sizeof(s_active_model_name) - 1);
            strncpy(s_active_word_name, "Hi ESP", sizeof(s_active_word_name) - 1);
            ESP_LOGW(TAG_WW, "Selected available model: %s", s_active_model_name);
        } else {
            ESP_LOGE(TAG_WW, "No compatible WakeNet models found in partition!");
            return ESP_ERR_NOT_FOUND;
        }
    }

    // 3. Configure AFE (Audio Front-End)
    afe_config_t *afe_config = afe_config_init("M", s_models, AFE_TYPE_SR, AFE_MODE_LOW_COST);
    if (!afe_config) {
        ESP_LOGE(TAG_WW, "Failed to create AFE configuration!");
        return ESP_FAIL;
    }

    afe_config->wakenet_init = true;
    afe_config->wakenet_model_name = s_active_model_name;
    afe_config->wakenet_mode = DET_MODE_95; // High sensitivity
    afe_config->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;
    afe_config->afe_ringbuf_size = 50;

    afe_config_print(afe_config);

    s_afe_handle = esp_afe_handle_from_config(afe_config);
    if (!s_afe_handle) {
        ESP_LOGE(TAG_WW, "Failed to get AFE handle!");
        return ESP_FAIL;
    }

    s_afe_data = s_afe_handle->create_from_config(afe_config);
    if (!s_afe_data) {
        ESP_LOGE(TAG_WW, "Failed to instantiate AFE data!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_WW, "✅ ESP-SR AFE initialized with model '%s' (feed_chunksize=%d)",
             s_active_model_name, s_afe_handle->get_feed_chunksize(s_afe_data));

    return ESP_OK;
}

esp_err_t wake_word_sr_start(void)
{
    if (!s_afe_handle || !s_afe_data) {
        ESP_LOGE(TAG_WW, "Cannot start wake word engine: not initialized!");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_ww_task_handle) {
        s_listening = true;
        return ESP_OK;
    }

    s_listening = true;
    BaseType_t ret = xTaskCreatePinnedToCore(
        wake_word_task,
        "wake_word_sr",
        8192,
        NULL,
        5, // Audio task priority
        &s_ww_task_handle,
        1  // Core 1 (leave Core 0 for WiFi & display)
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG_WW, "Failed to create wake word task!");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_WW, "✅ Wake word task started on Core 1 (Listening for '%s')", s_active_word_name);
    return ESP_OK;
}

void wake_word_sr_suspend(void)
{
    if (s_listening) {
        s_listening = false;
        if (s_afe_handle && s_afe_data) {
            s_afe_handle->disable_wakenet(s_afe_data);
            s_afe_handle->reset_buffer(s_afe_data);
        }
        ESP_LOGI(TAG_WW, "⏸️ Wake word listening SUSPENDED (Half-duplex active)");
    }
}

void wake_word_sr_resume(void)
{
    if (!s_listening) {
        if (s_afe_handle && s_afe_data) {
            s_afe_handle->reset_buffer(s_afe_data);
            s_afe_handle->enable_wakenet(s_afe_data);
        }
        s_listening = true;
        ESP_LOGI(TAG_WW, "▶️ Wake word listening RESUMED (Listening for '%s')", s_active_word_name);
    }
}

bool wake_word_sr_is_listening(void)
{
    return s_listening;
}

const char *wake_word_sr_get_model_name(void)
{
    return s_active_model_name;
}

const char *wake_word_sr_get_word_name(void)
{
    return s_active_word_name;
}

static void wake_word_task(void *arg)
{
    board_audio_t *audio = board_audio_init();
    if (!audio) {
        ESP_LOGE(TAG_WW, "Audio instance is NULL, wake word task exiting");
        vTaskDelete(NULL);
        return;
    }

    int feed_chunksize = s_afe_handle->get_feed_chunksize(s_afe_data);
    if (feed_chunksize <= 0 || feed_chunksize > 1024) {
        feed_chunksize = 512;
    }

    const size_t raw_buf_bytes = feed_chunksize * sizeof(int32_t);
    int32_t *raw_buf = (int32_t *)malloc(raw_buf_bytes);
    int16_t *pcm_buf = (int16_t *)malloc(feed_chunksize * sizeof(int16_t));

    if (!raw_buf || !pcm_buf) {
        ESP_LOGE(TAG_WW, "Failed to allocate audio buffers for wake word task");
        if (raw_buf) free(raw_buf);
        if (pcm_buf) free(pcm_buf);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG_WW, "Wake word task listening loop active (chunk: %d samples)", feed_chunksize);

    uint32_t frame_count = 0;

    while (1) {
        if (!s_listening) {
            vTaskDelay(pdMS_TO_TICKS(40));
            continue;
        }

        // Ensure microphone is running
        esp_err_t err = board_audio_mic_start(audio);
        if (err != ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        size_t bytes_read = 0;
        err = board_audio_read(audio, raw_buf, raw_buf_bytes, &bytes_read, pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes_read != raw_buf_bytes) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        if (!s_listening) continue;

        // Convert 32-bit INMP441 raw samples to 16-bit PCM and measure energy
        int64_t chunk_sum_sq = 0;
        int16_t chunk_peak = 0;
        for (int i = 0; i < feed_chunksize; i++) {
            int16_t s = inmp441_to_pcm16(raw_buf[i]);
            pcm_buf[i] = s;
            int16_t abs_s = s < 0 ? -s : s;
            if (abs_s > chunk_peak) chunk_peak = abs_s;
            chunk_sum_sq += (int64_t)s * s;
        }
        float chunk_rms = sqrtf((float)chunk_sum_sq / feed_chunksize);

        // Feed to AFE
        s_afe_handle->feed(s_afe_data, pcm_buf);

        // Fetch result
        afe_fetch_result_t *res = s_afe_handle->fetch(s_afe_data);

        frame_count++;
        if (frame_count % 100 == 0) {
            ESP_LOGI(TAG_WW, "[WW_HEARTBEAT] frames=%u, mic_rms=%.0f, peak=%d, vad=%d, vol=%.1f dB",
                     (unsigned)frame_count, chunk_rms, (int)chunk_peak,
                     res ? res->vad_state : -1, res ? res->data_volume : 0.0f);
        }

        if (chunk_rms > 400.0f) {
            ESP_LOGI(TAG_WW, "[WW_AUDIO] mic_rms=%.0f, peak=%d, vad=%d, wake_state=%d",
                     chunk_rms, (int)chunk_peak, res ? res->vad_state : -1,
                     res ? res->wakeup_state : -1);
        }

        if (res && res->wakeup_state == WAKENET_DETECTED) {
            ESP_LOGI(TAG_WW, "🎯🎯🎯 [WAKENET] WAKE WORD DETECTED! (word_index=%d, model_index=%d) 🎯🎯🎯",
                     res->wake_word_index, res->wakenet_model_index);

            // Immediately suspend listening to prevent re-triggering during recording
            s_listening = false;
            s_afe_handle->disable_wakenet(s_afe_data);
            s_afe_handle->reset_buffer(s_afe_data);

            if (s_detected_cb) {
                s_detected_cb(s_active_word_name, res->wake_word_index);
            }
        }
    }

    free(raw_buf);
    free(pcm_buf);
    vTaskDelete(NULL);
}
