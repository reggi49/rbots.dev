#include "tts_client.h"

#include <string.h>
#include <stdlib.h>
#include "cJSON.h"
#include "esp_crt_bundle.h"
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "audio_player.h"
#include "board_status.h"
#include "cloud_client.h"
#include "esp_timer.h"

#define TTS_URL "https://rbots.dev/tts/azure"
#define TTS_VOICE "id-ID-ArdiNeural"
#define TTS_FORMAT "riff-16khz-16bit-mono-pcm"

#define TTS_HEADER_BUF_MAX 4096
#define TTS_DEFAULT_SR 16000
#define TTS_READ_CHUNK_BYTES 2048
#define TTS_PCM_PUSH_BYTES 512

typedef struct {
    bool header_ready;
    uint8_t header[TTS_HEADER_BUF_MAX];
    size_t header_len;
    uint32_t sample_rate;
    bool audio_started;
    size_t data_offset;
    uint32_t data_size;
    bool data_size_known;
    bool header_consumed;
    size_t total_bytes;
    size_t data_bytes;
    bool data_found;
    tts_fail_stage_t fail_stage;
    int fail_errno;
} tts_http_ctx_t;

static const char *TAG_TTS = "TTS";
static bool s_busy = false;

static uint32_t parse_sample_rate(const uint8_t *hdr)
{
    if (!hdr) return TTS_DEFAULT_SR;
    uint32_t sr = hdr[24] | (hdr[25] << 8) | (hdr[26] << 16) | (hdr[27] << 24);
    return sr ? sr : TTS_DEFAULT_SR;
}

static bool tts_header_valid(const uint8_t *hdr, size_t len)
{
    if (len < 12) return false;
    return memcmp(hdr, "RIFF", 4) == 0 && memcmp(hdr + 8, "WAVE", 4) == 0;
}

static void tts_log_header_preview(const uint8_t *hdr, size_t len)
{
    if (!hdr || len == 0) return;
    size_t take = len > 16 ? 16 : len;
    char hex[(3 * 16) + 1];
    size_t idx = 0;
    for (size_t i = 0; i < take; ++i) {
        idx += snprintf(hex + idx, sizeof(hex) - idx, "%02X%s", hdr[i], (i + 1 == take) ? "" : " ");
    }
    char ascii[17];
    for (size_t i = 0; i < take; ++i) {
        uint8_t c = hdr[i];
        ascii[i] = (c >= 32 && c <= 126) ? (char)c : '.';
    }
    ascii[take] = '\0';
    ESP_LOGI(TAG_TTS, "WAV preview ascii=\"%s\" hex=%s", ascii, hex);
}

