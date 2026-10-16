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

// -------------------------------------------------------------
// Official Kawaii Robot Cat Palette (Big-Endian for SPI DMA)
// -------------------------------------------------------------
#define RGB565_BE(r, g, b) (__builtin_bswap16(((uint16_t)((r) & 0xF8) << 8) | \
                                              ((uint16_t)((g) & 0xFC) << 3) | \
                                              ((uint16_t)(b) >> 3)))

#define COL_CAT_WHITE        RGB565_BE(255, 255, 255) // #FFFFFF - Main fur & white highlights
#define COL_CAT_CREAM        RGB565_BE(255, 243, 214) // #FFF3D6 - Pastel cream body & stars
#define COL_CAT_PINK         RGB565_BE(255, 117, 184) // #FF75B8 - Main ribbon bow & blush
#define COL_CAT_LIGHT_PINK   RGB565_BE(255, 214, 232) // #FFD6E8 - Inner ears & soft highlights
#define COL_CAT_DEEP_PINK    RGB565_BE(230, 40,  120) // Shading on bow & collar
#define COL_CAT_CYAN         RGB565_BE(86,  221, 245) // #56DDF5 - Headphones & eye glow
#define COL_CAT_SKY_BLUE     RGB565_BE(103, 168, 255) // #67A8FF - Eye iris & pastel bg
#define COL_CAT_VIOLET       RGB565_BE(168, 139, 255) // #A88BFF - Upper eye iris & lavender
#define COL_CAT_MINT         RGB565_BE(141, 235, 203) // #8DEBCB - Leaves & mint bg
#define COL_CAT_DARK_NAVY    RGB565_BE(32,  36,  68)  // #202444 - Eye pupils, outlines, mouth
#define COL_CAT_GOLD         RGB565_BE(255, 201, 74)  // #FFC94A - Golden bell & star sparkles
#define COL_CAT_DARK_GOLD    RGB565_BE(190, 130, 20)  // Bell shading & slit
#define COL_CAT_SHADOW       RGB565_BE(220, 214, 230) // Gentle shadow under chin & ears
#define COL_CAT_MOUTH_RED    RGB565_BE(180, 25,  65)  // Deep mouth interior
#define COL_CAT_TONGUE       RGB565_BE(255, 130, 170) // Soft pink cat tongue
#define COL_CAT_PILLOW       RGB565_BE(255, 175, 210) // Sleepy cushion pillow

#define COL_BLACK            RGB565_BE(0,   0,   0)
#define COL_GREEN            RGB565_BE(0,   255, 70)  // Full battery / Wi-Fi
#define COL_YELLOW           RGB565_BE(255, 220, 0)
#define COL_ERROR            RGB565_BE(255, 60,  60)  // Error coral red
#define COL_GRAY             RGB565_BE(85,  85,  85)
#define COL_DARK_GRAY        RGB565_BE(30,  35,  45)

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
static volatile server_status_t s_server_status = SERVER_STATUS_DISCONNECTED;
static volatile battery_state_t s_bat_state = BAT_FULL;
static volatile float s_audio_level_in = 0.0f;

// Timers and transitions
static int64_t s_state_start_ms = 0;
static int64_t s_timed_hold_until_ms = 0;
static int64_t s_last_interaction_ms = 0;
static bool s_is_sleepy = false;

// Independent Eye Blinking State
static bool s_blink_active = false;
static int64_t s_blink_start_ms = 0;
static int32_t s_blink_dur_ms = 160;
static int64_t s_next_blink_ms = 0;

// Idle Saccades
static int64_t s_next_glance_ms = 0;
static int64_t s_glance_return_ms = 0;
static float s_gaze_offset_x = 0.0f;
static float s_gaze_offset_y = 0.0f;

// Demo Mode (20 Complete States)
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
    {0b110,0b101,0b110,0b101,0b101}, // R
    {0b111,0b100,0b111,0b001,0b111}, // S
    {0b111,0b010,0b010,0b010,0b010}, // T
    {0b101,0b101,0b101,0b101,0b111}, // U
    {0b101,0b101,0b101,0b101,0b010}, // V
    {0b101,0b101,0b101,0b111,0b101}, // W
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
    {0b111,0b101,0b111,0b001,0b111}  // 9
};

// -------------------------------------------------------------
// Fast RGB565 Linear Color Interpolation
// -------------------------------------------------------------
static inline uint16_t rgb565_lerp(uint16_t c1_be, uint16_t c2_be, uint8_t t256)
{
    uint16_t c1 = __builtin_bswap16(c1_be);
    uint16_t c2 = __builtin_bswap16(c2_be);

    uint32_t r1 = (c1 >> 11) & 0x1F;
    uint32_t g1 = (c1 >> 5)  & 0x3F;
    uint32_t b1 = c1 & 0x1F;

    uint32_t r2 = (c2 >> 11) & 0x1F;
    uint32_t g2 = (c2 >> 5)  & 0x3F;
    uint32_t b2 = c2 & 0x1F;

    uint32_t r = r1 + (((r2 - r1) * t256) >> 8);
    uint32_t g = g1 + (((g2 - g1) * t256) >> 8);
    uint32_t b = b1 + (((b2 - b1) * t256) >> 8);

    return __builtin_bswap16((uint16_t)((r << 11) | (g << 5) | b));
}

// -------------------------------------------------------------
// Core 2D Graphics Primitives (Optimized, Clipped, Bounds-safe)
// -------------------------------------------------------------
static inline void fb_set_pixel(int x, int y, uint16_t col)
{
    if (x >= 0 && x < SCREEN_W && y >= 0 && y < SCREEN_H) {
        s_framebuf[y * SCREEN_W + x] = col;
    }
}

static void fb_fill_rect(int x, int y, int w, int h, uint16_t col)
{
    if (x >= SCREEN_W || y >= SCREEN_H) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_W) w = SCREEN_W - x;
    if (y + h > SCREEN_H) h = SCREEN_H - y;
    if (w <= 0 || h <= 0) return;

    for (int r = y; r < y + h; r++) {
        uint16_t *dst = &s_framebuf[r * SCREEN_W + x];
        for (int c = 0; c < w; c++) {
            dst[c] = col;
        }
    }
}


static void fb_gradient_v(int y0, int y1, uint16_t c_top, uint16_t c_bot)
{
    if (y0 < 0) y0 = 0;
    if (y1 >= SCREEN_H) y1 = SCREEN_H - 1;
    if (y0 > y1) return;

    int total_h = y1 - y0;
    if (total_h == 0) {
        fb_fill_rect(0, y0, SCREEN_W, 1, c_top);
        return;
    }

    for (int y = y0; y <= y1; y++) {
        uint8_t t = (uint8_t)(((y - y0) * 255) / total_h);
        uint16_t col = rgb565_lerp(c_top, c_bot, t);
        uint16_t *dst = &s_framebuf[y * SCREEN_W];
        for (int x = 0; x < SCREEN_W; x++) {
            dst[x] = col;
        }
    }
}

static void fb_fill_circle(int cx, int cy, int r, uint16_t col)
{
    if (r <= 0) return;
    int r2 = r * r;
    for (int dy = -r; dy <= r; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= SCREEN_H) continue;
        int dx = (int)sqrtf((float)(r2 - dy * dy));
        int x0 = cx - dx;
        int x1 = cx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
        if (x0 <= x1) {
            uint16_t *dst = &s_framebuf[y * SCREEN_W + x0];
            int count = x1 - x0 + 1;
            for (int i = 0; i < count; i++) dst[i] = col;
        }
    }
}

static void fb_draw_circle(int cx, int cy, int r, uint16_t col)
{
    int x = 0, y = r;
    int d = 3 - 2 * r;
    while (y >= x) {
        fb_set_pixel(cx + x, cy + y, col);
        fb_set_pixel(cx - x, cy + y, col);
        fb_set_pixel(cx + x, cy - y, col);
        fb_set_pixel(cx - x, cy - y, col);
        fb_set_pixel(cx + y, cy + x, col);
        fb_set_pixel(cx - y, cy + x, col);
        fb_set_pixel(cx + y, cy - x, col);
        fb_set_pixel(cx - y, cy - x, col);
        if (d < 0) d += 4 * x + 6;
        else {
            d += 4 * (x - y) + 10;
            y--;
        }
        x++;
    }
}

static void fb_fill_ellipse(int cx, int cy, int rx, int ry, uint16_t col)
{
    if (rx <= 0 || ry <= 0) return;
    float rx_f = (float)rx;
    float ry_f = (float)ry;
    for (int dy = -ry; dy <= ry; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= SCREEN_H) continue;
        float term = 1.0f - ((float)(dy * dy)) / (ry_f * ry_f);
        if (term < 0.0f) continue;
        int dx = (int)roundf(rx_f * sqrtf(term));
        int x0 = cx - dx;
        int x1 = cx + dx;
        if (x0 < 0) x0 = 0;
        if (x1 >= SCREEN_W) x1 = SCREEN_W - 1;
        if (x0 <= x1) {
            uint16_t *dst = &s_framebuf[y * SCREEN_W + x0];
            int count = x1 - x0 + 1;
            for (int i = 0; i < count; i++) dst[i] = col;
        }
    }
}

