#include "board_face.h"
#include "board_pins.h"
#include "board_display.h"
#include "board_types.h"
#include "audio_player.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

static const char *TAG_FACE = "BD_FACE";

#define SCREEN_W 128
#define SCREEN_H 160

// Colors in Big-Endian for direct ST7735 SPI DMA transfer
#define RGB565_BE(r, g, b) (__builtin_bswap16(((uint16_t)((r) & 0xF8) << 8) | ((uint16_t)((g) & 0xFC) << 3) | ((uint16_t)(b) >> 3)))

#define COL_BLACK       RGB565_BE(0,   0,   0)
#define COL_CYAN        RGB565_BE(0,   229, 255)
#define COL_CYAN_BRIGHT RGB565_BE(80,  255, 255)
#define COL_CYAN_DIM    RGB565_BE(50,  120, 150)
#define COL_THINKING    RGB565_BE(0,   215, 255)
#define COL_SPEAKING    RGB565_BE(0,   240, 255)
#define COL_HAPPY       RGB565_BE(0,   255, 180)
#define COL_ERROR       RGB565_BE(255, 55,  55)
#define COL_WHITE       RGB565_BE(255, 255, 255)
#define COL_GRAY        RGB565_BE(90,  90,  90)
#define COL_DARK_GRAY   RGB565_BE(35,  40,  50)
#define COL_GREEN       RGB565_BE(0,   255, 70)
#define COL_YELLOW      RGB565_BE(255, 220, 0)
#define COL_ORANGE      RGB565_BE(255, 140, 0)
#define COL_STATUS_BG   RGB565_BE(14,  18,  26)

// 40KB Framebuffer allocated in DMA-capable memory
static uint16_t *s_framebuf = NULL;
static board_display_t *s_disp = NULL;
static TaskHandle_t s_face_task_handle = NULL;
static SemaphoreHandle_t s_face_mutex = NULL;

// Global Face State
static volatile face_state_t s_current_state = FACE_BOOT;
static volatile face_error_type_t s_error_type = FACE_ERR_NONE;
static char s_custom_status[32] = {0};
static char s_error_msg[24] = {0};
static volatile wifi_status_t s_wifi_status = WIFI_CONNECTING;
static volatile battery_state_t s_bat_state = BAT_FULL;
static volatile float s_audio_level_in = 0.0f;

// Timers and transitions
static int64_t s_state_start_ms = 0;
static int64_t s_happy_until_ms = 0;
static int64_t s_error_until_ms = 0;

// Gaze and Eyelid Parameters
static float s_openness = 0.0f;       // 0.0 = closed, 1.0 = fully open
static float s_target_openness = 1.0f;
static float s_gaze_x = 0.0f;         // -5.0 to +5.0
static float s_gaze_y = 0.0f;         // -4.0 to +4.0
static float s_target_gaze_x = 0.0f;
static float s_target_gaze_y = 0.0f;

static int64_t s_next_blink_ms = 0;
static bool s_is_blinking = false;
static int64_t s_blink_start_ms = 0;
static int32_t s_blink_duration_ms = 180;
static bool s_double_blink_pending = false;

static int64_t s_next_gaze_shift_ms = 0;
static int64_t s_gaze_return_ms = 0;

// Demo Mode
static volatile bool s_demo_active = false;
static int s_demo_step = 0;
static int64_t s_demo_step_until_ms = 0;

// Metrics
static float s_metric_fps = 0.0f;
static float s_metric_avg_render_ms = 0.0f;
static float s_metric_max_render_ms = 0.0f;
static uint32_t s_frames_rendered = 0;
static int64_t s_fps_window_start_ms = 0;

// 3x5 font table
static const uint8_t s_font3x5[36][5] = {
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

static inline void fb_set_pixel(int x, int y, uint16_t col)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        s_framebuf[y * SCREEN_W + x] = col;
    }
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t col)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    for (int j = 0; j < h; j++) {
        uint16_t *line = &s_framebuf[(y + j) * SCREEN_W + x];
        for (int i = 0; i < w; i++) {
            line[i] = col;
        }
    }
}

