#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "driver/i2s_std.h"
#include <math.h>
#include "esp_log.h"
#include "esp_random.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "lwip/sockets.h"
#include "lwip/inet.h"
#include <arpa/inet.h>
#include "lwip/dns.h"
#include <errno.h>
#include <time.h>
#include <netdb.h>
#include <ctype.h>
#include "esp_http_client.h"
#include "wake_word_runtime.h"
#include "esp_tls.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"
#include "cloud_client.h"
#include "command_dispatcher.h"
#include "board_status.h"
#include "audio_player.h"
#include "tts_client.h"
#include "wake_word_model.h"
#include "wake_word_runtime.h"

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  160

#define CHAT_RECT_X 4
#define CHAT_RECT_Y 130
#define CHAT_RECT_W (SCREEN_WIDTH - 8)
#define CHAT_RECT_H 26
#define AI_RECT_X 4
#define AI_RECT_Y 104
#define AI_RECT_W (SCREEN_WIDTH - 8)
#define AI_RECT_H 52
#define AI_TEXT_MAX 160

#define TFT_CS       7
#define TFT_DC      10
#define TFT_RST     21
#define TFT_SCK      8
#define TFT_MOSI     9
#define PIN_TOUCH    0
#if CONFIG_IDF_TARGET_ESP32C3
#define MIC_I2S_PORT I2S_NUM_0
#else
#define MIC_I2S_PORT I2S_NUM_1
#endif
#define MIC_I2S_WS   1
#define MIC_I2S_SCK  4
#define MIC_I2S_SD   2

#define WIFI_SSID "rara"
#define WIFI_PASS "123456788"

#define MAX_RECONNECT_ATTEMPT 3
#define MIN_RECONNECT_INTERVAL_MS 10000   // 10 detik
#define WIFI_STABLE_DELAY_MS 5000

static i2s_chan_handle_t g_rx_handle = NULL;
static bool g_mic_ready = false;

#define NET_CHECK_INTERVAL_MS 30000
// End-to-end budget for DNS + TCP connect (keep non-blocking behavior)
#define HTTP_TIMEOUT_MS 5000
#define NET_CHECK_HOST "rbots.dev"
#define NET_CHECK_PORT 443

#define NETWORK_LATCH_MS 60000
#define CHAT_DISPLAY_MAX_CHARS 200

#define MAX_RECONNECT_BOOT 10
#define MAX_RECONNECT_RUNTIME 3

static const char *TAG_STATE = "STATE";
static const char *TAG_FEATURE = "FEATURE";
static const char *TAG_FAULT = "FAULT";

static spi_device_handle_t s_tft = NULL;
static face_state_t g_current_face = FACE_NEUTRAL;
static blink_state_t g_blink_state = BLINK_OPEN;

typedef struct {
    blink_state_t blink;
    face_state_t mood;
    int8_t off_x;
    int8_t off_y;
    int8_t off_size;
} face_render_state_t;

static uint32_t random_delay_ticks(void)
{
    return pdMS_TO_TICKS(150 + (esp_random() % 151));
}

typedef enum {
    WIFI_PHASE_BOOT,
    WIFI_PHASE_RUNTIME
} wifi_phase_t;

static volatile wifi_status_t g_wifi_status = WIFI_OFF;
static volatile bool g_wifi_dirty = true;
static bool g_wifi_anim_phase = false;
static uint32_t g_wifi_anim_last = 0;
static uint8_t g_wifi_retry_count = 0;
static int64_t g_wifi_last_retry_ms = 0;
static esp_timer_handle_t g_wifi_reconnect_timer = NULL;
static bool g_wifi_reconnect_pending = false;
static void wifi_reconnect_timer_cb(void *arg);
static void wifi_schedule_reconnect(uint32_t delay_ms);
static wifi_phase_t g_wifi_phase = WIFI_PHASE_BOOT;
static uint32_t g_wifi_connected_time = 0;
static void wifi_set_status(wifi_status_t status);
static void net_abort_check(void);

static volatile net_state_t g_net_state = NET_UNKNOWN;
static volatile bool g_net_dirty = true;
static bool g_net_anim_phase = false;
static uint32_t g_net_anim_last = 0;
static int64_t g_net_last_check_ms = 0;
static bool g_net_force_check = false;
static int g_net_sock = -1;
static int64_t g_net_check_start_ms = 0;
static int64_t g_net_last_success_ms = 0;

// Non-blocking DNS + TCP reachability (for https://rbots.dev/...) 
static ip_addr_t g_net_ip;
static bool g_net_ip_valid = false;
static bool g_net_dns_pending = false;
static bool g_net_connected = false;
typedef enum {
    NET_FAIL_NONE,
    NET_FAIL_DNS,
    NET_FAIL_TCP,
    NET_FAIL_TLS,
    NET_FAIL_HTTP
} net_fail_stage_t;
static net_fail_stage_t g_net_fail_stage = NET_FAIL_NONE;
static int g_net_fail_code = 0;
static int g_net_fail_errno = 0;
static char g_net_ip_str[INET_ADDRSTRLEN] = {0};
static bool g_net_dns_ok_logged = false;
static bool g_net_tcp_connect_logged = false;
static bool g_net_tcp_ok_logged = false;
static bool g_net_tls_start_logged = false;

static volatile battery_state_t g_battery_state = BAT_MED;
static volatile bool g_battery_dirty = true;
static bool g_battery_anim_phase = false;
static uint32_t g_battery_anim_last = 0;
static bool g_battery_initialized = false;
static uint32_t g_battery_boot_ticks = 0;
static volatile face_state_t g_requested_face = FACE_NEUTRAL;
static volatile bool g_requested_face_active = false;
static bool g_time_sync_started = false;
static bool g_time_synced = false;

static char g_pending_chat_prompt[128] = {0};
static bool g_chat_prompt_active = false;
static char g_ui_chat_answer[256] = {0};
static chat_state_t g_chat_state = CHAT_IDLE;

static ai_state_t g_ai_state = AI_IDLE;
static char g_ai_text[AI_TEXT_MAX] = {0};
static int g_ai_ttl_ms = 0;
static int64_t g_ai_expire_ms = 0;
static bool g_ai_dirty = true;
static bool g_ai_anim_phase = false;
static uint32_t g_ai_anim_last = 0;

// Auto-send hello (once per boot)
static bool g_autosend_hello_sent = false;
static bool g_ai_agent_started = false;

typedef enum {
    PIPE_IDLE = 0,
    PIPE_LISTENING,
    PIPE_STT,
    PIPE_CHAT,
    PIPE_TTS,
    PIPE_ERROR
} pipe_stage_t;

static bool g_pipeline_active = false;
static pipe_stage_t g_pipe_stage = PIPE_IDLE;
static char g_pipe_stt_text[256] = {0};
static char g_pipe_answer[256] = {0};
static TaskHandle_t g_pipe_task = NULL;
static int16_t *g_audio_buf = NULL;
static size_t g_audio_buf_samples = 0;

face_state_t board_get_face_state(void)
{
    return g_current_face;
}

void board_set_face_state(face_state_t face)
{
    g_current_face = face;
}

void board_request_face_state(face_state_t face)
{
    g_requested_face = face;
    g_requested_face_active = true;
}

bool board_consume_requested_face_state(face_state_t *face)
{
    if (!g_requested_face_active) return false;
    if (face) *face = g_requested_face;
    g_requested_face_active = false;
    return true;
}

static void initialize_sntp(void)
{
    if (g_time_sync_started) return;
    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    g_time_sync_started = true;
}

bool board_wait_for_time_sync(uint32_t timeout_ms)
{
    initialize_sntp();
    if (g_time_synced) return true;

    time_t now = 0;
    time(&now);
    if (now >= 1609459200) {
        g_time_synced = true;
        return true;
    }

    uint32_t start = esp_timer_get_time() / 1000;
    while ((esp_timer_get_time() / 1000 - start) < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(500));
        time(&now);
        if (now >= 1609459200) {
            g_time_synced = true;
            return true;
        }
    }

    return g_time_synced;
}

bool board_wifi_is_stable(void)
{
    if (g_wifi_status == WIFI_CONNECTED_STABLE) return true;
    if (g_wifi_status != WIFI_CONNECTED) return false;
    if (g_wifi_connected_time == 0) return false;
    if ((xTaskGetTickCount() - g_wifi_connected_time) >= pdMS_TO_TICKS(WIFI_STABLE_DELAY_MS)) {
        wifi_set_status(WIFI_CONNECTED_STABLE);
        return true;
    }
    return false;
}

bool board_wifi_in_runtime_phase(void)
{
    return g_wifi_phase == WIFI_PHASE_RUNTIME;
}

wifi_status_t board_get_wifi_status(void)
{
    return g_wifi_status;
}

bool board_can_network(void)
{
    wifi_status_t ws = board_get_wifi_status();
    bool wifi_ok = (ws == WIFI_CONNECTED || ws == WIFI_CONNECTED_STABLE);
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool net_ok = (g_net_state == NET_ONLINE) ||
                  (g_net_last_success_ms != 0 && (now_ms - g_net_last_success_ms) < NETWORK_LATCH_MS);
    
    static bool last_result = true;
    bool result = wifi_ok && net_ok;
    
    if (result != last_result) {
        if (!result) {
            ESP_LOGW("NET", "Network requests BLOCKED (Wifi:%d, Net:%d)", ws, g_net_state);
        } else {
            ESP_LOGI("NET", "Network requests ALLOWED");
        }
        last_result = result;
    }
    
    return result;
}

battery_state_t board_get_battery_state(void)
{
    return g_battery_state;
}

static inline uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static void chat_prepare_text(char *dst, const char *src);

static void net_set_state(net_state_t state)
{
    if (g_net_state == state) return;
    g_net_state = state;
    g_net_dirty = true;

    switch (state) {
        case NET_CHECKING:
            ESP_LOGI("NET", "CHECKING");
            break;
        case NET_WIFI_ONLY:
            ESP_LOGI("NET", "WIFI ONLY");
            break;
        case NET_ONLINE:
            ESP_LOGI("NET", "ONLINE");
            g_net_last_success_ms = esp_timer_get_time() / 1000;
            break;
        case NET_OFFLINE:
            if (g_net_fail_stage == NET_FAIL_NONE) {
                ESP_LOGE("NET", "OFFLINE");
            }
            break;
        default:
            break;
    }
}

static void net_store_ip_string(void)
{
    if (!g_net_ip_valid || !IP_IS_V4(&g_net_ip)) {
        g_net_ip_str[0] = '\0';
        return;
    }

    struct in_addr in = {.s_addr = ip_2_ip4(&g_net_ip)->addr};
    inet_ntop(AF_INET, &in, g_net_ip_str, sizeof(g_net_ip_str));
}

static void net_log_dns_ok(void)
{
    if (g_net_dns_ok_logged) return;
    net_store_ip_string();
    if (g_net_ip_str[0] == '\0') return;
    ESP_LOGI("NET", "DNS ok ip=%s", g_net_ip_str);
    g_net_dns_ok_logged = true;
}

static void net_log_tcp_ok(void)
{
    if (g_net_tcp_ok_logged) return;
    ESP_LOGI("NET", "TCP ok");
    g_net_tcp_ok_logged = true;
}