static void fb_fill_rounded_rect(int x, int y, int w, int h, int r, uint16_t col)
{
    if (r <= 0) {
        fb_fill_rect(x, y, w, h, col);
        return;
    }
    if (r * 2 > w) r = w / 2;
    if (r * 2 > h) r = h / 2;

    fb_fill_rect(x + r, y, w - 2 * r, h, col);
    fb_fill_rect(x, y + r, r, h - 2 * r, col);
    fb_fill_rect(x + w - r, y + r, r, h - 2 * r, col);

    fb_fill_circle(x + r, y + r, r, col);
    fb_fill_circle(x + w - r - 1, y + r, r, col);
    fb_fill_circle(x + r, y + h - r - 1, r, col);
    fb_fill_circle(x + w - r - 1, y + h - r - 1, r, col);
}

static void fb_fill_triangle(int x0, int y0, int x1, int y1, int x2, int y2, uint16_t col)
{
    // Sort vertices by Y
    if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }
    if (y1 > y2) { int t = x1; x1 = x2; x2 = t; t = y1; y1 = y2; y2 = t; }
    if (y0 > y1) { int t = x0; x0 = x1; x1 = t; t = y0; y0 = y1; y1 = t; }

    int total_h = y2 - y0;
    if (total_h == 0) return;

    for (int y = y0; y <= y2; y++) {
        if (y < 0 || y >= SCREEN_H) continue;
        bool second_half = (y > y1) || (y1 == y0);
        int segment_h = second_half ? (y2 - y1) : (y1 - y0);
        if (segment_h == 0) continue;

        float alpha = (float)(y - y0) / (float)total_h;
        float beta  = (float)(y - (second_half ? y1 : y0)) / (float)segment_h;

        int ax = x0 + (int)roundf((float)(x2 - x0) * alpha);
        int bx = second_half ? (x1 + (int)roundf((float)(x2 - x1) * beta))
                             : (x0 + (int)roundf((float)(x1 - x0) * beta));

        if (ax > bx) { int t = ax; ax = bx; bx = t; }
        if (ax < 0) ax = 0;
        if (bx >= SCREEN_W) bx = SCREEN_W - 1;
        if (ax <= bx) {
            uint16_t *dst = &s_framebuf[y * SCREEN_W + ax];
            int count = bx - ax + 1;
            for (int i = 0; i < count; i++) dst[i] = col;
        }
    }
}