static uint16_t read_u16_le(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32_le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static bool tts_parse_wav_header(tts_http_ctx_t *ctx)
{
    if (!ctx || ctx->header_len < 12) return false;
    if (!tts_header_valid(ctx->header, ctx->header_len)) {
        ESP_LOGE(TAG_TTS, "WAV invalid RIFF/WAVE");
        return false;
    }

    tts_log_header_preview(ctx->header, ctx->header_len);

    bool fmt_seen = false;
    uint16_t fmt_format = 0;
    uint16_t fmt_channels = 0;
    uint32_t fmt_sr = 0;
    uint16_t fmt_bits = 0;

    size_t offset = 12; // skip RIFF header
    while (offset + 8 <= ctx->header_len) {
        const uint8_t *chunk = ctx->header + offset;
        char cid[5];
        memcpy(cid, chunk, 4);
        cid[4] = '\0';
        uint32_t chunk_size = read_u32_le(chunk + 4);
        size_t chunk_data = offset + 8;
        size_t chunk_end = chunk_data + chunk_size;

        ESP_LOGI(TAG_TTS, "WAV chunk id=%s size=%u", cid, (unsigned)chunk_size);

        bool chunk_complete = chunk_end <= ctx->header_len;
        if (!chunk_complete && memcmp(cid, "data", 4) != 0) {
            if (ctx->header_len >= TTS_HEADER_BUF_MAX) {
                ESP_LOGE(TAG_TTS, "WAV chunk overruns header buffer (id=%s size=%u hdr_len=%u)",
                         cid, (unsigned)chunk_size, (unsigned)ctx->header_len);
                return false;
            }
            return false; // need more header bytes
        }

        if (memcmp(cid, "fmt ", 4) == 0) {
            if (chunk_size < 16) {
                ESP_LOGE(TAG_TTS, "WAV fmt too small size=%u", (unsigned)chunk_size);
                return false;
            }
            fmt_format = read_u16_le(ctx->header + chunk_data);
            fmt_channels = read_u16_le(ctx->header + chunk_data + 2);
            fmt_sr = read_u32_le(ctx->header + chunk_data + 4);
            fmt_bits = read_u16_le(ctx->header + chunk_data + 14);
            fmt_seen = true;
        } else if (memcmp(cid, "data", 4) == 0) {
            ctx->data_offset = chunk_data;
            ctx->data_size = chunk_size;
            ctx->data_size_known = true;
            ctx->data_found = true;
            break; // data chunk found; payload may follow later
        }

        offset = chunk_end + (chunk_size & 1); // word-align
    }

    if (!ctx->data_found) {
        if (ctx->header_len >= TTS_HEADER_BUF_MAX) {
            ESP_LOGE(TAG_TTS, "WAV data chunk not found within %u bytes", (unsigned)ctx->header_len);
            return false;
        }
        return false; // wait for more header bytes
    }

    if (!fmt_seen) {
        ESP_LOGE(TAG_TTS, "WAV fmt chunk missing");
        return false;
    }

    ctx->sample_rate = fmt_sr ? fmt_sr : TTS_DEFAULT_SR;

    if (!(fmt_format == 1 && fmt_channels == 1 && fmt_bits == 16 && ctx->sample_rate == 16000)) {
        ESP_LOGE(TAG_TTS, "WAV fmt mismatch fmt=%u ch=%u sr=%u bits=%u", (unsigned)fmt_format,
                 (unsigned)fmt_channels, (unsigned)ctx->sample_rate, (unsigned)fmt_bits);
        return false;
    }

    ESP_LOGI(TAG_TTS, "wav ok sr=%u ch=%u bps=%u data_bytes=%u data_offset=%u hdr_len=%u",
             (unsigned)ctx->sample_rate, (unsigned)fmt_channels, (unsigned)fmt_bits,
             (unsigned)ctx->data_size, (unsigned)ctx->data_offset, (unsigned)ctx->header_len);
    return true;
}

static bool tts_push_pcm_chunk(const uint8_t *data, size_t len, tts_http_ctx_t *ctx)
{
    if (!ctx || !data || len == 0) return false;
    const uint8_t *scan = data;
    size_t remaining = len;
    while (remaining > 0) {
        size_t chunk = remaining > TTS_PCM_PUSH_BYTES ? TTS_PCM_PUSH_BYTES : remaining;
        if (!audio_player_submit_pcm(scan, chunk)) {
            ctx->fail_stage = TTS_FAIL_I2S;
            ctx->fail_errno = ESP_FAIL;
            return false;
        }
        scan += chunk;
        remaining -= chunk;
        ctx->data_bytes += chunk;
        if (ctx->data_size_known) {
            if (ctx->data_size <= chunk) {
                ctx->data_size = 0;
            } else {
                ctx->data_size -= chunk;
            }
        }
    }
    return true;
}

static bool tts_consume_header_audio(tts_http_ctx_t *ctx)
{
    if (!ctx) return false;
    if (ctx->header_consumed) return true;
    size_t header_audio = ctx->header_len > ctx->data_offset ? ctx->header_len - ctx->data_offset : 0;
    if (ctx->data_size_known && header_audio > ctx->data_size) {
        header_audio = ctx->data_size;
    }
    if (header_audio > 0) {
        if (!tts_push_pcm_chunk(ctx->header + ctx->data_offset, header_audio, ctx)) {
            return false;
        }
    }
    ctx->header_consumed = true;
    return true;
}

static void tts_log_request_preview(const char *body, size_t len)
{
    if (!body || len == 0) return;
    size_t preview = len > 120 ? 120 : len;
    ESP_LOGE(TAG_TTS, "TTS: req_preview=\"%.*s\"", (int)preview, body);
}

static void tts_log_error_body(esp_http_client_handle_t client)
{
    if (!client) return;
    char buffer[256];
    int read = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
    if (read < 0) {
        read = 0;
    }
    buffer[read] = '\0';
    ESP_LOGE(TAG_TTS, "TTS: err_body_preview=\"%s\"", buffer);
}

static esp_err_t tts_stream_read_data(esp_http_client_handle_t client, tts_http_ctx_t *ctx)
{
    if (!client || !ctx) return ESP_ERR_INVALID_ARG;

    uint8_t buffer[TTS_READ_CHUNK_BYTES];
    while (true) {
        int read = esp_http_client_read(client, (char *)buffer, sizeof(buffer));
        if (read < 0) {
            ctx->fail_stage = TTS_FAIL_HTTP;
            ctx->fail_errno = ESP_FAIL;
            ESP_LOGE(TAG_TTS, "stream read fail %d", read);
            return ESP_FAIL;
        }
        if (read == 0) {
            break;
        }
        ctx->total_bytes += (size_t)read;
        size_t offset = 0;
        while (offset < (size_t)read) {
            if (!ctx->header_ready) {
                size_t need = sizeof(ctx->header) - ctx->header_len;
                size_t available = (size_t)read - offset;
                size_t take = (need < available) ? need : available;
                if (take > 0) {
                    memcpy(ctx->header + ctx->header_len, buffer + offset, take);
                    ctx->header_len += take;
                    offset += take;
                }
                if (ctx->header_len >= sizeof(ctx->header)) {
                    ctx->fail_stage = TTS_FAIL_WAV;
                    ctx->fail_errno = ESP_ERR_INVALID_RESPONSE;
                    ESP_LOGE(TAG_TTS, "WAV header too large");
                    return ESP_ERR_INVALID_RESPONSE;
                }
                if (tts_parse_wav_header(ctx)) {
                    if (!ctx->sample_rate) {
                        ctx->sample_rate = parse_sample_rate(ctx->header);
                    }
                    if (audio_player_start(ctx->sample_rate) == ESP_OK) {
                        ctx->audio_started = true;
                        ESP_LOGI(TAG_TTS, "wav ok data_bytes=%u sr=%u",
                                 (unsigned)ctx->data_size,
                                 (unsigned)ctx->sample_rate);
                        board_set_ai_state(AI_ANSWERING);
                    } else {
                        ctx->fail_stage = TTS_FAIL_I2S;
                        ctx->fail_errno = ESP_FAIL;
                        return ESP_FAIL;
                    }
                    ctx->header_ready = true;
                }
                if (!ctx->header_ready) {
                    continue;
                }
            }

            if (!tts_consume_header_audio(ctx)) {
                return ESP_FAIL;
            }

            if (offset >= (size_t)read) {
                break;
            }

            if (ctx->data_size_known && ctx->data_size == 0) {
                return ESP_OK;
            }

            size_t available = (size_t)read - offset;
            size_t to_write = available;
            if (ctx->data_size_known && to_write > ctx->data_size) {
                to_write = ctx->data_size;
            }
            if (to_write == 0) {
                break;
            }
            if (!tts_push_pcm_chunk(buffer + offset, to_write, ctx)) {
                return ESP_FAIL;
            }
            offset += to_write;
        }
    }
    return ESP_OK;
}

static esp_err_t tts_stream_internal(const char *text, tts_stream_result_t *result)
{
    if (!text || text[0] == '\0') return ESP_ERR_INVALID_ARG;

    if (result) {
        result->status_code = -1;
        result->bytes = 0;
        result->data_bytes = 0;
        result->fail_stage = TTS_FAIL_NONE;
        result->fail_errno = 0;
        result->fail_msg[0] = '\0';
    }

    cJSON *root = cJSON_CreateObject();
    if (!root) return ESP_ERR_NO_MEM;
    cJSON_AddStringToObject(root, "text", text);
    cJSON_AddStringToObject(root, "voice", TTS_VOICE);
    cJSON_AddStringToObject(root, "format", TTS_FORMAT);
    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) return ESP_ERR_NO_MEM;
    size_t body_len = strlen(body);

    tts_http_ctx_t *ctx = calloc(1, sizeof(tts_http_ctx_t));
    if (!ctx) {
        free(body);
        return ESP_ERR_NO_MEM;
    }
    ctx->sample_rate = TTS_DEFAULT_SR;
    bool log_error_payload = false;

    esp_http_client_config_t cfg = {
        .url = TTS_URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .timeout_ms = 20000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(body);
        free(ctx);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "Accept", "audio/wav");

    audio_player_flush();

    ESP_LOGI(TAG_TTS, "start text_len=%d post_len=%zu heap=%u", (int)strlen(text),
             body_len, (unsigned)esp_get_free_heap_size());

    static int64_t last_lock_log_ms = 0;
    bool lock_acquired = net_http_lock_take(20000);
    if (!lock_acquired) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_lock_log_ms > 2000) {
            ESP_LOGW(TAG_TTS, "HTTP lock busy, skip tts");
            last_lock_log_ms = now_ms;
        }
        ctx->fail_stage = TTS_FAIL_HTTP;
        ctx->fail_errno = ESP_ERR_INVALID_STATE;
        if (result) {
            result->fail_stage = TTS_FAIL_HTTP;
            result->fail_errno = ESP_ERR_INVALID_STATE;
            snprintf(result->fail_msg, sizeof(result->fail_msg), "lock busy");
        }
        esp_http_client_cleanup(client);
        free(ctx);
        free(body);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = esp_http_client_open(client, body_len);
    int status = -1;
    if (err != ESP_OK) {
        ctx->fail_stage = TTS_FAIL_HTTP;
        ctx->fail_errno = err;
        goto finalize;
    }

    int written = esp_http_client_write(client, body, body_len);
    ESP_LOGI(TAG_TTS, "write=%d/%zu", written, body_len);
    if (written != (int)body_len) {
        err = ESP_FAIL;
        ctx->fail_stage = TTS_FAIL_HTTP;
        ctx->fail_errno = ESP_FAIL;
        goto finalize;
    }

    int64_t cl = esp_http_client_fetch_headers(client);
    if (cl < 0) {
        err = ESP_FAIL;
        ctx->fail_stage = TTS_FAIL_HTTP;
        ctx->fail_errno = ESP_FAIL;
        goto finalize;
    }

    status = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG_TTS, "status=%d cl=%lld", status, (long long)cl);
    if (status != 200) {
        err = ESP_FAIL;
        ctx->fail_stage = TTS_FAIL_HTTP;
        ctx->fail_errno = status;
        log_error_payload = true;
        goto finalize;
    }

    err = tts_stream_read_data(client, ctx);
    if (err != ESP_OK) {
        if (ctx->fail_stage == TTS_FAIL_NONE) {
            ctx->fail_stage = TTS_FAIL_HTTP;
            ctx->fail_errno = err;
        }
        goto finalize;
    }

    if (!ctx->header_ready || (!ctx->data_found && ctx->data_bytes == 0)) {
        ctx->fail_stage = TTS_FAIL_WAV;
        ctx->fail_errno = ESP_ERR_INVALID_RESPONSE;
        err = ESP_ERR_INVALID_RESPONSE;
    }

