#include <stdint.h>
#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_system.h"
#include "esp_random.h"

// Modules
#include "board_types.h"
#include "board_display.h"
#include "board_network.h"
#include "board_audio.h"
#include "board_power.h"

// Logic Headers
#include "cloud_client.h"
#include "command_dispatcher.h"
#include "board_status.h"
#include "audio_player.h"
#include "tts_client.h"

#define TAG_STATE "STATE"
#define TAG_FEATURE "FEATURE"

// --- Global Handles ---
static board_display_t *g_disp = NULL;
static board_network_t *g_net = NULL;
static board_audio_t *g_audio = NULL;
static board_power_t *g_power = NULL;

extern const char *g_ui_chat_answer; // Usually in board_status or similar, assuming extern char array

// --- Application Config ---
#define BOOT_WIFI_TIMEOUT_MS 12000
#define WIFI_STABLE_CHECK_MS 2000
#define OFFLINE_RETRY_BACKOFF_MS 8000

#define WAKE_COOLDOWN_MS 1500
#define WAKE_SCORE_THRESHOLD 0.80f
#define WAKE_MIN_ENERGY_RMS 3000

// --- Application State ---
static volatile net_state_t g_net_state = NET_UNKNOWN;
static volatile ai_state_t g_ai_state = AI_IDLE;
static char g_ai_text[160] = {0};
static int64_t g_ai_expire_ms = 0;

// Animation State
static uint32_t g_ai_anim_last = 0;
static bool g_ai_anim_phase = false;
static bool g_ai_dirty = false;

static volatile face_state_t g_current_face = FACE_NEUTRAL;
static volatile blink_state_t g_blink_state = BLINK_OPEN;
static volatile face_state_t g_requested_face = FACE_NEUTRAL;
static bool g_requested_face_active = false;

static volatile chat_state_t g_chat_state = CHAT_IDLE;
#define CHAT_DISPLAY_MAX_CHARS 200

// Pipeline Globals
typedef enum {
    PIPE_IDLE,
    PIPE_LISTENING,
    PIPE_STT,
    PIPE_CHAT,
    PIPE_TTS
} pipe_stage_t;

static bool g_pipeline_active = false;
static bool g_pipe_claimed = false;
static pipe_stage_t g_pipe_stage = PIPE_IDLE;
static TaskHandle_t g_pipe_task = NULL;

static int16_t *g_audio_buf = NULL;
static size_t g_audio_buf_samples = 0;
static char g_pipe_stt_text[512];
static char g_pipe_answer[1024]; // Large buffer for answer

// Boot State
typedef enum {
    BOOT_INIT_UI,
    BOOT_WIFI_START,
    BOOT_WIFI_WAIT,
    BOOT_MIC_ONLINE,
    BOOT_MIC_OFFLINE,
    BOOT_DONE
} boot_state_t;

static boot_state_t g_boot_state = BOOT_INIT_UI;
static int64_t g_boot_state_start_ms = 0;
static int64_t g_boot_timeout_ms = 0;
static int64_t g_offline_retry_backoff_ms = 0;
static bool g_wifi_stable_detected = false;
// static bool g_autosend_hello_sent = false;  // Removed (unused)
// static bool g_ai_agent_started = false;     // Removed (unused)

// Helpers prototypes
static void board_set_net_state_internal(net_state_t state);
static void pipe_task(void *arg);

// --- Interface Implementation ---

face_state_t board_get_face_state(void) { return g_current_face; }
void board_set_face_state(face_state_t face) { g_current_face = face; }
void board_request_face_state(face_state_t face) { g_requested_face = face; g_requested_face_active = true; }
bool board_consume_requested_face_state(face_state_t *face) {
    if (!g_requested_face_active) return false;
    if (face) *face = g_requested_face;
    g_requested_face_active = false;
    return true;
}

wifi_status_t board_get_wifi_status(void) { return board_network_get_status(g_net); }
battery_state_t board_get_battery_state(void) { return board_power_get_state(g_power); }
bool board_wifi_is_stable(void) { return board_network_is_stable(g_net); }
bool board_wifi_in_runtime_phase(void) { return board_network_in_runtime_phase(g_net); }

bool board_can_network(void) {
    return board_network_is_connected(g_net);
}
void board_set_net_state(net_state_t state) { board_set_net_state_internal(state); }

ai_state_t board_get_ai_state(void) { return g_ai_state; }

void board_set_ai_state(ai_state_t s) {
    if (g_ai_state == s) return;
    g_ai_state = s;
    g_ai_dirty = true;
}