static void fb_draw_round_rect(int x, int y, int w, int h, int r, uint16_t col)
{
    if (w <= 0 || h <= 0) return;
    if (r > w / 2) r = w / 2;
    if (r > h / 2) r = h / 2;

    for (int j = 0; j < h; j++) {
        int dx = 0;
        if (j < r) {
            int dy = r - 1 - j;
            dx = r - (int)roundf(sqrtf((float)(r * r - dy * dy)));
        } else if (j >= h - r) {
            int dy = j - (h - r);
            dx = r - (int)roundf(sqrtf((float)(r * r - dy * dy)));
        }

        int start_x = x + dx;
        int end_x = x + w - 1 - dx;
        int py = y + j;

        if (py < 0 || py >= SCREEN_H) continue;
        if (start_x < 0) start_x = 0;
        if (end_x >= SCREEN_W) end_x = SCREEN_W - 1;

        if (end_x >= start_x) {
            uint16_t *row = &s_framebuf[py * SCREEN_W];
            for (int px = start_x; px <= end_x; px++) {
                row[px] = col;
            }
        }
    }
}

static void fb_draw_happy_arc(int cx, int cy, int ro, int thickness, uint16_t col)
{
    int ri = ro - thickness;
    int ro2 = ro * ro;
    int ri2 = ri * ri;

    for (int y = cy - ro; y <= cy; y++) {
        if (y < 0 || y >= SCREEN_H) continue;
        int dy = y - cy;
        for (int x = cx - ro; x <= cx + ro; x++) {
            if (x < 0 || x >= SCREEN_W) continue;
            int dx = x - cx;
            int dist2 = dx * dx + dy * dy;
            if (dist2 <= ro2 && dist2 >= ri2) {
                s_framebuf[y * SCREEN_W + x] = col;
            }
        }
    }
}

static void fb_draw_char(int x, int y, char ch, uint16_t col, int scale)
{
    int idx = -1;
    if (ch >= 'A' && ch <= 'Z') idx = ch - 'A';
    else if (ch >= 'a' && ch <= 'z') idx = ch - 'a';
    else if (ch >= '0' && ch <= '9') idx = 26 + (ch - '0');
    else if (ch == '.') {
        fb_fill_rect(x, y + 4 * scale, scale, scale, col);
        return;
    } else if (ch == ':') {
        fb_fill_rect(x, y + 1 * scale, scale, scale, col);
        fb_fill_rect(x, y + 3 * scale, scale, scale, col);
        return;
    } else if (ch == '!') {
        fb_fill_rect(x, y, scale, 3 * scale, col);
        fb_fill_rect(x, y + 4 * scale, scale, scale, col);
        return;
    } else if (ch == '-') {
        fb_fill_rect(x, y + 2 * scale, 3 * scale, scale, col);
        return;
    } else if (ch == '(') {
        fb_fill_rect(x + scale, y, scale, scale, col);
        fb_fill_rect(x, y + scale, scale, 3 * scale, col);
        fb_fill_rect(x + scale, y + 4 * scale, scale, scale, col);
        return;
    } else if (ch == ')') {
        fb_fill_rect(x, y, scale, scale, col);
        fb_fill_rect(x + scale, y + scale, scale, 3 * scale, col);
        fb_fill_rect(x, y + 4 * scale, scale, scale, col);
        return;
    }

    if (idx < 0 || idx >= 36) return;

    const uint8_t *pat = s_font3x5[idx];
    for (int r = 0; r < 5; r++) {
        uint8_t bits = pat[r];
        for (int c = 0; c < 3; c++) {
            if ((bits >> (2 - c)) & 1) {
                fb_fill_rect(x + c * scale, y + r * scale, scale, scale, col);
            }
        }
    }
}

static void fb_draw_text(int x, int y, const char *txt, uint16_t col, int scale)
{
    if (!txt) return;
    int cur_x = x;
    while (*txt) {
        if (*txt == ' ') {
            cur_x += 3 * scale;
        } else {
            fb_draw_char(cur_x, y, *txt, col, scale);
            cur_x += 4 * scale;
        }
        txt++;
    }
}

static int fb_get_text_width(const char *txt, int scale)
{
    if (!txt) return 0;
    int len = 0;
    while (*txt) {
        if (*txt == ' ') len += 3 * scale;
        else len += 4 * scale;
        txt++;
    }
    return len > 0 ? (len - scale) : 0;
}

static void fb_draw_text_centered(int y, const char *txt, uint16_t col, int scale)
{
    int w = fb_get_text_width(txt, scale);
    int x = (SCREEN_W - w) / 2;
    if (x < 2) x = 2;
    fb_draw_text(x, y, txt, col, scale);
}

