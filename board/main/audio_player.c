#include "audio_player.h"
#include "board_pins.h"            /* Single source of truth for GPIOs */

#include "driver/i2s_std.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/ringbuf.h"

/* Speaker uses its own dedicated I2S port (separate from mic) */
#define SPK_I2S_PORT  SPK_I2S_PORT_NUM   /* from board_pins.h */

#define AUDIO_VOLUME_SHIFT 0   /* Set to 0 for full volume test (normally 1 for safety) */
#define AUDIO_RING_BUFFER_BYTES (32 * 1024)
#define AUDIO_MAX_FRAME_SAMPLES 256

static const char *TAG_AUDIO = "AUDIO";

static RingbufHandle_t s_audio_rb = NULL;
static TaskHandle_t s_audio_task = NULL;
static i2s_chan_handle_t s_tx_handle = NULL;
static bool s_i2s_ready = false;
static bool s_started = false;

static void audio_task(void *arg)
{
    (void)arg;
    size_t total_played = 0;
    int64_t last_log_us = 0;
    uint32_t frame_count = 0;
    
    ESP_LOGI(TAG_AUDIO, "Audio task started");
    
    while (true) {
        size_t item_size = 0;
        uint8_t *item = (uint8_t *)xRingbufferReceive(s_audio_rb, &item_size, portMAX_DELAY);
        if (!item) continue;

        // Log first receive
        if (frame_count == 0) {
            ESP_LOGI(TAG_AUDIO, "First audio data received: %u bytes", item_size);
        }

        size_t offset = 0;
        while (offset + 1 < item_size) {
            size_t avail_bytes = item_size - offset;
            size_t samples = avail_bytes / 2;
            if (samples > AUDIO_MAX_FRAME_SAMPLES) {
                samples = AUDIO_MAX_FRAME_SAMPLES;
            }
            int16_t frame_stereo[AUDIO_MAX_FRAME_SAMPLES * 2];
            for (size_t i = 0; i < samples; i++) {
                int16_t s = (int16_t)(item[offset + i * 2] | (item[offset + i * 2 + 1] << 8));
                s >>= AUDIO_VOLUME_SHIFT;
                frame_stereo[i * 2] = s;
                frame_stereo[i * 2 + 1] = s;
            }

            // Log first frame values
            if (frame_count == 0) {
                ESP_LOGI(TAG_AUDIO, "First frame: samples=%u L=%d R=%d", 
                         samples, (int)frame_stereo[0], (int)frame_stereo[1]);
            }

            size_t bytes_to_write = samples * 2 * sizeof(int16_t);
            size_t written = 0;
            // Use i2s_std channel write
            if (s_tx_handle) {
                esp_err_t err = i2s_channel_write(s_tx_handle, frame_stereo, bytes_to_write, &written, portMAX_DELAY);
                if (err != ESP_OK) {
                    ESP_LOGE(TAG_AUDIO, "i2s_channel_write fail %d", err);
                    break;
                }
                
                // Log first successful write
                if (frame_count == 0) {
                    ESP_LOGI(TAG_AUDIO, "First I2S write: bytes=%u written=%u", bytes_to_write, written);
                }
            } else {
                ESP_LOGE(TAG_AUDIO, "TX handle is NULL!");
                break;
            }

            total_played += written;
            frame_count++;
            
            int64_t now = esp_timer_get_time();
            if (now - last_log_us >= 1000000) {
                size_t free_bytes = xRingbufferGetCurFreeSize(s_audio_rb);
                size_t used_bytes = AUDIO_RING_BUFFER_BYTES - free_bytes;
                ESP_LOGI(TAG_AUDIO, "bytes_played=%u rb_used=%u frames=%u", 
                         (unsigned)total_played, (unsigned)used_bytes, frame_count);
                last_log_us = now;
            }
            offset += samples * 2;
        }

        vRingbufferReturnItem(s_audio_rb, (void *)item);
    }
}