void board_set_ai_text(const char *text, int ttl_ms) {
    if (text) snprintf(g_ai_text, sizeof(g_ai_text), "%s", text);
    else g_ai_text[0] = '\0';
    g_ai_expire_ms = (ttl_ms > 0) ? (esp_timer_get_time()/1000 + ttl_ms) : 0;
    g_ai_dirty = true;
}
void board_clear_ai_text(void) {
    g_ai_text[0] = '\0';
    g_ai_expire_ms = 0;
    if (g_ai_state != AI_IDLE) g_ai_state = AI_IDLE;
    g_ai_dirty = true;
}

static void board_set_net_state_internal(net_state_t state) {
    g_net_state = state;
    if (g_net) board_network_set_net_state(g_net, state);
}

// --- Wake Gate / Pipeline ---
typedef struct {
    int64_t cooldown_until_ms;
} wake_gate_t;
static wake_gate_t g_wake_gate = {0};
static bool g_wake_accepted = false;
static int64_t g_wake_accepted_until_ms = 0;
static float g_mic_level_ema = 0.0f;
static int64_t g_wake_block_until_ms = 0;
static char g_wake_block_msg[16] = {0};
static int64_t g_ai_wake_until_ms = 0;

void board_wakeword_notify(float score) {
    int64_t now_ms = esp_timer_get_time() / 1000;
    
    if (g_pipeline_active || g_pipe_claimed) return; // Busy
    
    if (tts_client_is_busy() || audio_player_is_playing()) {
         snprintf(g_wake_block_msg, sizeof(g_wake_block_msg), "BUSY");
         g_wake_block_until_ms = now_ms + 600;
         g_wake_gate.cooldown_until_ms = now_ms + 400;
         return;
    }
    
    if (!board_can_network()) {
         snprintf(g_wake_block_msg, sizeof(g_wake_block_msg), "OFFLINE");
         g_wake_block_until_ms = now_ms + 1500;
         board_set_ai_text("Wi-Fi not connected", 2000);
         g_wake_gate.cooldown_until_ms = now_ms + 800;
         return;
    }
    
    g_ai_wake_until_ms = now_ms + 600;
    g_wake_accepted = true;
    g_wake_accepted_until_ms = now_ms + 1000;
    g_wake_gate.cooldown_until_ms = now_ms + WAKE_COOLDOWN_MS;
    
    ESP_LOGI(TAG_FEATURE, "WAKE ACCEPTED score=%.2f", score);
    
    // Trigger Pipeline
    if (g_pipe_task == NULL) {
        xTaskCreate(pipe_task, "pipe_task", 8192, NULL, 5, &g_pipe_task);
    } else {
        xTaskNotifyGive(g_pipe_task);
    }
}

// --- Logic Ticks ---

static void wakeword_poll_tick(uint32_t now_ticks) {
    if (!g_audio) return;
    int64_t now_ms = esp_timer_get_time() / 1000;
    
    if (g_ai_state != AI_IDLE) return;
    if (g_pipeline_active || g_pipe_claimed) return;
    if (now_ms < g_wake_gate.cooldown_until_ms) return;
    if (tts_client_is_busy() || audio_player_is_playing()) return;

    board_audio_wake_result_t res = board_audio_process_wake_word(g_audio);
    
    g_mic_level_ema = 0.8f * g_mic_level_ema + 0.2f * res.score; // Simple EMA
    
    bool energy_ok = (res.rms >= WAKE_MIN_ENERGY_RMS);
    bool score_ok = (res.score >= 0.6f); 
    
    if (res.detected && energy_ok && score_ok) {
        board_wakeword_notify(res.score);
    }
}

static void boot_set_state(boot_state_t new_state) {
    if (g_boot_state == new_state) return;
    g_boot_state = new_state;
    g_boot_state_start_ms = esp_timer_get_time() / 1000;
}

static void boot_sequence_tick(uint32_t now_ticks) {
    int64_t now_ms = esp_timer_get_time() / 1000;
    
    switch (g_boot_state) {
        case BOOT_INIT_UI:
            boot_set_state(BOOT_WIFI_START);
            board_set_ai_text("Wi-Fi Connecting...", 0);
            g_boot_timeout_ms = now_ms + BOOT_WIFI_TIMEOUT_MS;
            break;
        case BOOT_WIFI_START:
             break;
        case BOOT_WIFI_WAIT:
             if (board_network_is_stable(g_net)) {
                 boot_set_state(BOOT_MIC_ONLINE);
                 board_set_ai_text("", 0);
                 g_wifi_stable_detected = true;
             } else if (now_ms >= g_boot_timeout_ms) {
                 boot_set_state(BOOT_MIC_OFFLINE);
                 board_set_ai_text("Offline Mode", 2000);
                 g_offline_retry_backoff_ms = now_ms + OFFLINE_RETRY_BACKOFF_MS;
             }
             break;
        case BOOT_MIC_ONLINE:
             boot_set_state(BOOT_DONE);
             break;
        case BOOT_MIC_OFFLINE:
             boot_set_state(BOOT_DONE);
             break;
        case BOOT_DONE:
             break;
    }
}

