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

// Colors in Big-Endian for ST7735 SPI DMA transfer
#define RGB565_BE(r, g, b) (__builtin_bswap16(((uint16_t)((r) & 0xF8) << 8) | ((uint16_t)((g) & 0xFC) << 3) | ((uint16_t)(b) >> 3)))

#define COL_BLACK        RGB565_BE(0,   0,   0)
#define COL_CYAN         RGB565_BE(0,   229, 255) // Primary friendly electric cyan
#define COL_CYAN_BRIGHT  RGB565_BE(90,  255, 255) // Listening / excited
#define COL_CYAN_DIM     RGB565_BE(40,  110, 140) // Sleepy / offline
#define COL_AMBER        RGB565_BE(255, 195, 40)  // Thinking
#define COL_MINT         RGB565_BE(0,   255, 170) // Happy soft & big
#define COL_WHITE        RGB565_BE(255, 255, 255) // Specular highlights
#define COL_ICE_BLUE     RGB565_BE(200, 245, 255) // Surprised
#define COL_ERROR        RGB565_BE(255, 60,  60)  // Coral red
#define COL_ORANGE       RGB565_BE(255, 140, 20)
#define COL_GREEN        RGB565_BE(0,   255, 70)  // Full battery / Wi-Fi
#define COL_YELLOW       RGB565_BE(255, 220, 0)
#define COL_GRAY         RGB565_BE(85,  85,  85)
#define COL_DARK_GRAY    RGB565_BE(30,  35,  45)
#define COL_STATUS_BG    RGB565_BE(10,  12,  18)

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

// Poses: Current interpolated pose and Target desired pose
static face_pose_t s_cur_pose;
static face_pose_t s_tgt_pose;

// Timers and transitions
static int64_t s_state_start_ms = 0;
static int64_t s_timed_hold_until_ms = 0;
static int64_t s_last_interaction_ms = 0;
static bool s_is_sleepy = false;

// Independent Eye Blinking State
static bool s_blink_active = false;
static int64_t s_blink_start_ms = 0;
static int32_t s_blink_dur_ms = 180;
static int32_t s_blink_r_delay_ms = 18; // Organic asymmetry: Right eye lags left by ~18ms
static bool s_double_blink_pending = false;
static int64_t s_next_blink_ms = 0;

// Idle Saccades & Micro-Expressions
static int64_t s_next_glance_ms = 0;
static int64_t s_glance_return_ms = 0;
static int64_t s_next_curious_ms = 0;
static int64_t s_curious_return_ms = 0;
static int64_t s_next_playful_ms = 0;
static int64_t s_playful_return_ms = 0;

// Thinking Orbital Sub-poses
static int s_thinking_subpose = 0;
static int64_t s_next_thinking_sub_ms = 0;

// Demo Mode (27 Steps)
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

// -------------------------------------------------------------
// Framebuffer Primitives
// -------------------------------------------------------------
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

static void fb_draw_arc(int cx, int cy, int ro, int thickness, uint16_t col)
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

