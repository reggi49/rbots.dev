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
static volatile bool s_button_interrupt = false;

extern board_display_t *g_disp;

void voice_chat_set_backend_url(const char *url)
{
    if (url && strlen(url) > 0) {
        strncpy(s_backend_url, url, sizeof(s_backend_url) - 1);
        s_backend_url[sizeof(s_backend_url) - 1] = '\0';
        ESP_LOGI(TAG_VC, "Backend URL updated to: %s", s_backend_url);
    }
}

void voice_chat_signal_button_pressed(void)
{
    s_button_interrupt = true;
}

bool voice_chat_consume_button_pressed(void)
{
    bool r = s_button_interrupt;
    s_button_interrupt = false;
    return r;
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
    board_face_set_server_status(SERVER_STATUS_CONNECTING);
    return ESP_OK;
}

void voice_chat_close_backend_session(const char *session_id)
{
    if (!session_id || strlen(session_id) == 0) return;

    char close_url[160];
    // Replace /chat/voice with /chat/session/close if present
    char *p = strstr(s_backend_url, "/chat/voice");
    if (p) {
        size_t base_len = p - s_backend_url;
        snprintf(close_url, sizeof(close_url), "%.*s/chat/session/close", (int)base_len, s_backend_url);
    } else {
        snprintf(close_url, sizeof(close_url), "%s/session/close", s_backend_url);
    }

    esp_http_client_config_t cfg = {
        .url = close_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 3000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) return;

    char body[64];
    snprintf(body, sizeof(body), "{\"session_id\":\"%s\"}", session_id);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "X-Session-Id", session_id);
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG_VC, "Backend session '%s' closed gracefully", session_id);
    }
    esp_http_client_cleanup(client);
}

typedef struct {
    bool is_session_end;
} http_resp_meta_t;

static esp_err_t _voice_chat_http_event(esp_http_client_event_t *evt) {
    if (evt->event_id == HTTP_EVENT_ON_HEADER) {
        if (strcasecmp(evt->header_key, "X-Session-Action") == 0) {
            http_resp_meta_t *meta = (http_resp_meta_t *)evt->user_data;
            if (meta && strstr(evt->header_value, "end") != NULL) {
                meta->is_session_end = true;
                ESP_LOGI(TAG_VC, "Intercepted response header X-Session-Action: %s", evt->header_value);
            }
        }
    }
    return ESP_OK;
}