// Pipeline Task
#define PIPE_CAPTURE_SEC 1
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
    
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        
        g_pipeline_active = true;
        g_pipe_stage = PIPE_LISTENING;
        board_set_ai_state(AI_LISTENING);
        board_set_ai_text(NULL, 0);
        
        // Wait for TTS
        int wait_count = 0;
        while ((tts_client_is_busy() || audio_player_is_playing()) && wait_count < 80) {
            vTaskDelay(pdMS_TO_TICKS(100));
            wait_count++;
        }
        if (tts_client_is_busy() || audio_player_is_playing()) {
            pipe_fail("S0_BUSY", "tts_busy", 0);
            continue;
        }

        size_t samples = PIPE_CAPTURE_SEC * PIPE_SAMPLE_RATE;
        if (!g_audio_buf || g_audio_buf_samples < samples) {
            free(g_audio_buf);
            g_audio_buf = (int16_t *)malloc(samples * sizeof(int16_t));
            g_audio_buf_samples = g_audio_buf ? samples : 0;
        }
        if (!g_audio_buf) {
            pipe_fail("S1_CAPTURE", "alloc", ESP_ERR_NO_MEM);
            continue;
        }

        // Capture Loop
        board_audio_mic_start(g_audio);
        size_t total_read_bytes = 0;
        int64_t start_cap = esp_timer_get_time();
        int32_t *raw_chunk = (int32_t*)malloc(1024); // Temp buffer for raw 32bit
        
        while (total_read_bytes < samples * sizeof(int16_t)) {
             size_t chunk_bytes = 0;
             if (board_audio_read(g_audio, raw_chunk, 1024, &chunk_bytes, pdMS_TO_TICKS(100)) == ESP_OK) {
                 size_t samples_read = chunk_bytes / sizeof(int32_t);
                 for(size_t i=0; i<samples_read; i++) {
                     int32_t val = raw_chunk[i];
                     if (total_read_bytes/2 < samples) {
                         // Conversion (assuming same shift as board_audio.c)
                         int32_t scaled = val >> 12; 
                         if (scaled > 32767) scaled = 32767;
                         if (scaled < -32768) scaled = -32768;
                         g_audio_buf[total_read_bytes/2] = (int16_t)scaled;
                         total_read_bytes += 2;
                     }
                 }
             } else {
                 break; // Timeout/Err
             }
             if ((esp_timer_get_time() - start_cap) > PIPE_CAPTURE_SEC * 1000000 + 500000) break;
        }
        free(raw_chunk);

        if (total_read_bytes == 0) {
            pipe_fail("S1_CAPTURE", "mic_fail", ESP_FAIL);
            continue;
        }

        if (!board_can_network()) {
            pipe_fail("S2_STT", "net_gate", ESP_ERR_INVALID_STATE);
            continue;
        }

        g_pipe_stage = PIPE_STT;
        board_set_ai_state(AI_THINKING);
        stt_request_result_t stt_res;
        memset(g_pipe_stt_text, 0, sizeof(g_pipe_stt_text));
        
        if (!cloud_client_stt_send(g_audio_buf, total_read_bytes / 2, g_pipe_stt_text, sizeof(g_pipe_stt_text), &stt_res)) {
            pipe_fail("S2_STT", stt_res.fail_msg[0] ? stt_res.fail_msg : "stt_fail", stt_res.fail_errno);
            continue;
        }
        
        // Free buffer
        free(g_audio_buf);
        g_audio_buf = NULL;
        g_audio_buf_samples = 0;

        g_pipe_stage = PIPE_CHAT;
        if (!board_can_network()) {
            pipe_fail("S3_CHAT", "net_gate", ESP_ERR_INVALID_STATE);
            continue;
        }
        chat_request_result_t chat_res;
        memset(&chat_res, 0, sizeof(chat_res));
        memset(g_pipe_answer, 0, sizeof(g_pipe_answer));
        
        if (cloud_client_chat_gradient(g_pipe_stt_text, g_pipe_answer, sizeof(g_pipe_answer), &chat_res) != ESP_OK || g_pipe_answer[0] == '\0') {
             pipe_fail("S3_CHAT", chat_res.fail_msg, chat_res.fail_errno);
             continue;
        }

        g_pipe_stage = PIPE_TTS;
        board_set_ai_state(AI_ANSWERING);
        board_set_ai_text(g_pipe_answer, 8000); // Show text
        
        tts_stream_result_t tts_res;
        if (cloud_client_tts_azure_stream_play(g_pipe_answer, &tts_res) != ESP_OK) {
             pipe_fail("S4_TTS", tts_res.fail_msg, tts_res.fail_errno);
             continue;
        }

        g_pipeline_active = false;
        g_pipe_stage = PIPE_IDLE;
        board_set_ai_state(AI_IDLE);
    }
}