static void fb_draw_sad_arc(int cx, int cy, int ro, int thickness, uint16_t col)
{
    int ri = ro - thickness;
    int ro2 = ro * ro;
    int ri2 = ri * ri;

    for (int y = cy; y <= cy + ro; y++) {
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
    } else if (ch == '?') {
        fb_fill_rect(x, y, 3 * scale, scale, col);
        fb_fill_rect(x + 2 * scale, y + scale, scale, scale, col);
        fb_fill_rect(x + scale, y + 2 * scale, scale, scale, col);
        fb_fill_rect(x + scale, y + 4 * scale, scale, scale, col);
        return;
    } else if (ch == '(' || ch == ')') {
        fb_fill_rect(x + scale, y, scale, 5 * scale, col);
        return;
    } else if (ch == ';') {
        fb_fill_rect(x + scale, y + 1 * scale, scale, scale, col);
        fb_fill_rect(x + scale, y + 3 * scale, scale, 2 * scale, col);
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
// Eye Rasterization: 5 Shape Families & Dynamic Parameters
// -------------------------------------------------------------
static void fb_render_eye(const eye_params_t *eye, float blink_factor)
{
    // 1. Calculate effective geometry
    float eff_open = eye->openness * blink_factor;
    int eff_h = (int)roundf(eye->height * eye->stretch * eff_open);
    if (eff_h < 3) eff_h = 3; // Sleek closed eyelid slit minimum

    int eff_w = (int)roundf(eye->width * eye->squash);
    if (eff_w < 6) eff_w = 6;

    int cx = (int)roundf(eye->center_x + eye->gaze_x);
    int cy = (int)roundf(eye->center_y + eye->gaze_y);

    int x_left = cx - eff_w / 2;
    int y_top = cy - eff_h / 2;

    // 2. Family A: ARC (Happy smiling crescent)
    if (eye->shape == EYE_SHAPE_ARC) {
        int arc_ro = eff_w / 2;
        int arc_thick = 5;
        if (arc_thick > arc_ro / 2) arc_thick = arc_ro / 2;
        fb_draw_arc(cx, cy + 4, arc_ro, arc_thick, eye->color);
        return;
    }

    // 3. Family B: OVAL (Ellipse)
    if (eye->shape == EYE_SHAPE_OVAL) {
        int rx = eff_w / 2;
        int ry = eff_h / 2;
        if (rx <= 0 || ry <= 0) return;

        for (int y = cy - ry; y <= cy + ry; y++) {
            if (y < 0 || y >= SCREEN_H) continue;
            int dy = y - cy;
            float ratio = (float)(dy * dy) / (float)(ry * ry);
            if (ratio > 1.0f) continue;
            int dx = (int)roundf((float)rx * sqrtf(1.0f - ratio));

            int start_x = cx - dx;
            int end_x = cx + dx;
            if (start_x < 0) start_x = 0;
            if (end_x >= SCREEN_W) end_x = SCREEN_W - 1;

            if (end_x >= start_x) {
                uint16_t *row = &s_framebuf[y * SCREEN_W];
                for (int px = start_x; px <= end_x; px++) {
                    row[px] = eye->color;
                }
            }
        }
    } else {
        // 4. Family C, D, E: ROUNDED, SQUINT, HALF-LID
        int r = (int)roundf(eye->corner_radius);
        if (eye->shape == EYE_SHAPE_SQUINT) {
            r = eff_h / 2;
        }
        if (r > eff_w / 2) r = eff_w / 2;
        if (r > eff_h / 2) r = eff_h / 2;

        int cut_top_px = (int)roundf((float)eff_h * eye->top_lid);
        int cut_bot_px = (int)roundf((float)eff_h * eye->bottom_lid);
        float tilt_tan = tanf(eye->tilt_deg * 3.14159f / 180.0f);

        for (int j = 0; j < eff_h; j++) {
            if (j < cut_top_px) continue;
            if (j >= eff_h - cut_bot_px) continue;

            int dx = 0;
            if (j < r) {
                int dy = r - 1 - j;
                dx = r - (int)roundf(sqrtf((float)(r * r - dy * dy)));
            } else if (j >= eff_h - r) {
                int dy = j - (eff_h - r);
                dx = r - (int)roundf(sqrtf((float)(r * r - dy * dy)));
            }

            int py = y_top + j;
            if (py < 0 || py >= SCREEN_H) continue;

            int start_x = x_left + dx;
            int end_x = x_left + eff_w - 1 - dx;

            // Apply tilt slant to top lid boundary
            if (eye->tilt_deg != 0.0f && j < eff_h / 2) {
                int slant_dy = (int)roundf((float)(dx - eff_w / 4) * tilt_tan);
                if (j < cut_top_px + slant_dy) continue;
            }

            if (start_x < 0) start_x = 0;
            if (end_x >= SCREEN_W) end_x = SCREEN_W - 1;

            if (end_x >= start_x) {
                uint16_t *row = &s_framebuf[py * SCREEN_W];
                for (int px = start_x; px <= end_x; px++) {
                    row[px] = eye->color;
                }
            }
        }
    }

    // 5. Specular Highlights / Gleam (Organic Liveliness)
    if (eye->highlight_int > 0.15f && eff_open > 0.40f && eye->shape != EYE_SHAPE_ARC) {
        int gx = cx + eff_w / 4 + (int)(eye->gaze_x * 0.3f);
        int gy = cy - eff_h / 4 + (int)(eye->gaze_y * 0.3f);
        int gleam_size = (eff_w > 30) ? 4 : 3;

        // Primary glossy specular shine
        fb_fill_rect(gx - gleam_size / 2, gy - gleam_size / 2, gleam_size, gleam_size, COL_WHITE);

        // Secondary subtle specular mini-gleam
        if (eff_h > 36) {
            int gx2 = cx - eff_w / 4;
            int gy2 = cy + eff_h / 6;
            fb_fill_rect(gx2, gy2, 2, 2, COL_WHITE);
        }
    }
}

// -------------------------------------------------------------
// Top Header: Minimal Wi-Fi & Multi-State Battery
// -------------------------------------------------------------
static void render_header(int64_t now_ms)
{
    // 1. Wi-Fi Indicator at (X=4, Y=3)
    const int wx = 4;
    const int wy = 3;
    uint16_t wcol = COL_GREEN;
    bool draw_strike = false;

    if (s_wifi_status == WIFI_CONNECTED || s_wifi_status == WIFI_CONNECTED_STABLE) {
        wcol = COL_GREEN;
    } else if (s_wifi_status == WIFI_CONNECTING) {
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
        for (int i = 0; i < 11; i++) {
            fb_set_pixel(wx + i, wy + i, COL_ERROR);
        }
    }

    // 2. Battery Indicator at (X=104, Y=3)
    const int bx = 104;
    const int by = 3;
    uint16_t bcol = COL_GREEN;
    int bars = 3;

    if (s_bat_state == BAT_CHARGING) {
        // Charging Animation: cycle 1 -> 2 -> 3 bars smoothly
        int step = (int)((now_ms / 280) % 4);
        bars = (step == 0) ? 1 : (step == 1 ? 2 : 3);
        bcol = COL_GREEN;
    } else if (s_bat_state == BAT_FULL) {
        bars = 3;
        bcol = COL_GREEN; // Full battery: Green
    } else if (s_bat_state == BAT_MED) {
        // 2 bars: Red as explicitly requested ("tinggal 2 bar berarti warna merah")
        bars = 2;
        bcol = COL_ERROR;
    } else if (s_bat_state == BAT_LOW) {
        bars = 1;
        bcol = COL_ERROR; // <30% 1 bar: Red
    } else if (s_bat_state == BAT_CRIT) {
        bars = 1;
        bcol = ((now_ms / 220) % 2 == 0) ? COL_ERROR : COL_DARK_GRAY; // Critical: Blinking Red
    }

    // Battery Shell Outline (17x9)
    fb_fill_rect(bx, by, 17, 1, COL_GRAY);
    fb_fill_rect(bx, by + 8, 17, 1, COL_GRAY);
    fb_fill_rect(bx, by, 1, 9, COL_GRAY);
    fb_fill_rect(bx + 16, by, 1, 9, COL_GRAY);
    // Battery Nipple
    fb_fill_rect(bx + 17, by + 2, 2, 5, (s_bat_state == BAT_CHARGING) ? COL_YELLOW : COL_GRAY);

    // Inner battery bars (3 slots, each 4x5 px)
    for (int i = 0; i < bars; i++) {
        fb_fill_rect(bx + 2 + i * 5, by + 2, 4, 5, bcol);
    }

    // Charging indicator symbol (lightning spark icon)
    if (s_bat_state == BAT_CHARGING) {
        fb_set_pixel(bx + 7, by + 3, COL_YELLOW);
        fb_set_pixel(bx + 8, by + 3, COL_YELLOW);
        fb_set_pixel(bx + 8, by + 4, COL_YELLOW);
        fb_set_pixel(bx + 9, by + 4, COL_YELLOW);
        fb_set_pixel(bx + 9, by + 5, COL_YELLOW);
    }
}

// -------------------------------------------------------------
// Mouth & Audio Modulation Rendering
// -------------------------------------------------------------
static void render_mouth(int64_t now_ms)
{
    if (s_cur_pose.mouth_policy == MOUTH_POLICY_NONE) {
        return;
    }

    const int mx = SCREEN_W / 2;
    const int my = 116;

    if (s_cur_pose.mouth_policy == MOUTH_POLICY_SPEAKING) {
        float lev = s_audio_level_in;
        // Non-blocking time-based fallback if audio level is quiet/zero
        float time_sec = (float)now_ms / 1000.0f;
        float synth = 0.28f + 0.35f * fabsf(sinf(time_sec * 11.0f));
        if (lev < 0.08f) lev = synth;

        const int num_bars = 5;
        const int bar_w = 4;
        const int bar_gap = 4;
        const float weights[5] = {0.55f, 0.90f, 1.0f, 0.90f, 0.55f};

        int total_w = num_bars * bar_w + (num_bars - 1) * bar_gap;
        int start_x = mx - total_w / 2;

        for (int i = 0; i < num_bars; i++) {
            int bh = 3 + (int)roundf(lev * weights[i] * 18.0f);
            if (bh > 22) bh = 22;
            int bx = start_x + i * (bar_w + bar_gap);
            int by = my - bh / 2;
            int r = (bh >= 6) ? 2 : 1;
            fb_fill_rect(bx, by, bar_w, bh, COL_CYAN_BRIGHT);
            if (r > 0) {
                fb_set_pixel(bx, by, COL_BLACK);
                fb_set_pixel(bx + bar_w - 1, by, COL_BLACK);
                fb_set_pixel(bx, by + bh - 1, COL_BLACK);
                fb_set_pixel(bx + bar_w - 1, by + bh - 1, COL_BLACK);
            }
        }
    } else if (s_cur_pose.mouth_policy == MOUTH_POLICY_SMILE) {
        fb_draw_arc(mx, my, 10, 3, COL_MINT);
    } else if (s_cur_pose.mouth_policy == MOUTH_POLICY_SAD) {
        fb_draw_sad_arc(mx, my - 4, 10, 3, COL_ERROR);
    } else if (s_cur_pose.mouth_policy == MOUTH_POLICY_SURPRISE) {
        fb_fill_rect(mx - 3, my - 5, 6, 10, COL_ICE_BLUE);
        fb_fill_rect(mx - 1, my - 3, 2, 6, COL_BLACK);
    }
}

// -------------------------------------------------------------
// Thinking Orbital Particles
// -------------------------------------------------------------
static void render_thinking_orbit(int64_t now_ms)
{
    if (s_current_state != FACE_THINKING) return;

    float t = (float)now_ms / 1000.0f;
    const int dot_y = 116;
    const int dot_cx = SCREEN_W / 2;

    for (int i = 0; i < 3; i++) {
        float phase = t * 6.5f - (float)i * 1.1f;
        float r_scale = 1.0f + 1.3f * (0.5f + 0.5f * sinf(phase));
        int dot_r = (int)roundf(r_scale);
        int dx = (i - 1) * 14;
        fb_fill_rect(dot_cx + dx - dot_r, dot_y - dot_r, dot_r * 2, dot_r * 2, COL_AMBER);
    }
}

// -------------------------------------------------------------
// Bottom Minimal Status Banner
// -------------------------------------------------------------
static void render_status_banner(void)
{
    const int by = 146;
    const int bh = 14;

    fb_fill_rect(0, by, SCREEN_W, bh, COL_STATUS_BG);
    fb_fill_rect(0, by, SCREEN_W, 1, COL_DARK_GRAY);

    const char *label = "READY";
    uint16_t txt_col = COL_WHITE;
    int scale = 1;

    if (s_demo_active && s_tgt_pose.status_label[0] != '\0') {
        label = s_tgt_pose.status_label;
        txt_col = COL_CYAN_BRIGHT;
    } else if (s_custom_status[0] != '\0') {
        label = s_custom_status;
        txt_col = COL_WHITE;
    } else {
        switch (s_current_state) {
            case FACE_BOOT:         label = "REGGI BOT"; txt_col = COL_CYAN; break;
            case FACE_IDLE:         label = s_is_sleepy ? "SLEEPY..." : "READY"; txt_col = COL_WHITE; break;
            case FACE_LISTENING:    label = "LISTENING"; txt_col = COL_CYAN_BRIGHT; break;
            case FACE_THINKING:     label = "THINKING"; txt_col = COL_AMBER; break;
            case FACE_SPEAKING:     label = "SPEAKING"; txt_col = COL_CYAN_BRIGHT; break;
            case FACE_HAPPY:        label = "HAPPY :)"; txt_col = COL_MINT; break;
            case FACE_ERROR:        label = (s_error_msg[0] != '\0') ? s_error_msg : "ERROR"; txt_col = COL_ERROR; break;
            case FACE_DISCONNECTED: label = "OFFLINE"; txt_col = COL_ORANGE; break;
        }
    }

    int text_y = by + (bh - 5 * scale) / 2;
    fb_draw_text_centered(text_y, label, txt_col, scale);
}

// -------------------------------------------------------------
// Pose Presets & Initialization
// -------------------------------------------------------------
static void pose_init_neutral_friendly(face_pose_t *p)
{
    memset(p, 0, sizeof(face_pose_t));

    // Left Eye: Baseline friendly rounded rectangle
    p->left_eye.center_x = 38.0f;
    p->left_eye.center_y = 68.0f;
    p->left_eye.width = 33.0f;
    p->left_eye.height = 48.0f;
    p->left_eye.corner_radius = 11.0f;
    p->left_eye.openness = 1.0f;
    p->left_eye.squash = 1.0f;
    p->left_eye.stretch = 1.0f;
    p->left_eye.highlight_int = 1.0f;
    p->left_eye.color = COL_CYAN;
    p->left_eye.shape = EYE_SHAPE_ROUNDED;

    // Right Eye: Subtle controlled organic asymmetry (+3% height)
    p->right_eye.center_x = 90.0f;
    p->right_eye.center_y = 68.0f;
    p->right_eye.width = 34.0f;
    p->right_eye.height = 49.5f;
    p->right_eye.corner_radius = 11.0f;
    p->right_eye.openness = 1.0f;
    p->right_eye.squash = 1.0f;
    p->right_eye.stretch = 1.0f;
    p->right_eye.highlight_int = 1.0f;
    p->right_eye.color = COL_CYAN;
    p->right_eye.shape = EYE_SHAPE_ROUNDED;

    p->blink_policy = BLINK_POLICY_NORMAL;
    p->mouth_policy = MOUTH_POLICY_NONE;
    strncpy(p->status_label, "READY", sizeof(p->status_label) - 1);
}

static void pose_init_curious(face_pose_t *p, bool left_larger)
{
    pose_init_neutral_friendly(p);

    if (left_larger) {
        // Left eye enlarged +24%, raised 3px
        p->left_eye.width = 39.0f;
        p->left_eye.height = 57.0f;
        p->left_eye.center_y = 65.0f;
        p->left_eye.tilt_deg = +4.0f; // Expressive quizzical brow tilt
        p->left_eye.color = COL_CYAN_BRIGHT;

        // Right eye slightly smaller -12%
        p->right_eye.width = 29.0f;
        p->right_eye.height = 43.0f;
        p->right_eye.center_y = 70.0f;
        p->right_eye.tilt_deg = -2.0f;

        p->global_gaze_x = -4.0f; // Looks towards the enlarged eye
    } else {
        // Right eye enlarged +24%, raised 3px
        p->left_eye.width = 29.0f;
        p->left_eye.height = 43.0f;
        p->left_eye.center_y = 70.0f;
        p->left_eye.tilt_deg = +2.0f;

        p->right_eye.width = 39.0f;
        p->right_eye.height = 57.0f;
        p->right_eye.center_y = 65.0f;
        p->right_eye.tilt_deg = -4.0f;
        p->right_eye.color = COL_CYAN_BRIGHT;

        p->global_gaze_x = 4.0f;
    }

    p->blink_policy = BLINK_POLICY_CURIOUS;
    strncpy(p->status_label, "CURIOUS", sizeof(p->status_label) - 1);
}

static void pose_init_attentive_listening(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    // Both eyes taller & wider, electric cyan
    p->left_eye.width = 36.0f;
    p->left_eye.height = 54.0f;
    p->left_eye.corner_radius = 12.0f;
    p->left_eye.color = COL_CYAN_BRIGHT;

    p->right_eye.width = 36.5f;
    p->right_eye.height = 54.5f;
    p->right_eye.corner_radius = 12.0f;
    p->right_eye.color = COL_CYAN_BRIGHT;

    strncpy(p->status_label, "LISTENING", sizeof(p->status_label) - 1);
}

static void pose_init_thinking(face_pose_t *p, int subpose)
{
    pose_init_neutral_friendly(p);

    p->left_eye.color = COL_AMBER;
    p->right_eye.color = COL_AMBER;

    if (subpose == 0) {
        // Look up-left
        p->left_eye.width = 29.0f;
        p->left_eye.height = 44.0f;
        p->left_eye.top_lid = 0.22f;
        p->left_eye.gaze_x = -4.0f;
        p->left_eye.gaze_y = -3.5f;

        p->right_eye.width = 35.0f;
        p->right_eye.height = 49.0f;
        p->right_eye.top_lid = 0.15f;
        p->right_eye.gaze_x = -4.0f;
        p->right_eye.gaze_y = -3.5f;
    } else if (subpose == 1) {
        // Look up-right
        p->left_eye.width = 35.0f;
        p->left_eye.height = 49.0f;
        p->left_eye.top_lid = 0.15f;
        p->left_eye.gaze_x = 4.0f;
        p->left_eye.gaze_y = -3.5f;

        p->right_eye.width = 29.0f;
        p->right_eye.height = 44.0f;
        p->right_eye.top_lid = 0.22f;
        p->right_eye.gaze_x = 4.0f;
        p->right_eye.gaze_y = -3.5f;
    } else {
        // Micro-squint thinking pause
        p->left_eye.top_lid = 0.35f;
        p->right_eye.top_lid = 0.35f;
        p->left_eye.gaze_x = -2.0f;
        p->right_eye.gaze_x = -2.0f;
    }

    strncpy(p->status_label, "THINKING", sizeof(p->status_label) - 1);
}

static void pose_init_speaking(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    // Warm, friendly soft squint while speaking
    p->left_eye.height = 44.0f;
    p->left_eye.top_lid = 0.12f;
    p->left_eye.color = COL_CYAN_BRIGHT;

    p->right_eye.height = 44.5f;
    p->right_eye.top_lid = 0.12f;
    p->right_eye.color = COL_CYAN_BRIGHT;

    p->mouth_policy = MOUTH_POLICY_SPEAKING;
    strncpy(p->status_label, "SPEAKING", sizeof(p->status_label) - 1);
}

static void pose_init_happy_soft(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    // Soft happy squint
    p->left_eye.shape = EYE_SHAPE_SQUINT;
    p->left_eye.height = 20.0f;
    p->left_eye.width = 34.0f;
    p->left_eye.tilt_deg = +5.0f;
    p->left_eye.color = COL_MINT;

    p->right_eye.shape = EYE_SHAPE_SQUINT;
    p->right_eye.height = 20.0f;
    p->right_eye.width = 34.0f;
    p->right_eye.tilt_deg = -5.0f;
    p->right_eye.color = COL_MINT;

    p->blink_policy = BLINK_POLICY_NONE;
    p->mouth_policy = MOUTH_POLICY_SMILE;
    strncpy(p->status_label, "HAPPY :)", sizeof(p->status_label) - 1);
}

static void pose_init_happy_big(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    // Crescent smiling arcs (^ ^)
    p->left_eye.shape = EYE_SHAPE_ARC;
    p->left_eye.width = 34.0f;
    p->left_eye.height = 34.0f;
    p->left_eye.center_y = 66.0f;
    p->left_eye.color = COL_MINT;

    p->right_eye.shape = EYE_SHAPE_ARC;
    p->right_eye.width = 34.0f;
    p->right_eye.height = 34.0f;
    p->right_eye.center_y = 66.0f;
    p->right_eye.color = COL_MINT;

    p->blink_policy = BLINK_POLICY_NONE;
    p->mouth_policy = MOUTH_POLICY_SMILE;
    strncpy(p->status_label, "HAPPY :D", sizeof(p->status_label) - 1);
}

static void pose_init_excited(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    p->left_eye.width = 38.0f;
    p->left_eye.height = 56.0f;
    p->left_eye.center_y = 66.0f;
    p->left_eye.squash = 1.05f;
    p->left_eye.color = COL_CYAN_BRIGHT;

    p->right_eye.width = 38.0f;
    p->right_eye.height = 56.0f;
    p->right_eye.center_y = 67.5f;
    p->right_eye.squash = 1.05f;
    p->right_eye.color = COL_CYAN_BRIGHT;

    p->mouth_policy = MOUTH_POLICY_SMILE;
    strncpy(p->status_label, "EXCITED!", sizeof(p->status_label) - 1);
}

static void pose_init_surprised(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    // Tall smooth ovals
    p->left_eye.shape = EYE_SHAPE_OVAL;
    p->left_eye.width = 32.0f;
    p->left_eye.height = 58.0f;
    p->left_eye.color = COL_ICE_BLUE;

    p->right_eye.shape = EYE_SHAPE_OVAL;
    p->right_eye.width = 32.0f;
    p->right_eye.height = 58.0f;
    p->right_eye.color = COL_ICE_BLUE;

    p->mouth_policy = MOUTH_POLICY_SURPRISE;
    strncpy(p->status_label, "WHOA!", sizeof(p->status_label) - 1);
}

static void pose_init_playful_squint(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    // One eye squints playfully, other stays open with soft gaze
    p->left_eye.shape = EYE_SHAPE_SQUINT;
    p->left_eye.height = 14.0f;
    p->left_eye.width = 32.0f;
    p->left_eye.tilt_deg = +4.0f;

    p->right_eye.width = 34.0f;
    p->right_eye.height = 49.0f;
    p->right_eye.tilt_deg = -2.0f;
    p->right_eye.gaze_x = 3.0f;

    p->mouth_policy = MOUTH_POLICY_SMILE;
    strncpy(p->status_label, "HEHE ;)", sizeof(p->status_label) - 1);
}

static void pose_init_wink(face_pose_t *p, bool left_eye)
{
    pose_init_neutral_friendly(p);

    if (left_eye) {
        p->left_eye.openness = 0.08f; // Closed wink slit
        p->left_eye.tilt_deg = +5.0f;

        p->right_eye.openness = 1.0f;
        p->right_eye.width = 35.0f;
        p->right_eye.height = 51.0f;
        p->right_eye.gaze_x = -2.0f;
    } else {
        p->left_eye.openness = 1.0f;
        p->left_eye.width = 35.0f;
        p->left_eye.height = 51.0f;
        p->left_eye.gaze_x = 2.0f;

        p->right_eye.openness = 0.08f;
        p->right_eye.tilt_deg = -5.0f;
    }

    p->mouth_policy = MOUTH_POLICY_SMILE;
    strncpy(p->status_label, "WINK ;)", sizeof(p->status_label) - 1);
}

static void pose_init_confused(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    // Asymmetric quizzical expression: Left lowered & half-lid, Right attentive
    p->left_eye.shape = EYE_SHAPE_HALF_LID;
    p->left_eye.width = 30.0f;
    p->left_eye.height = 42.0f;
    p->left_eye.center_y = 72.0f; // Lowered 4px
    p->left_eye.top_lid = 0.45f;
    p->left_eye.tilt_deg = -5.0f;

    p->right_eye.width = 36.0f;
    p->right_eye.height = 52.0f;
    p->right_eye.center_y = 66.0f; // Raised 2px
    p->right_eye.tilt_deg = +4.0f;

    p->global_gaze_x = -3.0f;
    strncpy(p->status_label, "HMM?", sizeof(p->status_label) - 1);
}

static void pose_init_sleepy(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    p->left_eye.shape = EYE_SHAPE_HALF_LID;
    p->left_eye.height = 26.0f;
    p->left_eye.top_lid = 0.60f;
    p->left_eye.gaze_y = 3.0f; // Gaze down
    p->left_eye.color = COL_CYAN_DIM;

    p->right_eye.shape = EYE_SHAPE_HALF_LID;
    p->right_eye.height = 26.0f;
    p->right_eye.top_lid = 0.60f;
    p->right_eye.gaze_y = 3.0f;
    p->right_eye.color = COL_CYAN_DIM;

    p->blink_policy = BLINK_POLICY_SLEEPY;
    strncpy(p->status_label, "SLEEPY...", sizeof(p->status_label) - 1);
}

static void pose_init_error_sad(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    p->left_eye.width = 28.0f;
    p->left_eye.height = 38.0f;
    p->left_eye.center_y = 70.0f;
    p->left_eye.top_lid = 0.28f;
    p->left_eye.tilt_deg = -8.0f; // Sad inward droop
    p->left_eye.color = COL_ERROR;

    p->right_eye.width = 28.0f;
    p->right_eye.height = 38.0f;
    p->right_eye.center_y = 70.0f;
    p->right_eye.top_lid = 0.28f;
    p->right_eye.tilt_deg = +8.0f;
    p->right_eye.color = COL_ERROR;

    p->mouth_policy = MOUTH_POLICY_SAD;
    strncpy(p->status_label, "ERR: SERVER", sizeof(p->status_label) - 1);
}

static void pose_init_disconnected(face_pose_t *p)
{
    pose_init_neutral_friendly(p);

    p->left_eye.shape = EYE_SHAPE_HALF_LID;
    p->left_eye.height = 24.0f;
    p->left_eye.top_lid = 0.65f;
    p->left_eye.color = COL_CYAN_DIM;

    p->right_eye.shape = EYE_SHAPE_HALF_LID;
    p->right_eye.height = 24.0f;
    p->right_eye.top_lid = 0.65f;
    p->right_eye.color = COL_CYAN_DIM;

    strncpy(p->status_label, "OFFLINE", sizeof(p->status_label) - 1);
}

// -------------------------------------------------------------
// Non-Blocking Pose Interpolator (Runs every frame)
// -------------------------------------------------------------
static void interpolate_eye(eye_params_t *cur, const eye_params_t *tgt, float factor)
{
    cur->center_x += (tgt->center_x - cur->center_x) * factor;
    cur->center_y += (tgt->center_y - cur->center_y) * factor;
    cur->width += (tgt->width - cur->width) * factor;
    cur->height += (tgt->height - cur->height) * factor;
    cur->corner_radius += (tgt->corner_radius - cur->corner_radius) * factor;
    cur->tilt_deg += (tgt->tilt_deg - cur->tilt_deg) * factor;
    cur->openness += (tgt->openness - cur->openness) * factor;
    cur->top_lid += (tgt->top_lid - cur->top_lid) * factor;
    cur->bottom_lid += (tgt->bottom_lid - cur->bottom_lid) * factor;
    cur->gaze_x += (tgt->gaze_x - cur->gaze_x) * factor;
    cur->gaze_y += (tgt->gaze_y - cur->gaze_y) * factor;
    cur->squash += (tgt->squash - cur->squash) * factor;
    cur->stretch += (tgt->stretch - cur->stretch) * factor;
    cur->curvature += (tgt->curvature - cur->curvature) * factor;
    cur->highlight_int += (tgt->highlight_int - cur->highlight_int) * factor;

    // Instant/near-instant discrete property switch
    if (factor > 0.15f || fabsf(tgt->openness - cur->openness) < 0.25f) {
        cur->shape = tgt->shape;
        cur->color = tgt->color;
    }
}

static void update_pose_physics(int64_t now_ms)
{
    // 1. Boot Sequence
    if (s_current_state == FACE_BOOT) {
        int64_t elapsed = now_ms - s_state_start_ms;
        if (elapsed < 1200) {
            float prog = (float)elapsed / 1200.0f;
            float boot_open = 0.5f - 0.5f * cosf(prog * 3.14159f);
            s_tgt_pose.left_eye.openness = boot_open;
            s_tgt_pose.right_eye.openness = boot_open;
        } else {
            s_current_state = FACE_IDLE;
            s_state_start_ms = now_ms;
            s_last_interaction_ms = now_ms;
            pose_init_neutral_friendly(&s_tgt_pose);
            ESP_LOGI(TAG_FACE, "Boot sequence completed -> Transition to IDLE");
        }
    }

    // 2. Timed Hold Expiry (Happy, Confused, Excited, etc.)
    if (s_timed_hold_until_ms > 0 && now_ms >= s_timed_hold_until_ms) {
        s_timed_hold_until_ms = 0;
        if (s_current_state == FACE_HAPPY || s_current_state == FACE_ERROR) {
            s_current_state = FACE_IDLE;
            s_state_start_ms = now_ms;
            pose_init_neutral_friendly(&s_tgt_pose);
        } else if (s_current_state == FACE_IDLE) {
            pose_init_neutral_friendly(&s_tgt_pose);
        }
    }

    // 3. Thinking Orbital Sub-poses
    if (s_current_state == FACE_THINKING) {
        if (now_ms >= s_next_thinking_sub_ms) {
            s_thinking_subpose = (s_thinking_subpose + 1) % 3;
            pose_init_thinking(&s_tgt_pose, s_thinking_subpose);
            s_next_thinking_sub_ms = now_ms + 900 + (esp_random() % 600);
        }
    }

    // 4. Sleepy Inactivity Detection (35 to 60s without user interaction in IDLE)
    if (s_current_state == FACE_IDLE && !s_demo_active) {
        if (!s_is_sleepy && (now_ms - s_last_interaction_ms >= 45000)) {
            s_is_sleepy = true;
            pose_init_sleepy(&s_tgt_pose);
            ESP_LOGI(TAG_FACE, "Inactivity timeout -> Entering SLEEPY state");
        }
    }

    // 5. Idle Micro-Expression & Saccade Scheduler
    if (s_current_state == FACE_IDLE && !s_is_sleepy && !s_demo_active) {
        // Gaze Saccades
        if (now_ms >= s_next_glance_ms) {
            int r = esp_random() % 100;
            if (r < 35) {
                s_tgt_pose.left_eye.gaze_x = -4.0f;
                s_tgt_pose.right_eye.gaze_x = -4.0f;
            } else if (r < 70) {
                s_tgt_pose.left_eye.gaze_x = 4.0f;
                s_tgt_pose.right_eye.gaze_x = 4.0f;
            } else {
                s_tgt_pose.left_eye.gaze_y = -3.0f;
                s_tgt_pose.right_eye.gaze_y = -3.0f;
            }
            s_glance_return_ms = now_ms + 900 + (esp_random() % 900);
            s_next_glance_ms = now_ms + 4000 + (esp_random() % 4000);
        } else if (s_glance_return_ms > 0 && now_ms >= s_glance_return_ms) {
            s_tgt_pose.left_eye.gaze_x = 0;
            s_tgt_pose.left_eye.gaze_y = 0;
            s_tgt_pose.right_eye.gaze_x = 0;
            s_tgt_pose.right_eye.gaze_y = 0;
            s_glance_return_ms = 0;
        }

        // Curious Asymmetry micro-pose
        if (now_ms >= s_next_curious_ms) {
            bool left_big = ((esp_random() % 2) == 0);
            pose_init_curious(&s_tgt_pose, left_big);
            s_curious_return_ms = now_ms + 1100 + (esp_random() % 600);
            s_next_curious_ms = now_ms + 12000 + (esp_random() % 10000);
        } else if (s_curious_return_ms > 0 && now_ms >= s_curious_return_ms) {
            pose_init_neutral_friendly(&s_tgt_pose);
            s_curious_return_ms = 0;
        }

        // Playful Wink micro-pose
        if (now_ms >= s_next_playful_ms) {
            bool left_w = ((esp_random() % 2) == 0);
            pose_init_wink(&s_tgt_pose, left_w);
            s_playful_return_ms = now_ms + 750;
            s_next_playful_ms = now_ms + 25000 + (esp_random() % 15000);
        } else if (s_playful_return_ms > 0 && now_ms >= s_playful_return_ms) {
            pose_init_neutral_friendly(&s_tgt_pose);
            s_playful_return_ms = 0;
        }
    }

    // 6. Organic Blinking Physics
    float blink_factor_l = 1.0f;
    float blink_factor_r = 1.0f;

    if (s_tgt_pose.blink_policy != BLINK_POLICY_NONE && s_current_state != FACE_BOOT) {
        if (!s_blink_active) {
            if (s_next_blink_ms == 0 || now_ms >= s_next_blink_ms) {
                s_blink_active = true;
                s_blink_start_ms = now_ms;
                s_blink_dur_ms = (s_tgt_pose.blink_policy == BLINK_POLICY_SLEEPY) ? 420 : (160 + (esp_random() % 70));
                s_blink_r_delay_ms = (s_tgt_pose.blink_policy == BLINK_POLICY_CURIOUS) ? 35 : (14 + (esp_random() % 14));
                s_double_blink_pending = ((esp_random() % 100) < 18);
            }
        } else {
            int64_t el_l = now_ms - s_blink_start_ms;
            int64_t el_r = now_ms - (s_blink_start_ms + s_blink_r_delay_ms);

            // Left Eye Blink
            if (el_l >= 0 && el_l < s_blink_dur_ms) {
                float p = (float)el_l / (float)s_blink_dur_ms;
                blink_factor_l = 0.54f + 0.46f * cosf(p * 6.28318f);
                if (blink_factor_l < 0.08f) blink_factor_l = 0.08f;
            }

            // Right Eye Blink (With organic delay)
            if (el_r >= 0 && el_r < s_blink_dur_ms) {
                float p = (float)el_r / (float)s_blink_dur_ms;
                blink_factor_r = 0.54f + 0.46f * cosf(p * 6.28318f);
                if (blink_factor_r < 0.08f) blink_factor_r = 0.08f;
            }

            if (el_l >= s_blink_dur_ms + s_blink_r_delay_ms) {
                s_blink_active = false;
                if (s_double_blink_pending) {
                    s_double_blink_pending = false;
                    s_next_blink_ms = now_ms + 110; // Rapid secondary blink
                } else {
                    int32_t interval = (s_tgt_pose.blink_policy == BLINK_POLICY_SLEEPY) ? 5500 : 3200;
                    s_next_blink_ms = now_ms + interval + (esp_random() % 2800);
                }
            }
        }
    }

    // 7. Smoothly Interpolate towards target pose (ease-out quadratic lerp)
    float factor = 0.24f;
    interpolate_eye(&s_cur_pose.left_eye, &s_tgt_pose.left_eye, factor);
    interpolate_eye(&s_cur_pose.right_eye, &s_tgt_pose.right_eye, factor);

    s_cur_pose.mouth_policy = s_tgt_pose.mouth_policy;
    s_cur_pose.blink_policy = s_tgt_pose.blink_policy;

    // 8. Render Both Eyes with their respective organic blink factors
    fb_render_eye(&s_cur_pose.left_eye, blink_factor_l);
    fb_render_eye(&s_cur_pose.right_eye, blink_factor_r);
}

// -------------------------------------------------------------
// Standalone Expression Demo Controller (27 Complete Steps)
// -------------------------------------------------------------
static void demo_mode_tick(int64_t now_ms)
{
    if (!s_demo_active) return;

    if (now_ms >= s_demo_step_until_ms) {
        s_demo_step++;
        switch (s_demo_step) {
            case 1:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 1/27] Neutral Friendly");
                s_current_state = FACE_IDLE;
                pose_init_neutral_friendly(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "1: NEUTRAL", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1800;
                break;
            case 2:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 2/27] Normal Blink");
                s_blink_active = true;
                s_blink_start_ms = now_ms;
                s_blink_dur_ms = 180;
                strncpy(s_tgt_pose.status_label, "2: BLINK", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1400;
                break;
            case 3:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 3/27] Double Blink");
                s_blink_active = true;
                s_blink_start_ms = now_ms;
                s_blink_dur_ms = 150;
                s_double_blink_pending = true;
                strncpy(s_tgt_pose.status_label, "3: DBL BLINK", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1600;
                break;
            case 4:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 4/27] Glance Left");
                s_tgt_pose.left_eye.gaze_x = -5.0f;
                s_tgt_pose.right_eye.gaze_x = -5.0f;
                strncpy(s_tgt_pose.status_label, "4: GLANCE L", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1600;
                break;
            case 5:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 5/27] Glance Right");
                s_tgt_pose.left_eye.gaze_x = 5.0f;
                s_tgt_pose.right_eye.gaze_x = 5.0f;
                strncpy(s_tgt_pose.status_label, "5: GLANCE R", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1600;
                break;
            case 6:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 6/27] Glance Up");
                s_tgt_pose.left_eye.gaze_x = 0; s_tgt_pose.left_eye.gaze_y = -4.0f;
                s_tgt_pose.right_eye.gaze_x = 0; s_tgt_pose.right_eye.gaze_y = -4.0f;
                strncpy(s_tgt_pose.status_label, "6: GLANCE UP", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1600;
                break;
            case 7:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 7/27] Curious, Left Eye Bigger");
                pose_init_curious(&s_tgt_pose, true);
                strncpy(s_tgt_pose.status_label, "7: CURIOUS L", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 8:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 8/27] Curious, Right Eye Bigger");
                pose_init_curious(&s_tgt_pose, false);
                strncpy(s_tgt_pose.status_label, "8: CURIOUS R", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 9:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 9/27] Attentive Listening");
                s_current_state = FACE_LISTENING;
                pose_init_attentive_listening(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "9: LISTEN", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 10:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 10/27] Thinking Up-Left");
                s_current_state = FACE_THINKING;
                pose_init_thinking(&s_tgt_pose, 0);
                strncpy(s_tgt_pose.status_label, "10: THINK L", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 11:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 11/27] Thinking Up-Right");
                s_current_state = FACE_THINKING;
                pose_init_thinking(&s_tgt_pose, 1);
                strncpy(s_tgt_pose.status_label, "11: THINK R", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 12:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 12/27] Speaking Low Level");
                s_current_state = FACE_SPEAKING;
                pose_init_speaking(&s_tgt_pose);
                s_audio_level_in = 0.25f;
                strncpy(s_tgt_pose.status_label, "12: SPEAK LOW", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1800;
                break;
            case 13:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 13/27] Speaking Mid Level");
                s_current_state = FACE_SPEAKING;
                pose_init_speaking(&s_tgt_pose);
                s_audio_level_in = 0.60f;
                strncpy(s_tgt_pose.status_label, "13: SPEAK MID", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1800;
                break;
            case 14:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 14/27] Speaking High Level");
                s_current_state = FACE_SPEAKING;
                pose_init_speaking(&s_tgt_pose);
                s_audio_level_in = 0.95f;
                strncpy(s_tgt_pose.status_label, "14: SPEAK HIGH", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1800;
                break;
            case 15:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 15/27] Happy Soft");
                s_current_state = FACE_HAPPY;
                pose_init_happy_soft(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "15: HAPPY SOFT", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 16:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 16/27] Happy Big");
                s_current_state = FACE_HAPPY;
                pose_init_happy_big(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "16: HAPPY BIG", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2200;
                break;
            case 17:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 17/27] Excited");
                s_current_state = FACE_IDLE;
                pose_init_excited(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "17: EXCITED", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 18:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 18/27] Surprised");
                s_current_state = FACE_IDLE;
                pose_init_surprised(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "18: SURPRISED", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 19:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 19/27] Playful Squint");
                pose_init_playful_squint(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "19: SQUINT", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 20:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 20/27] Wink Left");
                pose_init_wink(&s_tgt_pose, true);
                strncpy(s_tgt_pose.status_label, "20: WINK L", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1800;
                break;
            case 21:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 21/27] Wink Right");
                pose_init_wink(&s_tgt_pose, false);
                strncpy(s_tgt_pose.status_label, "21: WINK R", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 1800;
                break;
            case 22:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 22/27] Confused");
                pose_init_confused(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "22: CONFUSED", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2200;
                break;
            case 23:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 23/27] Sleepy");
                pose_init_sleepy(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "23: SLEEPY", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2200;
                break;
            case 24:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 24/27] Error Sad");
                s_current_state = FACE_ERROR;
                pose_init_error_sad(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "24: ERR SAD", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2200;
                break;
            case 25:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 25/27] Disconnected");
                s_current_state = FACE_DISCONNECTED;
                pose_init_disconnected(&s_tgt_pose);
                s_wifi_status = WIFI_OFF;
                strncpy(s_tgt_pose.status_label, "25: OFFLINE", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2200;
                break;
            case 26:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 26/27] Reconnection Recovery");
                s_current_state = FACE_HAPPY;
                s_wifi_status = WIFI_CONNECTED_STABLE;
                pose_init_happy_soft(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "26: RECOVERED", sizeof(s_tgt_pose.status_label) - 1);
                s_demo_step_until_ms = now_ms + 2000;
                break;
            case 27:
            default:
                ESP_LOGI(TAG_FACE, ">>> [DEMO 27/27] Return to Neutral Friendly. Demo Complete!");
                s_current_state = FACE_IDLE;
                s_wifi_status = WIFI_CONNECTED_STABLE;
                pose_init_neutral_friendly(&s_tgt_pose);
                strncpy(s_tgt_pose.status_label, "READY", sizeof(s_tgt_pose.status_label) - 1);
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
    ESP_LOGI(TAG_FACE, "Organic face animation task started (Priority 2, 20 FPS)");

    const TickType_t frame_period_ticks = pdMS_TO_TICKS(50);
    s_fps_window_start_ms = esp_timer_get_time() / 1000;

    while (true) {
        int64_t t_frame_start = esp_timer_get_time();
        int64_t now_ms = t_frame_start / 1000;

        // 1. Live audio level from audio_player
        float player_lev = audio_player_get_level();
        if (player_lev > 0.01f) {
            s_audio_level_in = player_lev;
        }

        // 2. Clear frame to pitch black
        memset(s_framebuf, 0, SCREEN_W * SCREEN_H * sizeof(uint16_t));

        // 3. Render Top Header (Wi-Fi + Battery)
        render_header(now_ms);

        // 4. Update Physics & Render Dynamic Eyes
        if (s_demo_active) {
            demo_mode_tick(now_ms);
        }
        update_pose_physics(now_ms);

        // 5. Render Mouth & Thinking Orbit
        render_mouth(now_ms);
        render_thinking_orbit(now_ms);

        // 6. Render Minimal Status Banner
        render_status_banner();

        // 7. Blast frame buffer over SPI DMA to ST7735 in one single burst
        if (s_disp) {
            board_display_send_bitmap(s_disp, 0, 0, SCREEN_W, SCREEN_H, s_framebuf);
        }

        // 8. Performance Metrics Calculation
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

        // Yield for the remainder of the 50ms period
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
    s_last_interaction_ms = s_state_start_ms;
    s_current_state = FACE_BOOT;

    pose_init_neutral_friendly(&s_cur_pose);
    s_cur_pose.left_eye.openness = 0.0f;
    s_cur_pose.right_eye.openness = 0.0f;

    pose_init_neutral_friendly(&s_tgt_pose);
    s_tgt_pose.left_eye.openness = 1.0f;
    s_tgt_pose.right_eye.openness = 1.0f;

    if (!s_face_task_handle) {
        BaseType_t ok = xTaskCreate(face_task, "face_task", 4096, NULL, 2, &s_face_task_handle);
        if (ok != pdPASS) {
            ESP_LOGE(TAG_FACE, "Failed to create face animation task!");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG_FACE, "Organic Face Engine initialized (Framebuffer: %zu bytes)",
             SCREEN_W * SCREEN_H * sizeof(uint16_t));
    return ESP_OK;
}

void board_face_set_state(face_state_t state)
{
    if (s_demo_active) return; // Don't override while demo is active

    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;

    if (s_current_state != state) {
        s_current_state = state;
        s_state_start_ms = now_ms;
        s_custom_status[0] = '\0';
        s_timed_hold_until_ms = 0;

        switch (state) {
            case FACE_BOOT:
                pose_init_neutral_friendly(&s_tgt_pose);
                s_tgt_pose.left_eye.openness = 0.0f;
                s_tgt_pose.right_eye.openness = 0.0f;
                break;
            case FACE_IDLE:
                pose_init_neutral_friendly(&s_tgt_pose);
                break;
            case FACE_LISTENING:
                pose_init_attentive_listening(&s_tgt_pose);
                break;
            case FACE_THINKING:
                s_thinking_subpose = 0;
                s_next_thinking_sub_ms = now_ms + 900;
                pose_init_thinking(&s_tgt_pose, 0);
                break;
            case FACE_SPEAKING:
                pose_init_speaking(&s_tgt_pose);
                break;
            case FACE_HAPPY:
                pose_init_happy_soft(&s_tgt_pose);
                s_timed_hold_until_ms = now_ms + 1200;
                break;
            case FACE_ERROR:
                pose_init_error_sad(&s_tgt_pose);
                s_timed_hold_until_ms = now_ms + 3000;
                break;
            case FACE_DISCONNECTED:
                pose_init_disconnected(&s_tgt_pose);
                break;
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
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;
    s_current_state = FACE_HAPPY;
    pose_init_happy_soft(&s_tgt_pose);
    s_timed_hold_until_ms = now_ms + ((duration_ms > 0) ? duration_ms : 1200);
}

void board_face_trigger_happy_big(uint32_t duration_ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;
    s_current_state = FACE_HAPPY;
    pose_init_happy_big(&s_tgt_pose);
    s_timed_hold_until_ms = now_ms + ((duration_ms > 0) ? duration_ms : 1500);
}

void board_face_trigger_curious(bool left_larger, uint32_t duration_ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;
    pose_init_curious(&s_tgt_pose, left_larger);
    s_timed_hold_until_ms = now_ms + ((duration_ms > 0) ? duration_ms : 1500);
}

void board_face_trigger_confused(uint32_t duration_ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;
    pose_init_confused(&s_tgt_pose);
    s_timed_hold_until_ms = now_ms + ((duration_ms > 0) ? duration_ms : 2000);
}

void board_face_trigger_excited(uint32_t duration_ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;
    pose_init_excited(&s_tgt_pose);
    s_timed_hold_until_ms = now_ms + ((duration_ms > 0) ? duration_ms : 1200);
}

void board_face_trigger_surprised(uint32_t duration_ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;
    pose_init_surprised(&s_tgt_pose);
    s_timed_hold_until_ms = now_ms + ((duration_ms > 0) ? duration_ms : 1200);
}

void board_face_trigger_wink(bool left_eye, uint32_t duration_ms)
{
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;
    pose_init_wink(&s_tgt_pose, left_eye);
    s_timed_hold_until_ms = now_ms + ((duration_ms > 0) ? duration_ms : 800);
}

void board_face_trigger_sleepy(void)
{
    s_is_sleepy = true;
    pose_init_sleepy(&s_tgt_pose);
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
    ESP_LOGI(TAG_FACE, "Starting Standalone Expression Demonstration Mode (27 Complete Steps)...");
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