static void fb_draw_line(int x0, int y0, int x1, int y1, uint16_t col)
{
    int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
    int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
    int err = dx + dy, e2;

    while (1) {
        fb_set_pixel(x0, y0, col);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

static void fb_draw_thick_line(int x0, int y0, int x1, int y1, int th, uint16_t col)
{
    int half = th / 2;
    for (int ox = -half; ox <= half; ox++) {
        for (int oy = -half; oy <= half; oy++) {
            fb_draw_line(x0 + ox, y0 + oy, x1 + ox, y1 + oy, col);
        }
    }
}

static void fb_draw_arc(int cx, int cy, int r, int thick, uint16_t col)
{
    int ro = r + thick / 2;
    int ri = r - thick / 2;
    if (ri < 0) ri = 0;
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


// -------------------------------------------------------------
// Thematic Decorative Elements (Stars, Hearts, Sweets, etc.)
// -------------------------------------------------------------
static void fb_draw_star(int cx, int cy, int r, uint16_t col)
{
    fb_fill_circle(cx, cy, 1, col);
    fb_draw_line(cx - r, cy, cx + r, cy, col);
    fb_draw_line(cx, cy - r, cx, cy + r, col);
    if (r >= 3) {
        fb_draw_line(cx - r/2, cy - r/2, cx + r/2, cy + r/2, col);
        fb_draw_line(cx - r/2, cy + r/2, cx + r/2, cy - r/2, col);
    }
}

static void fb_draw_heart(int cx, int cy, int size, uint16_t col)
{
    int r = size / 2;
    if (r < 1) r = 1;
    fb_fill_circle(cx - r, cy - r/2, r, col);
    fb_fill_circle(cx + r, cy - r/2, r, col);
    fb_fill_triangle(cx - 2 * r, cy - r/2, cx + 2 * r, cy - r/2, cx, cy + size, col);
}

static void fb_draw_broken_heart(int cx, int cy, uint16_t col)
{
    fb_draw_heart(cx, cy, 6, col);
    // Draw zigzag black crack
    fb_draw_line(cx, cy - 4, cx - 1, cy - 1, COL_BLACK);
    fb_draw_line(cx - 1, cy - 1, cx + 1, cy + 2, COL_BLACK);
    fb_draw_line(cx + 1, cy + 2, cx, cy + 6, COL_BLACK);
}

static void fb_draw_candy(int cx, int cy, uint16_t col1, uint16_t col2)
{
    // Candy body
    fb_fill_circle(cx, cy, 4, col1);
    // Left & right wrappers
    fb_fill_triangle(cx - 4, cy, cx - 8, cy - 3, cx - 8, cy + 3, col2);
    fb_fill_triangle(cx + 4, cy, cx + 8, cy - 3, cx + 8, cy + 3, col2);
}

static void fb_draw_exclamation_mark(int cx, int cy, int h, uint16_t col)
{
    fb_fill_rounded_rect(cx - 1, cy, 3, h - 3, 1, col);
    fb_fill_circle(cx, cy + h, 1, col);
}

static void fb_draw_question_mark(int cx, int cy, int h, uint16_t col)
{
    fb_draw_arc(cx, cy + 3, 3, 2, col);
    fb_fill_rect(cx + 1, cy + 3, 2, 3, col);
    fb_fill_circle(cx + 2, cy + 8, 1, col);
}

static void fb_draw_crescent_moon(int cx, int cy, int r, uint16_t col)
{
    fb_fill_circle(cx, cy, r, col);
    fb_fill_circle(cx + r/2, cy - r/3, r - 1, COL_CAT_DARK_NAVY);
}

static void fb_draw_zzz(int x, int y, int scale, uint16_t col)
{
    fb_draw_line(x, y, x + 3 * scale, y, col);
    fb_draw_line(x + 3 * scale, y, x, y + 4 * scale, col);
    fb_draw_line(x, y + 4 * scale, x + 3 * scale, y + 4 * scale, col);
}

static void fb_draw_leaf(int cx, int cy, int rx, int ry, uint16_t col)
{
    fb_fill_ellipse(cx, cy, rx, ry, col);
    fb_fill_circle(cx + rx, cy, 1, col);
}

// -------------------------------------------------------------
// Text Font Rendering (3x5 scalable)
// -------------------------------------------------------------
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
            cur_x += 2 * scale;
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
        len += (*txt == ' ') ? 2 * scale : 4 * scale;
        txt++;
    }
    return len > 0 ? (len - scale) : 0;
}

static void fb_draw_text_centered(int y, const char *txt, uint16_t col, int scale)
{
    int w = fb_get_text_width(txt, scale);
    int x = (SCREEN_W - w) / 2;
    fb_draw_text(x, y, txt, col, scale);
}

// -------------------------------------------------------------
// Background Renderer per Expression
// -------------------------------------------------------------
static void render_cat_background(face_state_t state, int64_t now_ms)
{
    float t_sec = (float)now_ms / 1000.0f;

    switch (state) {
        case FACE_EXCITED:
            // Warm sunshine gradient (Yellow to Peach/Orange)
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 235, 130), RGB565_BE(255, 160, 75));
            // Sunburst rays radiating from center
            for (int angle = 0; angle < 360; angle += 45) {
                float rad = (float)angle * 3.14159f / 180.0f + t_sec * 0.4f;
                int x1 = 64 + (int)(cosf(rad) * 90.0f);
                int y1 = 70 + (int)(sinf(rad) * 90.0f);
                fb_draw_line(64, 70, x1, y1, RGB565_BE(255, 245, 170));
            }
            // Sparkle stars
            fb_draw_star(16, 32, 3, COL_CAT_GOLD);
            fb_draw_star(112, 28, 4, COL_CAT_GOLD);
            fb_draw_star(20, 120, 3, COL_CAT_CREAM);
            fb_draw_star(108, 122, 3, COL_CAT_CREAM);
            break;

        case FACE_SURPRISED:
            // Pastel yellow to pink gradient
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 245, 160), RGB565_BE(255, 165, 200));
            // Floating exclamation marks '!!!'
            fb_draw_exclamation_mark(54, 18 + (int)(sinf(t_sec * 5.0f) * 2.0f), 8, COL_CAT_GOLD);
            fb_draw_exclamation_mark(64, 15 + (int)(sinf(t_sec * 5.0f + 1.0f) * 2.5f), 10, COL_CAT_PINK);
            fb_draw_exclamation_mark(74, 18 + (int)(sinf(t_sec * 5.0f + 2.0f) * 2.0f), 8, COL_CAT_GOLD);
            fb_draw_star(18, 38, 3, COL_CAT_CREAM);
            fb_draw_star(110, 36, 3, COL_CAT_CREAM);
            break;

        case FACE_SQUINT:
            // Bright cyan to sky blue
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(86, 221, 245), RGB565_BE(170, 235, 255));
            // Cute candies & stars
            fb_draw_candy(64, 20, COL_CAT_PINK, COL_CAT_LIGHT_PINK);
            fb_draw_star(22, 34, 3, COL_CAT_WHITE);
            fb_draw_star(106, 36, 4, COL_CAT_CREAM);
            break;

        case FACE_WINK_LEFT:
            // Mint green to sky blue
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(141, 235, 203), RGB565_BE(103, 168, 255));
            fb_draw_star(16, 26, 4, COL_CAT_CREAM);
            fb_draw_star(110, 32, 3, COL_CAT_WHITE);
            fb_draw_star(18, 126, 3, COL_CAT_WHITE);
            fb_draw_star(112, 124, 3, COL_CAT_CREAM);
            break;

        case FACE_WINK_RIGHT:
            // Peach to pastel pink
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 205, 160), RGB565_BE(255, 125, 185));
            fb_draw_heart(20, 28, 4, COL_CAT_LIGHT_PINK);
            fb_draw_star(108, 30, 3, COL_CAT_CREAM);
            fb_draw_star(16, 120, 3, COL_CAT_WHITE);
            fb_draw_heart(110, 124, 3, COL_CAT_LIGHT_PINK);
            break;

        case FACE_CONFUSED:
            // Lavender to purple
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(225, 205, 255), RGB565_BE(155, 125, 250));
            // Floating '???'
            fb_draw_question_mark(28, 22 + (int)(sinf(t_sec * 4.0f) * 2.0f), 8, COL_CAT_WHITE);
            fb_draw_question_mark(44, 16 + (int)(sinf(t_sec * 4.0f + 1.2f) * 2.5f), 10, COL_CAT_LIGHT_PINK);
            fb_draw_question_mark(60, 24 + (int)(sinf(t_sec * 4.0f + 2.4f) * 2.0f), 7, COL_CAT_CREAM);
            fb_draw_star(110, 32, 3, COL_CAT_WHITE);
            break;

        case FACE_SLEEPY:
            // Deep midnight indigo sky
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(18, 22, 50), RGB565_BE(36, 42, 90));
            // Golden crescent moon
            fb_draw_crescent_moon(26, 26, 7, COL_CAT_GOLD);
            // Twinkling stars
            fb_draw_star(44, 20, 2, COL_CAT_CREAM);
            fb_draw_star(18, 46, 2, COL_CAT_WHITE);
            fb_draw_star(60, 30, 1, COL_CAT_CREAM);
            // Floating 'Z z z'
            int z_off = (int)(t_sec * 12.0f) % 24;
            fb_draw_zzz(92, 38 - z_off, 1, COL_CAT_LIGHT_PINK);
            fb_draw_zzz(98, 28 - z_off, 2, COL_CAT_CYAN);
            fb_draw_zzz(106, 18 - z_off, 2, COL_CAT_WHITE);
            break;

        case FACE_SAD:
        case FACE_ERROR:
            // Crimson to dark rose wine
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(95, 18, 38), RGB565_BE(40, 8, 16));
            // Floating broken heart
            fb_draw_broken_heart(64, 20 + (int)(sinf(t_sec * 3.0f) * 2.0f), COL_CAT_PINK);
            // Soft falling tear accents
            fb_fill_circle(20, 40 + ((int)(now_ms / 30) % 80), 1, COL_CAT_CYAN);
            fb_fill_circle(108, 30 + ((int)(now_ms / 25) % 80), 1, COL_CAT_CYAN);
            break;

        case FACE_HAPPY:
            // Vibrant bright pink gradient
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 150, 200), RGB565_BE(255, 80, 150));
            // Floating hearts
            fb_draw_heart(24, 26 + (int)(sinf(t_sec * 4.0f) * 2.0f), 4, COL_CAT_WHITE);
            fb_draw_heart(64, 18 + (int)(sinf(t_sec * 4.0f + 1.5f) * 2.5f), 5, COL_CAT_LIGHT_PINK);
            fb_draw_heart(104, 28 + (int)(sinf(t_sec * 4.0f + 3.0f) * 2.0f), 4, COL_CAT_WHITE);
            break;

        case FACE_RECOVERED:
            // Morning sky blue to fresh mint
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(100, 195, 255), RGB565_BE(141, 235, 203));
            // Sunburst sparkles
            fb_draw_star(16, 28, 4, COL_CAT_GOLD);
            fb_draw_star(64, 18, 3, COL_CAT_CREAM);
            fb_draw_star(112, 30, 4, COL_CAT_WHITE);
            // Green foliage sprouts at bottom sides
            fb_draw_leaf(12, 130, 8, 5, RGB565_BE(40, 180, 110));
            fb_draw_leaf(22, 134, 6, 4, RGB565_BE(70, 210, 130));
            fb_draw_leaf(116, 130, 8, 5, RGB565_BE(40, 180, 110));
            fb_draw_leaf(106, 134, 6, 4, RGB565_BE(70, 210, 130));
            break;

        case FACE_LISTENING:
            // Electric cyan to mint gradient
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(70, 220, 240), RGB565_BE(60, 160, 230));
            // Acoustic wave pulses from headphones
            int wave_r = ((int)(now_ms / 30) % 18) + 6;
            fb_draw_circle(20, 64, wave_r, COL_CAT_WHITE);
            fb_draw_circle(108, 64, wave_r, COL_CAT_WHITE);
            fb_draw_star(64, 20, 3, COL_CAT_CREAM);
            break;

        case FACE_THINKING:
            // Violet to warm amber
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(180, 130, 255), RGB565_BE(255, 180, 70));
            // Orbiting thought sparkles above head
            for (int i = 0; i < 3; i++) {
                float rad = t_sec * 4.0f + (float)i * 2.094f;
                int ox = 64 + (int)(cosf(rad) * 22.0f);
                int oy = 22 + (int)(sinf(rad) * 6.0f);
                fb_fill_circle(ox, oy, 2, COL_CAT_CREAM);
                fb_draw_star(ox, oy, 3, COL_CAT_WHITE);
            }
            break;

        case FACE_SPEAKING:
            // Dynamic warm pastel yellow to coral
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 230, 140), RGB565_BE(255, 120, 170));
            // Sound pulses
            int p_r = (int)(s_audio_level_in * 16.0f) + 4;
            fb_draw_circle(64, 80, p_r, COL_CAT_LIGHT_PINK);
            fb_draw_star(20, 32, 3, COL_CAT_GOLD);
            fb_draw_star(108, 30, 3, COL_CAT_GOLD);
            break;

        case FACE_LOVE:
            // Romantic magenta/rose
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 80, 140), RGB565_BE(190, 25, 90));
            fb_draw_heart(22, 28 + (int)(sinf(t_sec * 5.0f) * 2.0f), 5, COL_CAT_LIGHT_PINK);
            fb_draw_heart(64, 18 + (int)(sinf(t_sec * 5.0f + 1.0f) * 2.5f), 6, COL_CAT_WHITE);
            fb_draw_heart(106, 26 + (int)(sinf(t_sec * 5.0f + 2.0f) * 2.0f), 5, COL_CAT_LIGHT_PINK);
            break;

        case FACE_ANGRY:
            // Fiery crimson/orange
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(240, 70, 70), RGB565_BE(140, 20, 20));
            // Red angry vein mark 💢
            fb_draw_line(100, 20, 108, 20, COL_CAT_WHITE);
            fb_draw_line(104, 16, 104, 24, COL_CAT_WHITE);
            fb_draw_line(100, 24, 108, 16, COL_CAT_WHITE);
            break;

        case FACE_PLAYFUL:
            // Bubblegum yellow to pink
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 225, 130), RGB565_BE(255, 110, 180));
            fb_draw_star(20, 28, 3, COL_CAT_GOLD);
            fb_draw_heart(64, 18, 4, COL_CAT_WHITE);
            fb_draw_star(108, 30, 4, COL_CAT_CREAM);
            break;

        case FACE_CURIOUS:
            // Soft periwinkle/lilac
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(195, 210, 255), RGB565_BE(160, 140, 250));
            fb_draw_star(22, 30, 3, COL_CAT_CREAM);
            fb_draw_star(106, 26, 4, COL_CAT_WHITE);
            break;

        case FACE_BOOT:
        case FACE_IDLE:
        default:
            // Calm friendly pastel sky blue
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(165, 220, 255), RGB565_BE(100, 165, 255));
            fb_draw_star(18, 30, 3, COL_CAT_CREAM);
            fb_draw_star(110, 28, 4, COL_CAT_WHITE);
            fb_draw_star(16, 122, 2, COL_CAT_WHITE);
            fb_draw_star(112, 120, 3, COL_CAT_CREAM);
            break;
    }
}

