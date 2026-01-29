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
#include "esp_wifi.h" // [1.1.43] For WiFi shutdown before audio

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
#include "wake_word_runtime.h" // [1.1.27]

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

// [1.1.27] Global Score
float last_score = 0.0f;

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
// [1.1.34] Global Task Handle
static TaskHandle_t g_wakeword_task_handle = NULL;

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
    
    // [1.1.38] Safe-Update Transition: Handover
    g_ai_wake_until_ms = now_ms + 600;
    g_wake_accepted = true;
    g_wake_accepted_until_ms = now_ms + 1000;
    g_wake_gate.cooldown_until_ms = now_ms + WAKE_COOLDOWN_MS;
    
    ESP_LOGI(TAG_FEATURE, "WAKE ACCEPTED score=%.2f -> HANDOVER START", score);
    board_set_ai_text("WOKE UP!", 2000);
    
    // 1. Stop Mic & Flush (Save RAM)
    if (g_wakeword_task_handle) {
        vTaskSuspend(g_wakeword_task_handle);
    }
    // board_audio_mic_stop(g_audio); // Optional: if deep clear needed
    
    // 2. Start WiFi (Handover)
    if (g_net) {
        board_network_connect(g_net);
        board_set_ai_text("Wi-Fi Starting...", 1000);
    }
    
    // [1.1.35] Heap Analytics
    ESP_LOGI("MEM", "Free Heap (Wake Handover): %d", (int)esp_get_free_heap_size());

    // Trigger Pipeline (It will wait for WiFi)
    if (g_pipe_task == NULL) {
        xTaskCreate(pipe_task, "pipe_task", 8192, NULL, 5, &g_pipe_task);
    } else {
        xTaskNotifyGive(g_pipe_task);
    }
}

// --- Logic Ticks ---

// [1.1.28] Wake Word Task
#define EI_WINDOW_SAMPLES 16000 // 1 Second window @ 16kHz
#define EI_SLIDE_SAMPLES 3200   // 200ms slide

// [1.1.43] Skip startup frames to avoid initial RMS spike
#define FRAMES_TO_SKIP 15  // Skip first ~3 seconds (15 * 200ms)

// [1.1.44] Moving Average Debounce for flexibility
#define SCORE_HISTORY_SIZE 3
#define AVG_SCORE_THRESHOLD 0.7f  // Average score needed
#define RMS_GATE_THRESHOLD 600.0f  // Lowered for softer speech

static uint32_t frame_count = 0;
static float score_history[SCORE_HISTORY_SIZE] = {0};
static uint8_t history_index = 0;
static bool history_filled = false;