finalize:
    if (result) {
        result->status_code = status;
        result->bytes = ctx->total_bytes;
        result->data_bytes = ctx->data_bytes;
        if (err != ESP_OK) {
            result->fail_stage = ctx->fail_stage != TTS_FAIL_NONE ? ctx->fail_stage : TTS_FAIL_HTTP;
            result->fail_errno = ctx->fail_errno ? ctx->fail_errno : err;
            if (result->fail_stage == TTS_FAIL_HTTP) {
                int sock_errno = esp_http_client_get_errno(client);
                const char *msg = esp_err_to_name(result->fail_errno ? result->fail_errno : err);
                snprintf(result->fail_msg, sizeof(result->fail_msg), "%s sock=%d", msg, sock_errno);
            } else if (result->fail_stage == TTS_FAIL_I2S) {
                snprintf(result->fail_msg, sizeof(result->fail_msg), "i2s");
            } else if (result->fail_stage == TTS_FAIL_WAV) {
                snprintf(result->fail_msg, sizeof(result->fail_msg), "wav");
            } else {
                snprintf(result->fail_msg, sizeof(result->fail_msg), "err");
            }
        } else {
            result->fail_stage = TTS_FAIL_NONE;
            result->fail_errno = 0;
            result->fail_msg[0] = '\0';
        }
    }

    if (lock_acquired) {
        net_http_lock_give();
    }

    if (status != 200) {
        log_error_payload = true;
    }

    if (log_error_payload) {
        ESP_LOGE(TAG_TTS, "TTS: status=%d", status);
        tts_log_request_preview(body, body_len);
        tts_log_error_body(client);
    }

    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    free(body);

    if (err != ESP_OK) {
        if (ctx->fail_stage == TTS_FAIL_HTTP) {
            int sock_errno = esp_http_client_get_errno(client);
            const char *msg = esp_err_to_name(result ? result->fail_errno : err);
            ESP_LOGE(TAG_TTS, "http err=%s(%d) sock_errno=%d status=%d",
                     msg, (int)(result ? result->fail_errno : err), sock_errno, status);
        } else if (ctx->fail_stage == TTS_FAIL_I2S) {
            ESP_LOGE(TAG_TTS, "fail stage=I2S errno=%d msg=stream", ctx->fail_errno);
        } else if (ctx->fail_stage == TTS_FAIL_WAV) {
            ESP_LOGE(TAG_TTS, "fail stage=WAV errno=%d msg=invalid", ctx->fail_errno);
        }
        free(ctx);
        return ESP_FAIL;
    }

    ESP_LOGI(TAG_TTS, "http status=%d", status);
    ESP_LOGI(TAG_TTS, "play done bytes=%u", (unsigned)(ctx->data_bytes ? ctx->data_bytes : ctx->total_bytes));
    free(ctx);
    return ESP_OK;
}