// -------------------------------------------------------------
// Top Header: Wi-Fi, Server Globe & Battery Indicators
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

    // 2. Server/Internet Globe Indicator at (X=19, Y=5)
    const int sx = 19;
    const int sy = 5;
    uint16_t scol = COL_GREEN;
    bool server_strike = false;

    if (s_server_status == SERVER_STATUS_CONNECTED) {
        scol = COL_CAT_MINT;
    } else if (s_server_status == SERVER_STATUS_CONNECTING) {
        scol = ((now_ms / 250) % 2 == 0) ? COL_YELLOW : COL_DARK_GRAY;
    } else {
        scol = COL_DARK_GRAY;
        server_strike = true;
    }

    // Globe bitmap 9x9
    static const uint16_t globe_bitmap[9] = {
        0b001111100, // ..*****..
        0b010010010, // .*..*..*.
        0b100010001, // *...*...*
        0b100010001, // *...*...*
        0b111111111, // *********
        0b100010001, // *...*...*
        0b100010001, // *...*...*
        0b010010010, // .*..*..*.
        0b001111100  // ..*****..
    };

    for (int r = 0; r < 9; r++) {
        uint16_t row_bits = globe_bitmap[r];
        for (int c = 0; c < 9; c++) {
            if (row_bits & (1 << (8 - c))) {
                fb_set_pixel(sx + c, sy + r, scol);
            }
        }
    }

    if (server_strike) {
        for (int i = 0; i < 9; i++) {
            fb_set_pixel(sx + i, sy + i, COL_ERROR);
        }
    }

    // 3. Battery Indicator at (X=104, Y=3)
    const int bx = 104;
    const int by = 3;
    uint16_t bcol = COL_GREEN;
    int bars = 3;

    if (s_bat_state == BAT_CHARGING) {
        int step = (int)((now_ms / 280) % 4);
        bars = (step == 0) ? 1 : (step == 1 ? 2 : 3);
        bcol = COL_GREEN;
    } else if (s_bat_state == BAT_FULL) {
        bars = 3;
        bcol = COL_GREEN;
    } else if (s_bat_state == BAT_MED) {
        bars = 2;
        bcol = COL_ERROR;
    } else if (s_bat_state == BAT_LOW) {
        bars = 1;
        bcol = COL_ERROR;
    } else if (s_bat_state == BAT_CRIT) {
        bars = 1;
        bcol = ((now_ms / 220) % 2 == 0) ? COL_ERROR : COL_DARK_GRAY;
    }

    // Battery Shell Outline (17x9)
    fb_fill_rect(bx, by, 17, 1, COL_GRAY);
    fb_fill_rect(bx, by + 8, 17, 1, COL_GRAY);
    fb_fill_rect(bx, by, 1, 9, COL_GRAY);
    fb_fill_rect(bx + 16, by, 1, 9, COL_GRAY);
    fb_fill_rect(bx + 17, by + 2, 2, 5, (s_bat_state == BAT_CHARGING) ? COL_YELLOW : COL_GRAY);

    // Inner battery bars (3 slots, each 4x5 px)
    for (int i = 0; i < 3; i++) {
        uint16_t slot_col = (i < bars) ? bcol : COL_DARK_GRAY;
        fb_fill_rect(bx + 2 + i * 5, by + 2, 4, 5, slot_col);
    }
}

// -------------------------------------------------------------
// Robot Cat Anatomy: Body, Collar & Golden Bell
// -------------------------------------------------------------
static void render_cat_body(int base_y, face_state_t state)
{
    // Torso / Chest
    fb_fill_rounded_rect(44, base_y + 24, 40, 24, 8, COL_CAT_WHITE);
    // Subtle shadow on body side
    fb_draw_line(44, base_y + 36, 44, base_y + 46, COL_CAT_SHADOW);
    fb_draw_line(83, base_y + 36, 83, base_y + 46, COL_CAT_SHADOW);

    // Red Collar Band
    fb_fill_rounded_rect(46, base_y + 26, 36, 6, 3, RGB565_BE(235, 40, 80));

    // Golden Bell
    int bell_x = 64;
    int bell_y = base_y + 34;
    fb_fill_circle(bell_x, bell_y, 5, COL_CAT_GOLD);
    fb_draw_circle(bell_x, bell_y, 5, COL_CAT_DARK_GOLD);
    // Specular highlight
    fb_set_pixel(bell_x - 2, bell_y - 2, COL_CAT_WHITE);
    fb_set_pixel(bell_x - 1, bell_y - 2, COL_CAT_WHITE);
    // Horizontal slit & center hole
    fb_draw_line(bell_x - 3, bell_y + 1, bell_x + 3, bell_y + 1, COL_CAT_DARK_NAVY);
    fb_set_pixel(bell_x, bell_y + 2, COL_CAT_DARK_NAVY);
}

// -------------------------------------------------------------
// Robot Cat Anatomy: Ears, Pink Ribbon Bow & Cyber Headphones
// -------------------------------------------------------------
static void render_cat_ears_and_bow(int base_y, face_state_t state)
{
    // 1. Cyber Headphone Headband (connecting behind ears)
    for (int x = 24; x <= 104; x++) {
        float norm = (float)(x - 64) / 40.0f;
        int y = (base_y - 32) + (int)(norm * norm * 24.0f);
        fb_set_pixel(x, y, COL_CAT_CYAN);
        fb_set_pixel(x, y + 1, COL_CAT_CYAN);
    }

    // 2. Left Ear (Screen Left)
    // Outer white triangle
    fb_fill_triangle(23, base_y - 14, 32, base_y - 44, 49, base_y - 20, COL_CAT_WHITE);
    fb_draw_line(23, base_y - 14, 32, base_y - 44, COL_CAT_SHADOW);
    fb_draw_line(32, base_y - 44, 49, base_y - 20, COL_CAT_SHADOW);
    // Inner pink triangle
    fb_fill_triangle(26, base_y - 18, 33, base_y - 39, 44, base_y - 23, COL_CAT_PINK);
    // Light pink inner highlight
    fb_draw_line(29, base_y - 22, 33, base_y - 36, COL_CAT_LIGHT_PINK);

    // 3. Right Ear (Screen Right)
    // Outer white triangle
    fb_fill_triangle(79, base_y - 20, 96, base_y - 44, 105, base_y - 14, COL_CAT_WHITE);
    fb_draw_line(79, base_y - 20, 96, base_y - 44, COL_CAT_SHADOW);
    fb_draw_line(96, base_y - 44, 105, base_y - 14, COL_CAT_SHADOW);
    // Inner pink triangle
    fb_fill_triangle(84, base_y - 23, 95, base_y - 39, 102, base_y - 18, COL_CAT_PINK);
    fb_draw_line(95, base_y - 36, 99, base_y - 22, COL_CAT_LIGHT_PINK);

    // 4. Pink Ribbon Bow at Right Ear Base (Viewer's Right)
    int bow_x = 98;
    int bow_y = base_y - 25;
    // Left & Right Loops
    fb_fill_circle(bow_x - 4, bow_y, 4, COL_CAT_PINK);
    fb_fill_circle(bow_x + 4, bow_y, 4, COL_CAT_PINK);
    // Loop highlights
    fb_set_pixel(bow_x - 4, bow_y - 2, COL_CAT_LIGHT_PINK);
    fb_set_pixel(bow_x + 4, bow_y - 2, COL_CAT_LIGHT_PINK);
    // Center knot
    fb_fill_circle(bow_x, bow_y, 3, COL_CAT_DEEP_PINK);
    fb_set_pixel(bow_x, bow_y - 1, COL_CAT_LIGHT_PINK);
    // Ribbon tails
    fb_draw_thick_line(bow_x - 1, bow_y + 2, bow_x - 4, bow_y + 7, 2, COL_CAT_PINK);
    fb_draw_thick_line(bow_x + 1, bow_y + 2, bow_x + 4, bow_y + 7, 2, COL_CAT_PINK);
}

// -------------------------------------------------------------
// Robot Cat Anatomy: Head & Cyber Headphones
// -------------------------------------------------------------
static void render_cat_head(int base_y, face_state_t state)
{
    // Head: Smooth large rounded shape (74 px wide, 54 px high)
    fb_fill_ellipse(64, base_y, 37, 27, COL_CAT_WHITE);
    // Bottom contour line
    for (int x = 32; x <= 96; x++) {
        float dx = (float)(x - 64) / 37.0f;
        if (dx * dx <= 1.0f) {
            int y = base_y + (int)(sqrtf(1.0f - dx * dx) * 27.0f);
            fb_set_pixel(x, y, COL_CAT_SHADOW);
        }
    }

    // Cyber Headphones (Left & Right Earpads)
    // Left Earpad
    fb_fill_rounded_rect(17, base_y - 7, 9, 18, 4, COL_CAT_CYAN);
    fb_fill_rounded_rect(19, base_y - 5, 5, 14, 2, COL_CAT_DARK_NAVY);
    fb_set_pixel(21, base_y - 1, COL_CAT_WHITE);

    // Right Earpad
    fb_fill_rounded_rect(102, base_y - 7, 9, 18, 4, COL_CAT_CYAN);
    fb_fill_rounded_rect(104, base_y - 5, 5, 14, 2, COL_CAT_DARK_NAVY);
    fb_set_pixel(106, base_y - 1, COL_CAT_WHITE);

    // Rosy Blush Cheeks
    int blush_w = (state == FACE_EXCITED || state == FACE_HAPPY || state == FACE_LOVE) ? 8 : 6;
    fb_fill_ellipse(35, base_y + 12, blush_w, 4, COL_CAT_PINK);
    fb_fill_ellipse(93, base_y + 12, blush_w, 4, COL_CAT_PINK);

    if (state == FACE_EXCITED || state == FACE_HAPPY || state == FACE_LOVE) {
        // Extra white shine sparkle on blush
        fb_set_pixel(34, base_y + 11, COL_CAT_WHITE);
        fb_set_pixel(94, base_y + 11, COL_CAT_WHITE);
    }
}