// -------------------------------------------------------------
// Top Header Rendering: Clean Wi-Fi & Battery Indicators
// -------------------------------------------------------------
static void render_header(void)
{
    // 1. Wi-Fi Indicator at (X=4, Y=3)
    const int wx = 4;
    const int wy = 3;
    uint16_t wcol = COL_GREEN;
    bool draw_strike = false;

    if (s_wifi_status == WIFI_CONNECTED || s_wifi_status == WIFI_CONNECTED_STABLE) {
        wcol = COL_GREEN;
    } else if (s_wifi_status == WIFI_CONNECTING) {
        // Blink yellow
        int64_t now_ms = esp_timer_get_time() / 1000;
        wcol = ((now_ms / 300) % 2 == 0) ? COL_YELLOW : COL_DARK_GRAY;
    } else {
        wcol = COL_ERROR;
        draw_strike = true;
    }

    // 3 stepped signal bars
    fb_fill_rect(wx + 0, wy + 8, 3, 3, (s_wifi_status == WIFI_OFF) ? COL_DARK_GRAY : wcol);
    fb_fill_rect(wx + 4, wy + 5, 3, 6, (s_wifi_status == WIFI_OFF || (s_wifi_status == WIFI_CONNECTING && wcol == COL_DARK_GRAY)) ? COL_DARK_GRAY : wcol);
    fb_fill_rect(wx + 8, wy + 2, 3, 9, (s_wifi_status != WIFI_CONNECTED && s_wifi_status != WIFI_CONNECTED_STABLE) ? COL_DARK_GRAY : wcol);

    if (draw_strike) {
        // Red diagonal strike line
        for (int i = 0; i < 11; i++) {
            fb_set_pixel(wx + i, wy + i, COL_ERROR);
        }
    }

    // 2. Battery Indicator at (X=104, Y=4)
    const int bx = 104;
    const int by = 4;
    uint16_t bcol = COL_GREEN;
    int bars = 3;

    if (s_bat_state == BAT_MED) {
        bars = 2;
        bcol = COL_GREEN;
    } else if (s_bat_state == BAT_LOW) {
        bars = 1;
        bcol = COL_ORANGE;
    } else if (s_bat_state == BAT_CRIT) {
        bars = 1;
        int64_t now_ms = esp_timer_get_time() / 1000;
        bcol = ((now_ms / 250) % 2 == 0) ? COL_ERROR : COL_DARK_GRAY;
    }

    // Battery Body Outline (18x9)
    fb_fill_rect(bx, by, 17, 1, COL_GRAY);
    fb_fill_rect(bx, by + 8, 17, 1, COL_GRAY);
    fb_fill_rect(bx, by, 1, 9, COL_GRAY);
    fb_fill_rect(bx + 16, by, 1, 9, COL_GRAY);
    // Nipple
    fb_fill_rect(bx + 17, by + 2, 2, 5, COL_GRAY);

    // Inner battery bars
    for (int i = 0; i < bars; i++) {
        fb_fill_rect(bx + 2 + i * 5, by + 2, 4, 5, bcol);
    }
}

