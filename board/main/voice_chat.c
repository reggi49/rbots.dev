#include "voice_chat.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"

#include "board_types.h"
#include "board_display.h"
#include "board_face.h"
#include "board_audio.h"
#include "board_network.h"
#include "board_status.h"
#include "audio_player.h"

static const char *TAG_VC = "VOICE_CHAT";

static char s_backend_url[128] = DEFAULT_BACKEND_VOICE_URL;
extern board_display_t *g_disp;

void voice_chat_set_backend_url(const char *url)
{
    if (url && strlen(url) > 0) {
        strncpy(s_backend_url, url, sizeof(s_backend_url) - 1);
        s_backend_url[sizeof(s_backend_url) - 1] = '\0';
        ESP_LOGI(TAG_VC, "Backend URL updated to: %s", s_backend_url);
    }
}

static void update_status(ai_state_t st, const char *msg, bool anim, float level)
{
    (void)anim;
    (void)level;
    switch (st) {
        case AI_LISTENING:
            board_face_set_state(FACE_LISTENING);
            break;
        case AI_THINKING:
            board_face_set_state(FACE_THINKING);
            break;
        case AI_ANSWERING:
            board_face_set_state(FACE_SPEAKING);
            break;
        case AI_ERROR:
            board_face_set_error(FACE_ERR_SERVER, msg);
            break;
        case AI_IDLE:
        default:
            board_face_set_state(FACE_IDLE);
            break;
    }
    board_face_set_status_text(msg);

    printf("STATUS:%s\r\n", msg);
    fflush(stdout);
}

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

esp_err_t voice_chat_init(void)
{
    ESP_LOGI(TAG_VC, "Voice chat module initialized (URL: %s)", s_backend_url);
    return ESP_OK;
}