// -------------------------------------------------------------
// Robot Cat Anatomy: Expressive Eyes
// -------------------------------------------------------------
static void draw_single_anime_eye(int cx, int cy, float openness, float gaze_x, float gaze_y, bool is_love)
{
    if (openness <= 0.08f) {
        // Closed eye crescent line
        fb_draw_thick_line(cx - 9, cy, cx - 3, cy - 3, 2, COL_CAT_DARK_NAVY);
        fb_draw_thick_line(cx - 3, cy - 3, cx + 3, cy - 3, 2, COL_CAT_DARK_NAVY);
        fb_draw_thick_line(cx + 3, cy - 3, cx + 9, cy, 2, COL_CAT_DARK_NAVY);
        // Eyelash flick
        fb_draw_line(cx + 8, cy - 1, cx + 11, cy - 3, COL_CAT_DARK_NAVY);
        return;
    }

    int rx = 10;
    int ry = (int)roundf(13.0f * openness);
    if (ry < 2) ry = 2;

    int gx = (int)roundf(gaze_x);
    int gy = (int)roundf(gaze_y);

    // Fill eye socket with 3-band anime gradient
    for (int dy = -ry; dy <= ry; dy++) {
        int y = cy + dy;
        if (y < 0 || y >= SCREEN_H) continue;
        float term = 1.0f - ((float)(dy * dy)) / ((float)(ry * ry));
        if (term < 0.0f) continue;
        int dx = (int)roundf((float)rx * sqrtf(term));
        int x0 = cx - dx;
        int x1 = cx + dx;

        for (int x = x0; x <= x1; x++) {
            uint16_t col;
            int rel_y = dy - gy;
            if (rel_y < -ry / 3) {
                // Top: Deep Navy/Violet
                col = COL_CAT_DARK_NAVY;
            } else if (rel_y <= ry / 4) {
                // Middle: Royal Sky Blue
                col = COL_CAT_SKY_BLUE;
            } else {
                // Bottom: Bright Cyan Glow
                col = COL_CAT_CYAN;
            }
            fb_set_pixel(x, y, col);
        }
    }

    // Outer dark outline
    fb_draw_circle(cx, cy, rx, COL_CAT_DARK_NAVY);

    if (openness >= 0.5f) {
        if (is_love) {
            // Heart-shaped highlight ♥
            fb_draw_heart(cx + gx, cy + gy, 4, COL_CAT_WHITE);
        } else {
            // Anime layered highlights
            // Primary large highlight (upper-left)
            fb_fill_circle(cx + gx - 3, cy + gy - 4, 3, COL_CAT_WHITE);
            // Secondary highlight (lower-right)
            fb_fill_circle(cx + gx + 3, cy + gy + 4, 1, COL_CAT_WHITE);
            // Diamond starlight dot
            fb_set_pixel(cx + gx + 4, cy + gy - 2, COL_CAT_WHITE);
        }
    }
}

static void render_cat_eyes(int base_y, face_state_t state, float blink_factor)
{
    int left_cx = 45;
    int right_cx = 83;
    int eye_cy = base_y;

    float gx = s_gaze_offset_x;
    float gy = s_gaze_offset_y;

    switch (state) {
        case FACE_EXCITED:
            // Big sparkling open eyes, gaze slightly up
            draw_single_anime_eye(left_cx, eye_cy, blink_factor, 0.0f, -2.0f, false);
            draw_single_anime_eye(right_cx, eye_cy, blink_factor, 0.0f, -2.0f, false);
            break;

        case FACE_SURPRISED:
            // Extra large wide eyes looking forward
            draw_single_anime_eye(left_cx, eye_cy, blink_factor, 0.0f, 0.0f, false);
            draw_single_anime_eye(right_cx, eye_cy, blink_factor, 0.0f, 0.0f, false);
            break;

        case FACE_SQUINT:
            // Left eye winks, right eye open sparkling!
            fb_draw_thick_line(left_cx - 8, eye_cy, left_cx, eye_cy - 4, 2, COL_CAT_DARK_NAVY);
            fb_draw_thick_line(left_cx, eye_cy - 4, left_cx + 8, eye_cy, 2, COL_CAT_DARK_NAVY);
            fb_draw_line(left_cx + 7, eye_cy - 1, left_cx + 10, eye_cy - 4, COL_CAT_DARK_NAVY);
            draw_single_anime_eye(right_cx, eye_cy, blink_factor, 0.0f, 0.0f, false);
            break;

        case FACE_WINK_LEFT:
            // Left wink arc, right eye wide open
            fb_draw_thick_line(left_cx - 8, eye_cy, left_cx, eye_cy - 4, 2, COL_CAT_DARK_NAVY);
            fb_draw_thick_line(left_cx, eye_cy - 4, left_cx + 8, eye_cy, 2, COL_CAT_DARK_NAVY);
            fb_draw_line(left_cx + 7, eye_cy - 1, left_cx + 10, eye_cy - 4, COL_CAT_DARK_NAVY);
            draw_single_anime_eye(right_cx, eye_cy, blink_factor, 0.0f, 0.0f, false);
            break;

        case FACE_WINK_RIGHT:
        case FACE_PLAYFUL:
            // Left eye open, right eye winks
            draw_single_anime_eye(left_cx, eye_cy, blink_factor, 0.0f, 0.0f, false);
            fb_draw_thick_line(right_cx - 8, eye_cy, right_cx, eye_cy - 4, 2, COL_CAT_DARK_NAVY);
            fb_draw_thick_line(right_cx, eye_cy - 4, right_cx + 8, eye_cy, 2, COL_CAT_DARK_NAVY);
            fb_draw_line(right_cx + 7, eye_cy - 1, right_cx + 10, eye_cy - 4, COL_CAT_DARK_NAVY);
            break;

        case FACE_HAPPY:
            // Both eyes closed in happy crescent arcs (^ ^)
            fb_draw_thick_line(left_cx - 8, eye_cy, left_cx, eye_cy - 4, 2, COL_CAT_DARK_NAVY);
            fb_draw_thick_line(left_cx, eye_cy - 4, left_cx + 8, eye_cy, 2, COL_CAT_DARK_NAVY);
            fb_draw_line(left_cx + 7, eye_cy - 1, left_cx + 10, eye_cy - 4, COL_CAT_DARK_NAVY);

            fb_draw_thick_line(right_cx - 8, eye_cy, right_cx, eye_cy - 4, 2, COL_CAT_DARK_NAVY);
            fb_draw_thick_line(right_cx, eye_cy - 4, right_cx + 8, eye_cy, 2, COL_CAT_DARK_NAVY);
            fb_draw_line(right_cx + 7, eye_cy - 1, right_cx + 10, eye_cy - 4, COL_CAT_DARK_NAVY);
            break;

        case FACE_CONFUSED:
            // Asymmetric eyes: left looking up-left, right slightly wider
            draw_single_anime_eye(left_cx, eye_cy, 0.85f * blink_factor, -3.0f, -3.0f, false);
            draw_single_anime_eye(right_cx, eye_cy, blink_factor, -3.0f, -2.0f, false);
            // Inquisitive curved brow
            fb_draw_line(left_cx - 6, eye_cy - 12, left_cx + 6, eye_cy - 14, COL_CAT_DARK_NAVY);
            fb_draw_line(right_cx - 6, eye_cy - 14, right_cx + 6, eye_cy - 11, COL_CAT_DARK_NAVY);
            break;

        case FACE_SLEEPY:
            // Relaxed horizontal sleepy eyes
            fb_draw_thick_line(left_cx - 8, eye_cy + 2, left_cx + 8, eye_cy + 2, 2, COL_CAT_DARK_NAVY);
            fb_draw_thick_line(right_cx - 8, eye_cy + 2, right_cx + 8, eye_cy + 2, 2, COL_CAT_DARK_NAVY);
            break;

        case FACE_SAD:
        case FACE_ERROR:
            // Sad watery eyes with tears
            draw_single_anime_eye(left_cx, eye_cy, 0.85f * blink_factor, 0.0f, 3.0f, false);
            draw_single_anime_eye(right_cx, eye_cy, 0.85f * blink_factor, 0.0f, 3.0f, false);
            // Slanted sad brows
            fb_draw_line(left_cx - 6, eye_cy - 14, left_cx + 6, eye_cy - 11, COL_CAT_DARK_NAVY);
            fb_draw_line(right_cx - 6, eye_cy - 11, right_cx + 6, eye_cy - 14, COL_CAT_DARK_NAVY);
            // Teardrops at outer corners
            fb_fill_circle(left_cx - 8, eye_cy + 8, 2, COL_CAT_CYAN);
            fb_fill_circle(right_cx + 8, eye_cy + 8, 2, COL_CAT_CYAN);
            break;

        case FACE_THINKING:
            // Looking up-right quizzically
            draw_single_anime_eye(left_cx, eye_cy, blink_factor, 3.0f, -3.5f, false);
            draw_single_anime_eye(right_cx, eye_cy, blink_factor, 3.0f, -3.5f, false);
            break;

        case FACE_LOVE:
            // Heart-shaped eyes ♥
            draw_single_anime_eye(left_cx, eye_cy, blink_factor, 0.0f, 0.0f, true);
            draw_single_anime_eye(right_cx, eye_cy, blink_factor, 0.0f, 0.0f, true);
            break;

        case FACE_ANGRY:
            // Slanted angry brows \ /
            draw_single_anime_eye(left_cx, eye_cy, 0.9f * blink_factor, 0.0f, 0.0f, false);
            draw_single_anime_eye(right_cx, eye_cy, 0.9f * blink_factor, 0.0f, 0.0f, false);
            fb_draw_thick_line(left_cx - 8, eye_cy - 12, left_cx + 6, eye_cy - 8, 2, COL_CAT_DARK_NAVY);
            fb_draw_thick_line(right_cx - 6, eye_cy - 8, right_cx + 8, eye_cy - 12, 2, COL_CAT_DARK_NAVY);
            break;

        case FACE_CURIOUS:
            // Asymmetric eyes
            draw_single_anime_eye(left_cx, eye_cy, 1.05f * blink_factor, 2.0f, -1.0f, false);
            draw_single_anime_eye(right_cx, eye_cy, 0.90f * blink_factor, 2.0f, -1.0f, false);
            break;

        case FACE_BOOT:
        case FACE_IDLE:
        case FACE_NEUTRAL:
        case FACE_RECOVERED:
        case FACE_LISTENING:
        case FACE_SPEAKING:
        default:
            // Normal sparkling anime eyes with saccade gaze
            draw_single_anime_eye(left_cx, eye_cy, blink_factor, gx, gy, false);
            draw_single_anime_eye(right_cx, eye_cy, blink_factor, gx, gy, false);
            break;
    }
}