// -------------------------------------------------------------
// Procedural Face Rendering
// -------------------------------------------------------------
static void render_face(int64_t now_ms)
{
    // Screen center of eyes: Left=(38, 66), Right=(90, 66)
    const int eye_left_cx = 38;
    const int eye_right_cx = 90;
    const int eye_cy = 66;

    const int eye_w = (s_current_state == FACE_LISTENING) ? 30 : 28;
    const int eye_base_h = (s_current_state == FACE_LISTENING) ? 42 : 38;

    uint16_t eye_col = COL_CYAN;
    switch (s_current_state) {
        case FACE_LISTENING:   eye_col = COL_CYAN_BRIGHT; break;
        case FACE_THINKING:    eye_col = COL_THINKING;    break;
        case FACE_SPEAKING:    eye_col = COL_SPEAKING;    break;
        case FACE_HAPPY:       eye_col = COL_HAPPY;       break;
        case FACE_ERROR:       eye_col = COL_ERROR;       break;
        case FACE_DISCONNECTED:eye_col = COL_CYAN_DIM;    break;
        default:               eye_col = COL_CYAN;        break;
    }

    // Happy Expression: Crescent Smiling Eyes
    if (s_current_state == FACE_HAPPY) {
        int arc_ro = 15;
        int arc_thick = 6;
        fb_draw_happy_arc(eye_left_cx + (int)s_gaze_x, eye_cy + 4, arc_ro, arc_thick, eye_col);
        fb_draw_happy_arc(eye_right_cx + (int)s_gaze_x, eye_cy + 4, arc_ro, arc_thick, eye_col);

        // Small sweet smile mouth curve below eyes (Y=108)
        fb_draw_happy_arc(SCREEN_W / 2, 116, 12, 4, eye_col);
        return;
    }

    // Normal & Other Expressions: Rounded Rectangles with Eyelid Openness
    int curr_h = (int)roundf((float)eye_base_h * s_openness);
    if (curr_h < 4) curr_h = 4; // Min slit height for closed eyelid

    int corner_r = 8;
    if (corner_r > curr_h / 2) corner_r = curr_h / 2;

    int left_x = (eye_left_cx - eye_w / 2) + (int)s_gaze_x;
    int right_x = (eye_right_cx - eye_w / 2) + (int)s_gaze_x;
    int eye_y = (eye_cy - curr_h / 2) + (int)s_gaze_y;

    // Draw Left & Right Eyes
    fb_draw_round_rect(left_x, eye_y, eye_w, curr_h, corner_r, eye_col);
    fb_draw_round_rect(right_x, eye_y, eye_w, curr_h, corner_r, eye_col);

    // Slanted eyebrows for ERROR
    if (s_current_state == FACE_ERROR && s_openness > 0.4f) {
        // Cut top slant across inner brows: left eye angles down-right, right eye angles down-left
        for (int i = 0; i < 12; i++) {
            fb_fill_rect(left_x + eye_w - 1 - i, eye_y, 1, 1 + (i / 2), COL_BLACK);
            fb_fill_rect(right_x + i, eye_y, 1, 1 + (i / 2), COL_BLACK);
        }
    }

    // Glossy Specular Gleam / Highlight (adds life when eye is open)
    if (s_openness > 0.6f && s_current_state != FACE_ERROR) {
        int gleam_size = 4;
        int gleam_ly = eye_y + 4;
        int gleam_lx = left_x + eye_w - 7;
        int gleam_rx = right_x + eye_w - 7;
        fb_draw_round_rect(gleam_lx, gleam_ly, gleam_size, gleam_size, 1, COL_WHITE);
        fb_draw_round_rect(gleam_rx, gleam_ly, gleam_size, gleam_size, 1, COL_WHITE);
    }

    // Speaking Expression: Dynamic Animated Soundwave Mouth
    if (s_current_state == FACE_SPEAKING) {
        float lev = s_audio_level_in;
        // Fallback procedural modulation if live audio level is quiet
        float time_sec = (float)now_ms / 1000.0f;
        float synth = 0.35f + 0.35f * fabsf(sinf(time_sec * 12.0f));
        if (lev < 0.1f) lev = synth;

        const int bar_cx = SCREEN_W / 2;
        const int bar_y_center = 114;
        const int bar_w = 4;
        const int bar_gap = 4;
        const int num_bars = 5;
        const float weights[5] = {0.5f, 0.85f, 1.0f, 0.85f, 0.5f};

        int total_w = num_bars * bar_w + (num_bars - 1) * bar_gap;
        int start_x = bar_cx - total_w / 2;

        for (int i = 0; i < num_bars; i++) {
            int bh = 3 + (int)roundf(lev * weights[i] * 16.0f);
            if (bh > 20) bh = 20;
            int bx = start_x + i * (bar_w + bar_gap);
            int by = bar_y_center - bh / 2;
            int br = 2;
            if (br > bh / 2) br = bh / 2;
            fb_draw_round_rect(bx, by, bar_w, bh, br, COL_SPEAKING);
        }
    }

    // Thinking Expression: Rotating Orbital Dots
    if (s_current_state == FACE_THINKING) {
        float t = (float)now_ms / 1000.0f;
        const int dot_y = 114;
        const int dot_cx = SCREEN_W / 2;
        for (int i = 0; i < 3; i++) {
            float phase = t * 6.0f - (float)i * 1.0f;
            float r_scale = 1.0f + 1.2f * (0.5f + 0.5f * sinf(phase));
            int dot_r = (int)roundf(r_scale);
            int dx = (i - 1) * 12;
            fb_draw_round_rect(dot_cx + dx - dot_r, dot_y - dot_r, dot_r * 2, dot_r * 2, dot_r, COL_THINKING);
        }
    }
}

