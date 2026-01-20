#include "audio_player.h"

#include "driver/i2s.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

#define I2S_PORT I2S_NUM_0
#define PIN_I2S_WS 1
#define PIN_I2S_SCK 4
#define PIN_I2S_DOUT 6

#define AUDIO_VOLUME_SHIFT 1
#define AUDIO_RING_BUFFER_BYTES (32 * 1024)
#define AUDIO_MAX_FRAME_SAMPLES 256

static const char *TAG_AUDIO = "AUDIO";

static RingbufHandle_t s_audio_rb = NULL;
static TaskHandle_t s_audio_task = NULL;
static bool s_i2s_ready = false;
static bool s_started = false;

static void audio_task(void *arg)
{
    (void)arg;
    size_t total_played = 0;
    int64_t last_log_us = 0;
    while (true) {
        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceive(s_audio_rb, &item_size, portMAX_DELAY);
        if (!item) continue;

        size_t offset = 0;
        while (offset + 1 < item_size) {
            size_t avail_bytes = item_size - offset;
            size_t samples = avail_bytes / 2;
            if (samples > AUDIO_MAX_FRAME_SAMPLES) {
                samples = AUDIO_MAX_FRAME_SAMPLES;
            }
            int32_t frame[AUDIO_MAX_FRAME_SAMPLES];
            for (size_t i = 0; i < samples; i++) {
                int16_t s = (int16_t)(item[offset + i * 2] | (item[offset + i * 2 + 1] << 8));
                s >>= AUDIO_VOLUME_SHIFT;
                frame[i] = ((int32_t)s) << 16;
            }

            size_t bytes_to_write = samples * sizeof(int32_t);
            size_t written = 0;
            esp_err_t err = i2s_write(I2S_PORT, frame, bytes_to_write, &written, portMAX_DELAY);
            if (err != ESP_OK) {
                ESP_LOGE(TAG_AUDIO, "i2s_write fail %d", err);
                break;
            }
            total_played += written;
            int64_t now = esp_timer_get_time();
            if (now - last_log_us >= 1000000) {
                size_t free_bytes = xRingbufferGetCurFreeSize(s_audio_rb);
                size_t used_bytes = AUDIO_RING_BUFFER_BYTES - free_bytes;
                ESP_LOGD(TAG_AUDIO, "bytes_played=%u rb_used=%u", (unsigned)total_played, (unsigned)used_bytes);
                last_log_us = now;
            }
            offset += samples * 2;
        }

        vRingbufferReturnItem(s_audio_rb, (void *)item);
    }
}

static esp_err_t audio_i2s_init(void)
{
    if (s_i2s_ready) return ESP_OK;

    i2s_config_t config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = 16000,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT,
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 6,
        .dma_buf_len = 256,
        .use_apll = false,
        .tx_desc_auto_clear = true,
        .fixed_mclk = -1,
        .mclk_multiple = I2S_MCLK_MULTIPLE_256,
        .bits_per_chan = I2S_BITS_PER_CHAN_32BIT
    };

    i2s_pin_config_t pins = {
        .bck_io_num = PIN_I2S_SCK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = PIN_I2S_DOUT,
        .data_in_num = I2S_PIN_NO_CHANGE
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &config, 0, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "i2s_driver_install fail %d", err);
        return err;
    }

    err = i2s_set_pin(I2S_PORT, &pins);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "i2s_set_pin fail %d", err);
        return err;
    }

    err = i2s_zero_dma_buffer(I2S_PORT);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "i2s_zero_dma_buffer fail %d", err);
        return err;
    }

    s_i2s_ready = true;
    return ESP_OK;
}

esp_err_t audio_player_init(void)
{
    if (!s_audio_rb) {
        s_audio_rb = xRingbufferCreate(AUDIO_RING_BUFFER_BYTES, RINGBUF_TYPE_BYTEBUF);
        if (!s_audio_rb) {
            ESP_LOGE(TAG_AUDIO, "ringbuffer alloc fail");
            return ESP_ERR_NO_MEM;
        }
    }

    if (!s_audio_task) {
        BaseType_t ok = xTaskCreate(audio_task, "audio_player", 4096, NULL, 5, &s_audio_task);
        if (ok != pdPASS) {
            ESP_LOGE(TAG_AUDIO, "task create fail");
            s_audio_task = NULL;
            return ESP_FAIL;
        }
    }

    return audio_i2s_init();
}

esp_err_t audio_player_start(uint32_t sample_rate_hz)
{
    esp_err_t err = audio_player_init();
    if (err != ESP_OK) return err;

    if (sample_rate_hz == 0) sample_rate_hz = 16000;
    err = i2s_set_sample_rates(I2S_PORT, sample_rate_hz);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "set_sample_rates fail %d", err);
        return err;
    }
    i2s_zero_dma_buffer(I2S_PORT);
    i2s_start(I2S_PORT);
    s_started = true;
    return ESP_OK;
}

void audio_player_stop(void)
{
    if (!s_i2s_ready) return;
    i2s_stop(I2S_PORT);
    s_started = false;
}

void audio_player_flush(void)
{
    if (!s_audio_rb) return;
    size_t item_size = 0;
    for (;;) {
        uint8_t *item = (uint8_t *)xRingbufferReceive(s_audio_rb, &item_size, 0);
        if (!item) {
            break;
        }
        vRingbufferReturnItem(s_audio_rb, (void *)item);
    }
}

bool audio_player_submit_pcm(const uint8_t *data, size_t len)
{
    if (!s_started || !s_audio_rb || !data || len == 0) return false;
    if (len % 2 != 0) len -= 1;
    if (len == 0) return false;

    const TickType_t wait_ticks = pdMS_TO_TICKS(200);
    static bool waiting = false;
    for (;;) {
        BaseType_t ok = xRingbufferSend(s_audio_rb, data, len, wait_ticks);
        if (ok == pdTRUE) {
            if (waiting) {
                waiting = false;
            }
            return true;
        }
        if (!waiting) {
            ESP_LOGW(TAG_AUDIO, "AUDIO: rb full, backpressure wait...");
            waiting = true;
        }
    }
    return false;
}