// -------------------------------------------------------------
// Robot Cat Anatomy: Nose & Mouth
// -------------------------------------------------------------
static void render_cat_mouth(int base_y, face_state_t state, int64_t now_ms)
{
    // Tiny pink cat nose
    fb_fill_triangle(63, base_y + 8, 65, base_y + 8, 64, base_y + 10, COL_CAT_PINK);

    int mx = 64;
    int my = base_y + 13;

    switch (state) {
        case FACE_EXCITED:
        case FACE_HAPPY:
        case FACE_WINK_LEFT:
        case FACE_WINK_RIGHT:
            // Big open happy mouth showing tongue
            fb_fill_triangle(mx - 6, my, mx + 6, my, mx, my + 8, COL_CAT_MOUTH_RED);
            fb_fill_circle(mx, my + 6, 3, COL_CAT_TONGUE);
            fb_set_pixel(mx, my + 1, COL_CAT_WHITE); // Tiny tooth highlight
            break;

        case FACE_SURPRISED:
            // Small open 'O' mouth
            fb_fill_ellipse(mx, my + 3, 3, 4, COL_CAT_MOUTH_RED);
            fb_fill_circle(mx, my + 5, 2, COL_CAT_TONGUE);
            break;

        case FACE_SAD:
        case FACE_ERROR:
            // Sad downturned pout
            fb_draw_thick_line(mx - 5, my + 4, mx, my + 1, 2, COL_CAT_DARK_NAVY);
            fb_draw_thick_line(mx, my + 1, mx + 5, my + 4, 2, COL_CAT_DARK_NAVY);
            break;

        case FACE_CONFUSED:
            // Wavy quizzical mouth (~)
            fb_draw_line(mx - 4, my + 2, mx - 1, my, COL_CAT_DARK_NAVY);
            fb_draw_line(mx - 1, my, mx + 1, my + 3, COL_CAT_DARK_NAVY);
            fb_draw_line(mx + 1, my + 3, mx + 4, my + 1, COL_CAT_DARK_NAVY);
            break;

        case FACE_SPEAKING: {
            // Dynamic speaking mouth modulation
            float lev = s_audio_level_in;
            float synth = 0.3f + 0.4f * fabsf(sinf((float)now_ms * 0.012f));
            if (lev < 0.08f) lev = synth;
            int open_h = 3 + (int)(lev * 8.0f);
            if (open_h > 10) open_h = 10;

            if (open_h <= 3) {
                // Closed smile
                fb_draw_line(mx - 5, my + 1, mx, my + 2, COL_CAT_DARK_NAVY);
                fb_draw_line(mx, my + 2, mx + 5, my + 1, COL_CAT_DARK_NAVY);
            } else {
                fb_fill_triangle(mx - 5, my, mx + 5, my, mx, my + open_h, COL_CAT_MOUTH_RED);
                fb_fill_circle(mx, my + open_h - 2, 2, COL_CAT_TONGUE);
            }
            break;
        }

        case FACE_PLAYFUL:
            // Smile with tongue sticking out (blep :P)
            fb_draw_line(mx - 4, my + 1, mx, my + 2, COL_CAT_DARK_NAVY);
            fb_draw_line(mx, my + 2, mx + 4, my + 1, COL_CAT_DARK_NAVY);
            fb_fill_rounded_rect(mx, my + 2, 4, 5, 2, COL_CAT_TONGUE);
            break;

        case FACE_ANGRY:
            // Downward frown with fangs
            fb_draw_thick_line(mx - 5, my + 3, mx + 5, my + 3, 2, COL_CAT_DARK_NAVY);
            fb_fill_triangle(mx - 3, my + 3, mx - 1, my + 3, mx - 2, my + 5, COL_CAT_WHITE);
            fb_fill_triangle(mx + 1, my + 3, mx + 3, my + 3, mx + 2, my + 5, COL_CAT_WHITE);
            break;

        case FACE_SLEEPY:
            // Gentle relaxed mouth line
            fb_draw_line(mx - 3, my + 1, mx + 3, my + 1, COL_CAT_DARK_NAVY);
            break;

        case FACE_BOOT:
        case FACE_IDLE:
        case FACE_NEUTRAL:
        case FACE_RECOVERED:
        case FACE_SQUINT:
        default:
            // Classic sweet feline ':3' double curve smile
            fb_draw_line(mx - 5, my, mx - 2, my + 2, COL_CAT_DARK_NAVY);
            fb_draw_line(mx - 2, my + 2, mx, my + 1, COL_CAT_DARK_NAVY);
            fb_draw_line(mx, my + 1, mx + 2, my + 2, COL_CAT_DARK_NAVY);
            fb_draw_line(mx + 2, my + 2, mx + 5, my, COL_CAT_DARK_NAVY);
            break;
    }
}

// -------------------------------------------------------------
// Robot Cat Anatomy: Paws & Gestures
// -------------------------------------------------------------
static void draw_single_paw(int cx, int cy, bool raised)
{
    // White rounded paw body
    fb_fill_ellipse(cx, cy, 7, 6, COL_CAT_WHITE);
    fb_draw_circle(cx, cy, 7, COL_CAT_SHADOW);

    // Pink paw pads (1 large central bean + 3 tiny toe beans)
    fb_fill_ellipse(cx, cy + 1, 3, 2, COL_CAT_PINK);
    fb_fill_circle(cx - 3, cy - 2, 1, COL_CAT_PINK);
    fb_fill_circle(cx, cy - 4, 1, COL_CAT_PINK);
    fb_fill_circle(cx + 3, cy - 2, 1, COL_CAT_PINK);
}

static void render_cat_paws(int base_y, face_state_t state)
{
    switch (state) {
        case FACE_EXCITED:
        case FACE_HAPPY:
            // Both paws raised up waving happily!
            draw_single_paw(34, base_y + 38, true);
            draw_single_paw(94, base_y + 38, true);
            break;

        case FACE_SURPRISED:
            // Paws brought up to cheeks ("gasp" / surprised)
            draw_single_paw(44, base_y + 18, true);
            draw_single_paw(84, base_y + 18, true);
            break;

        case FACE_SQUINT:
        case FACE_WINK_LEFT:
        case FACE_WINK_RIGHT:
        case FACE_PLAYFUL:
            // One paw raised waving, one paw resting
            draw_single_paw(34, base_y + 36, true);
            draw_single_paw(86, base_y + 48, false);
            break;

        case FACE_CONFUSED:
            // One paw touching chin in deep thought!
            draw_single_paw(40, base_y + 48, false);
            draw_single_paw(70, base_y + 24, true);
            break;

        case FACE_SLEEPY:
            // Resting peacefully on soft pink cushion pillow
            fb_fill_rounded_rect(24, base_y + 44, 80, 16, 6, COL_CAT_PILLOW);
            fb_draw_circle(24, base_y + 52, 6, COL_CAT_DEEP_PINK);
            draw_single_paw(54, base_y + 48, false);
            draw_single_paw(74, base_y + 48, false);
            break;

        case FACE_SAD:
        case FACE_ERROR:
            // Paws clutched together at chest nervously
            draw_single_paw(58, base_y + 42, false);
            draw_single_paw(70, base_y + 42, false);
            break;

        case FACE_BOOT:
        case FACE_IDLE:
        case FACE_NEUTRAL:
        case FACE_RECOVERED:
        case FACE_LISTENING:
        case FACE_THINKING:
        case FACE_SPEAKING:
        default:
            // Resting peacefully in front of chest
            draw_single_paw(44, base_y + 46, false);
            draw_single_paw(84, base_y + 46, false);
            break;
    }
}