// -------------------------------------------------------------
// Bottom Status Banner Rendering
// -------------------------------------------------------------
static void render_status_banner(void)
{
    const int by = 142;
    const int bh = 17;

    // Subtle dark banner container
    fb_fill_rect(0, by, SCREEN_W, bh, COL_STATUS_BG);
    fb_fill_rect(0, by, SCREEN_W, 1, COL_DARK_GRAY);

    const char *label = "RBOT READY";
    uint16_t txt_col = COL_WHITE;
    int scale = 2;

    if (s_custom_status[0] != '\0') {
        label = s_custom_status;
        scale = (strlen(label) > 10) ? 1 : 2;
    } else {
        switch (s_current_state) {
            case FACE_BOOT:
                label = "REGGI BOT";
                txt_col = COL_CYAN;
                scale = 2;
                break;
            case FACE_IDLE:
                label = "READY";
                txt_col = COL_WHITE;
                scale = 2;
                break;
            case FACE_LISTENING:
                label = "LISTENING";
                txt_col = COL_CYAN_BRIGHT;
                scale = 1;
                break;
            case FACE_THINKING:
                label = "THINKING";
                txt_col = COL_THINKING;
                scale = 1;
                break;
            case FACE_SPEAKING:
                label = "SPEAKING";
                txt_col = COL_SPEAKING;
                scale = 1;
                break;
            case FACE_HAPPY:
                label = "HAPPY :)";
                txt_col = COL_HAPPY;
                scale = 2;
                break;
            case FACE_ERROR:
                if (s_error_msg[0] != '\0') {
                    label = s_error_msg;
                } else {
                    label = "ERROR";
                }
                txt_col = COL_ERROR;
                scale = 1;
                break;
            case FACE_DISCONNECTED:
                label = "OFFLINE";
                txt_col = COL_ORANGE;
                scale = 2;
                break;
        }
    }

    int text_y = by + (bh - (5 * scale)) / 2;
    fb_draw_text_centered(text_y, label, txt_col, scale);
}

// -------------------------------------------------------------
// Animation Update Physics (Runs every frame)
// -------------------------------------------------------------
static void update_animation_physics(int64_t now_ms)
{
    // 1. Boot Sequence
    if (s_current_state == FACE_BOOT) {
        int64_t elapsed = now_ms - s_state_start_ms;
        if (elapsed < 1200) {
            // Smoothly open eyes: 0.0 -> 1.0
            float prog = (float)elapsed / 1200.0f;
            s_openness = 0.5f - 0.5f * cosf(prog * 3.14159f);
        } else {
            s_openness = 1.0f;
            s_current_state = FACE_IDLE;
            s_state_start_ms = now_ms;
            ESP_LOGI(TAG_FACE, "Boot sequence completed -> Transition to IDLE");
        }
        return;
    }

    // 2. Happy Timed Hold
    if (s_current_state == FACE_HAPPY) {
        if (now_ms >= s_happy_until_ms) {
            s_current_state = FACE_IDLE;
            s_state_start_ms = now_ms;
        }
        return;
    }

    // 3. Error Timed Hold & Auto-recovery
    if (s_current_state == FACE_ERROR && s_error_until_ms > 0) {
        if (now_ms >= s_error_until_ms) {
            s_current_state = FACE_IDLE;
            s_error_until_ms = 0;
            s_error_type = FACE_ERR_NONE;
            s_error_msg[0] = '\0';
        }
    }

    // 4. Disconnected State (Droopy Eyes)
    if (s_current_state == FACE_DISCONNECTED) {
        s_target_openness = 0.35f;
    } else {
        s_target_openness = 1.0f;
    }

    // 5. Automatic Natural Blinking (IDLE, THINKING, SPEAKING, DISCONNECTED)
    if (s_current_state != FACE_HAPPY) {
        if (!s_is_blinking) {
            if (s_next_blink_ms == 0 || now_ms >= s_next_blink_ms) {
                s_is_blinking = true;
                s_blink_start_ms = now_ms;
                s_blink_duration_ms = 140 + (esp_random() % 90); // 140ms - 230ms
                s_double_blink_pending = ((esp_random() % 100) < 18); // 18% double-blink
            }
        } else {
            int64_t blink_elapsed = now_ms - s_blink_start_ms;
            if (blink_elapsed < s_blink_duration_ms) {
                float phase = (float)blink_elapsed / (float)s_blink_duration_ms;
                // Cosine curve: 1.0 -> 0.08 -> 1.0
                float blink_factor = 0.54f + 0.46f * cosf(phase * 6.28318f);
                if (blink_factor < 0.08f) blink_factor = 0.08f;
                s_openness = s_target_openness * blink_factor;
            } else {
                s_is_blinking = false;
                s_openness = s_target_openness;

                if (s_double_blink_pending) {
                    s_double_blink_pending = false;
                    s_next_blink_ms = now_ms + 120; // Immediate second blink
                } else {
                    // Next blink in 2.5s to 5.5s
                    s_next_blink_ms = now_ms + 2500 + (esp_random() % 3000);
                }
            }
        }
    }

    // Smoothly ease openness towards target if not blinking
    if (!s_is_blinking) {
        s_openness += (s_target_openness - s_openness) * 0.25f;
    }

    // 6. Gaze Shifts (Saccades in IDLE)
    if (s_current_state == FACE_IDLE) {
        if (s_next_gaze_shift_ms == 0 || now_ms >= s_next_gaze_shift_ms) {
            int r = esp_random() % 100;
            if (r < 35) {
                s_target_gaze_x = -4.0f; // Look left
                s_target_gaze_y = 0.0f;
            } else if (r < 70) {
                s_target_gaze_x = 4.0f;  // Look right
                s_target_gaze_y = 0.0f;
            } else {
                s_target_gaze_x = 0.0f;  // Center
                s_target_gaze_y = 0.0f;
            }
            s_gaze_return_ms = now_ms + 1000 + (esp_random() % 1200);
            s_next_gaze_shift_ms = now_ms + 3500 + (esp_random() % 3000);
        } else if (s_gaze_return_ms > 0 && now_ms >= s_gaze_return_ms) {
            s_target_gaze_x = 0.0f;
            s_target_gaze_y = 0.0f;
            s_gaze_return_ms = 0;
        }
    } else if (s_current_state == FACE_THINKING) {
        s_target_gaze_x = -4.0f;
        s_target_gaze_y = -3.0f;
    } else {
        s_target_gaze_x = 0.0f;
        s_target_gaze_y = 0.0f;
    }

    // Smooth saccadic gaze interpolation
    s_gaze_x += (s_target_gaze_x - s_gaze_x) * 0.20f;
    s_gaze_y += (s_target_gaze_y - s_gaze_y) * 0.20f;
}