esp_err_t voice_chat_execute_turn(uint32_t speech_start_timeout_ms,
                                  const char *session_id,
                                  int turn_index,
                                  rbot_trigger_t trigger,
                                  rbot_turn_result_t *out_result)
{
    if (!out_result) return ESP_ERR_INVALID_ARG;
    *out_result = TURN_RESULT_ERROR;

    const uint32_t sample_rate = 16000;
    const uint32_t max_recording_samples = (RBOT_MAX_RECORDING_DURATION_MS / 1000) * sample_rate;
    const size_t max_buf_bytes = max_recording_samples * sizeof(int16_t);
    const size_t wav_header_len = 44;
    const float hpf_alpha = 0.96586f; // ~80 Hz cutoff for 16 kHz sample rate

    ESP_LOGI(TAG_VC, "=== Execute Turn %d (Session: %s, Speech Start Timeout: %u ms, Max Rec: %u ms) ===",
             turn_index, session_id ? session_id : "none",
             (unsigned)speech_start_timeout_ms, (unsigned)RBOT_MAX_RECORDING_DURATION_MS);

    // 0. Check Wi-Fi connectivity
    wifi_status_t wf = board_get_wifi_status();
    if (wf != WIFI_CONNECTED && wf != WIFI_CONNECTED_STABLE) {
        ESP_LOGW(TAG_VC, "Wi-Fi not connected! Waiting up to 5s...");
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

    // 1. Allocate Audio Buffer in PSRAM for recording
    int16_t *buf = (int16_t *)heap_caps_malloc(max_buf_bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buf) {
        buf = (int16_t *)malloc(max_buf_bytes);
    }
    if (!buf) {
        ESP_LOGE(TAG_VC, "Failed to allocate %zu bytes for recording buffer", max_buf_bytes);
        board_face_set_error(FACE_ERR_GENERIC, "OUT OF MEM");
        return ESP_ERR_NO_MEM;
    }

    // Pre-roll Circular Buffer (~300 ms = 4800 samples)
    const size_t pre_roll_samples = (RBOT_PRE_ROLL_DURATION_MS * sample_rate) / 1000;
    int16_t *preroll = (int16_t *)malloc(pre_roll_samples * sizeof(int16_t));
    if (!preroll) {
        ESP_LOGE(TAG_VC, "Failed to allocate pre-roll buffer");
        free(buf);
        return ESP_ERR_NO_MEM;
    }
    size_t preroll_head = 0;
    size_t preroll_count = 0;

    board_audio_t *audio = board_audio_init();
    if (!audio) {
        free(preroll);
        free(buf);
        board_face_set_error(FACE_ERR_AUDIO, "AUDIO FAIL");
        return ESP_FAIL;
    }

    // 2. Start Microphone & discard initial startup thump
    esp_err_t err = board_audio_mic_start(audio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_VC, "Failed to start microphone: %d", err);
        update_status(AI_IDLE, "MIC ERROR", false, 0.0f);
        free(preroll);
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

    // Clear any pending button signal
    voice_chat_consume_button_pressed();

    // Visual prompt
    if (turn_index == 1) {
        update_status(AI_LISTENING, "LISTENING...", true, 0.8f);
    } else {
        update_status(AI_LISTENING, "TANYA LAGI...", true, 0.8f);
    }

    // 3. Phase A: Wait for Speech Onset (with Rolling Pre-Roll)
    int64_t wait_start_time_ms = esp_timer_get_time() / 1000;
    int64_t rec_start_time_ms = 0;
    bool speech_detected = false;
    uint32_t captured = 0;
    int32_t peak = 0;
    int64_t sum_sq = 0;

    int32_t raw_chunk[128];
    float prev_x = 0.0f, prev_y = 0.0f;
    int last_rem_wait_sec = -1;

    const float speech_rms_threshold = 350.0f;
    const uint32_t silence_cutoff_samples = (RBOT_END_OF_TURN_SILENCE_MS * sample_rate) / 1000; // 24000 = 1.5s
    const uint32_t min_speech_samples = (RBOT_MIN_SPEECH_DURATION_MS * sample_rate) / 1000;     // 6400 = 0.4s

    while (!speech_detected) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if ((now_ms - wait_start_time_ms) >= speech_start_timeout_ms) {
            ESP_LOGI(TAG_VC, "⏳ Speech start timeout reached (%lld ms, no speech onset)", now_ms - wait_start_time_ms);
            break;
        }

        // Countdown visual update for follow-up wait
        int rem_wait_sec = (int)((speech_start_timeout_ms - (now_ms - wait_start_time_ms) + 999) / 1000);
        if (rem_wait_sec < 1) rem_wait_sec = 1;
        if (turn_index > 1 && rem_wait_sec != last_rem_wait_sec) {
            last_rem_wait_sec = rem_wait_sec;
            char msg[24];
            snprintf(msg, sizeof(msg), "TANYA LAGI %d", rem_wait_sec);
            update_status(AI_LISTENING, msg, true, 0.8f);
        }

        // Check if button was tapped to force start recording immediately
        if (voice_chat_consume_button_pressed()) {
            ESP_LOGI(TAG_VC, "Button pressed during wait: forcing speech onset!");
            speech_detected = true;
            rec_start_time_ms = esp_timer_get_time() / 1000;
            break;
        }

        size_t bytes_read = 0;
        err = board_audio_read(audio, raw_chunk, sizeof(raw_chunk), &bytes_read, pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t samples = bytes_read / sizeof(int32_t);
        int64_t chunk_sum_sq = 0;

        for (size_t i = 0; i < samples; i++) {
            int16_t s = inmp441_to_pcm16(raw_chunk[i]);
            float x = (float)s;
            float y = hpf_alpha * (prev_y + x - prev_x);
            prev_x = x;
            prev_y = y;

            int32_t s_clean = (int32_t)y;
            if (s_clean > 32767)  s_clean = 32767;
            if (s_clean < -32768) s_clean = -32768;

            preroll[preroll_head] = (int16_t)s_clean;
            preroll_head = (preroll_head + 1) % pre_roll_samples;
            if (preroll_count < pre_roll_samples) {
                preroll_count++;
            }

            chunk_sum_sq += (int64_t)s_clean * s_clean;
        }

        if (samples > 0) {
            float chunk_rms = sqrtf((float)chunk_sum_sq / samples);
            if (chunk_rms > speech_rms_threshold) {
                speech_detected = true;
                rec_start_time_ms = esp_timer_get_time() / 1000;
                ESP_LOGI(TAG_VC, "🗣️ Speech onset detected! (RMS: %.0f, waited %lld ms)",
                         chunk_rms, rec_start_time_ms - wait_start_time_ms);
                break;
            }
        }
    }

    if (!speech_detected) {
        // No speech onset occurred during timeout window
        board_audio_mic_stop(audio);
        free(preroll);
        free(buf);
        *out_result = TURN_RESULT_NO_SPEECH_TIMEOUT;
        return ESP_OK;
    }

    // 4. Phase B: Speech Onset Detected -> Record Speech with VAD Early-Cutoff & Max Duration
    // Copy pre-roll buffer in chronological order into recording buffer
    size_t start_idx = (preroll_head + pre_roll_samples - preroll_count) % pre_roll_samples;
    for (size_t p = 0; p < preroll_count; p++) {
        int16_t s = preroll[(start_idx + p) % pre_roll_samples];
        buf[captured++] = s;
        int32_t a = (s < 0) ? -s : s;
        if (a > peak) peak = a;
        sum_sq += (int64_t)s * s;
    }
    free(preroll);
    preroll = NULL;

    ESP_LOGI(TAG_VC, "🎤 RECORDING SPEECH (Pre-roll: %u samples, max %u ms)...",
             (unsigned)captured, (unsigned)RBOT_MAX_RECORDING_DURATION_MS);

    uint32_t speech_samples = captured;
    uint32_t silent_samples = 0;
    int current_rec_sec = -1;

    while (captured < max_recording_samples) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        int64_t elapsed_rec_ms = now_ms - rec_start_time_ms;

        // Check Maximum Recording Duration
        if (elapsed_rec_ms >= RBOT_MAX_RECORDING_DURATION_MS) {
            ESP_LOGI(TAG_VC, "⏹️ Maximum recording duration reached (%u ms)", (unsigned)RBOT_MAX_RECORDING_DURATION_MS);
            break;
        }

        // Remaining recording seconds countdown
        int rem_rec_sec = (int)((RBOT_MAX_RECORDING_DURATION_MS - elapsed_rec_ms + 999) / 1000);
        if (rem_rec_sec < 1) rem_rec_sec = 1;
        if (rem_rec_sec != current_rec_sec) {
            current_rec_sec = rem_rec_sec;
            char msg[24];
            snprintf(msg, sizeof(msg), "LISTENING %d", rem_rec_sec);
            update_status(AI_LISTENING, msg, true, 0.8f);
        }

        size_t bytes_read = 0;
        err = board_audio_read(audio, raw_chunk, sizeof(raw_chunk), &bytes_read, pdMS_TO_TICKS(100));
        if (err != ESP_OK || bytes_read == 0) {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        size_t samples = bytes_read / sizeof(int32_t);
        int64_t chunk_sum_sq = 0;

        for (size_t i = 0; i < samples && captured < max_recording_samples; i++) {
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
            chunk_sum_sq += (int64_t)s_clean * s_clean;
        }

        // VAD Continuous Silence & Early-Cutoff Logic
        if (samples > 0) {
            float chunk_rms = sqrtf((float)chunk_sum_sq / samples);
            if (chunk_rms > speech_rms_threshold) {
                speech_samples += samples;
                silent_samples = 0; // CONTINUOUS SILENCE RESET!
            } else {
                silent_samples += samples;
                if (speech_samples >= min_speech_samples && silent_samples >= silence_cutoff_samples) {
                    ESP_LOGI(TAG_VC, "⏹️ VAD Early-Cutoff: Continuous silence (1.5s) detected (dur=%lld ms, samples=%u)",
                             now_ms - rec_start_time_ms, (unsigned)captured);
                    break;
                }
            }
        }
    }

    board_audio_mic_stop(audio);
    float rms = (captured > 0) ? sqrtf((float)sum_sq / captured) : 0.0f;
    ESP_LOGI(TAG_VC, "Recording finished: %u samples (RMS: %.0f, Peak: %d, Dur: %lld ms)",
             (unsigned)captured, rms, (int)peak, (esp_timer_get_time() / 1000) - rec_start_time_ms);

    const size_t actual_buf_bytes = captured * sizeof(int16_t);
    const size_t actual_upload_len = wav_header_len + actual_buf_bytes;

    // 5. Send Audio via HTTP POST to Backend
    update_status(AI_THINKING, "THINKING...", true, 0.5f);
    ESP_LOGI(TAG_VC, "Sending audio (%u bytes) to backend: %s [Session: %s, Turn: %d]",
             (unsigned)actual_upload_len, s_backend_url, session_id ? session_id : "none", turn_index);

    // Prepare 44-byte WAV header with actual length
    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4);
    uint32_t chunk_size = (uint32_t)(actual_upload_len - 8);
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
    header[40] = actual_buf_bytes & 0xFF;
    header[41] = (actual_buf_bytes >> 8) & 0xFF;
    header[42] = (actual_buf_bytes >> 16) & 0xFF;
    header[43] = (actual_buf_bytes >> 24) & 0xFF;

    http_resp_meta_t resp_meta = {0};
    esp_http_client_config_t http_cfg = {
        .url = s_backend_url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 35000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .event_handler = _voice_chat_http_event,
        .user_data = &resp_meta,
    };

    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        ESP_LOGE(TAG_VC, "Failed to initialize HTTP client");
        board_face_set_error(FACE_ERR_SERVER, "HTTP FAIL");
        free(buf);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "audio/wav");
    if (session_id && strlen(session_id) > 0) {
        esp_http_client_set_header(client, "X-Session-Id", session_id);
    }
    char turn_hdr[16];
    snprintf(turn_hdr, sizeof(turn_hdr), "%d", turn_index);
    esp_http_client_set_header(client, "X-Turn-Index", turn_hdr);

    err = esp_http_client_open(client, actual_upload_len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG_VC, "Failed to open HTTP connection: %d", err);
        esp_http_client_cleanup(client);
        free(buf);
        board_face_set_error(FACE_ERR_SERVER, "CONN FAIL");
        board_face_set_server_status(SERVER_STATUS_DISCONNECTED);
        return err;
    }

    // Write WAV header
    esp_http_client_write(client, (const char *)header, 44);

    // Stream PCM audio in 1024-byte chunks
    const size_t chunk_sz = 1024;
    size_t sent = 0;
    const uint8_t *raw_pcm = (const uint8_t *)buf;
    while (sent < actual_buf_bytes) {
        size_t to_send = actual_buf_bytes - sent;
        if (to_send > chunk_sz) to_send = chunk_sz;
        int written = esp_http_client_write(client, (const char *)(raw_pcm + sent), to_send);
        if (written < 0) {
            ESP_LOGE(TAG_VC, "Error writing audio to HTTP stream");
            break;
        }
        sent += written;
    }

    // Free recording buffer immediately
    free(buf);
    buf = NULL;

    // 6. Fetch HTTP Response & Headers
    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG_VC, "HTTP Response status=%d, content_length=%d", status_code, content_length);

    if (status_code != 200) {
        ESP_LOGE(TAG_VC, "Backend returned error HTTP %d", status_code);
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        board_face_set_error(FACE_ERR_SERVER, "SERVER ERR");
        board_face_set_server_status(SERVER_STATUS_DISCONNECTED);
        return ESP_FAIL;
    }

    board_face_set_server_status(SERVER_STATUS_CONNECTED);

    // Check if backend signaled session closure (explicit verbal exit)
    bool is_session_end = resp_meta.is_session_end;
    if (is_session_end) {
        ESP_LOGI(TAG_VC, "Backend signaled X-Session-Action: end");
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

    ESP_LOGI(TAG_VC, "Submitted %u bytes to audio player, waiting playback to complete...",
             (unsigned)total_played_bytes);

    audio_player_wait_empty(15000);
    audio_player_stop();

    esp_http_client_close(client);
    esp_http_client_cleanup(client);

    // 8. Speaker Cooldown & Microphone Flush (Self-detection prevention)
    vTaskDelay(pdMS_TO_TICKS(RBOT_SPEAKER_COOLDOWN_MS));
    if (audio) {
        int32_t discard[128];
        size_t br = 0;
        for (int i = 0; i < 10; i++) {
            board_audio_read(audio, discard, sizeof(discard), &br, pdMS_TO_TICKS(10));
        }
    }

    ESP_LOGI(TAG_VC, "=== Turn %d Completed Successfully (Action: %s) ===",
             turn_index, is_session_end ? "END" : "CONTINUE");

    if (is_session_end) {
        board_face_trigger_happy(1200);
        printf("STATUS:HAPPY_END\r\n");
        *out_result = TURN_RESULT_SESSION_END;
    } else {
        board_face_trigger_happy(800);
        printf("STATUS:HAPPY\r\n");
        *out_result = TURN_RESULT_CONTINUE;
    }
    fflush(stdout);

    return ESP_OK;
}

