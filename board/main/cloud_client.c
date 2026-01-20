#include "cloud_client.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_heap_caps.h"
#include "cJSON.h"
#include "esp_err.h"
#include "esp_crt_bundle.h"
#include "lwip/netdb.h"
#include <time.h>
#include <strings.h>
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "command_dispatcher.h"
#include "board_status.h"
#include "tts_client.h"

#define TAG "CLOUD"
#define CHAT_TAG "CHAT"
#define CHAT_EVT "CHAT_EVT"
#define HEARTBEAT_URL "https://rbots.dev/heartbeat"
#define CHAT_GRADIENT_URL "https://rbots.dev/chat-gradient"
#define HEARTBEAT_INTERVAL_MS 30000
#define HEARTBEAT_TIMEOUT_MS 15000
#define CHAT_TIMEOUT_MS 20000

static SemaphoreHandle_t g_http_lock = NULL;
static bool g_http_lock_ready = false;

static void net_http_lock_init(void)
{
    if (g_http_lock_ready) return;
    g_http_lock = xSemaphoreCreateMutex();
    g_http_lock_ready = (g_http_lock != NULL);
}

bool net_http_lock_take(uint32_t timeout_ms)
{
    net_http_lock_init();
    if (!g_http_lock) return false;
    TickType_t ticks = pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(g_http_lock, ticks) == pdTRUE;
}

void net_http_lock_give(void)
{
    if (g_http_lock) {
        xSemaphoreGive(g_http_lock);
    }
}

typedef struct {
    char *buffer;
    size_t capacity;
    size_t length;
    size_t total_len;
    bool chunked;
    bool connected;
    int data_events;
} chat_http_capture_t;

static esp_err_t chat_http_event_handle(esp_http_client_event_t *evt)
{
    chat_http_capture_t *cap = (chat_http_capture_t *)evt->user_data;
    switch (evt->event_id) {
        case HTTP_EVENT_ERROR:
            ESP_LOGW(CHAT_EVT, "ERROR event");
            break;
        case HTTP_EVENT_ON_CONNECTED:
            if (cap) cap->connected = true;
            ESP_LOGI(CHAT_EVT, "CONNECTED");
            break;
        case HTTP_EVENT_HEADERS_SENT:
            ESP_LOGI(CHAT_EVT, "HEADERS_SENT");
            break;
        case HTTP_EVENT_ON_HEADER:
            if (evt->header_key && evt->header_value) {
                if (!strcasecmp(evt->header_key, "Content-Length") ||
                    !strcasecmp(evt->header_key, "Transfer-Encoding") ||
                    !strcasecmp(evt->header_key, "Content-Type")) {
                    ESP_LOGI(CHAT_EVT, "HEADER %s=%s", evt->header_key, evt->header_value);
                }
            }
            break;
        case HTTP_EVENT_ON_DATA: {
            bool chunked = esp_http_client_is_chunked_response(evt->client);
            if (cap) {
                cap->data_events++;
                if (chunked) cap->chunked = true;
                cap->total_len += evt->data_len;
                if (cap->buffer && cap->capacity > cap->length + 1) {
                    size_t available = cap->capacity - cap->length - 1;
                    size_t to_copy = (size_t)evt->data_len;
                    if (to_copy > available) to_copy = available;
                    memcpy(cap->buffer + cap->length, evt->data, to_copy);
                    cap->length += to_copy;
                    cap->buffer[cap->length] = '\0';
                }
            }
            ESP_LOGI(CHAT_EVT, "DATA len=%d chunked=%d total=%u",
                     evt->data_len,
                     chunked,
                     cap ? (unsigned)cap->length : 0u);
            break;
        }
            break;
        case HTTP_EVENT_ON_FINISH:
            ESP_LOGI(CHAT_EVT, "FINISH total=%u", cap ? (unsigned)cap->length : 0u);
            break;
        case HTTP_EVENT_DISCONNECTED:
            ESP_LOGI(CHAT_EVT, "DISCONNECTED errno=%d", esp_http_client_get_errno(evt->client));
            break;
        default:
            break;
    }
    return ESP_OK;
}