static void net_log_tls_start(void)
{
    if (g_net_tls_start_logged) return;
    ESP_LOGI("NET", "TLS start host=%s", NET_CHECK_HOST);
    g_net_tls_start_logged = true;
}

static void net_log_reset(void)
{
    g_net_dns_ok_logged = false;
    g_net_tcp_connect_logged = false;
    g_net_tcp_ok_logged = false;
    g_net_tls_start_logged = false;
    g_net_ip_str[0] = '\0';
}

static void net_fail_set(net_fail_stage_t stage, int code, int errno_copy)
{
    g_net_fail_stage = stage;
    g_net_fail_code = code;
    g_net_fail_errno = errno_copy;
    net_set_state(NET_OFFLINE);

    switch (stage) {
        case NET_FAIL_DNS:
            ESP_LOGE("NET", "OFFLINE reason=DNS_FAIL code=%d", code);
            break;
        case NET_FAIL_TCP: {
            const char *msg = strerror(errno_copy);
            ESP_LOGE("NET", "OFFLINE reason=TCP_FAIL errno=%d msg=%s", errno_copy, msg ? msg : "?");
            break;
        }
        case NET_FAIL_TLS:
            ESP_LOGE("NET", "OFFLINE reason=TLS_FAIL esp_err=%d", code);
            break;
        case NET_FAIL_HTTP:
            ESP_LOGE("NET", "OFFLINE reason=HTTP_FAIL esp_err=%d", code);
            break;
        default:
            ESP_LOGE("NET", "OFFLINE reason=UNKNOWN");
            break;
    }

    g_net_fail_stage = NET_FAIL_NONE;
    g_net_fail_code = 0;
    g_net_fail_errno = 0;
    net_log_reset();
}

static void net_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    (void)name;
    (void)callback_arg;
    if (ipaddr == NULL) {
        g_net_ip_valid = false;
        g_net_dns_pending = false;
        net_fail_set(NET_FAIL_DNS, 0, 0);
        net_abort_check();
        return;
    }
    if (IP_IS_V4(ipaddr)) {
        g_net_ip = *ipaddr;
        g_net_ip_valid = true;
        g_net_dns_pending = false;
        net_log_dns_ok();
    } else {
        g_net_ip_valid = false;
        g_net_dns_pending = false;
    }
}


void board_set_net_state(net_state_t state)
{
    net_set_state(state);
}

void board_submit_chat_prompt(const char *text)
{
    if (!text) return;
    strncpy(g_pending_chat_prompt, text, sizeof(g_pending_chat_prompt) - 1);
    g_pending_chat_prompt[sizeof(g_pending_chat_prompt) - 1] = '\0';
    g_chat_prompt_active = true;
    size_t text_len = strlen(text);
    size_t preview_len = (text_len > 40) ? 40 : text_len;
    char preview[41];
    memcpy(preview, text, preview_len);
    preview[preview_len] = '\0';
    ESP_LOGI("CHAT", "start msg=\"%s%s\"", preview, (text_len > 40) ? "..." : "");
}

bool board_consume_chat_prompt(char *out, size_t max_len)
{
    if (!g_chat_prompt_active) return false;
    if (out) {
        strncpy(out, g_pending_chat_prompt, max_len - 1);
        out[max_len - 1] = '\0';
    }
    g_chat_prompt_active = false;
    return true;
}

ai_state_t board_get_ai_state(void)
{
    return g_ai_state;
}

void board_set_ai_state(ai_state_t s)
{
    if (g_ai_state == s) return;
    g_ai_state = s;
    g_ai_dirty = true;

    switch (s) {
        case AI_IDLE:
            ESP_LOGI("AIUI", "state=A0_IDLE");
            g_ai_expire_ms = 0;
            g_ai_ttl_ms = 0;
            g_ai_text[0] = '\0';
            break;
        case AI_LISTENING:
            ESP_LOGI("AIUI", "state=A1_LISTENING");
            break;
        case AI_THINKING:
            ESP_LOGI("AIUI", "state=A2_THINKING");
            break;
        case AI_ANSWERING:
            ESP_LOGI("AIUI", "state=A3_ANSWERING");
            break;
        case AI_ERROR:
            ESP_LOGI("AIUI", "state=A4_ERROR");
            break;
        default:
            break;
    }
}

void board_set_ai_text(const char *text, int ttl_ms)
{
    if (text) {
        chat_prepare_text(g_ai_text, text);
    } else {
        g_ai_text[0] = '\0';
    }

    g_ai_ttl_ms = ttl_ms > 0 ? ttl_ms : 0;
    if (g_ai_ttl_ms > 0) {
        g_ai_expire_ms = (esp_timer_get_time() / 1000) + g_ai_ttl_ms;
    } else {
        g_ai_expire_ms = 0;
    }

    g_ai_dirty = true;

    if (g_ai_state == AI_ANSWERING) {
        int len = (int)strnlen(g_ai_text, sizeof(g_ai_text));
        ESP_LOGI("AIUI", "answer_len=%d ttl=%d", len, g_ai_ttl_ms);
    }
}

void board_clear_ai_text(void)
{
    g_ai_text[0] = '\0';
    g_ai_ttl_ms = 0;
    g_ai_expire_ms = 0;
    if (g_ai_state != AI_IDLE) {
        g_ai_state = AI_IDLE;
        ESP_LOGI("AIUI", "state=A0_IDLE");
    }
    g_ai_dirty = true;
}

// -------- Audio Capture (INMP441) --------

static void mic_release(void)
{
    if (g_rx_handle) {
        i2s_channel_disable(g_rx_handle);
        i2s_del_channel(g_rx_handle);
        g_rx_handle = NULL;
    }
    g_mic_ready = false;
}

static esp_err_t mic_init(void)
{
    if (g_mic_ready) return ESP_OK;

    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num = 2;
    chan_cfg.dma_frame_num = 120;

    esp_err_t err = i2s_new_channel(&chan_cfg, NULL, &g_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE("MIC", "i2s_new_channel fail %d", err);
        return err;
    }

    i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(16000),
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

    err = i2s_channel_init_std_mode(g_rx_handle, &std_cfg);
    if (err != ESP_OK) {
        ESP_LOGE("MIC", "i2s_init_std fail %d", err);
        mic_release();
        return err;
    }

    err = i2s_channel_enable(g_rx_handle);
    if (err != ESP_OK) {
        ESP_LOGE("MIC", "i2s_enable fail %d", err);
        mic_release();
        return err;
    }

    g_mic_ready = true;
    return ESP_OK;
}

static esp_err_t mic_capture(int16_t *buf, size_t samples, size_t *out_bytes, uint32_t *dur_ms)
{
    if (!buf || samples == 0) return ESP_ERR_INVALID_ARG;
    esp_err_t err = mic_init();
    if (err != ESP_OK) return err;

    size_t target_bytes = samples * sizeof(int16_t);
    size_t filled_bytes = 0;
    int64_t start_us = esp_timer_get_time();

    int32_t raw_buffer[64];
    size_t bytes_read = 0;
    
    int16_t max_peak = 0;
    double rms_sum = 0;
    int samples_count = 0;

    while (filled_bytes < target_bytes) {
        esp_err_t err_read = i2s_channel_read(g_rx_handle, raw_buffer, sizeof(raw_buffer), &bytes_read, pdMS_TO_TICKS(200));
        if (err_read != ESP_OK) {
             ESP_LOGW("MIC", "Read fail 0x%x", err_read);
             break;
        }

        size_t int32_count = bytes_read / sizeof(int32_t);

        // Stereo: L, R, L, R...
        for (size_t i = 0; i < int32_count; i += 2) {
             if (filled_bytes >= target_bytes) break;

             // Use Left channel (i). Skip Right (i+1).
             int32_t val = raw_buffer[i];
             // Shift >> 14 for 24-bit INMP441 in 32-bit slot
             int16_t s = (int16_t)(val >> 14);
             
             buf[filled_bytes / 2] = s;
             filled_bytes += 2;
             
             if (abs(s) > max_peak) max_peak = abs(s);
             rms_sum += (double)s * s;
             samples_count++;
        }
    }
    
    int16_t rms = 0;
    if (samples_count > 0) rms = (int16_t)sqrt(rms_sum / samples_count);

    ESP_LOGI("MIC", "Read %d bytes. Peak: %d, RMS: %d", (int)filled_bytes, max_peak, rms);
    
    if (filled_bytes >= 20) {
        ESP_LOGI("MIC", "Samples: %d %d %d %d %d %d %d %d %d %d", 
           buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7], buf[8], buf[9]);
    }

    if (out_bytes) *out_bytes = filled_bytes;
    if (dur_ms) {
        *dur_ms = (uint32_t)((esp_timer_get_time() - start_us) / 1000);
    }
    
    mic_release();
    return (filled_bytes > 0) ? ESP_OK : ESP_FAIL;
}

// -------- STT Client --------
typedef enum {
    STT_FAIL_NONE = 0,
    STT_FAIL_HTTP,
    STT_FAIL_JSON,
    STT_FAIL_EMPTY
} stt_fail_stage_t;

typedef struct {
    int status_code;
    int bytes;
    stt_fail_stage_t fail_stage;
    int fail_errno;
    char fail_msg[64];
} stt_request_result_t;