esp_err_t voice_chat_trigger_turn(uint32_t duration_sec, rbot_trigger_t trigger)
{
    (void)duration_sec;
    rbot_turn_result_t res;
    return voice_chat_execute_turn(RBOT_SPEECH_START_TIMEOUT_INITIAL_MS, "legacy_turn", 1, trigger, &res);
}

esp_err_t voice_chat_check_server_health(void)
{
    char health_url[160];
    char *p = strstr(s_backend_url, "/chat/voice");
    if (p) {
        size_t base_len = p - s_backend_url;
        snprintf(health_url, sizeof(health_url), "%.*s/health", (int)base_len, s_backend_url);
    } else {
        snprintf(health_url, sizeof(health_url), "%s/health", s_backend_url);
    }

    esp_http_client_config_t cfg = {
        .url = health_url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 1500,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        board_face_set_server_status(SERVER_STATUS_DISCONNECTED);
        return ESP_FAIL;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int sc = esp_http_client_get_status_code(client);
        if (sc == 200) {
            board_face_set_server_status(SERVER_STATUS_CONNECTED);
            esp_http_client_cleanup(client);
            return ESP_OK;
        }
    }
    board_face_set_server_status(SERVER_STATUS_DISCONNECTED);
    esp_http_client_cleanup(client);
    return ESP_FAIL;
}