static void ai_overlay_tick(uint32_t now_ticks)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (g_ai_expire_ms > 0 && now_ms >= g_ai_expire_ms) {
        board_clear_ai_text();
    }

    if (g_ai_state == AI_THINKING || g_ai_state == AI_LISTENING) {
        if ((now_ticks - g_ai_anim_last) > pdMS_TO_TICKS(500)) {
            g_ai_anim_last = now_ticks;
            g_ai_anim_phase = !g_ai_anim_phase;
            g_ai_dirty = true;
        }
    } else {
        g_ai_anim_phase = false;
    }

    // Check for state changes to trigger redraw
    static net_state_t last_net = (net_state_t)-1;
    static wifi_status_t last_wifi = (wifi_status_t)-1;
    static battery_state_t last_bat = (battery_state_t)-1;

    net_state_t curr_net = board_network_get_net_state(g_net);
    wifi_status_t curr_wifi = board_network_get_status(g_net);
    battery_state_t curr_bat = board_power_get_state(g_power);

    if (curr_net != last_net || curr_wifi != last_wifi || curr_bat != last_bat) {
        last_net = curr_net;
        last_wifi = curr_wifi;
        last_bat = curr_bat;
        g_ai_dirty = true;
    }

    if (g_ai_dirty) {
        g_ai_dirty = false;
        bool anim = g_ai_anim_phase;
        float mic = g_mic_level_ema;
        if (g_wake_accepted && now_ms > g_wake_accepted_until_ms) g_wake_accepted = false;
        
        board_display_draw_overlay(g_disp, g_ai_state, g_ai_text, 
            curr_wifi,
            curr_net, curr_bat, 
            anim, mic, g_wake_accepted, g_wake_block_until_ms, g_wake_block_msg, g_ai_wake_until_ms);
    }
}

static void face_anim_tick(uint32_t now_ticks)
{
    static uint32_t last_blink = 0;
    static uint32_t blink_duration = 0;
    
    // Initial random start
    if (last_blink == 0) last_blink = now_ticks;

    // Logic to blink eyes occasionally
    if (g_blink_state == BLINK_OPEN) {
        if ((now_ticks - last_blink) > pdMS_TO_TICKS(3000 + (esp_random() % 4000))) {
            g_blink_state = BLINK_CLOSED;
            last_blink = now_ticks;
        }
    } else if (g_blink_state == BLINK_CLOSED) {
        if ((now_ticks - last_blink) > pdMS_TO_TICKS(150)) {
            g_blink_state = BLINK_OPEN;
            last_blink = now_ticks;
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG_STATE, "Starting R-BOT (Modular)");
    
    g_disp = board_display_init();    
    if (g_disp) board_display_clear(g_disp);
    g_power = board_power_init();
    audio_player_init();
    g_audio = board_audio_init();
    g_net = board_network_init();
    cloud_client_init();
    
    boot_set_state(BOOT_WIFI_WAIT);
    g_boot_timeout_ms = esp_timer_get_time()/1000 + BOOT_WIFI_TIMEOUT_MS;

    while(1) {
        uint32_t now = xTaskGetTickCount();
        board_network_tick(g_net);
        boot_sequence_tick(now);
        wakeword_poll_tick(now);
        ai_overlay_tick(now);
        face_anim_tick(now);
        board_display_draw_face(g_disp, g_current_face, g_blink_state, 0, 0, 0);
        
        if (g_disp) {
            static int last_touch = 0;
            int touch = board_display_get_touch_level(g_disp);
            if (last_touch == 0 && touch == 1 && !g_pipeline_active) {
                board_wakeword_notify(0.0f);
            }
            last_touch = touch;
        }
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