esp_err_t voice_chat_trigger_turn(uint32_t duration_sec)
{
    if (duration_sec == 0) duration_sec = 5;
    if (duration_sec > 15) duration_sec = 15;

    const uint32_t sample_rate = 16000;
    const uint32_t total_samples = duration_sec * sample_rate;
    const size_t buf_bytes = total_samples * sizeof(int16_t);
    const size_t wav_header_len = 44;
    const size_t total_upload_len = wav_header_len + buf_bytes;
    const float hpf_alpha = 0.96586f;

    ESP_LOGI(TAG_VC, "=== Starting Voice Chat Turn (%u sec record) ===", (unsigned)duration_sec);

    // 0. Check Wi-Fi
    wifi_status_t wf = board_get_wifi_status();
    if (wf != WIFI_CONNECTED && wf != WIFI_CONNECTED_STABLE) {
        ESP_LOGW(TAG_VC, "Wi-Fi not connected yet! Waiting up to 5s...");
        update_status(AI_IDLE, "CONNECTING WIFI...", false, 0.0f);
        int waited = 0;
        while (waited < 50) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wf = board_get_wifi_status();
            if (wf == WIFI_CONNECTED || wf == WIFI_CONNECTED_STABLE) break;
            waited++;
        }
        if (wf != WIFI_CONNECTED && wf != WIFI_CONNECTED_STABLE) {
            ESP_LOGE(TAG_VC, "Wi-Fi connection failed or not ready!");
            board_face_set_error(FACE_ERR_WIFI, "NO WIFI");
            return ESP_ERR_INVALID_STATE;
        }
    }

    // 1. Allocate Audio Buffer in PSRAM
    int16_t *buf = (int16_t *)heap_caps_malloc(buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (int16_t *)malloc(buf_bytes);
    }
    if (!buf) {
        ESP_LOGE(TAG_VC, "Failed to allocate %zu bytes for recording", buf_bytes);
        board_face_set_error(FACE_ERR_GENERIC, "OUT OF MEM");
        return ESP_ERR_NO_MEM;
    }

    board_audio_t *audio = board_audio_init();
    if (!audio) {
        free(buf);
        board_face_set_error(FACE_ERR_AUDIO, "AUDIO FAIL");
        return ESP_FAIL;
    }

    // 2. Countdown 3.. 2.. 1..
    for (int cd = 3; cd >= 1; cd--) {
        char msg[24];
        snprintf(msg, sizeof(msg), "TALK IN %d", cd);
        update_status(AI_LISTENING, msg, false, 0.0f);
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // 3. Start Mic & Discard Pre-roll
    esp_err_t err = board_audio_mic_start(audio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_VC, "Failed to start microphone: %d", err);
        update_status(AI_IDLE, "MIC ERROR", false, 0.0f);
        free(buf);
        return err;
    }

    int32_t discard_buf[128];
    uint32_t discarded = 0;
    while (discarded < 1600) {
        size_t br = 0;
        board_audio_read(audio, discard_buf, sizeof(discard_buf), &br, pdMS_TO_TICKS(100));
        if (br > 0) discarded += (br / sizeof(int32_t));
        else break;
    }

    // 4. Recording Loop
    ESP_LOGI(TAG_VC, "🎤 RECORDING NOW (%u seconds)...", (unsigned)duration_sec);
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
            char msg[24];
            snprintf(msg, sizeof(msg), "RECORDING %d", rem_rec_sec);
            update_status(AI_LISTENING, msg, true, 0.8f);
        }

        size_t bytes_read = 0;
        err = board_audio_read(audio, raw_chunk, sizeof(raw_chunk), &bytes_read, pdMS_TO_TICKS(200));
        if (err != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t samples = bytes_read / sizeof(int32_t);
        for (size_t i = 0; i < samples && captured < total_samples; i++) {
            int16_t s = inmp441_to_pcm16(raw_chunk[i]);
            float x = (float)s;
            float y = hpf_alpha * (prev_y + x - prev_x);
            prev_x = x;
            prev_y = y;

            int32_t s_clean = (int32_t)y;
            if (s_clean > 32767)  s_clean = 32767;
            if (s_clean < -32768) s_clean = -32768;

            buf[captured++] = (int16_t)s_clean;

            int32_t a = (s_clean < 0) ? -s_clean : s_clean;
            if (a > peak) peak = a;
            sum_sq += (int64_t)s_clean * s_clean;
        }
    }

    board_audio_mic_stop(audio);
    float rms = (captured > 0) ? sqrtf((float)sum_sq / captured) : 0.0f;
    ESP_LOGI(TAG_VC, "Recording finished: %u samples (RMS: %.0f, Peak: %d)",
             (unsigned)captured, rms, (int)peak);

    // 5. Send Audio via HTTP POST to Backend
    update_status(AI_THINKING, "THINKING...", true, 0.5f);
    ESP_LOGI(TAG_VC, "Sending audio to backend: %s", s_backend_url);

    // Prepare 44-byte WAV header
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4);
    uint32_t chunk_size = (uint32_t)(total_upload_len - 8);
    header[4] = chunk_size & 0xFF;
    header[5] = (chunk_size >> 8) & 0xFF;
    header[6] = (chunk_size >> 16) & 0xFF;
    header[7] = (chunk_size >> 24) & 0xFF;
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16;
    header[20] = 1; header[21] = 0;  // PCM
    header[22] = 1; header[23] = 0;  // Mono
    header[24] = 0x80; header[25] = 0x3E; // 16000 Hz
    header[26] = 0x00; header[27] = 0x00;
    uint32_t byte_rate = 16000 * 2;
    header[28] = byte_rate & 0xFF;
    header[29] = (byte_rate >> 8) & 0xFF;
    header[30] = (byte_rate >> 16) & 0xFF;
    header[31] = (byte_rate >> 24) & 0xFF;
    header[32] = 2; header[33] = 0;  // Block align
    header[34] = 16; header[35] = 0; // 16-bit
    memcpy(header + 36, "data", 4);
    header[40] = buf_bytes & 0xFF;
    header[41] = (buf_bytes >> 8) & 0xFF;
    header[42] = (buf_bytes >> 16) & 0xFF;
    header[43] = (buf_bytes >> 24) & 0xFF;

    esp_http_client_config_t http_cfg = {
        .url = s_backend_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 35000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG_VC, "Failed to initialize HTTP client");
        board_face_set_error(FACE_ERR_SERVER, "HTTP FAIL");
        free(buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "audio/wav");

    err = esp_http_client_open(client, total_upload_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_VC, "Failed to open HTTP connection: %d", err);
        esp_http_client_cleanup(client);
        free(buf);
        board_face_set_error(FACE_ERR_SERVER, "CONN FAIL");
        return err;
    }

    // Write WAV header
    esp_http_client_write(client, (const char *)header, 44);

    // Stream PCM audio in 1024-byte chunks
    const size_t chunk_sz = 1024;
    size_t sent = 0;
    const uint8_t *raw_pcm = (const uint8_t *)buf;
    while (sent < buf_bytes) {
        size_t to_send = buf_bytes - sent;
        if (to_send > chunk_sz) to_send = chunk_sz;
        int written = esp_http_client_write(client, (const char *)(raw_pcm + sent), to_send);
        if (written < 0) {
            ESP_LOGE(TAG_VC, "Error writing audio to HTTP stream");
            break;
        }
        sent += written;
    }

    // Audio buffer in PSRAM no longer needed
    free(buf);
    buf = NULL;

    // 6. Fetch HTTP Response
    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG_VC, "HTTP Response status=%d, content_length=%d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGE(TAG_VC, "Backend returned error HTTP %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        board_face_set_error(FACE_ERR_SERVER, "SERVER ERR");
        return ESP_FAIL;
    }

    // 7. Stream Response Audio to Speaker
    update_status(AI_ANSWERING, "SPEAKING...", true, 0.8f);
    audio_player_init();
    audio_player_start(16000);

    // Skip first 44 bytes (WAV header)
    char temp_header[44];
    int header_read = 0;
    while (header_read < 44) {
        int r = esp_http_client_read(client, temp_header + header_read, 44 - header_read);
        if (r <= 0) break;
        header_read += r;
    }

    // Stream remaining PCM samples directly to audio player
    uint8_t play_chunk[1024];
    size_t total_played_bytes = 0;
    while (1) {
        int r = esp_http_client_read(client, (char *)play_chunk, sizeof(play_chunk));
        if (r <= 0) break;
        audio_player_submit_pcm(play_chunk, (size_t)r);
        total_played_bytes += r;
    }

    ESP_LOGI(TAG_VC, "Submitted %u bytes to audio player, waiting playback...",
             (unsigned)total_played_bytes);

    audio_player_wait_empty(10000);
    audio_player_stop();

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    ESP_LOGI(TAG_VC, "=== Voice Chat Turn Finished Successfully ===");
    board_face_trigger_happy(1200);
    printf("STATUS:HAPPY\r\n");
    fflush(stdout);

    return ESP_OK;
}