static bool stt_send_buffer(const int16_t *pcm, size_t samples, char *text_out, size_t text_out_len, stt_request_result_t *res)
{
    if (res) {
        memset(res, 0, sizeof(*res));
        res->status_code = -1;
        res->fail_stage = STT_FAIL_NONE;
    }
    if (!pcm || samples == 0 || !text_out || text_out_len == 0) return false;

    const size_t data_bytes = samples * sizeof(int16_t);
    const size_t wav_header = 44;
    const size_t total_len = wav_header + data_bytes;

    uint8_t header[44] = {0};
    memcpy(header, "RIFF", 4);
    uint32_t chunk_size = (uint32_t)(total_len - 8);
    header[4] = chunk_size & 0xFF;
    header[5] = (chunk_size >> 8) & 0xFF;
    header[6] = (chunk_size >> 16) & 0xFF;
    header[7] = (chunk_size >> 24) & 0xFF;
    memcpy(header + 8, "WAVEfmt ", 8);
    header[16] = 16; // PCM fmt chunk size
    header[20] = 1; header[21] = 0; // PCM
    header[22] = 1; header[23] = 0; // mono
    header[24] = 0x80; header[25] = 0x3E; // 16000
    header[26] = 0x00; header[27] = 0x00;
    uint32_t byte_rate = 16000 * 2;
    header[28] = byte_rate & 0xFF;
    header[29] = (byte_rate >> 8) & 0xFF;
    header[30] = (byte_rate >> 16) & 0xFF;
    header[31] = (byte_rate >> 24) & 0xFF;
    header[32] = 2; header[33] = 0; // block align
    header[34] = 16; header[35] = 0; // bits
    memcpy(header + 36, "data", 4);
    header[40] = data_bytes & 0xFF;
    header[41] = (data_bytes >> 8) & 0xFF;
    header[42] = (data_bytes >> 16) & 0xFF;
    header[43] = (data_bytes >> 24) & 0xFF;

    esp_http_client_config_t cfg = {
        .url = "https://rbots.dev/speech-to-text",
        .method = HTTP_METHOD_POST,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .transport_type = HTTP_TRANSPORT_OVER_SSL,
        .timeout_ms = 20000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        if (res) {
            res->fail_stage = STT_FAIL_HTTP;
            res->fail_errno = ESP_FAIL;
        }
        return false;
    }

    esp_http_client_set_header(client, "Content-Type", "audio/wav; codecs=audio/pcm; samplerate=16000");

    bool lock = net_http_lock_take(20000);
    if (!lock) {
        esp_http_client_cleanup(client);
        if (res) {
            res->fail_stage = STT_FAIL_HTTP;
            res->fail_errno = ESP_ERR_INVALID_STATE;
            snprintf(res->fail_msg, sizeof(res->fail_msg), "lock busy");
        }
        return false;
    }

    ESP_LOGI("STT", "dns host=rbots.dev");

    esp_err_t err = esp_http_client_open(client, total_len);
    if (err != ESP_OK) {
        if (res) {
            res->fail_stage = STT_FAIL_HTTP;
            res->fail_errno = err;
        }
        net_http_lock_give();
        esp_http_client_cleanup(client);
        ESP_LOGE("STT", "tcp/tls open fail err=%d", err);
        return false;
    }
    ESP_LOGI("STT", "tcp ok");
    ESP_LOGI("STT", "tls ok");

    int written = esp_http_client_write(client, (const char *)header, wav_header);
    if (written != (int)wav_header) {
        net_http_lock_give();
        esp_http_client_cleanup(client);
        if (res) {
            res->fail_stage = STT_FAIL_HTTP;
            res->fail_errno = ESP_FAIL;
        }
        ESP_LOGE("STT", "write header fail");
        return false;
    }

    const uint8_t *pcm_bytes = (const uint8_t *)pcm;
    size_t remaining = data_bytes;
    while (remaining > 0) {
        size_t chunk = remaining > 2048 ? 2048 : remaining;
        int w = esp_http_client_write(client, (const char *)pcm_bytes + (data_bytes - remaining), chunk);
        if (w <= 0) {
            net_http_lock_give();
            esp_http_client_cleanup(client);
            if (res) {
                res->fail_stage = STT_FAIL_HTTP;
                res->fail_errno = ESP_FAIL;
            }
            ESP_LOGE("STT", "write data fail");
            return false;
        }
        remaining -= (size_t)w;
    }

    int64_t cl = esp_http_client_fetch_headers(client);
    int status = esp_http_client_get_status_code(client);
    if (res) res->status_code = status;
    ESP_LOGI("STT", "http status=%d cl=%lld", status, (long long)cl);

    char resp[512] = {0};
    int read = esp_http_client_read_response(client, resp, sizeof(resp) - 1);
    if (read < 0) read = 0;
    resp[read] = '\0';
    ESP_LOGI("STT", "body_len=%d", read);
    ESP_LOGI("STT", "body_preview=\"%.*s\"", read > 120 ? 120 : read, resp);
    
    if (status != 200) {
        int sock_errno = esp_http_client_get_errno(client);
        if (res) {
            res->fail_stage = STT_FAIL_HTTP;
            res->fail_errno = status;
            snprintf(res->fail_msg, sizeof(res->fail_msg), "status %d body=%.*s", status, read > 80 ? 80 : read, resp);
        }
        ESP_LOGE("STT", "HTTP status=%d sock_errno=%d", status, sock_errno);
        ESP_LOGE("PIPE", "stage=S2_STT fail reason=\"http_%d\" status=%d sock_errno=%d", status, status, sock_errno);
        net_http_lock_give();
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        return false;
    }

    cJSON *root = cJSON_Parse(resp);
    if (!root) {
        if (res) {
            res->fail_stage = STT_FAIL_JSON;
            res->fail_errno = ESP_FAIL;
        }
        net_http_lock_give();
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ESP_LOGE("STT", "json parse fail");
        return false;
    }
    const cJSON *text = cJSON_GetObjectItem(root, "text");
    const char *val = cJSON_IsString(text) ? text->valuestring : NULL;
    if (!val || val[0] == '\0') {
        if (res) {
            res->fail_stage = STT_FAIL_EMPTY;
            res->fail_errno = ESP_FAIL;
        }
        cJSON_Delete(root);
        net_http_lock_give();
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
        ESP_LOGE("STT", "missing field 'text' or empty");
        ESP_LOGE("PIPE", "stage=S2_STT fail reason=\"missing_text\" json=\"%.*s\"", read > 100 ? 100 : read, resp);
        return false;
    }
    strncpy(text_out, val, text_out_len - 1);
    text_out[text_out_len - 1] = '\0';
    cJSON_Delete(root);
    net_http_lock_give();
    esp_http_client_close(client);
    esp_http_client_cleanup(client);
    // Text logging moved to pipe_task after this function returns
    return true;
}

float battery_get_voltage(void)
{
    return 3.9f;
}

static bool battery_is_charging(void)
{
    return true;
}

static battery_state_t battery_get_state(void)
{
    if (battery_is_charging()) return BAT_CHARGING;

    float v = battery_get_voltage();
    if (v >= 4.0f) return BAT_FULL;
    if (v >= 3.7f) return BAT_MED;
    if (v >= 3.4f) return BAT_LOW;
    if (v < 3.3f) return BAT_CRIT;
    return BAT_LOW;
}

static void battery_set_state(battery_state_t state)
{
    if (g_battery_state == state) return;
    g_battery_state = state;
    g_battery_dirty = true;

    switch (state) {
        case BAT_FULL:
            ESP_LOGI("BAT", "FULL");
            break;
        case BAT_MED:
            ESP_LOGI("BAT", "MED");
            break;
        case BAT_LOW:
            ESP_LOGI("BAT", "LOW");
            break;
        case BAT_CRIT:
            ESP_LOGI("BAT", "CRITICAL");
            break;
        case BAT_CHARGING:
            ESP_LOGI("BAT", "CHARGING");
            break;
        default:
            break;
    }
}

static void wifi_set_status(wifi_status_t status)
{
    if (g_wifi_status == status) return;
    g_wifi_status = status;
    g_wifi_dirty = true;

    switch (status) {
        case WIFI_OFF:
            ESP_LOGI("WIFI", "OFF");
            break;
        case WIFI_CONNECTING:
            ESP_LOGI("WIFI", "CONNECTING");
            break;
        case WIFI_CONNECTED:
            ESP_LOGI("WIFI", "CONNECTED (waiting for stable)");
            break;
        case WIFI_CONNECTED_STABLE:
            ESP_LOGI("WIFI", "CONNECTED STABLE");
            break;
        case WIFI_ERROR:
            ESP_LOGE("WIFI", "ERROR");
            net_set_state(NET_UNKNOWN); // Changed from NET_OFFLINE as per Task B
            break;
        default:
            break;
    }
}

static void wifi_reconnect_timer_cb(void *arg)
{
    (void)arg;
    g_wifi_reconnect_pending = false;

    // If we already recovered, do nothing.
    if (g_wifi_status == WIFI_CONNECTED || g_wifi_status == WIFI_CONNECTED_STABLE) {
        return;
    }

    int max_retry = (g_wifi_phase == WIFI_PHASE_RUNTIME) ? MAX_RECONNECT_RUNTIME : MAX_RECONNECT_BOOT;
    if (g_wifi_retry_count >= max_retry) {
        if (g_wifi_phase == WIFI_PHASE_RUNTIME) {
            wifi_set_status(WIFI_ERROR);
            ESP_LOGE("WIFI", "GIVE UP");
        } else {
            ESP_LOGW("WIFI", "BOOT retry exhausted, waiting");
        }
        return;
    }

    // Attempt reconnect now (this is the only place we call connect when delayed)
    g_wifi_last_retry_ms = esp_timer_get_time() / 1000;
    g_wifi_retry_count++;
    ESP_LOGI("WIFI", "RETRY %d", (int)g_wifi_retry_count);
    wifi_set_status(WIFI_CONNECTING);
    esp_wifi_connect();
}

static void wifi_schedule_reconnect(uint32_t delay_ms)
{
    if (g_wifi_reconnect_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = &wifi_reconnect_timer_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_reconn"
        };
        (void)esp_timer_create(&args, &g_wifi_reconnect_timer);
    }

    if (g_wifi_reconnect_timer == NULL) {
        // If timer creation failed, fall back to immediate retry path.
        wifi_reconnect_timer_cb(NULL);
        return;
    }

    // Replace any pending schedule with the new one.
    if (g_wifi_reconnect_pending) {
        (void)esp_timer_stop(g_wifi_reconnect_timer);
    }
    g_wifi_reconnect_pending = true;
    (void)esp_timer_start_once(g_wifi_reconnect_timer, (uint64_t)delay_ms * 1000ULL);
}

static void net_abort_check(void)
{
    if (g_net_sock >= 0) {
        close(g_net_sock);
        g_net_sock = -1;
    }
    g_net_check_start_ms = 0;
    g_net_force_check = false;
    g_net_connected = false;
    g_net_dns_pending = false;
    // keep g_net_ip_valid as a cache; it will be refreshed on failure/when needed
}

static esp_err_t st7735_transmit(const uint8_t *data, size_t len, bool is_data)
{
    assert(s_tft != NULL);
    assert(len > 0);

    gpio_set_level(TFT_DC, is_data ? 1 : 0);
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data
    };

    return spi_device_transmit(s_tft, &trans);
}

static esp_err_t st7735_send_command(uint8_t command)
{
    return st7735_transmit(&command, 1, false);
}

static esp_err_t st7735_send_data(const uint8_t *data, size_t len)
{
    return st7735_transmit(data, len, true);
}

static void tft_set_addr_window(uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    const uint8_t col_data[] = {0x00, x0, 0x00, x1};
    const uint8_t row_data[] = {0x00, y0, 0x00, y1};

    st7735_send_command(0x2A);
    st7735_send_data(col_data, sizeof(col_data));
    st7735_send_command(0x2B);
    st7735_send_data(row_data, sizeof(row_data));
    st7735_send_command(0x2C);
}

static void tft_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;

    tft_set_addr_window(x, y, x + w - 1, y + h - 1);

    uint8_t px[2] = {(uint8_t)(color >> 8), (uint8_t)(color & 0xFF)};
    size_t total = w * h;
    
    // Use a larger buffer for faster fills
    const size_t buf_size = 256;
    uint8_t line_buf[512]; // Stack allocated to avoid OOM/fragmentation
    for(int i=0; i<buf_size; i++) {
        line_buf[i*2] = px[0];
        line_buf[i*2+1] = px[1];
    }

    while(total > 0) {
        size_t batch = (total > buf_size) ? buf_size : total;
        st7735_send_data(line_buf, batch * 2);
        total -= batch;
    }
}

static void tft_fill_screen(uint16_t color)
{
    tft_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, color);
}