// -------------------------------------------------------------
// Bottom Minimal Status Banner
// -------------------------------------------------------------
static void render_status_banner(face_state_t state)
{
    const int bx = 8;
    const int by = 141;
    const int bw = 112;
    const int bh = 14;

    uint16_t pill_col = COL_CAT_PINK;
    const char *label = "READY";

    if (s_demo_active) {
        switch (state) {
            case FACE_EXCITED:    pill_col = RGB565_BE(255, 120, 180); label = "1. EXCITED"; break;
            case FACE_SURPRISED:  pill_col = RGB565_BE(255, 100, 170); label = "2. SURPRISED"; break;
            case FACE_SQUINT:     pill_col = RGB565_BE(70,  180, 245); label = "3. SQUINT"; break;
            case FACE_WINK_LEFT:  pill_col = RGB565_BE(60,  210, 170); label = "4. WINK L"; break;
            case FACE_WINK_RIGHT: pill_col = RGB565_BE(255, 140, 90);  label = "5. WINK R"; break;
            case FACE_CONFUSED:   pill_col = RGB565_BE(160, 120, 245); label = "6. CONFUSED"; break;
            case FACE_SLEEPY:     pill_col = RGB565_BE(70,  80,  160); label = "7. SLEEPY"; break;
            case FACE_SAD:        pill_col = RGB565_BE(230, 50,  70);  label = "8. ERROR SAD"; break;
            case FACE_HAPPY:      pill_col = RGB565_BE(255, 90,  160); label = "9. HAPPY"; break;
            case FACE_RECOVERED:  pill_col = RGB565_BE(40,  190, 180); label = "10. RECOVERED"; break;
            case FACE_NEUTRAL:    pill_col = RGB565_BE(80,  160, 240); label = "11. NEUTRAL"; break;
            case FACE_LISTENING:  pill_col = RGB565_BE(40,  210, 220); label = "12. LISTENING"; break;
            case FACE_THINKING:   pill_col = RGB565_BE(245, 170, 40);  label = "13. THINKING"; break;
            case FACE_SPEAKING:   pill_col = RGB565_BE(255, 120, 160); label = "14. SPEAKING"; break;
            case FACE_BLINK:      pill_col = RGB565_BE(100, 170, 240); label = "15. BLINK"; break;
            case FACE_CURIOUS:    pill_col = RGB565_BE(170, 130, 245); label = "16. CURIOUS"; break;
            case FACE_ANGRY:      pill_col = RGB565_BE(235, 60,  60);  label = "17. ANGRY"; break;
            case FACE_LOVE:       pill_col = RGB565_BE(255, 60,  130); label = "18. LOVE"; break;
            case FACE_PLAYFUL:    pill_col = RGB565_BE(255, 130, 180); label = "19. PLAYFUL"; break;
            case FACE_ERROR:      pill_col = RGB565_BE(235, 50,  50);  label = "20. ERROR"; break;
            default:              pill_col = COL_CAT_PINK;             label = "RBOT CAT"; break;
        }
    } else if (s_custom_status[0] != '\0') {
        label = s_custom_status;
        pill_col = COL_CAT_DARK_NAVY;
    } else {
        switch (state) {
            case FACE_BOOT:         label = "RBOT CAT"; pill_col = RGB565_BE(80, 180, 240); break;
            case FACE_IDLE:         label = s_is_sleepy ? "SLEEPY..." : "READY"; pill_col = s_is_sleepy ? RGB565_BE(70, 80, 160) : COL_CAT_PINK; break;
            case FACE_LISTENING:    label = "LISTENING"; pill_col = RGB565_BE(40, 210, 220); break;
            case FACE_THINKING:     label = "THINKING"; pill_col = RGB565_BE(245, 170, 40); break;
            case FACE_SPEAKING:     label = "SPEAKING"; pill_col = RGB565_BE(255, 120, 160); break;
            case FACE_HAPPY:        label = "HAPPY :)"; pill_col = RGB565_BE(255, 90, 160); break;
            case FACE_ERROR:        label = (s_error_msg[0] != '\0') ? s_error_msg : "ERROR"; pill_col = RGB565_BE(235, 50, 50); break;
            case FACE_DISCONNECTED: label = "OFFLINE"; pill_col = RGB565_BE(220, 120, 40); break;
            case FACE_EXCITED:      label = "EXCITED"; pill_col = RGB565_BE(255, 120, 180); break;
            case FACE_SURPRISED:    label = "SURPRISED"; pill_col = RGB565_BE(255, 100, 170); break;
            case FACE_SQUINT:       label = "SQUINT"; pill_col = RGB565_BE(70, 180, 245); break;
            case FACE_WINK_LEFT:    label = "WINK L"; pill_col = RGB565_BE(60, 210, 170); break;
            case FACE_WINK_RIGHT:   label = "WINK R"; pill_col = RGB565_BE(255, 140, 90); break;
            case FACE_CONFUSED:     label = "CONFUSED"; pill_col = RGB565_BE(160, 120, 245); break;
            case FACE_SLEEPY:       label = "SLEEPY"; pill_col = RGB565_BE(70, 80, 160); break;
            case FACE_SAD:          label = "SAD"; pill_col = RGB565_BE(230, 50, 70); break;
            case FACE_RECOVERED:    label = "RECOVERED"; pill_col = RGB565_BE(40, 190, 180); break;
            case FACE_NEUTRAL:      label = "READY"; pill_col = COL_CAT_PINK; break;
            case FACE_CURIOUS:      label = "CURIOUS"; pill_col = RGB565_BE(170, 130, 245); break;
            case FACE_ANGRY:        label = "ANGRY"; pill_col = RGB565_BE(235, 60, 60); break;
            case FACE_LOVE:         label = "LOVE <3"; pill_col = RGB565_BE(255, 60, 130); break;
            case FACE_PLAYFUL:      label = "PLAYFUL"; pill_col = RGB565_BE(255, 130, 180); break;
            default:                label = "READY"; pill_col = COL_CAT_PINK; break;
        }
    }

    // Draw Rounded Pill Background
    fb_fill_rounded_rect(bx, by, bw, bh, 7, pill_col);
    // Draw Subtle White Outline
    fb_draw_circle(bx + 7, by + 7, 7, COL_CAT_WHITE);
    fb_draw_circle(bx + bw - 8, by + 7, 7, COL_CAT_WHITE);
    fb_draw_line(bx + 7, by, bx + bw - 8, by, COL_CAT_WHITE);
    fb_draw_line(bx + 7, by + bh - 1, bx + bw - 8, by + bh - 1, COL_CAT_WHITE);

    // Draw Centered Label
    int text_y = by + (bh - 5) / 2;
    fb_draw_text_centered(text_y, label, COL_CAT_WHITE, 1);
}

// -------------------------------------------------------------
// Demo Mode Sequencer (20 Complete States)
// -------------------------------------------------------------
static void demo_mode_tick(int64_t now_ms)
{
    if (!s_demo_active) return;

    if (now_ms >= s_demo_step_until_ms) {
        s_demo_step++;
        switch (s_demo_step) {
            case 1:  s_current_state = FACE_EXCITED;    s_demo_step_until_ms = now_ms + 2200; break;
            case 2:  s_current_state = FACE_SURPRISED;  s_demo_step_until_ms = now_ms + 2200; break;
            case 3:  s_current_state = FACE_SQUINT;     s_demo_step_until_ms = now_ms + 2200; break;
            case 4:  s_current_state = FACE_WINK_LEFT;  s_demo_step_until_ms = now_ms + 2000; break;
            case 5:  s_current_state = FACE_WINK_RIGHT; s_demo_step_until_ms = now_ms + 2000; break;
            case 6:  s_current_state = FACE_CONFUSED;   s_demo_step_until_ms = now_ms + 2400; break;
            case 7:  s_current_state = FACE_SLEEPY;     s_demo_step_until_ms = now_ms + 2400; break;
            case 8:  s_current_state = FACE_SAD;        s_demo_step_until_ms = now_ms + 2400; break;
            case 9:  s_current_state = FACE_HAPPY;      s_demo_step_until_ms = now_ms + 2200; break;
            case 10: s_current_state = FACE_RECOVERED;  s_demo_step_until_ms = now_ms + 2200; break;
            case 11: s_current_state = FACE_NEUTRAL;    s_demo_step_until_ms = now_ms + 2000; break;
            case 12: s_current_state = FACE_LISTENING;  s_demo_step_until_ms = now_ms + 2200; break;
            case 13: s_current_state = FACE_THINKING;   s_demo_step_until_ms = now_ms + 2400; break;
            case 14: s_current_state = FACE_SPEAKING;   s_demo_step_until_ms = now_ms + 2200; break;
            case 15: s_current_state = FACE_BLINK;      s_demo_step_until_ms = now_ms + 1600; break;
            case 16: s_current_state = FACE_CURIOUS;    s_demo_step_until_ms = now_ms + 2200; break;
            case 17: s_current_state = FACE_ANGRY;      s_demo_step_until_ms = now_ms + 2200; break;
            case 18: s_current_state = FACE_LOVE;       s_demo_step_until_ms = now_ms + 2400; break;
            case 19: s_current_state = FACE_PLAYFUL;    s_demo_step_until_ms = now_ms + 2200; break;
            case 20: s_current_state = FACE_ERROR;      s_demo_step_until_ms = now_ms + 2400; break;
            case 21:
            default:
                s_current_state = FACE_IDLE;
                s_demo_active = false;
                ESP_LOGI(TAG_FACE, "Demo mode completed -> Returning to IDLE");
                break;
        }
    }
}