static void wakeword_task(void *arg) {
    // [1.1.37] Static Buffer Lockdown
    int16_t *window_buf = wake_word_get_window_buffer(); 
    // Clear initial buffer
    memset(window_buf, 0, 16000 * sizeof(int16_t));
    
    int32_t *raw_buf = (int32_t*)malloc(EI_SLIDE_SAMPLES * sizeof(int32_t)); // Temp raw buffer
    
    ESP_LOGI("WW_TASK", "Started Edge Impulse Wake Word Task (Static Buffer)");

    while(1) {
        // [1.1.38] Suspend check (should be handled by vTaskSuspend logic, but safety first)
        if (!g_audio || g_ai_state != AI_IDLE || g_pipeline_active || g_pipe_claimed || 
            tts_client_is_busy() || audio_player_is_playing()) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }

        int64_t now_ms = esp_timer_get_time() / 1000;
        if (now_ms < g_wake_gate.cooldown_until_ms) {
            vTaskDelay(pdMS_TO_TICKS(50));
            continue;
        }
        
        // 1. Capture Slide Chunk
        esp_err_t err = board_audio_mic_start(g_audio);
        if (err != ESP_OK) {
             vTaskDelay(pdMS_TO_TICKS(100));
             continue;
        }

        size_t bytes_read = 0;
        // Read 200ms data
        err = board_audio_read(g_audio, raw_buf, EI_SLIDE_SAMPLES * sizeof(int32_t), &bytes_read, pdMS_TO_TICKS(300));
        
        if (err == ESP_OK && bytes_read > 0) {
            // Shift window
            size_t samples_read = bytes_read / sizeof(int32_t);
            size_t shift_dist = samples_read;
            
            // Move old data left
            memmove(window_buf, window_buf + shift_dist, (EI_WINDOW_SAMPLES - shift_dist) * sizeof(int16_t));
            
            int64_t sum_sq = 0;

            // Append new data (convert 32->16) and calc RMS
            for (size_t i = 0; i < samples_read; i++) {
                int32_t val = raw_buf[i];
                // Assuming 12-bit alignment shift from board_audio.c
                int32_t scaled = val >> 12; 
                if (scaled > 32767) scaled = 32767;
                if (scaled < -32768) scaled = -32768;
                
                int16_t s16 = (int16_t)scaled;
                window_buf[(EI_WINDOW_SAMPLES - shift_dist) + i] = s16;
                
                sum_sq += (int64_t)s16 * s16;
            }
            
            // [1.1.32] Update Mic Level for UI
            float rms = (samples_read > 0) ? sqrtf((float)sum_sq / samples_read) : 0.0f;
            g_mic_level_ema = 0.6f * g_mic_level_ema + 0.4f * rms; // Faster update for UI
            g_ai_dirty = true; // Trigger redraw

            // 2. Inference
            float score = 0.0f;
            if (ei_wake_word_engine_infer(window_buf, EI_WINDOW_SAMPLES, &score)) {
                last_score = score;
                // [1.1.43] Skip startup frames to avoid RMS spike
                if (frame_count < FRAMES_TO_SKIP) {
                    frame_count++;
                    ESP_LOGD("WW", "Skipping frame %d/%d", frame_count, FRAMES_TO_SKIP);
                    continue;  // Skip to next iteration
                }
                
                // [1.1.45] Real-time RMS Logging for mic gain analysis
                ESP_LOGI("WW", "Score: %.2f (RMS: %.0f)", score, rms);

                // [1.1.44] Moving Average Debounce
                // Update score history
                score_history[history_index] = score;
                history_index = (history_index + 1) % SCORE_HISTORY_SIZE;
                if (history_index == 0) history_filled = true;
                
                // Calculate average score
                float avg_score = 0.0f;
                uint8_t count = history_filled ? SCORE_HISTORY_SIZE : (history_index + 1);
                for (uint8_t i = 0; i < count; i++) {
                    avg_score += score_history[i];
                }
                avg_score /= count;
                
                // Check if average score and RMS meet thresholds
                if (avg_score > AVG_SCORE_THRESHOLD && rms > RMS_GATE_THRESHOLD) {
                    ESP_LOGI("WW", "MOVING AVG PASSED (%.2f > %.2f, RMS: %.0f) -> HANDOVER", 
                             avg_score, AVG_SCORE_THRESHOLD, rms);
                    board_wakeword_notify(score);
                    
                    // Clear buffer and reset history
                    memset(window_buf, 0, 16000 * sizeof(int16_t));
                    memset(score_history, 0, sizeof(score_history));
                    history_index = 0;
                    history_filled = false;
                    frame_count = 0;  // Reset frame counter after trigger
                    ESP_LOGW("WW", "Buffer and history cleared after WAKE");
                } else if (score > WAKE_SCORE_THRESHOLD && rms <= RMS_GATE_THRESHOLD) {
                    // High score but low RMS = false positive
                    ESP_LOGD("WW", "Low RMS: %.0f (Score: %.2f, Avg: %.2f)", rms, score, avg_score);
                } else {
                    // Low score - just update history
                    ESP_LOGD("WW", "Avg score: %.2f (Current: %.2f)", avg_score, score);
                }
            }
        }
        
    }
    free(raw_buf);
    vTaskDelete(NULL);
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
            // [1.1.43] Total Radio Shutdown - Free RAM for MFCC
            esp_wifi_stop();
            esp_wifi_deinit();
            ESP_LOGI(TAG_STATE, "WiFi Radio Disabled (RAM Freed)");
            
            // [1.1.36] Kill-Switch: Skip WiFi, Go strict to Mic
            boot_set_state(BOOT_MIC_ONLINE);
            board_set_ai_text("OFFLINE MODE", 0);
            
            // [1.1.34] Start WW Task
            // [1.1.45] Increased stack to 16384 for MFCC + TFLite computation
            if (g_wakeword_task_handle == NULL) {
                xTaskCreate(wakeword_task, "ei_ww_task", 32768, NULL, configMAX_PRIORITIES - 1, &g_wakeword_task_handle);
            }
            break;
        case BOOT_WIFI_START:
             // Deprecated phase
             break;
        case BOOT_WIFI_WAIT:
             // Deprecated phase
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
    
    // [1.1.34] Resume WW Task on Fail
    if (g_wakeword_task_handle) {
        vTaskResume(g_wakeword_task_handle);
    }
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

        // Wait for Network (Handover from Wake)
        int net_wait = 0;
        const int max_net_wait = 200; // 20 seconds (100ms * 200)
        
        while (!board_can_network() && net_wait < max_net_wait) {
             if (net_wait == 0) board_set_ai_text("Connecting...", 0);
             vTaskDelay(pdMS_TO_TICKS(100));
             net_wait++;
        }

        if (!board_can_network()) {
            pipe_fail("S2_STT", "net_timeout", ESP_ERR_TIMEOUT);
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
        
        // [1.1.34] Resume WW Task on specific Success (Idle is safer)
        if (g_wakeword_task_handle) {
            vTaskResume(g_wakeword_task_handle);
        }
        // [1.1.35] Heap Analytics
        ESP_LOGI("MEM", "Free Heap (Pipe Done): %d", (int)esp_get_free_heap_size());
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
    
    // boot_set_state(BOOT_WIFI_WAIT); // [1.1.36] Disabled for Kill Switch
    // g_boot_timeout_ms = esp_timer_get_time()/1000 + BOOT_WIFI_TIMEOUT_MS;

    while(1) {
        uint32_t now = xTaskGetTickCount();
        board_network_tick(g_net);
        boot_sequence_tick(now);
        // wakeword_poll_tick(now); // Replaced by task
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