static const uint8_t chat_font_alnum[36][5] = {
    {0b010,0b101,0b111,0b101,0b101}, // A
    {0b110,0b101,0b110,0b101,0b110}, // B
    {0b111,0b100,0b100,0b100,0b111}, // C
    {0b110,0b101,0b101,0b101,0b110}, // D
    {0b111,0b100,0b110,0b100,0b111}, // E
    {0b111,0b100,0b110,0b100,0b100}, // F
    {0b111,0b100,0b101,0b101,0b111}, // G
    {0b101,0b101,0b111,0b101,0b101}, // H
    {0b111,0b010,0b010,0b010,0b111}, // I
    {0b111,0b010,0b010,0b110,0b010}, // J
    {0b101,0b101,0b110,0b101,0b101}, // K
    {0b100,0b100,0b100,0b100,0b111}, // L
    {0b101,0b111,0b111,0b101,0b101}, // M
    {0b101,0b111,0b111,0b111,0b101}, // N
    {0b111,0b101,0b101,0b101,0b111}, // O
    {0b111,0b101,0b111,0b100,0b100}, // P
    {0b111,0b101,0b101,0b111,0b011}, // Q
    {0b111,0b101,0b111,0b110,0b101}, // R
    {0b111,0b100,0b111,0b001,0b111}, // S
    {0b111,0b010,0b010,0b010,0b010}, // T
    {0b101,0b101,0b101,0b101,0b111}, // U
    {0b101,0b101,0b101,0b101,0b010}, // V
    {0b101,0b101,0b111,0b111,0b101}, // W
    {0b101,0b101,0b010,0b101,0b101}, // X
    {0b101,0b101,0b010,0b010,0b010}, // Y
    {0b111,0b001,0b010,0b100,0b111}, // Z
    {0b111,0b101,0b101,0b101,0b111}, // 0
    {0b010,0b110,0b010,0b010,0b111}, // 1
    {0b111,0b001,0b111,0b100,0b111}, // 2
    {0b111,0b001,0b111,0b001,0b111}, // 3
    {0b101,0b101,0b111,0b001,0b001}, // 4
    {0b111,0b100,0b111,0b001,0b111}, // 5
    {0b111,0b100,0b111,0b101,0b111}, // 6
    {0b111,0b001,0b010,0b010,0b010}, // 7
    {0b111,0b101,0b111,0b101,0b111}, // 8
    {0b111,0b101,0b111,0b001,0b111}, // 9
};
static const uint8_t dot_pattern[5] = {0, 0, 0, 0, 0b001};

static const uint8_t *chat_font_pattern(char ch)
{
    if (ch >= 'A' && ch <= 'Z') {
        return chat_font_alnum[ch - 'A'];
    }
    if (ch >= '0' && ch <= '9') {
        return chat_font_alnum[26 + (ch - '0')];
    }
    if (ch == '.') {
        return dot_pattern;
    }
    return NULL;
}

static uint8_t font3x5(char ch, uint8_t row)
{
    const uint8_t *pattern = chat_font_pattern(ch);
    return pattern ? pattern[row] : 0;
}

static void tft_draw_text3x5(uint16_t x, uint16_t y, const char *text, uint16_t fg)
{
    for (size_t i = 0; text[i] != '\0'; i++) {
        char ch = text[i];
        if (ch == ' ') {
            x += 4;
            continue;
        }
        for (uint8_t row = 0; row < 5; row++) {
            uint8_t bits = font3x5(ch, row);
            for (uint8_t col = 0; col < 3; ) {
                if (((bits >> (2 - col)) & 1) == 0) {
                    col++;
                    continue;
                }
                uint8_t run = 1;
                while ((col + run) < 3 && ((bits >> (2 - (col + run))) & 1)) run++;
                tft_fill_rect(x + col, y + row, run, 1, fg);
                col += run;
            }
        }
        x += 4;
    }
}

static void chat_prepare_text(char *dst, const char *src)
{
    size_t idx = 0;
    while (*src && idx + 1 < CHAT_DISPLAY_MAX_CHARS) {
        char ch = *src++;
        if (ch == '\r') continue;
        if (ch == '\n') {
            dst[idx++] = '\n';
            continue;
        }
        if (ch >= 'a' && ch <= 'z') ch -= 32;
        if (ch < 32 || ch > 126) ch = ' ';
        dst[idx++] = ch;
    }
    dst[idx] = '\0';
}

static void tft_draw_chat_answer(const char *text)
{
    const uint16_t bg = color565(20, 20, 20);
    const uint16_t fg = color565(200, 200, 200);

    tft_fill_rect(CHAT_RECT_X, CHAT_RECT_Y, CHAT_RECT_W, CHAT_RECT_H, bg);
    if (!text || text[0] == '\0') return;

    char prepared[CHAT_DISPLAY_MAX_CHARS];
    chat_prepare_text(prepared, text);

    int x = CHAT_RECT_X + 2;
    int y = CHAT_RECT_Y + 2;
    int max_line_chars = CHAT_RECT_W / 4;
    if (max_line_chars < 1) max_line_chars = 1;
    int max_lines = CHAT_RECT_H / 6;
    if (max_lines < 1) max_lines = 1;

    char line[64];
    int line_idx = 0;
    int line_count = 0;
    size_t i = 0;

    while (prepared[i] != '\0' && line_count < max_lines) {
        char ch = prepared[i++];
        if (ch == '\n' || line_idx >= max_line_chars) {
            line[line_idx] = '\0';
            if (line_idx > 0) {
                tft_draw_text3x5(x, y, line, fg);
            }
            line_idx = 0;
            line_count++;
            y += 6;
            if (line_count >= max_lines) break;
            if (ch == '\n') continue;
        }
        line[line_idx++] = ch;
    }

    if (line_idx > 0 && line_count < max_lines) {
        line[line_idx] = '\0';
        tft_draw_text3x5(x, y, line, fg);
    }
}

static void tft_draw_text3x5_scaled(uint16_t x, uint16_t y, const char *text, uint16_t fg, uint8_t scale)
{
    if (scale == 0) scale = 1;
    for (size_t i = 0; text[i] != '\0'; i++) {
        char ch = text[i];
        if (ch == ' ') {
            x += 4 * scale;
            continue;
        }
        for (uint8_t row = 0; row < 5; row++) {
            uint8_t bits = font3x5(ch, row);
            for (uint8_t col = 0; col < 3; ) {
                if (((bits >> (2 - col)) & 1) == 0) {
                    col++;
                    continue;
                }
                uint8_t run = 1;
                while ((col + run) < 3 && ((bits >> (2 - (col + run))) & 1)) run++;
                tft_fill_rect(x + col * scale, y + row * scale, run * scale, scale, fg);
                col += run;
            }
        }
        x += 4 * scale;
    }
}

static void tft_draw_ai_overlay_text(const char *text, uint16_t fg, uint8_t scale)
{
    if (!text || text[0] == '\0') return;
    if (scale == 0) scale = 1;

    char prepared[AI_TEXT_MAX];
    chat_prepare_text(prepared, text);

    const uint16_t pad = 6;
    int max_line_chars = (AI_RECT_W - pad * 2) / (4 * scale);
    if (max_line_chars < 1) max_line_chars = 1;
    const int max_lines = 2;

    int x = AI_RECT_X + pad;
    int y = AI_RECT_Y + pad;

    char line[AI_TEXT_MAX];
    int line_idx = 0;
    int line_count = 0;
    size_t i = 0;

    while (prepared[i] != '\0' && line_count < max_lines) {
        char ch = prepared[i++];
        if (ch == '\n' || line_idx >= max_line_chars) {
            line[line_idx] = '\0';
            if (line_idx > 0) {
                tft_draw_text3x5_scaled(x, y, line, fg, scale);
            }
            line_idx = 0;
            line_count++;
            y += (6 * scale);
            if (line_count >= max_lines) break;
            if (ch == '\n') continue;
        }
        line[line_idx++] = ch;
    }

    if (line_idx > 0 && line_count < max_lines) {
        line[line_idx] = '\0';
        tft_draw_text3x5_scaled(x, y, line, fg, scale);
    }
}

static void tft_draw_ai_overlay(ai_state_t state, const char *text, bool phase)
{
    const uint16_t bg = color565(0, 0, 0);
    const uint16_t listening = color565(50, 90, 170);
    const uint16_t thinking = color565(30, 60, 120);
    const uint16_t answering = color565(0, 110, 110);
    const uint16_t error = color565(160, 40, 40);
    const uint16_t fg = color565(240, 240, 240);
    const uint16_t accent = color565(255, 255, 0);

    tft_fill_rect(AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, bg);

    switch (state) {
        case AI_IDLE:
            return;
        case AI_LISTENING: {
            tft_fill_rect(AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, listening);
            uint16_t cy = AI_RECT_Y + AI_RECT_H / 2;
            uint16_t cx = AI_RECT_X + AI_RECT_W / 2;
            tft_fill_rect(cx - 22, cy - 8, 14, 14, fg);
            tft_fill_rect(cx + 8, cy - 8, 14, 14, fg);
            break;
        }
        case AI_THINKING: {
            tft_fill_rect(AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, thinking);
            uint16_t cy = AI_RECT_Y + AI_RECT_H / 2;
            uint16_t start_x = AI_RECT_X + AI_RECT_W / 2 - 18;
            uint16_t dot = phase ? fg : accent;
            for (int i = 0; i < 3; i++) {
                tft_fill_rect(start_x + i * 16, cy - 5, 10, 10, dot);
                dot = fg;
            }
            break;
        }
        case AI_ANSWERING: {
            tft_fill_rect(AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, answering);
            tft_draw_ai_overlay_text(text, fg, 2);
            break;
        }
        case AI_ERROR: {
            tft_fill_rect(AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, error);
            const char *msg = (text && text[0]) ? text : "COBA LAGI YA~";
            tft_draw_ai_overlay_text(msg, fg, 2);
            break;
        }
        default:
            break;
    }
}

static void tft_draw_net_badge(net_state_t state, bool phase)
{
    const uint16_t bg = color565(0, 0, 0);
    const uint16_t green = color565(0, 255, 0);
    const uint16_t red = color565(255, 50, 50);
    const uint16_t yellow = color565(255, 255, 0);
    const uint16_t text = color565(0, 0, 0);

    const uint16_t x0 = 4 + 22 + 6;
    const uint16_t y0 = 5;
    const uint16_t w = 56;
    const uint16_t h = 14;

    tft_fill_rect(x0, y0, w, h, bg);
    
    // Hide badge if unknown or wifi_only (pre-check)
    if (state == NET_UNKNOWN || state == NET_WIFI_ONLY) return;

    bool visible = true;
    uint16_t fill = red;
    const char *label = "OFF";

    if (state == NET_ONLINE) {
        fill = green;
        label = "ONLINE";
    } else if (state == NET_CHECKING) {
        fill = yellow;
        label = "CHECKING";
        visible = phase;
    } else if (state == NET_OFFLINE) {
        fill = red;
        label = "OFF";
    }

    if (!visible) return;
    tft_fill_rect(x0, y0, w, h, fill);
    tft_draw_text3x5(x0 + 4, y0 + 5, label, text);
}

