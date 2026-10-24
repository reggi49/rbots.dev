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
// Premium Kawaii 3D Cat Palette — warm pastels, pseudo-3D depth
// Big-Endian for SPI DMA (ST7735 expects MSB first)
// Light source: upper-left
// -------------------------------------------------------------
#define RGB565_BE(r, g, b) (__builtin_bswap16(((uint16_t)((r) & 0xF8) << 8) | \
                                              ((uint16_t)((g) & 0xFC) << 3) | \
                                              ((uint16_t)(b) >> 3)))

// --- Fur & Body ---
#define COL_FUR_BASE         RGB565_BE(255, 249, 242) // Warm milk white #FFF9F2
#define COL_FUR_HIGHLIGHT    RGB565_BE(255, 255, 255) // Pure white specular
#define COL_FUR_SHADOW       RGB565_BE(228, 218, 232) // Lavender shadow on fur
#define COL_FUR_DEEP_SHADOW  RGB565_BE(200, 190, 210) // Deeper chin/under shadow
#define COL_OUTLINE          RGB565_BE(81,  68,  95)  // Soft outline #51445F

// --- Pink family ---
#define COL_CHEEK_PINK       RGB565_BE(255, 184, 210) // Blush #FFB8D2
#define COL_CHEEK_HIGHLIGHT  RGB565_BE(255, 225, 238) // Blush inner highlight
#define COL_BOW_PINK         RGB565_BE(242, 138, 181) // Rose accent bow #F28AB5
#define COL_BOW_HIGHLIGHT    RGB565_BE(255, 200, 220) // Bow highlight
#define COL_BOW_SHADOW       RGB565_BE(210, 100, 140) // Bow shadow
#define COL_EAR_INNER        RGB565_BE(255, 214, 232) // Soft inner ear pink
#define COL_COLLAR_PINK      RGB565_BE(242, 138, 181) // Collar matches bow
#define COL_COLLAR_SHADOW    RGB565_BE(200, 100, 130) // Collar shadow

// --- Eyes ---
#define COL_EYE_OUTER        RGB565_BE(37,  38,  79)  // Deep eye contour #25264F
#define COL_IRIS_TOP         RGB565_BE(102, 140, 255) // Iris upper blue #668CFF
#define COL_IRIS_BOT         RGB565_BE(142, 220, 255) // Iris lower light blue #8EDCFF
#define COL_PUPIL            RGB565_BE(25,  25,  50)  // Dark pupil
#define COL_EYE_HIGHLIGHT    RGB565_BE(255, 255, 255) // White highlight
#define COL_EYE_HIGHLIGHT2   RGB565_BE(200, 220, 255) // Secondary reflection

// --- Accessories ---
#define COL_BELL_GOLD        RGB565_BE(255, 214, 110) // Bell gold #FFD66E
#define COL_BELL_HIGHLIGHT   RGB565_BE(255, 240, 180) // Bell specular
#define COL_BELL_SHADOW      RGB565_BE(201, 149, 67)  // Warm shadow gold #C99543
#define COL_BELL_DARK        RGB565_BE(140, 100, 40)  // Bell slit

// --- Background ---
#define COL_BG_TOP           RGB565_BE(200, 216, 255) // Background blue top #C8D8FF
#define COL_BG_BOT           RGB565_BE(220, 210, 245) // Background lavender bottom
#define COL_BG_WARM          RGB565_BE(255, 242, 229) // Cream background #FFF2E5
#define COL_SPARKLE          RGB565_BE(255, 255, 255) // Sparkle white
#define COL_SPARKLE_SOFT     RGB565_BE(230, 225, 245) // Soft sparkle

// --- Mouth ---
#define COL_MOUTH_DARK       RGB565_BE(81,  68,  95)  // Mouth line
#define COL_MOUTH_RED        RGB565_BE(180, 50,  80)  // Open mouth interior
#define COL_TONGUE           RGB565_BE(255, 150, 180) // Tongue pink

// --- UI / Status ---
#define COL_BLACK            RGB565_BE(0,   0,   0)
#define COL_GREEN            RGB565_BE(0,   210, 80)
#define COL_YELLOW           RGB565_BE(255, 210, 0)
#define COL_ERROR            RGB565_BE(240, 70,  70)
#define COL_GRAY             RGB565_BE(100, 100, 110)
#define COL_DARK_GRAY        RGB565_BE(50,  50,  60)
#define COL_SHADOW_LAV       RGB565_BE(156, 139, 198) // Shadow lavender #9C8BC6
#define COL_PILLOW           RGB565_BE(255, 195, 220) // Sleepy cushion

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
static int64_t s_boot_start_ms = 0;
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