static bool debug_resolve_host(void)
{
    ESP_LOGI("NET", "Resolving rbots.dev");
    struct addrinfo hints = {0};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    struct addrinfo *res = NULL;
    int err = getaddrinfo("rbots.dev", "443", &hints, &res);
    if (err != 0) {
        ESP_LOGE("NET", "DNS resolution failed: %d", err);
        return false;
    }

    freeaddrinfo(res);
    ESP_LOGI("NET", "Resolving rbots.dev OK");
    return true;
}
static void copy_value(char *dst, const char *src, size_t dst_size)
{
    if (!dst || !src) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void parse_command_payload(const char *payload, cloud_command_t *out)
{
    if (!payload || !out) return;
    cJSON *root = cJSON_Parse(payload);
    if (!root) return;

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (!cJSON_IsString(cmd_item)) {
        cJSON_Delete(root);
        return;
    }

    cJSON *value_item = cJSON_GetObjectItem(root, "value");

    if (strcmp(cmd_item->valuestring, "SET_MOOD") == 0) {
        out->type = CLOUD_CMD_SET_MOOD;
        if (cJSON_IsString(value_item)) {
            copy_value(out->value, value_item->valuestring, sizeof(out->value));
        }
    } else if (strcmp(cmd_item->valuestring, "SHOW_ICON") == 0) {
        out->type = CLOUD_CMD_SHOW_ICON;
        if (cJSON_IsString(value_item)) {
            copy_value(out->value, value_item->valuestring, sizeof(out->value));
        }
    } else if (strcmp(cmd_item->valuestring, "RESET") == 0) {
        out->type = CLOUD_CMD_RESET;
    }

    cJSON_Delete(root);
}

static bool build_heartbeat_payload(char **payload_out)
{
    cJSON *payload = cJSON_CreateObject();
    if (!payload) return false;

    cJSON_AddStringToObject(payload, "device", "rbot-78BC");
    cJSON_AddNumberToObject(payload, "uptime_sec", esp_timer_get_time() / 1000000);

    const char *wifi_label = "DISCONNECTED";
    if (board_get_wifi_status() == WIFI_CONNECTED) {
        wifi_label = "CONNECTED";
    }
    cJSON_AddStringToObject(payload, "wifi", wifi_label);

    const char *battery_label = "MED";
    battery_state_t batt = board_get_battery_state();
    switch (batt) {
        case BAT_FULL: battery_label = "FULL"; break;
        case BAT_MED: battery_label = "MED"; break;
        case BAT_LOW: battery_label = "LOW"; break;
        case BAT_CRIT: battery_label = "CRIT"; break;
        case BAT_CHARGING: battery_label = "MED"; break;
        default: break;
    }
    cJSON_AddStringToObject(payload, "battery", battery_label);

    const char *mood = "IDLE";
    if (board_get_face_state() == FACE_HAPPY) mood = "HAPPY";
    cJSON_AddStringToObject(payload, "mood", mood);

    char *str = cJSON_PrintUnformatted(payload);
    cJSON_Delete(payload);
    if (!str) return false;

    *payload_out = str;
    return true;
}

bool cloud_client_send_heartbeat(cloud_command_t *out)
{
    if (!out) return false;
    memset(out, 0, sizeof(*out));

    char *payload = NULL;
    if (!build_heartbeat_payload(&payload)) {
        return false;
    }

    if (!board_wait_for_time_sync(60000)) {
        ESP_LOGE("CLOUD", "TIME SYNC NOT READY");
        free(payload);
        return false;
    }

    time_t now;
    time(&now);
    if (now < 1700000000) {
        ESP_LOGI("TIME", "Clock not synced, skip heartbeat");
        free(payload);
        return false;
    }
    ESP_LOGI("TIME", "SYNC OK");

    if (!debug_resolve_host()) {
        free(payload);
        return false;
    }
    ESP_LOGI("NET", "Connecting TLS");
    board_set_net_state(NET_CHECKING);

    esp_http_client_config_t config = {
        .url = HEARTBEAT_URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .timeout_ms = HEARTBEAT_TIMEOUT_MS,
        .disable_auto_redirect = true,
        .keep_alive_enable = true,
        .keep_alive_idle = 5000,
        .keep_alive_interval = 5000,
        .keep_alive_count = 3,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        ESP_LOGE(TAG, "FAIL: HTTP INIT");
        free(payload);
        return false;
    }


    ESP_LOGI("CLOUD", "Sending heartbeat");
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, payload, strlen(payload));

    bool success = false;
    static int64_t last_lock_log_ms = 0;
    bool locked = net_http_lock_take(HEARTBEAT_TIMEOUT_MS);
    if (!locked) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_lock_log_ms > 2000) {
            ESP_LOGW("CLOUD", "HTTP lock busy, skip heartbeat");
            last_lock_log_ms = now_ms;
        }
        esp_http_client_cleanup(client);
        free(payload);
        return false;
    }

    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        int status = esp_http_client_get_status_code(client);
        ESP_LOGI("CLOUD", "Heartbeat response %d", status);
        if (status == 200) {
            success = true;
            char buffer[256] = {0};
            int content_len = esp_http_client_read_response(client, buffer, sizeof(buffer) - 1);
            if (content_len > 0) {
                parse_command_payload(buffer, out);
            }
        } else {
            ESP_LOGE(TAG, "FAIL: HTTP RESPONSE %d", status);
        }
    } else {
        int sock_errno = esp_http_client_get_errno(client);
        ESP_LOGE("CLOUD", "FAIL: TCP/TLS CONNECT or SEND err=%s(%d) sock_errno=%d",
                 esp_err_to_name(err), (int)err, sock_errno);
    }

    net_http_lock_give();

    esp_http_client_cleanup(client);
    free(payload);

    return success;
}