static void tft_draw_battery_icon(battery_state_t state, bool phase)
{
    const uint16_t bg = color565(0, 0, 0);
    const uint16_t green = color565(0, 255, 0);
    const uint16_t orange = color565(255, 165, 0);
    const uint16_t red = color565(255, 50, 50);

    uint16_t fill = green;
    uint8_t level = 3;

    if (state == BAT_LOW) {
        fill = orange;
        level = 1;
    } else if (state == BAT_CRIT) {
        fill = red;
        level = phase ? 1 : 0;
    } else if (state == BAT_FULL) {
        fill = green;
        level = 3;
    } else if (state == BAT_MED) {
        fill = green;
        level = 2;
    } else if (state == BAT_CHARGING) {
        fill = green;
        level = 3;
    }

    const uint16_t w = 26;
    const uint16_t h = 14;
    const uint16_t x0 = SCREEN_WIDTH - w - 4;
    const uint16_t y0 = 5;

    tft_fill_rect(x0, y0, w, h, bg);

    const uint16_t outline = fill;
    const uint16_t cap_w = 3;
    const uint16_t cap_h = 6;

    tft_fill_rect(x0, y0 + 2, w - cap_w, 2, outline);
    tft_fill_rect(x0, y0 + h - 4, w - cap_w, 2, outline);
    tft_fill_rect(x0, y0 + 2, 2, h - 4, outline);
    tft_fill_rect(x0 + (w - cap_w) - 2, y0 + 2, 2, h - 4, outline);

    tft_fill_rect(x0 + (w - cap_w), y0 + (h - cap_h) / 2, cap_w, cap_h, outline);

    const uint16_t pad = 3;
    const uint16_t inner_w = (w - cap_w) - pad * 2;
    const uint16_t inner_h = (h - 4) - 2;
    const uint16_t ix = x0 + pad;
    const uint16_t iy = y0 + 3;
    tft_fill_rect(ix, iy, inner_w, inner_h, bg);

    uint16_t fill_w = 0;
    if (level == 1) fill_w = inner_w / 3;
    else if (level == 2) fill_w = (inner_w * 2) / 3;
    else if (level >= 3) fill_w = inner_w;

    if (fill_w > 0) {
        tft_fill_rect(ix, iy, fill_w, inner_h, fill);
    }

    if (state == BAT_CHARGING) {
        // Horizontal lightning "cutout" (drawn with background color over fill)
        const uint16_t bx = x0 + 8;
        const uint16_t by = y0 + 7;

        tft_fill_rect(bx + 0, by - 1, 8, 2, bg);
        tft_fill_rect(bx + 4, by - 4, 8, 2, bg);
        tft_fill_rect(bx + 2, by + 2, 8, 2, bg);

        tft_fill_rect(bx + 6, by - 2, 2, 6, bg);
    }
}

static void chat_task(void *pvParameters)
{
    char *prompt = (char *)pvParameters;
    char answer[sizeof(g_ui_chat_answer)];
    chat_request_result_t result;
    esp_err_t err = cloud_client_chat_gradient(prompt, answer, sizeof(answer), &result);
    ESP_LOGI("CHAT", "http status=%d bytes=%lld", result.status_code, result.bytes);

    if (err == ESP_OK) {
        size_t answer_len = strnlen(answer, sizeof(answer));
        ESP_LOGI("CHAT", "parse ok answer_len=%d", (int)answer_len);
        board_set_ai_state(AI_ANSWERING);
        board_set_ai_text(answer, 8000);
        g_chat_state = CHAT_RENDER;

        (void)tts_client_speak_async(answer);
        
        // Mark hello as sent if this was the auto-send
        if (strcmp(prompt, "hello") == 0) {
            g_autosend_hello_sent = true;
        }
    } else {
        g_chat_state = CHAT_ERROR;
        const char *stage_name = "NONE";
        switch (result.fail_stage) {
            case CHAT_FAIL_DNS: stage_name = "DNS"; break;
            case CHAT_FAIL_TCP: stage_name = "TCP"; break;
            case CHAT_FAIL_TLS: stage_name = "TLS"; break;
            case CHAT_FAIL_HTTP: stage_name = "HTTP"; break;
            case CHAT_FAIL_JSON: stage_name = "JSON"; break;
            default: break;
        }
        ESP_LOGE("CHAT", "fail stage=%s errno=%d msg=%s", stage_name, result.fail_errno, result.fail_msg);
        char reason[64];
        snprintf(reason, sizeof(reason), "%s", stage_name);
        board_set_ai_state(AI_ERROR);
        board_set_ai_text("COBA LAGI YA~", 4000);
        ESP_LOGI("AIUI", "state=A4_ERROR reason=%s", reason);
    }

    free(prompt);
    vTaskDelete(NULL);
}

static const char *chat_stage_label(chat_fail_stage_t stage)
{
    switch (stage) {
        case CHAT_FAIL_DNS: return "DNS";
        case CHAT_FAIL_TCP: return "TCP";
        case CHAT_FAIL_TLS: return "TLS";
        case CHAT_FAIL_HTTP: return "HTTP";
        case CHAT_FAIL_JSON: return "JSON";
        default: return "NONE";
    }
}

static const char *tts_stage_label(tts_fail_stage_t stage)
{
    switch (stage) {
        case TTS_FAIL_HTTP: return "HTTP";
        case TTS_FAIL_WAV: return "WAV";
        case TTS_FAIL_I2S: return "I2S";
        default: return "NONE";
    }
}

static void ai_agent_task(void *arg)
{
    (void)arg;
    const char *prompt = "hello";
    char answer[sizeof(g_ui_chat_answer)];

    int chat_attempt = 0;
    chat_request_result_t chat_result;

    while (chat_attempt < 2) {
        if (!board_can_network() || g_net_state != NET_ONLINE) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }

        board_set_ai_state(AI_THINKING);
        board_set_ai_text("...", 4000);
        ESP_LOGI("CHAT", "start msg=\"%s\"", prompt);

        esp_err_t err = cloud_client_chat_gradient(prompt, answer, sizeof(answer), &chat_result);
        if (err == ESP_OK) {
            size_t len = strnlen(answer, sizeof(answer));
            ESP_LOGI("CHAT", "got answer len=%d", (int)len);
            break;
        }

        ESP_LOGE("CHAT", "fail stage=%s errno=%d msg=%s", chat_stage_label(chat_result.fail_stage), chat_result.fail_errno, chat_result.fail_msg);
        chat_attempt++;
        if (chat_attempt >= 2) {
            board_set_ai_state(AI_ERROR);
            board_set_ai_text("lagi ya~", 4000);
            goto done;
        }
        vTaskDelay(pdMS_TO_TICKS(10000));
    }

    board_set_ai_state(AI_ANSWERING);
    char preview[64];
    strlcpy(preview, answer, sizeof(preview));
    board_set_ai_text(preview, 8000);

    tts_stream_result_t tts_result;
    int tts_attempt = 0;

    while (tts_attempt < 2) {
        esp_err_t terr = cloud_client_tts_azure_stream_play(answer, &tts_result);
        if (terr == ESP_OK) {
            ESP_LOGI("TTS", "play done data_bytes=%u", (unsigned)(tts_result.data_bytes ? tts_result.data_bytes : tts_result.bytes));
            board_set_ai_state(AI_IDLE);
            goto done;
        }

        ESP_LOGE("TTS", "fail stage=%s errno=%d msg=%s", tts_stage_label(tts_result.fail_stage), tts_result.fail_errno, tts_result.fail_msg);
        tts_attempt++;
        if (tts_attempt >= 2) {
            board_set_ai_state(AI_ERROR);
            board_set_ai_text("COBA LAGI YA~", 4000);
            goto done;
        }
        vTaskDelay(pdMS_TO_TICKS(3000));
    }

done:
    g_autosend_hello_sent = true;
    g_ai_agent_started = false;
    vTaskDelete(NULL);
}

static void ai_agent_tick(void)
{
    if (g_pipeline_active) return;
    if (g_autosend_hello_sent || g_ai_agent_started) return;
    if (g_net_state != NET_ONLINE || !board_can_network()) return;

    g_ai_agent_started = true;
    if (xTaskCreate(ai_agent_task, "ai_agent", 8192, NULL, 5, NULL) != pdPASS) {
        g_ai_agent_started = false;
        ESP_LOGE("CHAT", "fail stage=HTTP errno=%d msg=ai_agent_create", ESP_FAIL);
    }
}

static void chat_engine_tick(void)
{
    if (g_pipeline_active) return;
    static char prompt_buffer[128];
    switch (g_chat_state) {
        case CHAT_IDLE:
            if (board_consume_chat_prompt(prompt_buffer, sizeof(prompt_buffer))) {
                g_chat_state = CHAT_SENDING;
                board_set_ai_state(AI_THINKING);
                board_set_ai_text(NULL, 0);
            }
            break;

        case CHAT_SENDING:
        {
            wifi_status_t ws = board_get_wifi_status();
            bool wifi_ready = (ws == WIFI_CONNECTED || ws == WIFI_CONNECTED_STABLE);
            bool allowed = board_can_network();
            if (allowed) {
                g_chat_state = CHAT_WAITING;
                char *prompt_copy = strdup(prompt_buffer);
                if (prompt_copy) {
                    xTaskCreate(chat_task, "chat_v1", 8192, prompt_copy, 5, NULL);
                } else {
                    g_chat_state = CHAT_ERROR;
                    board_set_ai_state(AI_ERROR);
                    board_set_ai_text("COBA LAGI YA~", 4000);
                    ESP_LOGI("AIUI", "state=A4_ERROR reason=OOM");
                    ESP_LOGE("CHAT", "fail stage=HTTP errno=%d msg=alloc fail", ESP_ERR_NO_MEM);
                }
            } else {
                g_chat_state = CHAT_BLOCKED;
                int64_t now_ms = esp_timer_get_time() / 1000;
                bool latched_net = (g_net_state == NET_ONLINE) ||
                                   (g_net_last_success_ms != 0 && now_ms - g_net_last_success_ms < NETWORK_LATCH_MS);
                ESP_LOGW("CHAT", "gate blocked wifi=%d net=%d", wifi_ready ? 1 : 0, latched_net ? 1 : 0);
                board_set_ai_state(AI_ERROR);
                board_set_ai_text("COBA LAGI YA~", 3000);
                ESP_LOGI("AIUI", "state=A4_ERROR reason=OFFLINE");
            }
        }
            break;

        case CHAT_BLOCKED:
            g_chat_state = CHAT_IDLE;
            break;

        case CHAT_WAITING:
            break;

        case CHAT_RENDER:
        case CHAT_ERROR:
            g_chat_state = CHAT_IDLE;
            break;
    }
}