// Draw just the outline of an axis-aligned ellipse (Bresenham midpoint)
static void fb_draw_ellipse(int cx, int cy, int rx, int ry, uint16_t col)
{
    if (rx <= 0 || ry <= 0) return;
    // Midpoint ellipse algorithm
    int x = 0, y = ry;
    long rx2 = (long)rx * rx, ry2 = (long)ry * ry;
    long px = 0, py = 2 * rx2 * y;
    long d;

    // Region 1
    d = (long)(ry2 - rx2 * ry + 0.25f * rx2);
    while (px < py) {
        fb_set_pixel(cx + x, cy + y, col);
        fb_set_pixel(cx - x, cy + y, col);
        fb_set_pixel(cx + x, cy - y, col);
        fb_set_pixel(cx - x, cy - y, col);
        x++;
        px += 2 * ry2;
        if (d < 0) {
            d += ry2 + px;
        } else {
            y--;
            py -= 2 * rx2;
            d += ry2 + px - py;
        }
    }
    // Region 2
    d = (long)(ry2 * (x + 0.5f) * (x + 0.5f) + rx2 * (y - 1) * (y - 1) - rx2 * ry2);
    while (y >= 0) {
        fb_set_pixel(cx + x, cy + y, col);
        fb_set_pixel(cx - x, cy + y, col);
        fb_set_pixel(cx + x, cy - y, col);
        fb_set_pixel(cx - x, cy - y, col);
        y--;
        py -= 2 * rx2;
        if (d > 0) {
            d += rx2 - py;
        } else {
            x++;
            px += 2 * ry2;
            d += rx2 - py + px;
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

static void fb_draw_head_contour(int cx, int cy, int rx, int ry, uint16_t col)
{
    int x = 0;
    int y = ry;
    int a2 = rx * rx;
    int b2 = ry * ry;
    int err = b2 - (2 * ry - 1) * a2;
    int e2;

    do {
        // Bottom points (always draw around cheeks and chin)
        fb_set_pixel(cx + x, cy + y, col);
        fb_set_pixel(cx - x, cy + y, col);

        // Top points (skip under ear bases so ears connect cleanly)
        int px_r = cx + x;
        int px_l = cx - x;
        int py_t = cy - y;

        if (px_r < 79 || px_r > 105) {
            fb_set_pixel(px_r, py_t, col);
        }
        if (px_l > 49 || px_l < 23) {
            fb_set_pixel(px_l, py_t, col);
        }

        e2 = 2 * err;
        if (e2 < (2 * x + 1) * b2) { x++; err += (2 * x + 1) * b2; }
        if (e2 > -(2 * y - 1) * a2) { y--; err -= (2 * y - 1) * a2; }
    } while (y >= 0);
}

static void fb_draw_sweat_drop(int cx, int cy, int size, uint16_t col)
{
    fb_fill_triangle(cx, cy - size, cx - size / 2, cy, cx + size / 2, cy, col);
    fb_fill_circle(cx, cy, size / 2, col);
    fb_set_pixel(cx - 1, cy - size / 3, COL_FUR_HIGHLIGHT);
}

static void fb_draw_anger_mark(int cx, int cy, int size, uint16_t col)
{
    fb_draw_thick_line(cx - size, cy - size / 2, cx + size, cy - size / 2, 2, col);
    fb_draw_thick_line(cx - size, cy + size / 2, cx + size, cy + size / 2, 2, col);
    fb_draw_thick_line(cx - size / 2, cy - size, cx - size / 2, cy + size, 2, col);
    fb_draw_thick_line(cx + size / 2, cy - size, cx + size / 2, cy + size, 2, col);
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
    fb_fill_circle(cx + r/2, cy - r/3, r - 1, COL_EYE_OUTER);
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
    (void)t_sec; // used by some states

    // --- Unified soft pastel background base ---
    // Most states share the same gentle sky-blue to lavender gradient.
    // Only a few states use a slightly different tint, keeping the overall
    // visual language consistent and avoiding rainbow-chaos.

    switch (state) {
        case FACE_SLEEPY:
            // Soft dark indigo for night
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(30, 30, 60), RGB565_BE(50, 45, 90));
            // Crescent moon
            fb_draw_crescent_moon(24, 22, 6, COL_BELL_GOLD);
            // Small stars
            fb_draw_star(50, 18, 2, COL_SPARKLE_SOFT);
            fb_draw_star(16, 42, 1, COL_SPARKLE);
            // Floating Z's
            {
                int z_off = (int)(t_sec * 10.0f) % 20;
                fb_draw_zzz(94, 36 - z_off, 1, COL_SPARKLE_SOFT);
                fb_draw_zzz(102, 26 - z_off, 2, COL_SPARKLE);
            }
            break;

        case FACE_SAD:
        case FACE_ERROR:
            // Muted dusty rose — sad but not alarming
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(180, 160, 190), RGB565_BE(140, 120, 155));
            // Single small broken heart accent
            fb_draw_broken_heart(64, 20 + (int)(sinf(t_sec * 2.5f) * 1.5f), COL_BOW_PINK);
            break;

        case FACE_ANGRY:
            // Warm dark rose
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(200, 130, 140), RGB565_BE(160, 90, 100));
            // Small anger cross mark
            fb_draw_line(102, 18, 110, 18, COL_FUR_HIGHLIGHT);
            fb_draw_line(106, 14, 106, 22, COL_FUR_HIGHLIGHT);
            break;

        case FACE_LOVE:
            // Soft warm pink
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 210, 225), RGB565_BE(240, 180, 210));
            // Floating hearts
            fb_draw_heart(20, 24 + (int)(sinf(t_sec * 3.0f) * 2.0f), 4, COL_SPARKLE);
            fb_draw_heart(108, 28 + (int)(sinf(t_sec * 3.0f + 2.0f) * 2.0f), 3, COL_BOW_HIGHLIGHT);
            break;

        case FACE_HAPPY:
        case FACE_HAPPY_BIG:
        case FACE_EXCITED:
        case FACE_RECOVERED:
            // Cheerful warm cream-pink
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 235, 220), RGB565_BE(250, 210, 230));
            // A couple of gentle sparkle stars
            fb_draw_star(16, 28, 3, COL_SPARKLE);
            fb_draw_star(112, 26, 3, COL_SPARKLE_SOFT);
            break;

        case FACE_LISTENING:
            // Same calm blue gradient with subtle pulse accents
            fb_gradient_v(0, SCREEN_H - 1, COL_BG_TOP, COL_BG_BOT);
            {
                int wr = ((int)(now_ms / 40) % 14) + 4;
                fb_draw_circle(18, 64, wr, COL_SPARKLE_SOFT);
                fb_draw_circle(110, 64, wr, COL_SPARKLE_SOFT);
            }
            break;

        case FACE_THINKING:
            // Slightly warmer lavender tint
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(210, 200, 245), RGB565_BE(190, 185, 235));
            // Orbiting thought dots
            for (int i = 0; i < 3; i++) {
                float rad = t_sec * 3.0f + (float)i * 2.094f;
                int ox = 64 + (int)(cosf(rad) * 20.0f);
                int oy = 20 + (int)(sinf(rad) * 5.0f);
                fb_fill_circle(ox, oy, 2, COL_SPARKLE);
            }
            break;

        case FACE_SPEAKING:
            // Warm cream with gentle pulse
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 240, 225), RGB565_BE(245, 220, 235));
            {
                int pr = (int)(s_audio_level_in * 12.0f) + 3;
                fb_draw_circle(64, 80, pr, COL_SPARKLE_SOFT);
            }
            break;

        case FACE_CONFUSED:
            // Slightly deeper lavender
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(215, 200, 250), RGB565_BE(185, 170, 235));
            // Single floating question mark
            fb_draw_question_mark(52, 18 + (int)(sinf(t_sec * 3.0f) * 2.0f), 8, COL_SPARKLE);
            break;

        case FACE_SURPRISED:
            // Warm cream-peach
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(255, 240, 210), RGB565_BE(255, 215, 220));
            // Small exclamation accents
            fb_draw_exclamation_mark(58, 17 + (int)(sinf(t_sec * 4.0f) * 1.5f), 7, COL_BELL_GOLD);
            fb_draw_exclamation_mark(68, 15 + (int)(sinf(t_sec * 4.0f + 1.5f) * 1.5f), 8, COL_BOW_PINK);
            break;

        case FACE_DISCONNECTED:
            // Muted gray-blue — subdued
            fb_gradient_v(0, SCREEN_H - 1, RGB565_BE(160, 165, 180), RGB565_BE(130, 135, 155));
            break;

        case FACE_BOOT:
        case FACE_IDLE:
        default:
            // Calm cohesive pastel sky-blue to soft lavender
            fb_gradient_v(0, SCREEN_H - 1, COL_BG_TOP, COL_BG_BOT);
            // A few tiny sparkles to give the world life
            fb_draw_star(16, 28, 2, COL_SPARKLE);
            fb_draw_star(112, 26, 3, COL_SPARKLE_SOFT);
            fb_draw_star(14, 120, 2, COL_SPARKLE_SOFT);
            fb_draw_star(114, 118, 2, COL_SPARKLE);
            break;
    }

    // --- Subtle ground shadow beneath the character ---
    // Creates a sense of grounding and depth
    fb_fill_ellipse(64, 138, 30, 4, rgb565_lerp(COL_BG_BOT, COL_BLACK, 40));
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
        scol = COL_GREEN;
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
// Kawaii Cat Anatomy: Body, Pink Collar & Golden Bell
// No headphones — replaced with natural fur body.
// -------------------------------------------------------------
static void render_cat_body(int base_y, face_state_t state)
{
    // Torso / Chest — warm white with subtle shadow
    fb_fill_rounded_rect(44, base_y + 24, 40, 24, 8, COL_FUR_BASE);
    // Right edge shadow (light from upper-left)
    fb_draw_line(83, base_y + 36, 83, base_y + 46, COL_FUR_SHADOW);
    // Bottom shadow
    fb_draw_line(46, base_y + 47, 82, base_y + 47, COL_FUR_SHADOW);

    // Pink Collar Band (matches bow)
    fb_fill_rounded_rect(46, base_y + 26, 36, 6, 3, COL_COLLAR_PINK);
    // Collar shadow on bottom edge
    fb_draw_line(46, base_y + 31, 82, base_y + 31, COL_COLLAR_SHADOW);

    // Golden Bell with pseudo-3D shading
    int bell_x = 64;
    int bell_y = base_y + 34;
    fb_fill_circle(bell_x, bell_y, 5, COL_BELL_GOLD);
    fb_draw_circle(bell_x, bell_y, 5, COL_BELL_SHADOW);
    // Upper-left highlight (specular)
    fb_set_pixel(bell_x - 2, bell_y - 2, COL_BELL_HIGHLIGHT);
    fb_set_pixel(bell_x - 1, bell_y - 2, COL_BELL_HIGHLIGHT);
    fb_set_pixel(bell_x - 2, bell_y - 1, COL_BELL_HIGHLIGHT);
    // Horizontal slit & center hole
    fb_draw_line(bell_x - 3, bell_y + 1, bell_x + 3, bell_y + 1, COL_BELL_DARK);
    fb_set_pixel(bell_x, bell_y + 2, COL_BELL_DARK);
}