// -------------------------------------------------------------
// Face Animation Main Task (Cap: 20 FPS = 50ms period)
// -------------------------------------------------------------
static void face_task(void *arg)
{
    ESP_LOGI(TAG_FACE, "Kawaii Robot Cat Animation Task started (20 FPS)");

    const TickType_t frame_period_ticks = pdMS_TO_TICKS(50);
    s_fps_window_start_ms = esp_timer_get_time() / 1000;
    s_next_blink_ms = s_fps_window_start_ms + 3000 + (esp_random() % 3000);
    s_next_glance_ms = s_fps_window_start_ms + 4000;

    while (true) {
        int64_t t_frame_start = esp_timer_get_time();
        int64_t now_ms = t_frame_start / 1000;
        float time_sec = (float)now_ms / 1000.0f;

        // 1. Audio Level from player
        float player_lev = audio_player_get_level();
        if (player_lev > 0.01f) {
            s_audio_level_in = player_lev;
        }

        // 2. Demo & Timed Hold management
        if (s_demo_active) {
            demo_mode_tick(now_ms);
        } else if (s_timed_hold_until_ms > 0 && now_ms >= s_timed_hold_until_ms) {
            s_timed_hold_until_ms = 0;
            s_current_state = FACE_IDLE;
        }

        // 3. Sleepy Inactivity Detection (45 seconds without interaction)
        if (s_current_state == FACE_IDLE && !s_demo_active) {
            if (!s_is_sleepy && (now_ms - s_last_interaction_ms >= 45000)) {
                s_is_sleepy = true;
                ESP_LOGI(TAG_FACE, "Inactivity timeout -> Entering SLEEPY state");
            }
        }

        // 4. Natural Blinking Logic
        float blink_factor = 1.0f;
        if (s_current_state == FACE_BLINK) {
            blink_factor = 0.0f;
        } else if (!s_is_sleepy && s_current_state != FACE_SLEEPY && s_current_state != FACE_HAPPY) {
            if (!s_blink_active && now_ms >= s_next_blink_ms) {
                s_blink_active = true;
                s_blink_start_ms = now_ms;
                s_blink_dur_ms = 160;
            }

            if (s_blink_active) {
                int64_t elapsed = now_ms - s_blink_start_ms;
                if (elapsed >= s_blink_dur_ms) {
                    s_blink_active = false;
                    s_next_blink_ms = now_ms + 3500 + (esp_random() % 3000);
                    blink_factor = 1.0f;
                } else {
                    float phase = (float)elapsed / (float)s_blink_dur_ms;
                    // Cosine curve: 1.0 -> 0.0 -> 1.0
                    blink_factor = 0.5f + 0.5f * cosf(phase * 6.28318f);
                }
            }
        }

        // 5. Idle Saccades / Eye Gaze
        if (s_current_state == FACE_IDLE && !s_is_sleepy && !s_demo_active) {
            if (now_ms >= s_next_glance_ms) {
                int r = esp_random() % 100;
                if (r < 35) {
                    s_gaze_offset_x = -3.0f; s_gaze_offset_y = 0.0f;
                } else if (r < 70) {
                    s_gaze_offset_x = 3.0f; s_gaze_offset_y = 0.0f;
                } else {
                    s_gaze_offset_x = 0.0f; s_gaze_offset_y = -2.0f;
                }
                s_glance_return_ms = now_ms + 900 + (esp_random() % 900);
                s_next_glance_ms = now_ms + 4000 + (esp_random() % 4000);
            } else if (s_glance_return_ms > 0 && now_ms >= s_glance_return_ms) {
                s_gaze_offset_x = 0.0f;
                s_gaze_offset_y = 0.0f;
                s_glance_return_ms = 0;
            }
        } else {
            s_gaze_offset_x = 0.0f;
            s_gaze_offset_y = 0.0f;
        }

        // 6. Gentle Idle Bobbing Movement (Breathing)
        int bob_y = 0;
        if (s_current_state == FACE_EXCITED || s_current_state == FACE_HAPPY) {
            bob_y = (int)roundf(sinf(time_sec * 6.0f) * 1.8f);
        } else if (s_current_state != FACE_SLEEPY && !s_is_sleepy) {
            bob_y = (int)roundf(sinf(time_sec * 2.5f) * 1.0f);
        }
        int base_y = 62 + bob_y;

        face_state_t effective_state = s_current_state;
        if (s_is_sleepy && effective_state == FACE_IDLE) {
            effective_state = FACE_SLEEPY;
        }

        // -------------------------------------------------------------
        // Render Full-Color Kawaii Cat Scene Layer by Layer
        // -------------------------------------------------------------
        // Layer 1: Background Gradient & Decorations
        render_cat_background(effective_state, now_ms);

        // Layer 2: Top Connectivity & Battery Header
        render_header(now_ms);

        // Layer 3: Headband & Outer Ears
        render_cat_ears_and_bow(base_y, effective_state);

        // Layer 4: Torso, Red Collar & Golden Bell
        render_cat_body(base_y, effective_state);

        // Layer 5: Chibi Head & Cyber Headphones
        render_cat_head(base_y, effective_state);

        // Layer 6: Expressive Anime Eyes
        render_cat_eyes(base_y, effective_state, blink_factor);

        // Layer 7: Cat Nose & Animated Mouth
        render_cat_mouth(base_y, effective_state, now_ms);

        // Layer 8: Cute Cat Paws
        render_cat_paws(base_y, effective_state);

        // Layer 9: Bottom Rounded Pill Status Banner
        render_status_banner(effective_state);

        // 7. Sleepy Screen Dimming (50% reduction if SLEEPY, wake word remains 100% active)
        if (s_is_sleepy) {
            for (int px = 0; px < SCREEN_W * SCREEN_H; px++) {
                uint16_t c = s_framebuf[px];
                if (c != 0) {
                    uint16_t r = (c >> 11) & 0x1F;
                    uint16_t g = (c >> 5)  & 0x3F;
                    uint16_t b = c & 0x1F;
                    s_framebuf[px] = ((r >> 1) << 11) | ((g >> 1) << 5) | (b >> 1);
                }
            }
        }

        // 8. Blast Framebuffer over SPI DMA to ST7735 in Single Burst
        if (s_disp) {
            board_display_send_bitmap(s_disp, 0, 0, SCREEN_W, SCREEN_H, s_framebuf);
        }

        // 9. Metrics Calculation
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
// Public APIs & Convenience Kawaii Cat Interfaces
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

    if (!s_face_task_handle) {
        BaseType_t ok = xTaskCreate(face_task, "face_task", 4096, NULL, 2, &s_face_task_handle);
        if (ok != pdPASS) {
            ESP_LOGE(TAG_FACE, "Failed to create face animation task!");
            return ESP_FAIL;
        }
    }

    ESP_LOGI(TAG_FACE, "Kawaii Robot Cat Face Engine initialized (Framebuffer: %zu bytes)",
             SCREEN_W * SCREEN_H * sizeof(uint16_t));
    return ESP_OK;
}

void board_face_set_state(face_state_t state)
{
    if (s_demo_active) return;

    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;

    if (s_current_state != state) {
        s_current_state = state;
        s_state_start_ms = now_ms;
        s_custom_status[0] = '\0';
        s_timed_hold_until_ms = 0;
    }
}

face_state_t board_face_get_state(void)
{
    return s_current_state;
}

void board_face_set_error(face_error_type_t err_type, const char *short_msg)
{
    s_error_type = err_type;
    if (short_msg && short_msg[0] != '\0') {
        strncpy(s_error_msg, short_msg, sizeof(s_error_msg) - 1);
        s_error_msg[sizeof(s_error_msg) - 1] = '\0';
    } else {
        s_error_msg[0] = '\0';
    }
    board_face_set_state(FACE_ERROR);
}

void board_face_trigger_expression(face_state_t exp, uint32_t duration_ms)
{
    if (s_demo_active) return;
    int64_t now_ms = esp_timer_get_time() / 1000;
    s_last_interaction_ms = now_ms;
    s_is_sleepy = false;
    s_current_state = exp;
    s_timed_hold_until_ms = now_ms + duration_ms;
}

void board_face_trigger_happy(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_HAPPY, duration_ms);
}

void board_face_trigger_happy_big(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_HAPPY, duration_ms);
}

void board_face_trigger_squint(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_SQUINT, duration_ms);
}

void board_face_trigger_recovered(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_RECOVERED, duration_ms);
}

void board_face_trigger_love(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_LOVE, duration_ms);
}

void board_face_trigger_angry(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_ANGRY, duration_ms);
}

void board_face_trigger_playful(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_PLAYFUL, duration_ms);
}

void board_face_trigger_sad(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_SAD, duration_ms);
}

void board_face_trigger_curious(bool left_larger, uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_CURIOUS, duration_ms);
}

void board_face_trigger_confused(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_CONFUSED, duration_ms);
}

void board_face_trigger_excited(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_EXCITED, duration_ms);
}

void board_face_trigger_surprised(uint32_t duration_ms)
{
    board_face_trigger_expression(FACE_SURPRISED, duration_ms);
}

void board_face_trigger_wink(bool left_eye, uint32_t duration_ms)
{
    board_face_trigger_expression(left_eye ? FACE_WINK_LEFT : FACE_WINK_RIGHT, duration_ms);
}

void board_face_trigger_sleepy(void)
{
    s_is_sleepy = true;
    s_current_state = FACE_SLEEPY;
}

void board_face_set_status_text(const char *text)
{
    if (!text) {
        s_custom_status[0] = '\0';
    } else {
        strncpy(s_custom_status, text, sizeof(s_custom_status) - 1);
        s_custom_status[sizeof(s_custom_status) - 1] = '\0';
    }
}

void board_face_set_audio_level(float level)
{
    s_audio_level_in = level;
}

void board_face_set_wifi_status(wifi_status_t status)
{
    s_wifi_status = status;
}

void board_face_set_server_status(server_status_t status)
{
    s_server_status = status;
}

server_status_t board_face_get_server_status(void)
{
    return s_server_status;
}

void board_face_set_battery_state(battery_state_t bat)
{
    s_bat_state = bat;
}

void board_face_trigger_demo(void)
{
    s_demo_active = true;
    s_demo_step = 0;
    s_demo_step_until_ms = 0;
    s_is_sleepy = false;
    ESP_LOGI(TAG_FACE, "Kawaii Robot Cat 20-State Expression Demo triggered!");
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

// -------------------------------------------------------------
// Kawaii Cat UI Convenience Functions
// -------------------------------------------------------------
void cat_set_expression(face_state_t expression)
{
    board_face_set_state(expression);
}

void cat_update(void)
{
    // Engine updates automatically in face_task
}

void cat_set_speaking(bool enabled)
{
    board_face_set_state(enabled ? FACE_SPEAKING : FACE_IDLE);
}

void cat_set_listening(bool enabled)
{
    board_face_set_state(enabled ? FACE_LISTENING : FACE_IDLE);
}

void cat_set_thinking(bool enabled)
{
    board_face_set_state(enabled ? FACE_THINKING : FACE_IDLE);
}

void cat_set_battery_level(battery_state_t level)
{
    board_face_set_battery_state(level);
}

void cat_set_connection_state(wifi_status_t state)
{
    board_face_set_wifi_status(state);
}