static void console_task(void *pvParameters)
{
    char line[128];
    ESP_LOGI("CHAT", "Console ready. Use 'chat <message>'");
    while (1) {
        if (fgets(line, sizeof(line), stdin)) {
            line[strcspn(line, "\r\n")] = 0;
            if (strncmp(line, "chat ", 5) == 0) {
                board_submit_chat_prompt(line + 5);
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

static void ai_overlay_tick(uint32_t now_ticks)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (g_ai_expire_ms > 0 && now_ms >= g_ai_expire_ms) {
        board_clear_ai_text();
    }

    if (g_ai_state == AI_THINKING) {
        if ((now_ticks - g_ai_anim_last) > pdMS_TO_TICKS(400)) {
            g_ai_anim_last = now_ticks;
            g_ai_anim_phase = !g_ai_anim_phase;
            g_ai_dirty = true;
        }
    } else if (g_ai_state == AI_LISTENING) {
        if ((now_ticks - g_ai_anim_last) > pdMS_TO_TICKS(600)) {
            g_ai_anim_last = now_ticks;
            g_ai_anim_phase = !g_ai_anim_phase;
            g_ai_dirty = true;
        }
    } else {
        g_ai_anim_phase = false;
    }

    if (g_ai_dirty) {
        g_ai_dirty = false;
        tft_draw_ai_overlay(g_ai_state, g_ai_text, g_ai_anim_phase);
    }
}

// -------- AI Pipeline (Audio -> STT -> Chat -> TTS) --------
#define PIPE_CAPTURE_SEC 1 // Reduced from 2s to 1s to save RAM
//#define PIPE_CAPTURE_SEC 2
#define PIPE_SAMPLE_RATE 16000

static void pipe_fail(const char *stage, const char *reason, int code)
{
    g_pipeline_active = false;
    g_pipe_stage = PIPE_IDLE;
    ESP_LOGE("PIPE", "fail stage=%s reason=%s code=%d", stage ? stage : "?", reason ? reason : "?", code);
    board_set_ai_state(AI_ERROR);
    board_set_ai_text("COBA LAGI YA~", 4000);
}

static void pipe_task(void *arg)
{
    (void)arg;
    g_pipeline_active = true;
    g_pipe_stage = PIPE_LISTENING;
    board_set_ai_state(AI_LISTENING);
    board_set_ai_text(NULL, 0);
    
    // Wait for previous TTS (HTTP + playback) to complete before starting capture
    int wait_count = 0;
    while ((tts_client_is_busy() || audio_player_is_playing()) && wait_count < 80) {
        ESP_LOGI("PIPE", "waiting for TTS to finish... (%d/80)", wait_count + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
        wait_count++;
    }
    if (tts_client_is_busy() || audio_player_is_playing()) {
        ESP_LOGW("PIPE", "timeout waiting for TTS, skip capture");
        g_pipeline_active = false;
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    size_t samples = PIPE_CAPTURE_SEC * PIPE_SAMPLE_RATE;
    size_t expected_bytes = samples * sizeof(int16_t);
    ESP_LOGI("AIUI", "state=A1_LISTENING");
    ESP_LOGI("PIPE", "stage=S1_CAPTURE start dur_ms=%u sr=%u ch=1 bps=16 expected_bytes=%u", 
             (unsigned)(PIPE_CAPTURE_SEC * 1000), (unsigned)PIPE_SAMPLE_RATE, (unsigned)expected_bytes);
    if (!g_audio_buf || g_audio_buf_samples < samples) {
        free(g_audio_buf);
        g_audio_buf = (int16_t *)malloc(samples * sizeof(int16_t));
        g_audio_buf_samples = g_audio_buf ? samples : 0;
    }
    if (!g_audio_buf) {
        pipe_fail("S1_CAPTURE", "alloc", ESP_ERR_NO_MEM);
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    size_t captured_bytes = 0;
    uint32_t dur_ms = 0;
    if (mic_capture(g_audio_buf, samples, &captured_bytes, &dur_ms) != ESP_OK || captured_bytes == 0) {
        pipe_fail("S1_CAPTURE", "mic_fail", ESP_FAIL);
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    // Optional audio signal analysis for debugging
    int64_t sum = 0;
    int32_t peak = 0;
    int zero_count = 0;
    size_t captured_samples = captured_bytes / sizeof(int16_t);
    // Sample every 16th frame to reduce CPU load
    for (size_t i = 0; i < captured_samples; i += 16) {
        int16_t val = g_audio_buf[i];
        sum += (int64_t)val;
        int32_t abs_val = val < 0 ? -val : val;
        if (abs_val > peak) peak = abs_val;
        if (val == 0) zero_count++;
    }
    int32_t dc_offset = (int32_t)(sum / (int64_t)(captured_samples / 16));
    int32_t rms_approx = peak / 3; // Rough RMS approximation
    
    ESP_LOGI("PIPE", "stage=S1_CAPTURE done bytes=%u ms=%u", (unsigned)captured_bytes, (unsigned)dur_ms);
    ESP_LOGI("MIC", "rms=%d peak=%d dc=%d zeros=%d/%u", 
             (int)rms_approx, (int)peak, (int)dc_offset, zero_count, (unsigned)(captured_samples / 16));

    if (!board_can_network()) {
        pipe_fail("S2_STT", "net_gate", ESP_ERR_INVALID_STATE);
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    g_pipe_stage = PIPE_STT;
    board_set_ai_state(AI_THINKING);
    ESP_LOGI("AIUI", "state=A2_THINKING");
    stt_request_result_t stt_res;
    memset(g_pipe_stt_text, 0, sizeof(g_pipe_stt_text));
    ESP_LOGI("PIPE", "stage=S2_STT start wav_bytes=%u", (unsigned)(captured_bytes + 44));
    if (!stt_send_buffer(g_audio_buf, captured_bytes / 2, g_pipe_stt_text, sizeof(g_pipe_stt_text), &stt_res)) {
        pipe_fail("S2_STT", stt_res.fail_msg[0] ? stt_res.fail_msg : "stt_fail", stt_res.fail_errno);
        // Backoff to prevent spam on repeated failures
        vTaskDelay(pdMS_TO_TICKS(2000));
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    
    // Log STT success with transcript
    size_t text_len = strlen(g_pipe_stt_text);
    ESP_LOGI("STT", "text=\"%.*s\"", text_len > 200 ? 200 : (int)text_len, g_pipe_stt_text);
    ESP_LOGI("PIPE", "stage=S2_STT ok text_len=%u", (unsigned)text_len);
    
    // Optional: Show on TFT briefly (truncate to 48 chars)
    if (text_len > 0) {
        char preview[52];
        if (text_len > 48) {
            snprintf(preview, sizeof(preview), "%.48s...", g_pipe_stt_text);
        } else {
            strncpy(preview, g_pipe_stt_text, sizeof(preview) - 1);
            preview[sizeof(preview) - 1] = '\0';
        }
        board_set_ai_text(preview, 3000);
        ESP_LOGI("AIUI", "stt_preview_len=%u", (unsigned)strlen(preview));
    }

    // Free audio buffer after STT to reclaim ~96KB for TLS operations
    free(g_audio_buf);
    g_audio_buf = NULL;
    g_audio_buf_samples = 0;

    g_pipe_stage = PIPE_CHAT;
    ESP_LOGI("PIPE", "stage=S3_CHAT start");
    if (!board_can_network()) {
        pipe_fail("S3_CHAT", "net_gate", ESP_ERR_INVALID_STATE);
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    chat_request_result_t chat_res;
    memset(&chat_res, 0, sizeof(chat_res));
    memset(g_pipe_answer, 0, sizeof(g_pipe_answer));
    if (cloud_client_chat_gradient(g_pipe_stt_text, g_pipe_answer, sizeof(g_pipe_answer), &chat_res) != ESP_OK ||
        g_pipe_answer[0] == '\0') {
        pipe_fail("S3_CHAT", chat_res.fail_msg[0] ? chat_res.fail_msg : "chat_fail", chat_res.fail_errno);
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI("CHAT", "answer_len=%d preview=\"%.*s\"", (int)strlen(g_pipe_answer),
             (int)(strlen(g_pipe_answer) > 80 ? 80 : strlen(g_pipe_answer)), g_pipe_answer);

    g_pipe_stage = PIPE_TTS;
    ESP_LOGI("PIPE", "stage=S4_TTS start");
    if (!board_can_network()) {
        pipe_fail("S4_TTS", "net_gate", ESP_ERR_INVALID_STATE);
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }
    board_set_ai_state(AI_ANSWERING);
    tts_stream_result_t tts_res;
    esp_err_t tts_err = tts_client_stream_play(g_pipe_answer, &tts_res);
    if (tts_err != ESP_OK) {
        pipe_fail("S4_TTS", tts_res.fail_msg[0] ? tts_res.fail_msg : "tts_fail", tts_res.fail_errno);
        g_pipe_task = NULL;
        vTaskDelete(NULL);
        return;
    }

    board_set_ai_state(AI_IDLE);
    g_pipeline_active = false;
    g_pipe_stage = PIPE_IDLE;
    ESP_LOGI("PIPE", "done");
    g_pipe_task = NULL;
    vTaskDelete(NULL);
}

static void pipe_trigger_once(void)
{
    if (g_pipeline_active || g_pipe_task) return;
    if (!board_can_network()) return;
    
    // Block new pipeline if TTS is still busy to prevent I2S port conflict
    if (tts_client_is_busy() || audio_player_is_playing()) {
        ESP_LOGW("PIPE", "blocked: TTS busy");
        return;
    }
    
    g_pipe_stage = PIPE_IDLE;
    BaseType_t ok = xTaskCreate(pipe_task, "pipe_task", 8192, NULL, 5, &g_pipe_task);
    if (ok != pdPASS) {
        g_pipe_task = NULL;
        ESP_LOGE("PIPE", "task create failed");
    }
}

static void tft_draw_wifi_icon(wifi_status_t status, bool phase)
{
    const uint16_t bg = color565(0, 0, 0);
    const uint16_t fg = color565(255, 255, 255);
    const uint16_t dim = color565(60, 60, 60);
    const uint16_t err = color565(255, 50, 50);

    const uint16_t x0 = 4;
    const uint16_t y0 = 4;
    const uint16_t w = 22;
    const uint16_t h = 16;

    tft_fill_rect(x0, y0, w, h, bg);

    const uint16_t base_y = y0 + h - 2;
    const uint16_t bar_w = 3;
    const uint16_t gap = 2;
    const uint16_t bar_h[4] = {3, 6, 9, 12};

    for (int i = 0; i < 4; i++) {
        uint16_t bx = x0 + 2 + i * (bar_w + gap);
        uint16_t by = base_y - bar_h[i];
        tft_fill_rect(bx, by, bar_w, bar_h[i], dim);
    }

    if (status == WIFI_CONNECTED) {
        for (int i = 0; i < 4; i++) {
            uint16_t bx = x0 + 2 + i * (bar_w + gap);
            uint16_t by = base_y - bar_h[i];
            tft_fill_rect(bx, by, bar_w, bar_h[i], fg);
        }
        return;
    }

    if (status == WIFI_CONNECTING) {
        if (phase) {
            for (int i = 0; i < 2; i++) {
                uint16_t bx = x0 + 2 + i * (bar_w + gap);
                uint16_t by = base_y - bar_h[i];
                tft_fill_rect(bx, by, bar_w, bar_h[i], fg);
            }
        }
        return;
    }

    if (status == WIFI_ERROR) {
        for (int i = 0; i < (int)w; i++) {
            tft_fill_rect(x0 + i, y0 + (i * h) / w, 1, 1, err);
            tft_fill_rect(x0 + i, y0 + h - 1 - (i * h) / w, 1, 1, err);
        }
        return;
    }
}

static void wifi_overlay_tick(uint32_t now_ticks)
{
    wifi_status_t status = g_wifi_status;

    if (status == WIFI_CONNECTING) {
        if ((now_ticks - g_wifi_anim_last) > pdMS_TO_TICKS(450)) {
            g_wifi_anim_last = now_ticks;
            g_wifi_anim_phase = !g_wifi_anim_phase;
            g_wifi_dirty = true;
        }
    } else {
        g_wifi_anim_phase = false;
    }

    if (g_wifi_dirty) {
        g_wifi_dirty = false;
        tft_draw_wifi_icon(status, g_wifi_anim_phase);
    }
}

static void net_checker_tick(uint32_t now_ticks)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool wifi_connected = (g_wifi_status == WIFI_CONNECTED || g_wifi_status == WIFI_CONNECTED_STABLE);

    if (!wifi_connected) {
        if (g_net_state != NET_UNKNOWN) {
            net_abort_check();
            net_set_state(NET_UNKNOWN);
        }
        return;
    }

    if (g_net_state == NET_CHECKING) {
        if ((now_ticks - g_net_anim_last) > pdMS_TO_TICKS(500)) {
            g_net_anim_last = now_ticks;
            g_net_anim_phase = !g_net_anim_phase;
            g_net_dirty = true;
        }
    } else {
        g_net_anim_phase = false;
    }

    // Non-blocking DNS + TCP connect logic
    if (g_net_sock < 0 && !g_net_connected) {
        bool due = (g_net_last_check_ms == 0) || ((now_ms - g_net_last_check_ms) >= NET_CHECK_INTERVAL_MS);
        if (g_net_force_check) due = true;

        // If we are already in CHECKING, keep progressing without requiring `due`
        bool in_progress = (g_net_state == NET_CHECKING);
        if (!due && !in_progress) {
            if (g_net_dirty) {
                g_net_dirty = false;
                tft_draw_net_badge(g_net_state, g_net_anim_phase);
            }
            return;
        }

        // Start a new check (or continue one)
        if (!in_progress) {
            g_net_last_check_ms = now_ms;
            g_net_force_check = false;
            g_net_check_start_ms = now_ms;
            g_net_connected = false;
            net_log_reset();
            net_set_state(NET_CHECKING);
        }

        // Timeout budget for DNS + TCP connect
        if (g_net_check_start_ms != 0 && (now_ms - g_net_check_start_ms) > HTTP_TIMEOUT_MS) {
            // refresh DNS next time
            g_net_ip_valid = false;
            g_net_dns_pending = false;
            net_fail_set(NET_FAIL_TCP, 0, ETIMEDOUT);
            net_abort_check();
            goto draw_net_badge;
        }

        // Kick off DNS if needed (non-blocking)
        if (!g_net_ip_valid && !g_net_dns_pending) {
            ESP_LOGI("NET", "DNS host=%s", NET_CHECK_HOST);
            err_t derr = dns_gethostbyname(NET_CHECK_HOST, &g_net_ip, net_dns_found_cb, NULL);
            if (derr == ERR_OK) {
                // g_net_ip already filled
                g_net_ip_valid = IP_IS_V4(&g_net_ip);
                g_net_dns_pending = false;
                if (g_net_ip_valid) {
                    net_log_dns_ok();
                }
            } else if (derr == ERR_INPROGRESS) {
                g_net_dns_pending = true;
            } else {
                // DNS failed quickly
                g_net_ip_valid = false;
                g_net_dns_pending = false;
                net_fail_set(NET_FAIL_DNS, derr, 0);
                net_abort_check();
                goto draw_net_badge;
            }
        }

        // Wait for DNS result
        if (g_net_dns_pending || !g_net_ip_valid) {
            goto draw_net_badge;
        }

        if (!g_net_tcp_connect_logged) {
            ESP_LOGI("NET", "Free heap before connect: %u", esp_get_free_heap_size());
            ESP_LOGI("NET", "TCP connect ip=%s port=%d", g_net_ip_str, NET_CHECK_PORT);
            g_net_tcp_connect_logged = true;
        }

        // Have IPv4 address -> initiate non-blocking TCP connect
        g_net_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (g_net_sock < 0) {
            net_fail_set(NET_FAIL_TCP, 0, errno);
            net_abort_check();
            goto draw_net_badge;
        }

        int flags = fcntl(g_net_sock, F_GETFL, 0);
        (void)fcntl(g_net_sock, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(NET_CHECK_PORT);
        sa.sin_addr.s_addr = ip_2_ip4(&g_net_ip)->addr;

        int r = connect(g_net_sock, (struct sockaddr *)&sa, sizeof(sa));
        if (r == 0) {
            g_net_connected = true;
            net_log_tcp_ok();
            net_log_tls_start();
            net_set_state(NET_ONLINE);
            net_abort_check();
            goto draw_net_badge;
        }
        if (r < 0 && errno != EINPROGRESS) {
            // refresh DNS next time (could be stale)
            g_net_ip_valid = false;
            net_fail_set(NET_FAIL_TCP, 0, errno);
            net_abort_check();
            goto draw_net_badge;
        }

    } else if (g_net_sock >= 0) {
        // Progress non-blocking connect
        if ((now_ms - g_net_check_start_ms) > HTTP_TIMEOUT_MS) {
            g_net_ip_valid = false;
            net_fail_set(NET_FAIL_TCP, 0, ETIMEDOUT);
            net_abort_check();
            goto draw_net_badge;
        }

        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(g_net_sock, &wfds);
        struct timeval tv = {.tv_sec = 0, .tv_usec = 0};
        int sr = select(g_net_sock + 1, NULL, &wfds, NULL, &tv);

        if (sr > 0 && FD_ISSET(g_net_sock, &wfds)) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            (void)getsockopt(g_net_sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error == 0) {
                g_net_connected = true;
                net_log_tcp_ok();
                net_log_tls_start();
                net_set_state(NET_ONLINE);
            } else {
                // refresh DNS next time
                g_net_ip_valid = false;
                net_fail_set(NET_FAIL_TCP, so_error, so_error);
            }
            net_abort_check();
        }
    }

draw_net_badge:
    if (g_net_dirty) {
        g_net_dirty = false;
        tft_draw_net_badge(g_net_state, g_net_anim_phase);
    }
    return;
}

static void battery_overlay_tick(uint32_t now_ticks)
{
    if (!g_battery_initialized) {
        if (g_battery_boot_ticks == 0) g_battery_boot_ticks = now_ticks;
        if ((now_ticks - g_battery_boot_ticks) > pdMS_TO_TICKS(7000)) {
            g_battery_initialized = true;
            battery_set_state(battery_get_state());
        }
    } else {
        battery_set_state(battery_get_state());
    }

    battery_state_t state = g_battery_state;

    if (state == BAT_CRIT) {
        if ((now_ticks - g_battery_anim_last) > pdMS_TO_TICKS(600)) {
            g_battery_anim_last = now_ticks;
            g_battery_anim_phase = !g_battery_anim_phase;
            g_battery_dirty = true;
        }
    } else {
        g_battery_anim_phase = false;
    }

    if (g_battery_dirty) {
        g_battery_dirty = false;
        tft_draw_battery_icon(state, g_battery_anim_phase);
    }
}

static void draw_eye(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, blink_state_t blink, int8_t off_x, int8_t off_y, int8_t off_size)
{
    const uint16_t bg_color = color565(0, 0, 0);
    
    // Clear previous potential area (including shift margins)
    tft_fill_rect(x - 3, y - 3, w + 6, h + 6, bg_color);

    uint16_t rw = w + off_size;
    uint16_t rh = h + off_size;
    uint16_t rx = x + off_x;
    uint16_t ry = y + off_y;

    if (blink == BLINK_OPEN) {
        tft_fill_rect(rx, ry, rw, rh, color);
    } else if (blink == BLINK_HALF) {
        tft_fill_rect(rx, ry + rh / 3, rw, rh / 3, color);
    } else if (blink == BLINK_CLOSED) {
        tft_fill_rect(rx, ry + rh / 2 - 2, rw, 4, color);
    }
}

static void draw_robot_face(face_state_t face, blink_state_t blink, int8_t off_x, int8_t off_y, int8_t off_size)
{
    const uint16_t bg_color = color565(0, 0, 0);
    
    uint16_t eye_color;
    switch(face) {
        case FACE_HAPPY: eye_color = color565(0, 255, 0); break;
        case FACE_CONFUSED: eye_color = color565(255, 165, 0); break;
        case FACE_ERROR: eye_color = color565(255, 50, 50); break;
        default: eye_color = color565(0, 255, 255); break;
    }

    uint16_t ex1 = 26, ey1 = 58, ew1 = 20, eh1 = 26;
    uint16_t ex2 = 82, ey2 = 58, ew2 = 20, eh2 = 26;

    if (face == FACE_HAPPY) { ey1 = 54; eh1 = 20; ey2 = 54; eh2 = 20; }
    if (face == FACE_CONFUSED) { ey1 = 50; eh1 = 16; ey2 = 68; eh2 = 16; }

    draw_eye(ex1, ey1, ew1, eh1, eye_color, blink, off_x, off_y, off_size);
    draw_eye(ex2, ey2, ew2, eh2, eye_color, blink, off_x, off_y, off_size);

    if (face == FACE_HAPPY) {
        const uint16_t mouth_color = color565(255, 255, 0);
        tft_fill_rect(44, 100, 40, 5, mouth_color);
        tft_fill_rect(39, 95, 5, 5, mouth_color);
        tft_fill_rect(84, 95, 5, 5, mouth_color);
    } else {
        tft_fill_rect(36, 92, 56, 20, bg_color);
    }

    if (face == FACE_CONFUSED) {
        tft_fill_rect(54, 114, 20, 4, eye_color);
    } else {
        tft_fill_rect(54, 114, 20, 4, bg_color);
    }

    if (face == FACE_ERROR && blink == BLINK_OPEN) {
        const uint16_t x_color = color565(255, 50, 50);
        for (int i = 0; i < 15; i++) {
            tft_fill_rect(26 + i + off_x, 58 + i + off_y, 3, 3, x_color);
            tft_fill_rect(26 + 15 - i + off_x, 58 + i + off_y, 3, 3, x_color);
            tft_fill_rect(82 + i + off_x, 58 + i + off_y, 3, 3, x_color);
            tft_fill_rect(82 + 15 - i + off_x, 58 + i + off_y, 3, 3, x_color);
        }
    }
}

static void update_display(void)
{
    // Minimal wrapper for compatibility
    draw_robot_face(g_current_face, g_blink_state, 0, 0, 0);
}

static bool tft_init(void)
{
    ESP_LOGI(TAG_FEATURE, "TFT init start");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TFT_DC) | (1ULL << TFT_RST)
    };
    gpio_config(&io_conf);

    // Initialise Touch Pin
    gpio_config_t touch_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_TOUCH),
        .pull_up_en = GPIO_PULLUP_ENABLE, // Often touch/buttons pull to ground
        .pull_down_en = GPIO_PULLDOWN_DISABLE
    };
    gpio_config(&touch_conf);

    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = TFT_MOSI,
        .sclk_io_num = TFT_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = SCREEN_WIDTH * SCREEN_HEIGHT * 2
    };

    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) return false;

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26000000,
        .mode = 0,
        .spics_io_num = TFT_CS,
        .queue_size = 1
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_tft);
    if (ret != ESP_OK) return false;

    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    st7735_send_command(0x01);
    vTaskDelay(pdMS_TO_TICKS(120));
    st7735_send_command(0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t madctl = 0x00; // Rotation 0 (portrait)
    const uint8_t colmod = 0x05;
    st7735_send_command(0x36);
    st7735_send_data(&madctl, 1);
    st7735_send_command(0x3A);
    st7735_send_data(&colmod, 1);

    const uint8_t col_range[] = {0x00, 0x00, 0x00, 0x7F};
    const uint8_t row_range[] = {0x00, 0x00, 0x00, 0x9F};
    st7735_send_command(0x2A);
    st7735_send_data(col_range, sizeof(col_range));
    st7735_send_command(0x2B);
    st7735_send_data(row_range, sizeof(row_range));

    st7735_send_command(0x29);
    vTaskDelay(pdMS_TO_TICKS(20));

    ESP_LOGI(TAG_FEATURE, "TFT init done");
    return true;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)event_data;

    if (event_base != WIFI_EVENT) {
        return;
    }

    if (event_id == WIFI_EVENT_STA_START) {
        wifi_set_status(WIFI_CONNECTING);
        // Record the connect attempt time so MIN_RECONNECT_INTERVAL_MS applies consistently
        g_wifi_last_retry_ms = esp_timer_get_time() / 1000;
        // Cancel any pending delayed reconnect from a previous run
        if (g_wifi_reconnect_pending && g_wifi_reconnect_timer) {
            (void)esp_timer_stop(g_wifi_reconnect_timer);
            g_wifi_reconnect_pending = false;
        }
        esp_wifi_connect();
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI("WIFI", "DISCONNECTED");
        net_abort_check();
        net_set_state(NET_UNKNOWN);

        int64_t now_ms = esp_timer_get_time() / 1000;
        int max_retry = (g_wifi_phase == WIFI_PHASE_RUNTIME) ? MAX_RECONNECT_RUNTIME : MAX_RECONNECT_BOOT;
        g_wifi_connected_time = 0;

        if (g_wifi_retry_count >= max_retry) {
            if (g_wifi_phase == WIFI_PHASE_RUNTIME) {
                wifi_set_status(WIFI_ERROR);
                ESP_LOGE("WIFI", "GIVE UP");
            } else {
                ESP_LOGW("WIFI", "BOOT retry exhausted, waiting");
            }
            return;
        }

        // Enforce MIN_RECONNECT_INTERVAL_MS between connect attempts.
        uint32_t delay_ms = 0;
        if (g_wifi_last_retry_ms != 0) {
            int64_t elapsed = now_ms - g_wifi_last_retry_ms;
            if (elapsed < MIN_RECONNECT_INTERVAL_MS) {
                delay_ms = (uint32_t)(MIN_RECONNECT_INTERVAL_MS - elapsed);
            }
        }

        wifi_set_status(WIFI_CONNECTING);

        if (delay_ms > 0) {
            // Do not treat this as "exhausted"; we are simply respecting the interval.
            ESP_LOGI("WIFI", "WAIT %ums before retry", (unsigned)delay_ms);
            wifi_schedule_reconnect(delay_ms);
        } else {
            g_wifi_last_retry_ms = now_ms;
            g_wifi_retry_count++;
            ESP_LOGI("WIFI", "RETRY %d", (int)g_wifi_retry_count);
            // Cancel any pending timer since we're retrying immediately.
            if (g_wifi_reconnect_pending && g_wifi_reconnect_timer) {
                (void)esp_timer_stop(g_wifi_reconnect_timer);
                g_wifi_reconnect_pending = false;
            }
            esp_wifi_connect();
        }
    } else if (event_id == WIFI_EVENT_STA_STOP) {
        wifi_set_status(WIFI_OFF);
        g_wifi_connected_time = 0;
    }
}

static void on_got_ip(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (event_base != IP_EVENT || event_id != IP_EVENT_STA_GOT_IP) return;

    ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
    if (!event) return;

    ESP_LOGI("WIFI", "GOT IP: " IPSTR, IP2STR(&event->ip_info.ip));
    g_wifi_retry_count = 0;
    if (g_wifi_reconnect_pending && g_wifi_reconnect_timer) {
        (void)esp_timer_stop(g_wifi_reconnect_timer);
        g_wifi_reconnect_pending = false;
    }
    wifi_set_status(WIFI_CONNECTED);
    ESP_LOGI("WIFI", "CONNECTED");
    g_net_force_check = true;
    if (g_wifi_phase == WIFI_PHASE_BOOT) {
        g_wifi_phase = WIFI_PHASE_RUNTIME;
    }
    g_wifi_connected_time = xTaskGetTickCount();
    g_wifi_last_retry_ms = 0;
}

static bool wifi_manager_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        if (nvs_flash_erase() != ESP_OK) return false;
        ret = nvs_flash_init();
    }
    if (ret != ESP_OK) return false;

    ret = esp_netif_init();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return false;

    ret = esp_event_loop_create_default();
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return false;

    if (esp_netif_create_default_wifi_sta() == NULL) return false;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif != NULL) (void)esp_netif_set_hostname(netif, "R-BOT");

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    if (esp_wifi_init(&cfg) != ESP_OK) return false;

    if (esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL) != ESP_OK) return false;
    if (esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &on_got_ip, NULL) != ESP_OK) return false;

    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    wifi_config.sta.pmf_cfg.capable = true;
    wifi_config.sta.pmf_cfg.required = false;

    if (esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return false;
    if (esp_wifi_set_config(WIFI_IF_STA, &wifi_config) != ESP_OK) return false;

    esp_log_level_set("wifi", ESP_LOG_WARN);
    esp_log_level_set("esp_netif_handlers", ESP_LOG_WARN);

    wifi_set_status(WIFI_CONNECTING);
    if (esp_wifi_start() != ESP_OK) return false;

    return true;
}