// -------------------------------------------------------------
// Standalone Expression Demo Controller
// -------------------------------------------------------------
static void demo_mode_tick(int64_t now_ms)
{
    if (!s_demo_active) return;

    if (now_ms >= s_demo_step_until_ms) {
        s_demo_step++;
        switch (s_demo_step) {
            case 1:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 1/12] State: BOOT - Eyes opening gently, 'REGGI BOT'");
                s_current_state = FACE_BOOT;
                s_state_start_ms = now_ms;
                s_openness = 0.0f;
                s_demo_step_until_ms = now_ms + 1800;
                break;
            case 2:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 2/12] State: IDLE - Normal resting eyes");
                s_current_state = FACE_IDLE;
                s_target_gaze_x = 0; s_target_gaze_y = 0;
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 3:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 3/12] State: BLINK - Natural automatic eyelid blink");
                s_is_blinking = true;
                s_blink_start_ms = now_ms;
                s_blink_duration_ms = 200;
                s_demo_step_until_ms = now_ms + 1500;
                break;
            case 4:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 4/12] State: LOOK LEFT - Gaze saccade left");
                s_target_gaze_x = -5.0f; s_target_gaze_y = 0;
                s_demo_step_until_ms = now_ms + 1600;
                break;
            case 5:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 5/12] State: LOOK RIGHT - Gaze saccade right");
                s_target_gaze_x = 5.0f; s_target_gaze_y = 0;
                s_demo_step_until_ms = now_ms + 1600;
                break;
            case 6:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 6/12] State: LISTENING - Wide bright electric eyes");
                s_current_state = FACE_LISTENING;
                s_target_gaze_x = 0; s_target_gaze_y = 0;
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 7:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 7/12] State: THINKING - Looking up-left + orbital dots");
                s_current_state = FACE_THINKING;
                s_demo_step_until_ms = now_ms + 2500;
                break;
            case 8:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 8/12] State: SPEAKING - Soundwave mouth bars animating");
                s_current_state = FACE_SPEAKING;
                s_audio_level_in = 0.8f;
                s_demo_step_until_ms = now_ms + 3000;
                break;
            case 9:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 9/12] State: HAPPY - Cheerful smiling crescent eyes");
                s_current_state = FACE_HAPPY;
                s_happy_until_ms = now_ms + 2200;
                s_demo_step_until_ms = now_ms + 2200;
                break;
            case 10:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 10/12] State: ERROR - Slanted coral eyes, 'ERR: SERVER'");
                s_current_state = FACE_ERROR;
                strncpy(s_error_msg, "ERR: SERVER", sizeof(s_error_msg) - 1);
                s_demo_step_until_ms = now_ms + 2200;
                break;
            case 11:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 11/12] State: DISCONNECTED - Sleepy droopy eyes, 'OFFLINE'");
                s_current_state = FACE_DISCONNECTED;
                s_wifi_status = WIFI_OFF;
                s_demo_step_until_ms = now_ms + 2200;
                break;
            case 12:
            default:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 12/12] Demo Complete! Returning to normal IDLE state.");
                s_current_state = FACE_IDLE;
                s_wifi_status = WIFI_CONNECTED_STABLE;
                s_error_msg[0] = '\0';
                s_demo_active = false;
                break;
        }
    }
}