esp_err_t cloud_client_chat_gradient(const char *prompt, char *answer_out, size_t max_len, chat_request_result_t *result)
{
    if (!prompt || !answer_out || max_len == 0 || !result) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "message", prompt);
    char *post_data = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!post_data) return ESP_ERR_NO_MEM;

    result->status_code = -1;
    result->bytes = 0;
    result->fail_stage = CHAT_FAIL_NONE;
    result->fail_errno = 0;
    result->fail_msg[0] = '\0';

    char *response_buffer = malloc(4096);
    if (!response_buffer) {
        result->fail_stage = CHAT_FAIL_HTTP;
        result->fail_errno = ESP_ERR_NO_MEM;
        snprintf(result->fail_msg, sizeof(result->fail_msg), "memory");
        strncpy(answer_out, "ERR:FAIL:OOM", max_len - 1);
        answer_out[max_len - 1] = '\0';
        free(post_data);
        return ESP_ERR_NO_MEM;
    }

    chat_http_capture_t capture = {
        .buffer = response_buffer,
        .capacity = 4096,
        .length = 0,
        .total_len = 0,
        .chunked = false,
        .connected = false,
        .data_events = 0,
    };

    esp_http_client_config_t config = {
        .url = CHAT_GRADIENT_URL,
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .timeout_ms = CHAT_TIMEOUT_MS,
        .keep_alive_enable = true,
        .keep_alive_idle = 5000,
        .keep_alive_interval = 5000,
        .keep_alive_count = 3,
        .event_handler = chat_http_event_handle,
        .user_data = &capture,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        result->fail_stage = CHAT_FAIL_TCP;
        result->fail_errno = ESP_FAIL;
        snprintf(result->fail_msg, sizeof(result->fail_msg), "http init");
        strncpy(answer_out, "ERR:FAIL", max_len - 1);
        answer_out[max_len - 1] = '\0';
        free(response_buffer);
        free(post_data);
        return ESP_FAIL;
    }

    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_post_field(client, post_data, strlen(post_data));

    size_t heap_before = esp_get_free_heap_size();
    ESP_LOGI(CHAT_TAG, "post_len=%d heap=%u", (int)strlen(post_data), (unsigned)heap_before);

    static int64_t last_lock_log_ms = 0;
    bool lock_acquired = net_http_lock_take(CHAT_TIMEOUT_MS);
    if (!lock_acquired) {
        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms - last_lock_log_ms > 2000) {
            ESP_LOGW(CHAT_TAG, "HTTP lock busy, skip chat");
            last_lock_log_ms = now_ms;
        }
        result->fail_stage = CHAT_FAIL_HTTP;
        result->fail_errno = ESP_ERR_INVALID_STATE;
        snprintf(result->fail_msg, sizeof(result->fail_msg), "lock busy");
        strncpy(answer_out, "ERR:BUSY", max_len - 1);
        answer_out[max_len - 1] = '\0';
        esp_http_client_cleanup(client);
        free(response_buffer);
        free(post_data);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_FAIL;
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int content_length = esp_http_client_get_content_length(client);
    bool chunked_resp = esp_http_client_is_chunked_response(client);
    int fallback_read = -1;

    if (err != ESP_OK) {
        int sock_errno = esp_http_client_get_errno(client);
        result->fail_stage = CHAT_FAIL_TCP;
        result->fail_errno = err;
        snprintf(result->fail_msg, sizeof(result->fail_msg), "%s sock=%d", esp_err_to_name(err), sock_errno);
        ESP_LOGE(CHAT_TAG, "fail stage=TCP err=%s(%d) sock_errno=%d", esp_err_to_name(err), (int)err, sock_errno);
        ret = err;
        goto cleanup;
    }

    if (capture.length == 0) {
        fallback_read = esp_http_client_read_response(client, response_buffer, 4095);
        if (fallback_read > 0) {
            capture.length = fallback_read;
            capture.total_len = fallback_read;
            response_buffer[fallback_read] = '\0';
        }
        ESP_LOGI(CHAT_TAG, "fallback read=%d cl=%d chunked=%d", fallback_read, content_length, chunked_resp);
    }

    ESP_LOGI(CHAT_TAG, "status=%d cl=%d chunked=%d captured=%u events=on_data:%d connected:%d",
             status,
             content_length,
             chunked_resp,
             (unsigned)capture.length,
             capture.data_events,
             capture.connected ? 1 : 0);

    result->status_code = status;
    result->bytes = capture.length;

    if (capture.length > 0) {
        ESP_LOGI(CHAT_TAG, "body_preview=\"%.*s\"", 80, capture.buffer);
    } else {
        ESP_LOGI(CHAT_TAG, "body_preview=<empty>");
    }

    if (status != 200) {
        result->fail_stage = CHAT_FAIL_HTTP;
        result->fail_errno = status;
        snprintf(result->fail_msg, sizeof(result->fail_msg), "HTTP %d", status);
        snprintf(answer_out, max_len, "ERR:HTTP:%d", status);
        answer_out[max_len - 1] = '\0';
        ret = ESP_FAIL;
        goto cleanup;
    }

    if (capture.length <= 0) {
        result->fail_stage = CHAT_FAIL_JSON;
        result->fail_errno = 264;
        snprintf(result->fail_msg, sizeof(result->fail_msg), "empty");
        strncpy(answer_out, "ERR:NO_ANSWER", max_len - 1);
        answer_out[max_len - 1] = '\0';
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    cJSON *res_root = cJSON_Parse(capture.buffer);
    if (!res_root) {
        result->fail_stage = CHAT_FAIL_JSON;
        result->fail_errno = ESP_ERR_INVALID_RESPONSE;
        snprintf(result->fail_msg, sizeof(result->fail_msg), "json");
        strncpy(answer_out, "ERR:JSON", max_len - 1);
        answer_out[max_len - 1] = '\0';
        ESP_LOGE(CHAT_TAG, "parse fail body=\"%.*s\"", 300, capture.buffer);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    cJSON *ans = cJSON_GetObjectItemCaseSensitive(res_root, "answer");
    if (!cJSON_IsString(ans) || !ans->valuestring || ans->valuestring[0] == '\0') {
        cJSON *data = cJSON_GetObjectItemCaseSensitive(res_root, "data");
        if (cJSON_IsObject(data)) {
            ans = cJSON_GetObjectItemCaseSensitive(data, "answer");
        }
    }

    if (!cJSON_IsString(ans) || !ans->valuestring || ans->valuestring[0] == '\0') {
        result->fail_stage = CHAT_FAIL_JSON;
        result->fail_errno = 264;
        snprintf(result->fail_msg, sizeof(result->fail_msg), "empty");
        strncpy(answer_out, "ERR:NO_ANSWER", max_len - 1);
        answer_out[max_len - 1] = '\0';
        ESP_LOGE(CHAT_TAG, "parse fail body=\"%.*s\"", 300, capture.buffer);
        cJSON_Delete(res_root);
        ret = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }

    strlcpy(answer_out, ans->valuestring, max_len);
    ESP_LOGI(CHAT_TAG, "answer_len=%d", (int)strlen(answer_out));
    result->fail_stage = CHAT_FAIL_NONE;
    result->fail_errno = 0;
    result->fail_msg[0] = '\0';
    cJSON_Delete(res_root);
    ret = ESP_OK;

cleanup:
    if (lock_acquired) {
        net_http_lock_give();
    }
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    free(response_buffer);
    free(post_data);
    return ret;
}

__attribute__((unused)) static void heartbeat_task(void *arg)
{
    (void)arg;
    cloud_command_t cmd;
    while (true) {
        if (tts_client_is_busy()) {
            ESP_LOGI(TAG, "Heartbeat deferred because audio is busy");
        } else if (!board_can_network()) {
            ESP_LOGI("CLOUD", "WIFI or NET not ready, skip heartbeat");
        } else {
            ESP_LOGI(TAG, "Network ready, sending heartbeat");
            if (cloud_client_send_heartbeat(&cmd)) {
                ESP_LOGI(TAG, "HEARTBEAT SUCCESS");
                command_dispatcher_handle(&cmd);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(HEARTBEAT_INTERVAL_MS));
    }
}

void cloud_client_init(void)
{
    net_http_lock_init();
    ESP_LOGI(TAG, "heartbeat disabled (chat+tts focus)");
}

esp_err_t cloud_client_tts_azure(const char *text)
{
    if (!text || text[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (!board_can_network()) return ESP_ERR_INVALID_STATE;

    board_set_ai_state(AI_THINKING);
    return tts_client_speak_async(text);
}

esp_err_t cloud_client_tts_azure_stream_play(const char *text, tts_stream_result_t *result)
{
    if (!text || text[0] == '\0') return ESP_ERR_INVALID_ARG;
    if (!board_can_network()) return ESP_ERR_INVALID_STATE;
    return tts_client_stream_play(text, result);
}