void app_main(void)
{
    ESP_LOGI(TAG_STATE, "BOOT");
    printf("=== Wake Word Model Test ===\n");
    printf("Model size: %d bytes\n", halo_rbot_tflite_len);

    if (!tft_init()) {
        while (1) vTaskDelay(pdMS_TO_TICKS(1000));
    }

    tft_fill_screen(color565(0, 0, 0));

    if (audio_player_init() != ESP_OK) {
        ESP_LOGE(TAG_FEATURE, "Audio init failed");
    }

    // Wake word engine bring-up (TFLM AllocateTensors)
    wake_word_engine_init();

    wifi_manager_init(); // state must be determined by Wi-Fi/IP events
    initialize_sntp();
    cloud_client_init();
    xTaskCreate(console_task, "console", 4096, NULL, 5, NULL);

    // Idle Engine Timers
    uint32_t now = xTaskGetTickCount();
    uint32_t last_blink = now;
    uint32_t next_blink = pdMS_TO_TICKS(3000 + (esp_random() % 4000));
    uint32_t last_shift = now;
    uint32_t next_shift = pdMS_TO_TICKS(5000 + (esp_random() % 5000));
    uint32_t last_mood = now;
    uint32_t next_mood = pdMS_TO_TICKS(20000 + (esp_random() % 20000));
    uint32_t last_breath = now;
    uint32_t breath_period = pdMS_TO_TICKS(3000);

    // Current behavior state
    face_render_state_t state = {
        .blink = BLINK_OPEN,
        .mood = FACE_NEUTRAL,
        .off_x = 0,
        .off_y = 0,
        .off_size = 0
    };
    face_render_state_t pending = state;
    bool pending_active = false;
    uint32_t pending_due = 0;
    bool is_blinking = false;
    bool blink_end_pending = false;
    uint32_t blink_phase_start = now;
    uint32_t last_heartbeat = now;

    draw_robot_face(state.mood, state.blink, state.off_x, state.off_y, state.off_size);
    ai_overlay_tick(now);
    wifi_overlay_tick(now);
    if (!g_pipeline_active) {
        net_checker_tick(now);
    }
    battery_overlay_tick(now);
    ESP_LOGI(TAG_STATE, "RUNNING");

    while (1) {
        now = xTaskGetTickCount();
        if ((now - last_heartbeat) > pdMS_TO_TICKS(5000)) {
            last_heartbeat = now;
            ESP_LOGI(TAG_STATE, "RUNNING");
        }
        face_state_t requested_face;
        if (board_consume_requested_face_state(&requested_face)) {
            state.mood = requested_face;
            pending = state;
            pending_active = true;
            pending_due = now;
        }
        if (pending_active && (int32_t)(now - pending_due) >= 0) {
        face_render_state_t prev = state;
        state = pending;
        pending_active = false;
        board_set_face_state(state.mood);

        draw_robot_face(state.mood, state.blink, state.off_x, state.off_y, state.off_size);

            if (state.blink != prev.blink) {
                blink_phase_start = now;
            }
            if (blink_end_pending && state.blink == BLINK_OPEN) {
                blink_end_pending = false;
                is_blinking = false;
                last_blink = now;
                next_blink = pdMS_TO_TICKS(3000 + (esp_random() % 4000));
            }
            if (state.off_x != prev.off_x || state.off_y != prev.off_y) {
                last_shift = now;
                next_shift = pdMS_TO_TICKS(5000 + (esp_random() % 5000));
            }
            if (state.mood != prev.mood) {
                last_mood = now;
                next_mood = pdMS_TO_TICKS(20000 + (esp_random() % 20000));
            }
        }

        if (!pending_active) {
            // 1. Blink Logic
            if (!is_blinking) {
                if ((now - last_blink) > next_blink) {
                    is_blinking = true;
                    pending = state;
                    pending.blink = BLINK_HALF;
                    pending_active = true;
                    pending_due = now + random_delay_ticks();
                    ESP_LOGD("IDLE", "blink");
                }
            } else {
                uint32_t elapsed = (now - blink_phase_start) * portTICK_PERIOD_MS;
                if (state.blink == BLINK_HALF && elapsed >= 50) {
                    pending = state;
                    pending.blink = BLINK_CLOSED;
                    pending_active = true;
                    pending_due = now + random_delay_ticks();
                } else if (state.blink == BLINK_CLOSED && elapsed >= 100) {
                    pending = state;
                    pending.blink = BLINK_OPEN;
                    pending_active = true;
                    pending_due = now + random_delay_ticks();
                    blink_end_pending = true;
                }
            }

            // 2. Eye Shift Logic
            if (!pending_active && !is_blinking && (now - last_shift) > next_shift) {
                int8_t delta = (int8_t)(1 + (esp_random() % 2));
                int8_t dir = (esp_random() & 1) ? 1 : -1;
                pending = state;
                if (esp_random() & 1) {
                    pending.off_x = dir * delta;
                    pending.off_y = 0;
                } else {
                    pending.off_x = 0;
                    pending.off_y = dir * delta;
                }
                pending_active = true;
                pending_due = now + random_delay_ticks();
                ESP_LOGD("IDLE", "eye_shift");
            }

            // 3. Mood Drift Logic
            if (!pending_active && !is_blinking && (now - last_mood) > next_mood) {
                pending = state;
                pending.mood = (state.mood == FACE_NEUTRAL) ? FACE_HAPPY : FACE_NEUTRAL;
                pending_active = true;
                pending_due = now + random_delay_ticks();
                ESP_LOGD("IDLE", "mood_drift");
            }

            // 4. Breathing Illusion
            if (!pending_active && !is_blinking) {
                uint32_t b_elapsed = (now - last_breath) % breath_period;
                int8_t new_off_size = (b_elapsed < breath_period / 2) ? 1 : 0;
                if (new_off_size != state.off_size) {
                    pending = state;
                    pending.off_size = new_off_size;
                    pending_active = true;
                    pending_due = now + random_delay_ticks();
                }
            }
        }

        wifi_overlay_tick(now);
        board_wifi_is_stable(); // Ensure stability transition
        if (!g_pipeline_active) {
            net_checker_tick(now);
            ai_agent_tick();
            chat_engine_tick();
            pipe_trigger_once();
        }
        battery_overlay_tick(now);
        ai_overlay_tick(now);
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