// -------------------------------------------------------------
// Face Animation Main Task (Cap: 20 FPS = 50ms period)
// -------------------------------------------------------------
static void face_task(void *arg)
{
    ESP_LOGI(TAG_FACE, "Face animation rendering task started (Priority 2, 20 FPS)");

    const TickType_t frame_period_ticks = pdMS_TO_TICKS(50); // 20 FPS
    s_fps_window_start_ms = esp_timer_get_time() / 1000;

    while (true) {
        int64_t t_frame_start = esp_timer_get_time();
        int64_t now_ms = t_frame_start / 1000;

        // 1. Check live audio level from audio_player
        float player_lev = audio_player_get_level();
        if (player_lev > 0.01f) {
            s_audio_level_in = player_lev;
        }

        // 2. Demo tick or physics update
        if (s_demo_active) {
            demo_mode_tick(now_ms);
        }
        update_animation_physics(now_ms);

        // 3. Render frame into RAM framebuffer
        // Clear screen to solid black
        memset(s_framebuf, 0, SCREEN_W * SCREEN_H * sizeof(uint16_t));

        // Render layers
        render_header();
        render_face(now_ms);
        render_status_banner();

        // 4. Push rendered frame to ST7735 display via SPI DMA in one blast
        if (s_disp) {
            board_display_send_bitmap(s_disp, 0, 0, SCREEN_W, SCREEN_H, s_framebuf);
        }

        // 5. Measure render metrics
        int64_t t_frame_end = esp_timer_get_time();
        float render_ms = (float)(t_frame_end - t_frame_start) / 1000.0f;

        s_metric_avg_render_ms = (s_metric_avg_render_ms == 0.0f) ? render_ms : (s_metric_avg_render_ms * 0.95f + render_ms * 0.05f);
        if (render_ms > s_metric_max_render_ms) {
            s_metric_max_render_ms = render_ms;
        }

        s_frames_rendered++;
        if (now_ms - s_fps_window_start_ms >= 1000) {
            s_metric_fps = (float)s_frames_rendered * 1000.0f / (float)(now_ms - s_fps_window_start_ms);
            s_frames_rendered = 0;
            s_fps_window_start_ms = now_ms;
        }

        // Yield to other tasks for remaining frame time
        vTaskDelay(frame_period_ticks);
    }
}

// -------------------------------------------------------------
// Public APIs
// -------------------------------------------------------------
esp_err_t board_face_init(board_display_t *disp)
{
    if (!disp) return ESP_FAIL;
    s_disp = disp;

    if (!s_framebuf) {
        // Allocate 40KB in DMA-capable internal RAM
        s_framebuf = (uint16_t *)heap_caps_malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t),
                                                  MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
        if (!s_framebuf) {
            ESP_LOGW(TAG_FACE, "DMA internal RAM alloc failed, trying default caps");
            s_framebuf = (uint16_t *)malloc(SCREEN_W * SCREEN_H * sizeof(uint16_t));
        }
        if (!s_framebuf) {
            ESP_LOGE(TAG_FACE, "Fatal: Out of memory for face framebuffer!");
            return ESP_ERR_NO_MEM;
        }
        memset(s_framebuf, 0, SCREEN_W * SCREEN_H * sizeof(uint16_t));
    }

    if (!s_face_mutex) {
        s_face_mutex = xSemaphoreCreateMutex();
    }

    s_state_start_ms = esp_timer_get_time() / 1000;
    s_current_state = FACE_BOOT;
    s_openness = 0.0f;

    if (!s_face_task_handle) {
        BaseType_t ok = xTaskCreate(face_task, "face_task", 4096, NULL, 2, &s_face_task_handle);
        if (ok != pdPASS) {
            ESP_LOGE(TAG_FACE, "Failed to create face animation task!");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG_FACE, "Face Engine initialized successfully (Framebuffer: %zu bytes)",
             SCREEN_W * SCREEN_H * sizeof(uint16_t));
    return ESP_OK;
}