static esp_err_t audio_i2s_init(void)
{
    if (s_i2s_ready && s_tx_handle) {
        ESP_LOGI(TAG_AUDIO, "I2S already initialized");
        return ESP_OK;
    }

    ESP_LOGI(TAG_AUDIO, "Initializing I2S speaker on port %d", SPK_I2S_PORT);
    ESP_LOGI(TAG_AUDIO, "I2S GPIO: BCLK=%d WS=%d DOUT=%d", 
             PIN_SPK_I2S_BCK, PIN_SPK_I2S_WS, PIN_SPK_I2S_DOUT);

    /* Speaker TX on dedicated I2S port (separate from mic RX) */
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(SPK_I2S_PORT, I2S_ROLE_MASTER);
    esp_err_t err = i2s_new_channel(&chan_cfg, &s_tx_handle, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "i2s_new_channel fail %d", err);
        return err;
    }
    ESP_LOGI(TAG_AUDIO, "I2S channel created");

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = I2S_GPIO_UNUSED,
            .bclk = PIN_SPK_I2S_BCK,          /* GPIO 11: Bit Clock       */
            .ws   = PIN_SPK_I2S_WS,           /* GPIO 12: Word Select     */
            .dout = PIN_SPK_I2S_DOUT,          /* GPIO  6: Data Out        */
            .din  = I2S_GPIO_UNUSED,           /* No mic on this port      */
            .invert_flags = {
                .mclk_inv = false,
                .bclk_inv = false,
                .ws_inv   = false,
            },
        },
    };

    err = i2s_channel_init_std_mode(s_tx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "i2s_channel_init_std_mode fail %d", err);
        i2s_del_channel(s_tx_handle);
        s_tx_handle = NULL;
        return err;
    }
    ESP_LOGI(TAG_AUDIO, "I2S channel configured (Philips mode, 16-bit, stereo L/R dup)");

    /* Enable moved to start */
    /* err = i2s_channel_enable(s_tx_handle); */
    
    s_i2s_ready = true;
    ESP_LOGI(TAG_AUDIO, "I2S init complete");
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

    return ESP_OK;
}

esp_err_t audio_player_start(uint32_t sample_rate_hz)
{
    esp_err_t err = audio_player_init();
    if (err != ESP_OK) return err;

    ESP_LOGI(TAG_AUDIO, "Starting audio player @ %d Hz", sample_rate_hz);

    // Install I2S driver if not already done
    err = audio_i2s_init();
    if (err != ESP_OK) return err;

    if (sample_rate_hz == 0) sample_rate_hz = 16000;
    
    // Reconfigure clock if needed (optional optimization: only if changed)
    ESP_LOGI(TAG_AUDIO, "Reconfiguring I2S clock to %d Hz", sample_rate_hz);
    i2s_std_clk_config_t clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(sample_rate_hz);
    err = i2s_channel_reconfig_std_clock(s_tx_handle, &clk_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_AUDIO, "i2s_channel_reconfig_std_clock fail %d", err);
        return err;
    }

    if (!s_started) {
        ESP_LOGI(TAG_AUDIO, "Enabling I2S channel...");
        err = i2s_channel_enable(s_tx_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG_AUDIO, "i2s_channel_enable fail %d", err);
            return err;
        }
        s_started = true;
        ESP_LOGI(TAG_AUDIO, "I2S channel ENABLED and ready");
    }
    return ESP_OK;
}

void audio_player_stop(void)
{
    if (!s_i2s_ready || !s_tx_handle) return;
    
    if (s_started) {
        i2s_channel_disable(s_tx_handle);
        s_started = false;
    }
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

bool audio_player_wait_empty(uint32_t timeout_ms)
{
    if (!s_audio_rb) return true;
    int64_t start = esp_timer_get_time() / 1000;
    while (1) {
        size_t free_bytes = xRingbufferGetCurFreeSize(s_audio_rb);
        if (free_bytes == AUDIO_RING_BUFFER_BYTES) {
            return true;
        }
        if (timeout_ms > 0) {
            int64_t now = esp_timer_get_time() / 1000;
            if ((uint32_t)(now - start) >= timeout_ms) {
                return false;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

bool audio_player_is_playing(void)
{
    return s_started && s_i2s_ready;
}