// -------------------------------------------------------------
// Kawaii Cat Anatomy: Ears (3D shaded) & Volumetric Pink Bow
// No cyber headphones — pure organic cat appearance.
// Light source: upper-left
// -------------------------------------------------------------
static void render_cat_ears_and_bow(int base_y, face_state_t state)
{
    // --- Left Ear (Screen Left) ---
    // Outer fur — warm white base with shadow on right/bottom face
    fb_fill_triangle(22, base_y - 12, 31, base_y - 42, 50, base_y - 18, COL_FUR_BASE);
    // Outline (thick 2px line for solid, gap-free rendering)
    fb_draw_thick_line(22, base_y - 12, 31, base_y - 42, 2, COL_OUTLINE);
    fb_draw_thick_line(31, base_y - 42, 50, base_y - 18, 2, COL_OUTLINE);
    // Right-face shadow (darker — away from light)
    fb_fill_triangle(36, base_y - 14, 42, base_y - 36, 50, base_y - 18, COL_FUR_SHADOW);
    // Inner ear pink (slightly smaller)
    fb_fill_triangle(26, base_y - 16, 32, base_y - 37, 45, base_y - 21, COL_EAR_INNER);
    // Inner ear highlight (upper-left lit, brighter when LISTENING)
    uint16_t ear_hl = (state == FACE_LISTENING) ? COL_FUR_HIGHLIGHT : COL_CHEEK_HIGHLIGHT;
    fb_draw_line(28, base_y - 20, 32, base_y - 34, ear_hl);

    // --- Right Ear (Screen Right) ---
    // Outer fur
    fb_fill_triangle(78, base_y - 18, 97, base_y - 42, 106, base_y - 12, COL_FUR_BASE);
    // Outline (thick 2px line for solid rendering)
    fb_draw_thick_line(78, base_y - 18, 97, base_y - 42, 2, COL_OUTLINE);
    fb_draw_thick_line(97, base_y - 42, 106, base_y - 12, 2, COL_OUTLINE);
    // Right-face shadow
    fb_fill_triangle(78, base_y - 18, 86, base_y - 36, 92, base_y - 14, COL_FUR_SHADOW);
    // Inner ear pink
    fb_fill_triangle(83, base_y - 21, 95, base_y - 37, 101, base_y - 16, COL_EAR_INNER);
    // Inner ear highlight
    fb_draw_line(95, base_y - 34, 99, base_y - 20, ear_hl);

    // --- Volumetric Pink Bow at Right Ear Base ---
    // The bow sits at the base of the right ear (character's left = viewer's right)
    // Light source upper-left: left loop lit, right loop shaded, knot medium
    int bow_x = 99;
    int bow_y = base_y - 22;

    // Left loop (lighter — towards light source)
    fb_fill_circle(bow_x - 5, bow_y, 5, COL_BOW_HIGHLIGHT);
    fb_fill_circle(bow_x - 4, bow_y, 4, COL_BOW_PINK);
    // Right loop (darker — away from light)
    fb_fill_circle(bow_x + 5, bow_y, 5, COL_BOW_SHADOW);
    fb_fill_circle(bow_x + 4, bow_y, 4, COL_BOW_PINK);
    // Loop inner highlights (tiny specular)
    fb_set_pixel(bow_x - 5, bow_y - 2, COL_BOW_HIGHLIGHT);
    fb_set_pixel(bow_x + 3, bow_y - 2, COL_BOW_PINK);
    // Center knot (darker, dimensional)
    fb_fill_circle(bow_x, bow_y, 3, COL_BOW_SHADOW);
    fb_fill_circle(bow_x, bow_y, 2, COL_BOW_PINK);
    fb_set_pixel(bow_x - 1, bow_y - 1, COL_BOW_HIGHLIGHT);
    // Ribbon tails hanging down (from knot)
    fb_draw_thick_line(bow_x - 1, bow_y + 2, bow_x - 4, bow_y + 8, 2, COL_BOW_PINK);
    fb_draw_thick_line(bow_x + 1, bow_y + 2, bow_x + 4, bow_y + 8, 2, COL_BOW_SHADOW);
}

// -------------------------------------------------------------
// Kawaii Cat Anatomy: Head with Pseudo-3D Shading & Cheeks
// Light source: upper-left. Connected ears — seamless outline.
// -------------------------------------------------------------
static void render_cat_head(int base_y, face_state_t state)
{
    // --- Head ellipse: warm white fur ---
    fb_fill_ellipse(64, base_y, 37, 27, COL_FUR_BASE);

    // --- Pseudo-3D shading on head ---
    // Upper-left highlight arc (lit side)
    for (int x = 35; x <= 75; x++) {
        float dx = (float)(x - 64) / 37.0f;
        if (dx * dx <= 1.0f) {
            int y = base_y - (int)(sqrtf(1.0f - dx * dx) * 27.0f);
            if (y >= 0 && y < SCREEN_H) {
                fb_set_pixel(x, y + 1, COL_FUR_HIGHLIGHT);
            }
        }
    }

    // Lower-right shadow region (away from light)
    for (int x = 60; x <= 96; x++) {
        float dx = (float)(x - 64) / 37.0f;
        if (dx * dx <= 1.0f) {
            int y = base_y + (int)(sqrtf(1.0f - dx * dx) * 27.0f);
            if (y >= 0 && y < SCREEN_H) {
                fb_set_pixel(x, y, COL_FUR_SHADOW);
            }
        }
    }

    // Bottom contour/shadow line
    for (int x = 36; x <= 92; x++) {
        float dx = (float)(x - 64) / 37.0f;
        if (dx * dx <= 1.0f) {
            int y = base_y + (int)(sqrtf(1.0f - dx * dx) * 27.0f);
            fb_set_pixel(x, y, COL_FUR_SHADOW);
        }
    }

    // Head contour outline (skips ear attachment areas so ears connect cleanly)
    fb_draw_head_contour(64, base_y, 37, 27, COL_OUTLINE);

    // --- Rosy Blush Cheeks ---
    // Dynamically sized to emotional state
    int blush_w = 6;
    if (state == FACE_HAPPY_BIG) {
        blush_w = 9; // Extra wide and prominent for laughing joy
    } else if (state == FACE_EXCITED || state == FACE_LOVE) {
        blush_w = 8;
    } else if (state == FACE_HAPPY) {
        blush_w = 7;
    } else if (state == FACE_SLEEPY || state == FACE_OFFLINE) {
        blush_w = 5;
    }
    fb_fill_ellipse(35, base_y + 12, blush_w, 4, COL_CHEEK_PINK);
    fb_fill_ellipse(93, base_y + 12, blush_w, 4, COL_CHEEK_PINK);

    // Cheek highlight (upper-left lit)
    if (state == FACE_HAPPY_BIG || state == FACE_EXCITED || state == FACE_LOVE) {
        fb_set_pixel(33, base_y + 11, COL_CHEEK_HIGHLIGHT);
        fb_set_pixel(34, base_y + 11, COL_CHEEK_HIGHLIGHT);
        fb_set_pixel(33, base_y + 12, COL_CHEEK_HIGHLIGHT);
        fb_set_pixel(91, base_y + 11, COL_CHEEK_HIGHLIGHT);
        fb_set_pixel(92, base_y + 11, COL_CHEEK_HIGHLIGHT);
    } else if (state == FACE_HAPPY) {
        fb_set_pixel(34, base_y + 11, COL_CHEEK_HIGHLIGHT);
        fb_set_pixel(92, base_y + 11, COL_CHEEK_HIGHLIGHT);
    }
}

// -------------------------------------------------------------
// Kawaii Cat Anatomy: Glossy Layered Anime Eyes
// Multi-layer: dark contour → iris gradient → pupil → highlights
// Light source: upper-left (primary highlight at upper-left of eye)
// -------------------------------------------------------------
typedef enum {
    EYE_HL_NORMAL = 0,    // Primary circle + secondary circle + starlight dot
    EYE_HL_STAR,          // Radiant starlight diamond pupil and extra sparkles (EXCITED)
    EYE_HL_HEART,         // Heart highlight (LOVE)
    EYE_HL_SURPRISE,      // Constricted pupil (radius 2) + tiny specular dot (SURPRISED)
    EYE_HL_SOFT_SMILE     // Open iris with curved smiling lower eyelid (HAPPY)
} eye_highlight_t;

static void draw_eye_closed_happy(int cx, int cy, bool is_left)
{
    // Energetic joyful squeezed arc (^ ^)
    fb_draw_thick_line(cx - 8, cy + 2, cx - 3, cy - 4, 2, COL_EYE_OUTER);
    fb_draw_thick_line(cx - 3, cy - 4, cx + 3, cy - 4, 2, COL_EYE_OUTER);
    fb_draw_thick_line(cx + 3, cy - 4, cx + 8, cy + 2, 2, COL_EYE_OUTER);
    if (is_left) {
        fb_draw_line(cx - 8, cy + 1, cx - 11, cy - 2, COL_EYE_OUTER);
        fb_draw_line(cx - 7, cy + 3, cx - 10, cy + 2, COL_EYE_OUTER);
    } else {
        fb_draw_line(cx + 8, cy + 1, cx + 11, cy - 2, COL_EYE_OUTER);
        fb_draw_line(cx + 7, cy + 3, cx + 10, cy + 2, COL_EYE_OUTER);
    }
}

static void draw_eye_closed_sleepy(int cx, int cy, bool is_left)
{
    // Peaceful relaxed resting line with gentle dip (- -)
    fb_draw_thick_line(cx - 8, cy - 1, cx - 2, cy + 2, 2, COL_EYE_OUTER);
    fb_draw_thick_line(cx - 2, cy + 2, cx + 2, cy + 2, 2, COL_EYE_OUTER);
    fb_draw_thick_line(cx + 2, cy + 2, cx + 8, cy - 1, 2, COL_EYE_OUTER);
    if (is_left) {
        fb_draw_line(cx - 7, cy, cx - 10, cy + 3, COL_EYE_OUTER);
    } else {
        fb_draw_line(cx + 7, cy, cx + 10, cy + 3, COL_EYE_OUTER);
    }
}

