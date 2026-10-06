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
#include "driver/usb_serial_jtag_vfs.h"
#include "driver/uart_vfs.h"
#include "sdkconfig.h"

#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#define CONSOLE_SET_TX_EOL(m) usb_serial_jtag_vfs_set_tx_line_endings(m)
#else
#define CONSOLE_SET_TX_EOL(m) uart_vfs_dev_port_set_tx_line_endings(0, m)
#endif
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

    /* Generate 0.8 second of 1 kHz sine at comfortable, clear amplitude */
    const float freq = 1000.0f;
    const float amplitude = 24000.0f;          /* Clean amplitude preventing power dips */
    const uint32_t total_samples = (uint32_t)(sample_rate * 0.8f);
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
    audio_player_wait_empty(2000);
    audio_player_stop();

    ESP_LOGI(TAG_ST, "Speaker tone test DONE");
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────── */
/*  1b. Speaker Human Voice Test (Prabowo - MyInstants)     */
/* ──────────────────────────────────────────────────────── */

#include "prabowo_audio.h"

esp_err_t selftest_speaker_human_voice(void)
{
    ESP_LOGI(TAG_ST, "=== Speaker Human Voice Test (Prabowo - MyInstants) ===");

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

    const uint8_t *pcm_bytes = (const uint8_t *)prabowo_pcm_data;
    size_t total_bytes = sizeof(prabowo_pcm_data);
    const size_t chunk_bytes = 512;
    size_t offset = 0;

    ESP_LOGI(TAG_ST, "Streaming %zu bytes of human voice to I2S speaker (MAX98357A)...", total_bytes);

    while (offset < total_bytes) {
        size_t n = (total_bytes - offset < chunk_bytes) ? (total_bytes - offset) : chunk_bytes;
        while (!audio_player_submit_pcm(pcm_bytes + offset, n)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        offset += n;
    }

    /* Wait for audio ringbuffer to drain completely */
    audio_player_wait_empty(5000);
    vTaskDelay(pdMS_TO_TICKS(500)); /* Allow DMA FIFO to fully play through speaker */
    audio_player_stop();

    ESP_LOGI(TAG_ST, "Speaker human voice test DONE");
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
    ESP_LOGI(TAG_ST, "Audio initialized");

    esp_err_t err = board_audio_mic_start(audio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_ST, "mic_start failed: 0x%x", err);
        return err;
    }

    ESP_LOGI(TAG_ST, "Mic started - LISTENING for 1 second...");

    const uint32_t target_samples = 16000; /* 1 s @ 16 kHz */
    int32_t raw[256];
    uint32_t total = 0;
    int64_t sum_sq = 0;
    int32_t peak = 0;
    uint32_t reads = 0;

    while (total < target_samples) {
        size_t bytes_read = 0;
        err = board_audio_read(audio, raw, sizeof(raw), &bytes_read, pdMS_TO_TICKS(500));
        
        if (err != ESP_OK) {
            ESP_LOGE(TAG_ST, "board_audio_read error: 0x%x", err);
            break;
        }
        
        if (bytes_read == 0) {
            ESP_LOGW(TAG_ST, "No data read, timeout?");
            break;
        }
        
        reads++;
        if (reads == 1) {
            ESP_LOGI(TAG_ST, "First read: %u bytes", bytes_read);
        }

        size_t samples = bytes_read / sizeof(int32_t);
        for (size_t i = 0; i < samples; i++) {
            int32_t s32 = raw[i];
            int32_t s24 = s32 >> 8;
            if (s24 & 0x00800000) s24 |= 0xFF000000;
            int32_t s16 = s24 >> 8;
            if (s16 > 32767)  s16 = 32767;
            if (s16 < -32768) s16 = -32768;

            int32_t abs_s = (s16 < 0) ? -s16 : s16;
            sum_sq += (int64_t)s16 * s16;
            if (abs_s > peak) peak = abs_s;

            total++;
            
            // Log first 10 raw values
            if (total <= 10) {
                ESP_LOGI(TAG_ST, "  Sample %u: raw32=0x%08X (raw24=%d) → s16=%d",
                         total, (unsigned)s32, s24, s16);
            }
        }
    }

    board_audio_mic_stop(audio);

    float rms = (total > 0) ? sqrtf((float)sum_sq / total) : 0.0f;
    ESP_LOGI(TAG_ST, "\n========== MIC RESULT ==========");
    ESP_LOGI(TAG_ST, "Total samples: %u", total);
    ESP_LOGI(TAG_ST, "Total reads: %u", reads);
    ESP_LOGI(TAG_ST, "RMS level: %.0f", rms);
    ESP_LOGI(TAG_ST, "Peak: %d", (int)peak);
    ESP_LOGI(TAG_ST, "================================\n");

    if (total == 0) {
        ESP_LOGE(TAG_ST, "❌ No mic data captured — check wiring (WS=1, BCK=4, SD=2)");
        return ESP_FAIL;
    }
    
    if (rms < 500) {
        ESP_LOGW(TAG_ST, "⚠️  RMS very low (%.0f) - mic may not be working or input silent", rms);
    } else if (rms < 2000) {
        ESP_LOGW(TAG_ST, "⚠️  RMS low (%.0f) - check mic sensitivity/gain", rms);
    } else {
        ESP_LOGI(TAG_ST, "✓ Mic RMS OK (%.0f)", rms);
    }

    ESP_LOGI(TAG_ST, "Mic level test DONE");
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────── */
/*  2b. Mic Audio Streaming (audio_audit.py & record_voice.py) */
/* ──────────────────────────────────────────────────────── */

#define INMP441_DIGITAL_GAIN_SHIFT 6  /* >>6 gives 4x digital gain (+12 dB) with saturation limit */

static inline int16_t inmp441_to_pcm16(int32_t raw_sample)
{
    /* INMP441: 24-bit audio in bits [31:8] of 32-bit slot */
    int32_t s24 = raw_sample >> 8;
    if (s24 & 0x00800000) {
        s24 |= 0xFF000000;
    }
    /* Scale with 4x (+12 dB) gain: >>6 instead of >>8 preserves 2 more bits of precision */
    int32_t s16 = s24 >> INMP441_DIGITAL_GAIN_SHIFT;
    if (s16 > 32767)  s16 = 32767;
    if (s16 < -32768) s16 = -32768;
    return (int16_t)s16;
}

esp_err_t selftest_mic_stream(uint32_t duration_sec)
{
    if (duration_sec == 0) duration_sec = 5;
    if (duration_sec > 30) duration_sec = 30;
    const uint32_t sample_rate = 16000;
    const uint32_t total_samples = duration_sec * sample_rate;
    const size_t buf_bytes = total_samples * sizeof(int16_t);
    const float hpf_alpha = 0.96586f; // 90 Hz 1st-order IIR HPF @ 16 kHz

    int16_t *buf = (int16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (int16_t *)malloc(buf_bytes);
    }
    if (!buf) {
        ESP_LOGE(TAG_ST, "Failed to allocate %zu bytes for recording", buf_bytes);
        printf("RECORD_ERR no_mem\r\n");
        return ESP_ERR_NO_MEM;
    }

    board_audio_t *audio = board_audio_init();
    if (!audio) {
        free(buf);
        return ESP_FAIL;
    }

    esp_err_t err = board_audio_mic_start(audio);
    if (err != ESP_OK) {
        free(buf);
        return err;
    }

    // Discard 100ms pre-roll (1600 samples)
    int32_t discard_buf[128];
    uint32_t discarded = 0;
    while (discarded < 1600) {
        size_t br = 0;
        board_audio_read(audio, discard_buf, sizeof(discard_buf), &br, pdMS_TO_TICKS(100));
        if (br > 0) discarded += (br / sizeof(int32_t));
        else break;
    }

    // Signal Python to start recording timer
    printf("START_RECORD\r\n");
    fflush(stdout);

    int32_t raw_buf[128];
    float prev_x = 0.0f, prev_y = 0.0f;
    uint32_t captured = 0;

    // Capture uninterrupted at 16000 Hz into PSRAM
    while (captured < total_samples) {
        size_t bytes_read = 0;
        err = board_audio_read(audio, raw_buf, sizeof(raw_buf), &bytes_read, pdMS_TO_TICKS(500));
        if (err == ESP_OK && bytes_read > 0) {
            size_t n = bytes_read / sizeof(int32_t);
            for (size_t i = 0; i < n && captured < total_samples; i++) {
                int16_t raw_s16 = inmp441_to_pcm16(raw_buf[i]);
                float x = (float)raw_s16;
                float y = hpf_alpha * (prev_y + x - prev_x);
                prev_x = x;
                prev_y = y;
                if (y > 32767.0f) y = 32767.0f;
                else if (y < -32768.0f) y = -32768.0f;
                buf[captured++] = (int16_t)y;
            }
        } else if (err != ESP_OK) {
            break;
        }
    }

    board_audio_mic_stop(audio);

    // Handshake to notify Python recording phase has ended and stream transfer begins
    printf("STREAM_START\r\n");
    fflush(stdout);

    // CRITICAL: Disable CRLF translation so binary audio bytes (0x0A) are NOT mangled into 0x0D 0x0A!
    CONSOLE_SET_TX_EOL(ESP_LINE_ENDINGS_LF);

    // Transmit recorded PCM16 buffer to host in 512-byte chunks
    const uint8_t *pcm_bytes = (const uint8_t *)buf;
    size_t total_bytes_to_send = captured * sizeof(int16_t);
    size_t offset = 0;
    while (offset < total_bytes_to_send) {
        size_t chunk = (total_bytes_to_send - offset > 512) ? 512 : (total_bytes_to_send - offset);
        fwrite(pcm_bytes + offset, 1, chunk, stdout);
        offset += chunk;
    }
    fflush(stdout);

    free(buf);

    CONSOLE_SET_TX_EOL(ESP_LINE_ENDINGS_CRLF);
    printf("\nEND_RECORD\r\n");
    fflush(stdout);
    return ESP_OK;
}

/* ──────────────────────────────────────────────────────── */
/*  2c. Mic to Speaker Loopback (Parrot / Echo Test)       */
/* ──────────────────────────────────────────────────────── */

/* Draw a status on the LCD AND print the same text on serial ("STATUS:<text>")
 * so the host script (parrot_voice.py) stays in sync with the screen. */
static void parrot_status(ai_state_t st, const char *msg, bool anim, float level)
{
    extern board_display_t *g_disp;
    if (g_disp) {
        board_display_draw_overlay(g_disp, st, msg, WIFI_OFF, NET_UNKNOWN, BAT_FULL,
                                   anim, level, false, 0, "", 0);
    }
    printf("STATUS:%s\r\n", msg);
    fflush(stdout);
}

esp_err_t selftest_mic_speaker_loopback(uint32_t duration_sec)
{
    if (duration_sec == 0) duration_sec = 3;
    const uint32_t sample_rate = 16000;
    const uint32_t total_samples = duration_sec * sample_rate;
    const size_t buf_bytes = total_samples * sizeof(int16_t);
    const float hpf_alpha = 0.96586f;
    extern board_display_t *g_disp;
    (void)g_disp;

    ESP_LOGI(TAG_ST, "=== Loopback Parrot Test (%u s) ===", (unsigned)duration_sec);
    printf("ECHO_START\r\n");
    fflush(stdout);

    int16_t *buf = (int16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (int16_t *)malloc(buf_bytes);
    }
    if (!buf) {
        ESP_LOGE(TAG_ST, "Failed to allocate %zu bytes for loopback", buf_bytes);
        printf("ECHO_ERR no_mem\r\n");
        return ESP_ERR_NO_MEM;
    }

    board_audio_t *audio = board_audio_init();
    if (!audio) {
        free(buf);
        return ESP_FAIL;
    }

    // 1. COUNTDOWN BEFORE RECORDING (3, 2, 1)
    for (int cd = 3; cd >= 1; cd--) {
        char msg[20];
        snprintf(msg, sizeof(msg), "RECORD IN %d", cd);
        parrot_status(AI_LISTENING, msg, false, 0.0f);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    esp_err_t err = board_audio_mic_start(audio);
    if (err != ESP_OK) {
        printf("ECHO_ERR mic_start %d\r\n", err);
        parrot_status(AI_IDLE, "MIC ERROR", false, 0.0f);
        free(buf);
        return err;
    }

    // Discard pre-roll
    int32_t discard_buf[128];
    uint32_t discarded = 0;
    while (discarded < 1600) {
        size_t br = 0;
        board_audio_read(audio, discard_buf, sizeof(discard_buf), &br, pdMS_TO_TICKS(100));
        if (br > 0) discarded += (br / sizeof(int32_t));
        else break;
    }

    ESP_LOGI(TAG_ST, "🎤 RECORDING NOW! Speak into INMP441 mic (%u seconds)...", (unsigned)duration_sec);

    // 2. RECORDING (3 seconds, display: RECORDING 3 -> RECORDING 2 -> RECORDING 1)
    int32_t raw_chunk[128];
    float prev_x = 0.0f, prev_y = 0.0f;
    uint32_t captured = 0;
    int32_t peak = 0;
    int64_t sum_sq = 0;
    int current_rec_sec = -1;

    while (captured < total_samples) {
        int rem_rec_sec = (int)((total_samples - captured + sample_rate - 1) / sample_rate);
        if (rem_rec_sec < 1) rem_rec_sec = 1;
        if (rem_rec_sec != current_rec_sec) {
            current_rec_sec = rem_rec_sec;
            char msg[20];
            snprintf(msg, sizeof(msg), "RECORDING %d", current_rec_sec);
            parrot_status(AI_LISTENING, msg, true, 1.0f);
        }

        size_t bytes_read = 0;
        err = board_audio_read(audio, raw_chunk, sizeof(raw_chunk), &bytes_read, pdMS_TO_TICKS(500));
        if (err == ESP_OK && bytes_read > 0) {
            size_t n = bytes_read / sizeof(int32_t);
            for (size_t i = 0; i < n && captured < total_samples; i++) {
                int16_t raw_s16 = inmp441_to_pcm16(raw_chunk[i]);
                float x = (float)raw_s16;
                float y = hpf_alpha * (prev_y + x - prev_x);
                prev_x = x;
                prev_y = y;
                if (y > 32767.0f) y = 32767.0f;
                else if (y < -32768.0f) y = -32768.0f;
                int16_t s16 = (int16_t)y;
                buf[captured++] = s16;

                int32_t abs_s = (s16 < 0) ? -s16 : s16;
                if (abs_s > peak) peak = abs_s;
                sum_sq += (int64_t)s16 * s16;
            }
        } else if (err != ESP_OK) {
            break;
        }
    }

    board_audio_mic_stop(audio);

    float rms = (captured > 0) ? sqrtf((float)sum_sq / captured) : 0.0f;
    ESP_LOGI(TAG_ST, "🎤 Recording done! Captured %u samples (RMS: %.0f, Peak: %d)", (unsigned)captured, rms, (int)peak);

    // 3. COUNTDOWN BEFORE SPEAKING (3, 2, 1)
    for (int cd = 3; cd >= 1; cd--) {
        char msg[20];
        snprintf(msg, sizeof(msg), "SPEAK IN %d", cd);
        parrot_status(AI_ANSWERING, msg, false, 0.0f);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 4. PLAYBACK THROUGH SPEAKER (3 seconds, display: SPEAKING 3 -> SPEAKING 2 -> SPEAKING 1)
    ESP_LOGI(TAG_ST, "🔊 PLAYING BACK recorded audio through speaker...");
    audio_player_init();
    audio_player_start(sample_rate);

    const size_t chunk_bytes = 512;
    size_t offset = 0;
    const uint8_t *pcm_bytes = (const uint8_t *)buf;
    size_t total_bytes = captured * sizeof(int16_t);
    int current_spk_sec = -1;

    while (offset < total_bytes) {
        int rem_spk_sec = (int)(((total_bytes - offset) / 2 + sample_rate - 1) / sample_rate);
        if (rem_spk_sec < 1) rem_spk_sec = 1;
        if (rem_spk_sec != current_spk_sec) {
            current_spk_sec = rem_spk_sec;
            char msg[20];
            snprintf(msg, sizeof(msg), "SPEAKING %d", current_spk_sec);
            parrot_status(AI_ANSWERING, msg, true, 0.0f);
        }

        size_t n = (total_bytes - offset < chunk_bytes) ? (total_bytes - offset) : chunk_bytes;
        while (!audio_player_submit_pcm(pcm_bytes + offset, n)) {
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        offset += n;
    }

    audio_player_wait_empty(5000);
    vTaskDelay(pdMS_TO_TICKS(300));
    audio_player_stop();
    free(buf);

    // 5. FINISHED -> READY
    ESP_LOGI(TAG_ST, "🔊 Loopback playback complete!");
    if (g_disp) {
        board_display_draw_face(g_disp, FACE_HAPPY, BLINK_OPEN, 0, 0, 0);
    }
    parrot_status(AI_IDLE, "RBOT READY", false, 0.0f);
    printf("ECHO_DONE\r\n");
    fflush(stdout);
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
/*  3b. TFT color-cycle test (minimal wiring validation)    */
/* ──────────────────────────────────────────────────────── */

esp_err_t selftest_tft_color_cycle(void)
{
    ESP_LOGI(TAG_ST, "=== TFT Color Cycle Test (20 s) ===");

    board_display_t *disp = board_display_init();
    if (!disp) {
        ESP_LOGE(TAG_ST, "board_display_init failed");
        return ESP_FAIL;
    }

#if PIN_TFT_BL >= 0
    /* Optional: enable BL if wired to GPIO (safe if LED is hardwired to 3.3V) */
    gpio_config_t bl_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_TFT_BL)
    };
    gpio_config(&bl_conf);
    gpio_set_level(PIN_TFT_BL, 1);
#endif

    const uint16_t red   = 0xF800;
    const uint16_t green = 0x07E0;
    const uint16_t blue  = 0x001F;
    const uint16_t black = 0x0000;

    const int cycles = 5; /* 5 cycles * 4s = 20s */
    for (int i = 0; i < cycles; i++) {
        ESP_LOGI(TAG_ST, "TFT color: RED");
        board_display_fill_color(disp, red);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG_ST, "TFT color: GREEN");
        board_display_fill_color(disp, green);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG_ST, "TFT color: BLUE");
        board_display_fill_color(disp, blue);
        vTaskDelay(pdMS_TO_TICKS(1000));

        ESP_LOGI(TAG_ST, "TFT color: BLACK");
        board_display_fill_color(disp, black);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    ESP_LOGI(TAG_ST, "TFT color cycle DONE");
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