esp_err_t tts_client_stream_play(const char *text, tts_stream_result_t *result)
{
    return tts_stream_internal(text, result);
}

static void tts_task(void *arg)
{
    char *text = (char *)arg;
    if (!text) {
        s_busy = false;
        vTaskDelete(NULL);
        return;
    }

    tts_stream_result_t res;
    esp_err_t err = tts_stream_internal(text, &res);
    if (err != ESP_OK) {
        board_set_ai_state(AI_ERROR);
        board_set_ai_text("COBA LAGI YA~", 4000);
        ESP_LOGE(TAG_TTS, "fail stage=%d errno=%d msg=%s", res.fail_stage, res.fail_errno, res.fail_msg);
    } else {
        board_set_ai_state(AI_IDLE);
    }

    free(text);
    s_busy = false;
    vTaskDelete(NULL);
}

esp_err_t tts_client_speak_async(const char *text)
{
    if (!text || text[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (s_busy) return ESP_ERR_INVALID_STATE;
    if (!board_can_network()) return ESP_ERR_INVALID_STATE;

    char *copy = strdup(text);
    if (!copy) return ESP_ERR_NO_MEM;

    s_busy = true;
    BaseType_t ok = xTaskCreate(tts_task, "tts", 8192, copy, 5, NULL);
    if (ok != pdPASS) {
        s_busy = false;
        free(copy);
        return ESP_FAIL;
    }
    return ESP_OK;
}

bool tts_client_is_busy(void)
{
    return s_busy;
}