static void draw_eye_closed_wink(int cx, int cy, bool is_left)
{
    // Playful wink arc with sweet double flick
    fb_draw_thick_line(cx - 8, cy + 1, cx, cy - 4, 2, COL_EYE_OUTER);
    fb_draw_thick_line(cx, cy - 4, cx + 8, cy + 1, 2, COL_EYE_OUTER);
    if (is_left) {
        fb_draw_line(cx - 7, cy, cx - 11, cy - 3, COL_EYE_OUTER);
        fb_draw_line(cx - 6, cy + 2, cx - 9, cy + 2, COL_EYE_OUTER);
    } else {
        fb_draw_line(cx + 7, cy, cx + 11, cy - 3, COL_EYE_OUTER);
        fb_draw_line(cx + 6, cy + 2, cx + 9, cy + 2, COL_EYE_OUTER);
    }
}

static void draw_single_anime_eye(int cx, int cy, int rx, int ry, float gaze_x, float gaze_y, eye_highlight_t hl_mode)
{
    if (ry <= 2) {
        draw_eye_closed_happy(cx, cy, (cx < 64));
        return;
    }

    int gx = (int)roundf(gaze_x);
    int gy = (int)roundf(gaze_y);

    int max_dy = ry;
    if (hl_mode == EYE_HL_SOFT_SMILE) {
        max_dy = (int)roundf((float)ry * 0.72f);
    }

    // --- Fill iris with 3-band gradient ---
    for (int dy = -ry; dy <= max_dy; dy++) {
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
                col = COL_EYE_OUTER;
            } else if (rel_y <= ry / 4) {
                col = COL_IRIS_TOP;
            } else {
                col = COL_IRIS_BOT;
            }
            fb_set_pixel(x, y, col);
        }
    }

    // --- Pupil ---
    if (hl_mode == EYE_HL_SURPRISE) {
        fb_fill_circle(cx + gx, cy + gy, 2, COL_PUPIL);
    } else {
        int pup_ry = (ry > 6) ? 4 : 2;
        fb_fill_ellipse(cx + gx, cy + gy, 3, pup_ry, COL_PUPIL);
    }

    // --- Outer contour ---
    if (hl_mode == EYE_HL_SOFT_SMILE) {
        fb_draw_ellipse(cx, cy, rx, ry, COL_EYE_OUTER);
        fb_draw_thick_line(cx - rx + 1, cy + max_dy - 1, cx, cy + max_dy + 1, 2, COL_EYE_OUTER);
        fb_draw_thick_line(cx, cy + max_dy + 1, cx + rx - 1, cy + max_dy - 1, 2, COL_EYE_OUTER);
    } else {
        fb_draw_ellipse(cx, cy, rx, ry, COL_EYE_OUTER);
    }

    // --- Glossy highlights ---
    if (ry >= 5) {
        if (hl_mode == EYE_HL_HEART) {
            fb_draw_heart(cx + gx, cy + gy - 1, 4, COL_EYE_HIGHLIGHT);
        } else if (hl_mode == EYE_HL_STAR) {
            fb_fill_circle(cx + gx - 3, cy + gy - 4, 3, COL_EYE_HIGHLIGHT);
            fb_draw_line(cx + gx - 5, cy + gy - 4, cx + gx - 1, cy + gy - 4, COL_EYE_HIGHLIGHT);
            fb_draw_line(cx + gx - 3, cy + gy - 6, cx + gx - 3, cy + gy - 2, COL_EYE_HIGHLIGHT);
            fb_fill_circle(cx + gx + 3, cy + gy + 3, 1, COL_EYE_HIGHLIGHT2);
            fb_set_pixel(cx + gx + 4, cy + gy - 2, COL_EYE_HIGHLIGHT);
            fb_set_pixel(cx + gx - 4, cy + gy + 2, COL_EYE_HIGHLIGHT);
        } else if (hl_mode == EYE_HL_SURPRISE) {
            fb_fill_circle(cx + gx - 2, cy + gy - 3, 1, COL_EYE_HIGHLIGHT);
        } else {
            fb_fill_circle(cx + gx - 3, cy + gy - 4, 3, COL_EYE_HIGHLIGHT);
            fb_fill_circle(cx + gx + 3, cy + gy + 3, 1, COL_EYE_HIGHLIGHT2);
            fb_set_pixel(cx + gx + 4, cy + gy - 2, COL_EYE_HIGHLIGHT);
        }
    }
}