void board_face_set_state(face_state_t state)
{
    if (s_demo_active) return; // Don't override while demo is running

    if (s_current_state != state) {
        s_current_state = state;
        s_state_start_ms = esp_timer_get_time() / 1000;
        s_custom_status[0] = '\0';

        if (state == FACE_ERROR) {
            s_error_until_ms = s_state_start_ms + 3000;
        }
    }
}

face_state_t board_face_get_state(void)
{
    return s_current_state;
}

void board_face_set_error(face_error_type_t err_type, const char *short_msg)
{
    s_error_type = err_type;
    if (short_msg && strlen(short_msg) > 0) {
        snprintf(s_error_msg, sizeof(s_error_msg), "ERR: %s", short_msg);
    } else {
        switch (err_type) {
            case FACE_ERR_WIFI:   strncpy(s_error_msg, "ERR: WIFI", sizeof(s_error_msg) - 1); break;
            case FACE_ERR_SERVER: strncpy(s_error_msg, "ERR: SERVER", sizeof(s_error_msg) - 1); break;
            case FACE_ERR_STT:    strncpy(s_error_msg, "ERR: STT", sizeof(s_error_msg) - 1); break;
            case FACE_ERR_LLM:    strncpy(s_error_msg, "ERR: LLM", sizeof(s_error_msg) - 1); break;
            case FACE_ERR_TTS:    strncpy(s_error_msg, "ERR: TTS", sizeof(s_error_msg) - 1); break;
            case FACE_ERR_AUDIO:  strncpy(s_error_msg, "ERR: AUDIO", sizeof(s_error_msg) - 1); break;
            default:              strncpy(s_error_msg, "ERR: GENERIC", sizeof(s_error_msg) - 1); break;
        }
    }
    board_face_set_state(FACE_ERROR);
}

void board_face_trigger_happy(uint32_t duration_ms)
{
    s_current_state = FACE_HAPPY;
    s_state_start_ms = esp_timer_get_time() / 1000;
    s_happy_until_ms = s_state_start_ms + ((duration_ms > 0) ? duration_ms : 1200);
}

void board_face_set_status_text(const char *text)
{
    if (text) {
        strncpy(s_custom_status, text, sizeof(s_custom_status) - 1);
        s_custom_status[sizeof(s_custom_status) - 1] = '\0';
    } else {
        s_custom_status[0] = '\0';
    }
}

void board_face_set_audio_level(float level)
{
    if (level < 0.0f) level = 0.0f;
    if (level > 1.0f) level = 1.0f;
    s_audio_level_in = level;
}

void board_face_set_wifi_status(wifi_status_t status)
{
    wifi_status_t prev = s_wifi_status;
    s_wifi_status = status;

    if (status == WIFI_OFF || status == WIFI_ERROR) {
        if (s_current_state != FACE_BOOT && s_current_state != FACE_ERROR) {
            board_face_set_state(FACE_DISCONNECTED);
        }
    } else if ((status == WIFI_CONNECTED || status == WIFI_CONNECTED_STABLE) &&
               (prev == WIFI_OFF || prev == WIFI_ERROR || s_current_state == FACE_DISCONNECTED)) {
        // Reconnected! Flash happy briefly
        board_face_trigger_happy(1200);
    }
}

void board_face_set_battery_state(battery_state_t bat)
{
    s_bat_state = bat;
}

void board_face_trigger_demo(void)
{
    s_demo_active = true;
    s_demo_step = 0;
    s_demo_step_until_ms = esp_timer_get_time() / 1000;
    ESP_LOGI(TAG_FACE, "Starting Standalone Expression Demonstration Mode (12 States)...");
}

bool board_face_is_demo_active(void)
{
    return s_demo_active;
}

void board_face_get_metrics(float *out_fps, float *out_avg_render_ms, float *out_max_render_ms)
{
    if (out_fps) *out_fps = s_metric_fps;
    if (out_avg_render_ms) *out_avg_render_ms = s_metric_avg_render_ms;
    if (out_max_render_ms) *out_max_render_ms = s_metric_max_render_ms;
}