static void render_cat_eyebrows(int base_y, face_state_t state)
{
    int left_cx = 45;
    int right_cx = 83;
    int eye_cy = base_y;

    switch (state) {
        case FACE_EXCITED:
            // High arched enthusiastic brows
            fb_draw_thick_line(left_cx - 6, eye_cy - 16, left_cx, eye_cy - 18, 2, COL_OUTLINE);
            fb_draw_thick_line(left_cx, eye_cy - 18, left_cx + 5, eye_cy - 16, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx - 5, eye_cy - 16, right_cx, eye_cy - 18, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx, eye_cy - 18, right_cx + 6, eye_cy - 16, 2, COL_OUTLINE);
            break;

        case FACE_SURPRISED:
            // Very high startled straight-curved brows
            fb_draw_thick_line(left_cx - 6, eye_cy - 19, left_cx + 5, eye_cy - 19, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx - 5, eye_cy - 19, right_cx + 6, eye_cy - 19, 2, COL_OUTLINE);
            break;

        case FACE_CONFUSED:
            // Strongly asymmetric: left furrowed skepticism (\), right arched high (/)
            fb_draw_thick_line(left_cx - 6, eye_cy - 13, left_cx + 5, eye_cy - 16, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx - 5, eye_cy - 18, right_cx + 6, eye_cy - 14, 2, COL_OUTLINE);
            break;

        case FACE_THINKING:
            // Left brow raised contemplative arc, right brow neutral
            fb_draw_thick_line(left_cx - 5, eye_cy - 17, left_cx + 5, eye_cy - 16, 2, COL_OUTLINE);
            fb_draw_line(right_cx - 5, eye_cy - 14, right_cx + 5, eye_cy - 14, COL_OUTLINE);
            break;

        case FACE_CURIOUS_L:
            // Left brow raised high inquisitive arc
            fb_draw_thick_line(left_cx - 5, eye_cy - 17, left_cx + 5, eye_cy - 15, 2, COL_OUTLINE);
            fb_draw_line(right_cx - 5, eye_cy - 14, right_cx + 5, eye_cy - 14, COL_OUTLINE);
            break;

        case FACE_CURIOUS_R:
            // Right brow raised high inquisitive arc
            fb_draw_line(left_cx - 5, eye_cy - 14, left_cx + 5, eye_cy - 14, COL_OUTLINE);
            fb_draw_thick_line(right_cx - 5, eye_cy - 15, right_cx + 5, eye_cy - 17, 2, COL_OUTLINE);
            break;

        case FACE_HAPPY:
            // Soft sweet smiling brows
            fb_draw_line(left_cx - 5, eye_cy - 15, left_cx, eye_cy - 17, COL_OUTLINE);
            fb_draw_line(left_cx, eye_cy - 17, left_cx + 5, eye_cy - 15, COL_OUTLINE);
            fb_draw_line(right_cx - 5, eye_cy - 15, right_cx, eye_cy - 17, COL_OUTLINE);
            fb_draw_line(right_cx, eye_cy - 17, right_cx + 5, eye_cy - 15, COL_OUTLINE);
            break;

        case FACE_HAPPY_BIG:
            // High happy brows
            fb_draw_thick_line(left_cx - 6, eye_cy - 16, left_cx, eye_cy - 18, 2, COL_OUTLINE);
            fb_draw_thick_line(left_cx, eye_cy - 18, left_cx + 5, eye_cy - 16, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx - 5, eye_cy - 16, right_cx, eye_cy - 18, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx, eye_cy - 18, right_cx + 6, eye_cy - 16, 2, COL_OUTLINE);
            break;

        case FACE_ERROR:
        case FACE_SAD:
            // Worried slanted eyebrows angled inward/upward (/ \)
            fb_draw_thick_line(left_cx - 6, eye_cy - 13, left_cx + 5, eye_cy - 17, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx - 5, eye_cy - 17, right_cx + 6, eye_cy - 13, 2, COL_OUTLINE);
            break;

        case FACE_ANGRY:
            // Fierce slanted furrowed brows (\ /)
            fb_draw_thick_line(left_cx - 6, eye_cy - 16, left_cx + 5, eye_cy - 12, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx - 5, eye_cy - 12, right_cx + 6, eye_cy - 16, 2, COL_OUTLINE);
            break;

        case FACE_LISTENING:
            // Alert attentive slightly perked brows
            fb_draw_thick_line(left_cx - 5, eye_cy - 15, left_cx + 5, eye_cy - 15, 2, COL_OUTLINE);
            fb_draw_thick_line(right_cx - 5, eye_cy - 15, right_cx + 5, eye_cy - 15, 2, COL_OUTLINE);
            break;

        case FACE_SLEEPY:
            // Relaxed sleepy brows
            fb_draw_line(left_cx - 4, eye_cy - 10, left_cx + 4, eye_cy - 10, COL_OUTLINE);
            fb_draw_line(right_cx - 4, eye_cy - 10, right_cx + 4, eye_cy - 10, COL_OUTLINE);
            break;

        case FACE_OFFLINE:
        case FACE_IDLE:
        case FACE_NEUTRAL:
        case FACE_SPEAKING:
        default:
            // Gentle neutral brow lines
            fb_draw_line(left_cx - 4, eye_cy - 14, left_cx + 4, eye_cy - 14, COL_OUTLINE);
            fb_draw_line(right_cx - 4, eye_cy - 14, right_cx + 4, eye_cy - 14, COL_OUTLINE);
            break;
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
        case FACE_EXCITED: {
            // Wide sparkling anime eyes looking slightly upward with star specular
            int ry = (int)roundf(14.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 11, ry, 0.0f, -2.0f, EYE_HL_STAR);
            draw_single_anime_eye(right_cx, eye_cy, 11, ry, 0.0f, -2.0f, EYE_HL_STAR);
            break;
        }

        case FACE_SURPRISED: {
            // Tall round wide eyes with small constricted pupils
            int ry = (int)roundf(15.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 11, ry, 0.0f, 0.0f, EYE_HL_SURPRISE);
            draw_single_anime_eye(right_cx, eye_cy, 11, ry, 0.0f, 0.0f, EYE_HL_SURPRISE);
            break;
        }

        case FACE_HAPPY: {
            // Soft smiling OPEN anime eyes with upturned lower lids!
            if (blink_factor <= 0.2f) {
                draw_eye_closed_happy(left_cx, eye_cy, true);
                draw_eye_closed_happy(right_cx, eye_cy, false);
            } else {
                int ry = (int)roundf(12.0f * blink_factor);
                draw_single_anime_eye(left_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_SOFT_SMILE);
                draw_single_anime_eye(right_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_SOFT_SMILE);
            }
            break;
        }

        case FACE_HAPPY_BIG:
            // Squeezed shut into joyful happy upward arcs (^ ^)
            draw_eye_closed_happy(left_cx, eye_cy, true);
            draw_eye_closed_happy(right_cx, eye_cy, false);
            break;

        case FACE_CURIOUS_L: {
            // Left eye enlarged, right eye narrower half-lid, looking left
            int l_ry = (int)roundf(14.0f * blink_factor);
            int r_ry = (int)roundf(11.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 11, l_ry, -3.5f, 0.0f, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy, 9,  r_ry, -3.5f, 0.0f, EYE_HL_NORMAL);
            break;
        }

        case FACE_CURIOUS_R: {
            // Right eye enlarged, left eye narrower half-lid, looking right
            int l_ry = (int)roundf(11.0f * blink_factor);
            int r_ry = (int)roundf(14.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 9,  l_ry, 3.5f, 0.0f, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy, 11, r_ry, 3.5f, 0.0f, EYE_HL_NORMAL);
            break;
        }

        case FACE_SQUINT:
        case FACE_WINK_LEFT: {
            // Left eye closed wink, right eye wide open
            draw_eye_closed_wink(left_cx, eye_cy, true);
            int ry = (int)roundf(13.0f * blink_factor);
            draw_single_anime_eye(right_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
            break;
        }

        case FACE_WINK_RIGHT:
        case FACE_PLAYFUL: {
            // Left eye wide open, right eye closed wink
            int ry = (int)roundf(13.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
            draw_eye_closed_wink(right_cx, eye_cy, false);
            break;
        }

        case FACE_CONFUSED: {
            // Strongly asymmetric: left squinted skeptical, right wide open
            int l_ry = (int)roundf(9.0f * blink_factor);
            int r_ry = (int)roundf(13.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 9,  l_ry, -3.0f, -2.0f, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy, 11, r_ry, -2.0f, -2.5f, EYE_HL_NORMAL);
            break;
        }

        case FACE_SLEEPY:
            // Both eyes peacefully closed with downward resting lids
            draw_eye_closed_sleepy(left_cx, eye_cy, true);
            draw_eye_closed_sleepy(right_cx, eye_cy, false);
            break;

        case FACE_THINKING: {
            // Asymmetrical thoughtful upward-right glance
            int l_ry = (int)roundf(13.0f * blink_factor);
            int r_ry = (int)roundf(10.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 10, l_ry, 2.5f, -3.0f, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy, 9,  r_ry, 2.5f, -3.0f, EYE_HL_NORMAL);
            break;
        }

        case FACE_LISTENING: {
            // Attentive forward alert eyes
            int ry = (int)roundf(14.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 10, ry, 0.0f, -1.0f, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy, 10, ry, 0.0f, -1.0f, EYE_HL_NORMAL);
            break;
        }

        case FACE_ERROR:
        case FACE_SAD: {
            // Concerned / worried apologetic downward gaze
            int ry = (int)roundf(11.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy + 1, 10, ry, 1.0f, 2.0f, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy + 1, 10, ry, -1.0f, 2.0f, EYE_HL_NORMAL);
            break;
        }

        case FACE_DISCONNECTED: {
            // Subdued half-lidded calm eyes
            int ry = (int)roundf(8.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
            break;
        }

        case FACE_BOOT: {
            int64_t boot_elapsed = esp_timer_get_time() / 1000 - s_boot_start_ms;
            if (boot_elapsed < 800) {
                draw_eye_closed_sleepy(left_cx, eye_cy, true);
                draw_eye_closed_sleepy(right_cx, eye_cy, false);
            } else if (boot_elapsed < 1600) {
                float open = 0.5f + 0.3f * sinf((float)(boot_elapsed - 800) / 800.0f * 3.14159f);
                int ry = (int)roundf(10.0f * open);
                draw_single_anime_eye(left_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
                draw_single_anime_eye(right_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
            } else {
                int ry = (int)roundf(13.0f * blink_factor);
                draw_single_anime_eye(left_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
                draw_single_anime_eye(right_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
            }
            break;
        }

        case FACE_LOVE: {
            int ry = (int)roundf(13.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_HEART);
            draw_single_anime_eye(right_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_HEART);
            break;
        }

        case FACE_ANGRY: {
            int ry = (int)roundf(10.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy, 10, ry, 0.0f, 0.0f, EYE_HL_NORMAL);
            break;
        }

        case FACE_SPEAKING:
        case FACE_IDLE:
        case FACE_NEUTRAL:
        case FACE_RECOVERED:
        default: {
            int ry = (int)roundf(13.0f * blink_factor);
            draw_single_anime_eye(left_cx, eye_cy, 10, ry, gx, gy, EYE_HL_NORMAL);
            draw_single_anime_eye(right_cx, eye_cy, 10, ry, gx, gy, EYE_HL_NORMAL);
            break;
        }
    }
}

// -------------------------------------------------------------
// Kawaii Cat Anatomy: Nose & Expressive Mouth
// -------------------------------------------------------------
static void render_cat_mouth(int base_y, face_state_t state, int64_t now_ms)
{
    // Tiny pink cat nose (small inverted triangle)
    fb_fill_triangle(63, base_y + 8, 65, base_y + 8, 64, base_y + 10, COL_CHEEK_PINK);

    int mx = 64;
    int my = base_y + 13;

    switch (state) {
        case FACE_HAPPY:
            // Sweet curved happy smile
            fb_draw_thick_line(mx - 6, my + 1, mx, my + 3, 2, COL_EYE_OUTER);
            fb_draw_thick_line(mx, my + 3, mx + 6, my + 1, 2, COL_EYE_OUTER);
            fb_draw_line(mx - 6, my + 1, mx - 7, my - 1, COL_EYE_OUTER);
            fb_draw_line(mx + 6, my + 1, mx + 7, my - 1, COL_EYE_OUTER);
            break;

        case FACE_HAPPY_BIG:
            // Wide open joyful laughing mouth showing ruby interior, large pink tongue, and tooth highlight!
            fb_fill_triangle(mx - 7, my, mx + 7, my, mx, my + 9, COL_MOUTH_RED);
            fb_fill_circle(mx, my + 7, 3, COL_TONGUE);
            fb_set_pixel(mx, my + 1, COL_FUR_HIGHLIGHT); // Tooth specular
            fb_draw_line(mx - 7, my, mx + 7, my, COL_EYE_OUTER);
            fb_draw_line(mx - 7, my, mx, my + 9, COL_EYE_OUTER);
            fb_draw_line(mx + 7, my, mx, my + 9, COL_EYE_OUTER);
            break;

        case FACE_EXCITED:
            // Big enthusiastic open 'D' mouth with tongue
            fb_fill_triangle(mx - 6, my, mx + 6, my, mx, my + 8, COL_MOUTH_RED);
            fb_fill_circle(mx, my + 6, 3, COL_TONGUE);
            fb_set_pixel(mx, my + 1, COL_FUR_HIGHLIGHT);
            break;

        case FACE_SURPRISED:
            // Small round 'O' mouth
            fb_fill_ellipse(mx, my + 3, 3, 5, COL_MOUTH_RED);
            fb_fill_circle(mx, my + 5, 2, COL_TONGUE);
            fb_draw_ellipse(mx, my + 3, 3, 5, COL_EYE_OUTER);
            break;

        case FACE_LISTENING:
            // Small neat closed attentive mouth
            fb_draw_thick_line(mx - 3, my + 1, mx + 3, my + 1, 2, COL_EYE_OUTER);
            break;

        case FACE_THINKING:
            // Small thoughtful off-center pursed mouth
            fb_draw_thick_line(mx, my + 2, mx + 4, my + 1, 2, COL_EYE_OUTER);
            fb_draw_line(mx + 4, my + 1, mx + 3, my - 1, COL_EYE_OUTER);
            break;

        case FACE_SPEAKING: {
            // Dynamic speaking mouth modulation with 2 shapes (open 'ah' vs round 'oh')
            float lev = s_audio_level_in;
            float synth = 0.3f + 0.4f * fabsf(sinf((float)now_ms * 0.012f));
            if (lev < 0.08f) lev = synth;
            int open_h = 3 + (int)(lev * 8.0f);
            if (open_h > 10) open_h = 10;

            if (open_h <= 3) {
                // Closed friendly smile
                fb_draw_line(mx - 5, my + 1, mx, my + 2, COL_EYE_OUTER);
                fb_draw_line(mx, my + 2, mx + 5, my + 1, COL_EYE_OUTER);
            } else if (((now_ms / 200) % 2) == 0) {
                // Shape 1: Open 'ah' wedge
                fb_fill_triangle(mx - 5, my, mx + 5, my, mx, my + open_h, COL_MOUTH_RED);
                fb_fill_circle(mx, my + open_h - 2, 2, COL_TONGUE);
                fb_draw_line(mx - 5, my, mx + 5, my, COL_EYE_OUTER);
            } else {
                // Shape 2: Rounded 'oh' oval
                int oh_w = open_h > 6 ? 4 : 3;
                fb_fill_ellipse(mx, my + open_h / 2, oh_w, open_h / 2, COL_MOUTH_RED);
                fb_fill_circle(mx, my + open_h - 2, 2, COL_TONGUE);
                fb_draw_ellipse(mx, my + open_h / 2, oh_w, open_h / 2, COL_EYE_OUTER);
            }
            break;
        }

        case FACE_CONFUSED:
            // Crooked wavy quizzical mouth (~)
            fb_draw_thick_line(mx - 5, my + 3, mx - 2, my, 2, COL_EYE_OUTER);
            fb_draw_thick_line(mx - 2, my, mx + 1, my + 3, 2, COL_EYE_OUTER);
            fb_draw_thick_line(mx + 1, my + 3, mx + 5, my + 1, 2, COL_EYE_OUTER);
            break;

        case FACE_CURIOUS_L:
            // Inquisitive smile tilted up to the left
            fb_draw_thick_line(mx - 5, my - 1, mx - 1, my + 2, 2, COL_EYE_OUTER);
            fb_draw_thick_line(mx - 1, my + 2, mx + 5, my + 3, 2, COL_EYE_OUTER);
            break;

        case FACE_CURIOUS_R:
            // Inquisitive smile tilted up to the right
            fb_draw_thick_line(mx - 5, my + 3, mx + 1, my + 2, 2, COL_EYE_OUTER);
            fb_draw_thick_line(mx + 1, my + 2, mx + 5, my - 1, 2, COL_EYE_OUTER);
            break;

        case FACE_WINK_LEFT:
        case FACE_WINK_RIGHT:
            // Playful asymmetric cat smirk
            fb_draw_thick_line(mx - 4, my + 2, mx, my + 2, 2, COL_EYE_OUTER);
            fb_draw_thick_line(mx, my + 2, mx + 5, my - 1, 2, COL_EYE_OUTER);
            break;

        case FACE_PLAYFUL:
            // Smile with tongue sticking out (blep :P)
            fb_draw_line(mx - 4, my + 1, mx, my + 2, COL_EYE_OUTER);
            fb_draw_line(mx, my + 2, mx + 4, my + 1, COL_EYE_OUTER);
            fb_fill_rounded_rect(mx, my + 2, 4, 5, 2, COL_TONGUE);
            break;

        case FACE_ANGRY:
            // Downward frown with fangs
            fb_draw_thick_line(mx - 5, my + 3, mx + 5, my + 3, 2, COL_EYE_OUTER);
            fb_fill_triangle(mx - 3, my + 3, mx - 1, my + 3, mx - 2, my + 5, COL_FUR_HIGHLIGHT);
            fb_fill_triangle(mx + 1, my + 3, mx + 3, my + 3, mx + 2, my + 5, COL_FUR_HIGHLIGHT);
            break;

        case FACE_ERROR:
        case FACE_SAD:
            // Worried downturned apologetic pout
            fb_draw_thick_line(mx - 5, my + 3, mx, my, 2, COL_EYE_OUTER);
            fb_draw_thick_line(mx, my, mx + 5, my + 3, 2, COL_EYE_OUTER);
            break;

        case FACE_SLEEPY:
            // Gentle relaxed mouth line
            fb_draw_line(mx - 3, my + 1, mx + 3, my + 1, COL_EYE_OUTER);
            break;

        case FACE_OFFLINE:
            // Calm neutral straight mouth
            fb_draw_line(mx - 3, my + 1, mx + 3, my + 1, COL_EYE_OUTER);
            break;

        case FACE_BOOT: {
            int64_t boot_elapsed = now_ms - s_boot_start_ms;
            if (boot_elapsed < 800) {
                fb_draw_line(mx - 3, my + 1, mx + 3, my + 1, COL_EYE_OUTER);
            } else if (boot_elapsed < 1600) {
                // Sleepy kitten stretch yawn
                fb_fill_ellipse(mx, my + 3, 2, 3, COL_MOUTH_RED);
                fb_draw_ellipse(mx, my + 3, 2, 3, COL_EYE_OUTER);
            } else {
                // Waking feline smile (:3)
                fb_draw_line(mx - 5, my, mx - 2, my + 2, COL_EYE_OUTER);
                fb_draw_line(mx - 2, my + 2, mx, my + 1, COL_EYE_OUTER);
                fb_draw_line(mx, my + 1, mx + 2, my + 2, COL_EYE_OUTER);
                fb_draw_line(mx + 2, my + 2, mx + 5, my, COL_EYE_OUTER);
            }
            break;
        }

        case FACE_IDLE:
        case FACE_NEUTRAL:
        case FACE_RECOVERED:
        case FACE_SQUINT:
        default:
            // Classic sweet feline ':3' double curve smile
            fb_draw_line(mx - 5, my, mx - 2, my + 2, COL_EYE_OUTER);
            fb_draw_line(mx - 2, my + 2, mx, my + 1, COL_EYE_OUTER);
            fb_draw_line(mx, my + 1, mx + 2, my + 2, COL_EYE_OUTER);
            fb_draw_line(mx + 2, my + 2, mx + 5, my, COL_EYE_OUTER);
            break;
    }
}

// -------------------------------------------------------------
// Kawaii Cat Anatomy: Paws & Gestures
// -------------------------------------------------------------
static void draw_single_paw(int cx, int cy, bool raised)
{
    // Warm white rounded paw body
    fb_fill_ellipse(cx, cy, 7, 6, COL_FUR_BASE);
    fb_draw_circle(cx, cy, 7, COL_FUR_SHADOW);

    // Pink paw pads (1 large central bean + 3 tiny toe beans)
    fb_fill_ellipse(cx, cy + 1, 3, 2, COL_CHEEK_PINK);
    fb_fill_circle(cx - 3, cy - 2, 1, COL_CHEEK_PINK);
    fb_fill_circle(cx, cy - 4, 1, COL_CHEEK_PINK);
    fb_fill_circle(cx + 3, cy - 2, 1, COL_CHEEK_PINK);
}

static void render_cat_paws(int base_y, face_state_t state)
{
    switch (state) {
        case FACE_EXCITED:
        case FACE_HAPPY_BIG:
            // Both paws raised up waving happily!
            draw_single_paw(34, base_y + 36, true);
            draw_single_paw(94, base_y + 36, true);
            break;

        case FACE_SURPRISED:
            // Paws brought up to cheeks ("gasp" / surprised)
            draw_single_paw(42, base_y + 16, true);
            draw_single_paw(86, base_y + 16, true);
            break;

        case FACE_CONFUSED:
            // One paw touching chin in deep puzzlement!
            draw_single_paw(42, base_y + 46, false);
            draw_single_paw(70, base_y + 22, true);
            break;

        case FACE_CURIOUS_L:
            // Left paw raised inquisitive
            draw_single_paw(36, base_y + 36, true);
            draw_single_paw(84, base_y + 46, false);
            break;

        case FACE_CURIOUS_R:
            // Right paw raised inquisitive
            draw_single_paw(44, base_y + 46, false);
            draw_single_paw(92, base_y + 36, true);
            break;

        case FACE_SQUINT:
        case FACE_WINK_LEFT:
            draw_single_paw(34, base_y + 36, true);
            draw_single_paw(84, base_y + 46, false);
            break;

        case FACE_WINK_RIGHT:
        case FACE_PLAYFUL:
            draw_single_paw(44, base_y + 46, false);
            draw_single_paw(94, base_y + 36, true);
            break;

        case FACE_THINKING:
            // Right paw slightly lifted toward chin
            draw_single_paw(44, base_y + 46, false);
            draw_single_paw(78, base_y + 34, true);
            break;

        case FACE_SLEEPY:
            // Resting peacefully on soft pink cushion pillow
            fb_fill_rounded_rect(24, base_y + 44, 80, 16, 6, COL_PILLOW);
            fb_draw_circle(24, base_y + 52, 6, COL_BOW_SHADOW);
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
        case FACE_SPEAKING:
        case FACE_HAPPY:
        case FACE_OFFLINE:
        default:
            // Resting peacefully in front of chest
            draw_single_paw(44, base_y + 46, false);
            draw_single_paw(84, base_y + 46, false);
            break;
    }
}

// -------------------------------------------------------------
// Kawaii Cat Emotional Embellishments (Sweat drop, ?, !, Zzz, Hearts)
// -------------------------------------------------------------
static void render_cat_decorations(int base_y, face_state_t state, int64_t now_ms)
{
    switch (state) {
        case FACE_CONFUSED:
            // Floating '?' question mark above right head
            fb_draw_question_mark(88, base_y - 28, 9, COL_BOW_PINK);
            break;

        case FACE_SURPRISED:
            // Floating '!' surprise exclamation mark above head
            fb_draw_exclamation_mark(88, base_y - 28, 9, COL_BOW_PINK);
            break;

        case FACE_THINKING: {
            // 3 glowing thought dots floating near right ear
            float pulse = 0.5f + 0.5f * sinf((float)now_ms * 0.008f);
            uint16_t col = (pulse > 0.5f) ? COL_SPARKLE : COL_SPARKLE_SOFT;
            fb_fill_circle(88, base_y - 18, 2, col);
            fb_fill_circle(93, base_y - 25, 3, col);
            fb_fill_circle(99, base_y - 33, 4, col);
            break;
        }

        case FACE_ERROR:
        case FACE_SAD:
            // Apologetic kawaii sweat drop on forehead
            fb_draw_sweat_drop(44, base_y - 12, 5, RGB565_BE(140, 205, 255));
            break;

        case FACE_ANGRY:
            // Anime pop-vein anger mark 💢 above left ear
            fb_draw_anger_mark(36, base_y - 26, 4, RGB565_BE(235, 60, 60));
            break;

        case FACE_HAPPY_BIG:
        case FACE_LOVE: {
            // Floating hearts
            float drift = fmodf((float)now_ms * 0.02f, 20.0f);
            fb_draw_heart(92, base_y - 22 - (int)drift, 5, COL_BOW_PINK);
            fb_draw_heart(32, base_y - 16 - (int)(drift * 0.7f), 4, COL_CHEEK_PINK);
            break;
        }

        case FACE_SLEEPY: {
            // Floating 'zzz'
            float drift = fmodf((float)now_ms * 0.015f, 24.0f);
            fb_draw_zzz(90, base_y - 18 - (int)drift, 1, COL_FUR_SHADOW);
            fb_draw_zzz(96, base_y - 28 - (int)(drift * 0.8f), 2, COL_FUR_SHADOW);
            break;
        }

        case FACE_SQUINT:
            // Sweet pastel candy floating near paw
            fb_draw_candy(88, base_y + 24, COL_CHEEK_PINK, COL_BOW_HIGHLIGHT);
            break;

        case FACE_RECOVERED:
            // Fresh green spring leaf floating near ear
            fb_draw_leaf(88, base_y - 24, 4, 2, RGB565_BE(120, 220, 140));
            break;

        default:
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

    uint16_t pill_col = COL_BOW_PINK;
    const char *label = "READY";

    if (s_demo_active) {
        switch (state) {
            case FACE_EXCITED:      pill_col = RGB565_BE(255, 120, 180); label = "1. EXCITED"; break;
            case FACE_SURPRISED:    pill_col = RGB565_BE(255, 100, 170); label = "2. SURPRISED"; break;
            case FACE_SQUINT:       pill_col = RGB565_BE(70,  180, 245); label = "3. SQUINT"; break;
            case FACE_WINK_LEFT:    pill_col = RGB565_BE(60,  210, 170); label = "4. WINK L"; break;
            case FACE_WINK_RIGHT:   pill_col = RGB565_BE(255, 140, 90);  label = "5. WINK R"; break;
            case FACE_CONFUSED:     pill_col = RGB565_BE(160, 120, 245); label = "6. CONFUSED"; break;
            case FACE_SLEEPY:       pill_col = RGB565_BE(70,  80,  160); label = "7. SLEEPY"; break;
            case FACE_SAD:          pill_col = RGB565_BE(230, 50,  70);  label = "8. ERROR SAD"; break;
            case FACE_HAPPY:        pill_col = RGB565_BE(255, 90,  160); label = "9. HAPPY"; break;
            case FACE_HAPPY_BIG:    pill_col = RGB565_BE(255, 80,  150); label = "10. HAPPY BIG"; break;
            case FACE_RECOVERED:    pill_col = RGB565_BE(40,  190, 180); label = "11. RECOVERED"; break;
            case FACE_NEUTRAL:      pill_col = RGB565_BE(80,  160, 240); label = "12. NEUTRAL"; break;
            case FACE_LISTENING:    pill_col = RGB565_BE(40,  210, 220); label = "13. LISTENING"; break;
            case FACE_THINKING:     pill_col = RGB565_BE(245, 170, 40);  label = "14. THINKING"; break;
            case FACE_SPEAKING:     pill_col = RGB565_BE(255, 120, 160); label = "15. SPEAKING"; break;
            case FACE_BLINK:        pill_col = RGB565_BE(100, 170, 240); label = "16. BLINK"; break;
            case FACE_CURIOUS_L:    pill_col = RGB565_BE(170, 130, 245); label = "17. CURIOUS L"; break;
            case FACE_CURIOUS_R:    pill_col = RGB565_BE(170, 130, 245); label = "18. CURIOUS R"; break;
            case FACE_ANGRY:        pill_col = RGB565_BE(235, 60,  60);  label = "19. ANGRY"; break;
            case FACE_LOVE:         pill_col = RGB565_BE(255, 60,  130); label = "20. LOVE"; break;
            case FACE_PLAYFUL:      pill_col = RGB565_BE(255, 130, 180); label = "21. PLAYFUL"; break;
            case FACE_ERROR:        pill_col = RGB565_BE(235, 50,  50);  label = "22. ERROR"; break;
            case FACE_DISCONNECTED: pill_col = RGB565_BE(160, 130, 180); label = "23. OFFLINE"; break;
            default:                pill_col = COL_BOW_PINK;             label = "RBOT CAT"; break;
        }
    } else if (s_custom_status[0] != '\0') {
        label = s_custom_status;
        pill_col = COL_EYE_OUTER;
    } else {
        switch (state) {
            case FACE_BOOT:         label = "RBOT CAT"; pill_col = RGB565_BE(80, 180, 240); break;
            case FACE_IDLE:         label = s_is_sleepy ? "SLEEPY..." : "READY"; pill_col = s_is_sleepy ? RGB565_BE(70, 80, 160) : COL_BOW_PINK; break;
            case FACE_LISTENING:    label = "LISTENING"; pill_col = RGB565_BE(40, 210, 220); break;
            case FACE_THINKING:     label = "THINKING"; pill_col = RGB565_BE(245, 170, 40); break;
            case FACE_SPEAKING:     label = "SPEAKING"; pill_col = RGB565_BE(255, 120, 160); break;
            case FACE_HAPPY:        label = "HAPPY :)"; pill_col = RGB565_BE(255, 90, 160); break;
            case FACE_HAPPY_BIG:    label = "JOY :D"; pill_col = RGB565_BE(255, 80, 150); break;
            case FACE_ERROR:        label = (s_error_msg[0] != '\0') ? s_error_msg : "ERROR"; pill_col = RGB565_BE(235, 50, 50); break;
            case FACE_DISCONNECTED: label = "OFFLINE"; pill_col = RGB565_BE(160, 130, 180); break;
            case FACE_EXCITED:      label = "EXCITED"; pill_col = RGB565_BE(255, 120, 180); break;
            case FACE_SURPRISED:    label = "SURPRISED"; pill_col = RGB565_BE(255, 100, 170); break;
            case FACE_SQUINT:       label = "SQUINT"; pill_col = RGB565_BE(70, 180, 245); break;
            case FACE_WINK_LEFT:    label = "WINK L"; pill_col = RGB565_BE(60, 210, 170); break;
            case FACE_WINK_RIGHT:   label = "WINK R"; pill_col = RGB565_BE(255, 140, 90); break;
            case FACE_CONFUSED:     label = "CONFUSED"; pill_col = RGB565_BE(160, 120, 245); break;
            case FACE_SLEEPY:       label = "SLEEPY"; pill_col = RGB565_BE(70, 80, 160); break;
            case FACE_SAD:          label = "SAD"; pill_col = RGB565_BE(230, 50, 70); break;
            case FACE_RECOVERED:    label = "RECOVERED"; pill_col = RGB565_BE(40, 190, 180); break;
            case FACE_NEUTRAL:      label = "READY"; pill_col = COL_BOW_PINK; break;
            case FACE_CURIOUS_L:    label = "CURIOUS L"; pill_col = RGB565_BE(170, 130, 245); break;
            case FACE_CURIOUS_R:    label = "CURIOUS R"; pill_col = RGB565_BE(170, 130, 245); break;
            case FACE_ANGRY:        label = "ANGRY"; pill_col = RGB565_BE(235, 60, 60); break;
            case FACE_LOVE:         label = "LOVE <3"; pill_col = RGB565_BE(255, 60, 130); break;
            case FACE_PLAYFUL:      label = "PLAYFUL"; pill_col = RGB565_BE(255, 130, 180); break;
            default:                label = "READY"; pill_col = COL_BOW_PINK; break;
        }
    }

    // Draw Rounded Pill Background
    fb_fill_rounded_rect(bx, by, bw, bh, 7, pill_col);
    // Draw Subtle White Outline
    fb_draw_circle(bx + 7, by + 7, 7, COL_FUR_HIGHLIGHT);
    fb_draw_circle(bx + bw - 8, by + 7, 7, COL_FUR_HIGHLIGHT);
    fb_draw_line(bx + 7, by, bx + bw - 8, by, COL_FUR_HIGHLIGHT);
    fb_draw_line(bx + 7, by + bh - 1, bx + bw - 8, by + bh - 1, COL_FUR_HIGHLIGHT);

    // Draw Centered Label
    int text_y = by + (bh - 5) / 2;
    fb_draw_text_centered(text_y, label, COL_FUR_HIGHLIGHT, 1);
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
            case 1:  s_current_state = FACE_EXCITED;      s_demo_step_until_ms = now_ms + 2200; break;
            case 2:  s_current_state = FACE_SURPRISED;    s_demo_step_until_ms = now_ms + 2200; break;
            case 3:  s_current_state = FACE_SQUINT;       s_demo_step_until_ms = now_ms + 2200; break;
            case 4:  s_current_state = FACE_WINK_LEFT;    s_demo_step_until_ms = now_ms + 2000; break;
            case 5:  s_current_state = FACE_WINK_RIGHT;   s_demo_step_until_ms = now_ms + 2000; break;
            case 6:  s_current_state = FACE_CONFUSED;     s_demo_step_until_ms = now_ms + 2400; break;
            case 7:  s_current_state = FACE_SLEEPY;       s_demo_step_until_ms = now_ms + 2400; break;
            case 8:  s_current_state = FACE_SAD;          s_demo_step_until_ms = now_ms + 2400; break;
            case 9:  s_current_state = FACE_HAPPY;        s_demo_step_until_ms = now_ms + 2200; break;
            case 10: s_current_state = FACE_HAPPY_BIG;    s_demo_step_until_ms = now_ms + 2200; break;
            case 11: s_current_state = FACE_RECOVERED;    s_demo_step_until_ms = now_ms + 2200; break;
            case 12: s_current_state = FACE_NEUTRAL;      s_demo_step_until_ms = now_ms + 2000; break;
            case 13: s_current_state = FACE_LISTENING;    s_demo_step_until_ms = now_ms + 2200; break;
            case 14: s_current_state = FACE_THINKING;     s_demo_step_until_ms = now_ms + 2400; break;
            case 15: s_current_state = FACE_SPEAKING;     s_demo_step_until_ms = now_ms + 2200; break;
            case 16: s_current_state = FACE_BLINK;        s_demo_step_until_ms = now_ms + 1600; break;
            case 17: s_current_state = FACE_CURIOUS_L;    s_demo_step_until_ms = now_ms + 2200; break;
            case 18: s_current_state = FACE_CURIOUS_R;    s_demo_step_until_ms = now_ms + 2200; break;
            case 19: s_current_state = FACE_ANGRY;        s_demo_step_until_ms = now_ms + 2200; break;
            case 20: s_current_state = FACE_LOVE;         s_demo_step_until_ms = now_ms + 2400; break;
            case 21: s_current_state = FACE_PLAYFUL;      s_demo_step_until_ms = now_ms + 2200; break;
            case 22: s_current_state = FACE_ERROR;        s_demo_step_until_ms = now_ms + 2400; break;
            case 23: s_current_state = FACE_DISCONNECTED; s_demo_step_until_ms = now_ms + 2200; break;
            case 24:
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
        } else if (s_current_state == FACE_BOOT) {
            int64_t boot_elapsed = now_ms - s_boot_start_ms;
            if (boot_elapsed > 2600) {
                s_current_state = FACE_IDLE;
            }
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
        } else if (!s_is_sleepy && s_current_state != FACE_SLEEPY && s_current_state != FACE_HAPPY_BIG) {
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
        if (s_current_state == FACE_EXCITED || s_current_state == FACE_HAPPY || s_current_state == FACE_HAPPY_BIG) {
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

        // Layer 3: Connected Outer Ears & Volumetric Pink Bow
        render_cat_ears_and_bow(base_y, effective_state);

        // Layer 4: Torso, Pink Collar & Golden Bell
        render_cat_body(base_y, effective_state);

        // Layer 5: Chibi Head with Pseudo-3D Shading & Cheeks
        render_cat_head(base_y, effective_state);

        // Layer 6: Expressive Anime Eyes
        render_cat_eyes(base_y, effective_state, blink_factor);

        // Layer 7: Emotional Eyebrows
        render_cat_eyebrows(base_y, effective_state);

        // Layer 8: Cat Nose & Animated Mouth
        render_cat_mouth(base_y, effective_state, now_ms);

        // Layer 9: Cute Cat Paws & Gestures
        render_cat_paws(base_y, effective_state);

        // Layer 10: State-Specific Emotional Embellishments (Sweat drop, ?, !, Zzz, Hearts)
        render_cat_decorations(base_y, effective_state, now_ms);

        // Layer 11: Bottom Rounded Pill Status Banner
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
    s_boot_start_ms = s_state_start_ms;
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
    board_face_trigger_expression(FACE_HAPPY_BIG, duration_ms);
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
    board_face_trigger_expression(left_larger ? FACE_CURIOUS_L : FACE_CURIOUS_R, duration_ms);
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
