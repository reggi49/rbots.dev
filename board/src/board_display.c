#include "board_display.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_timer.h"
#include "board_types.h"

#define TAG "BD_DISP"

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  160

#define TFT_CS       7
#define TFT_DC      10
#define TFT_RST     21
#define TFT_SCK      8
#define TFT_MOSI     9
#define PIN_TOUCH    0

#define CHAT_RECT_X 4
#define CHAT_RECT_Y 130
#define CHAT_RECT_W (SCREEN_WIDTH - 8)
#define CHAT_RECT_H 26
#define AI_RECT_X 4
#define AI_RECT_Y 104
#define AI_RECT_W (SCREEN_WIDTH - 8)
#define AI_RECT_H 52
#define AI_TEXT_MAX 160
#define CHAT_DISPLAY_MAX_CHARS 200

// Matches typedef struct board_display_s in header
struct board_display_s {
    spi_device_handle_t spi_handle;
};

static struct board_display_s g_display_instance;

static inline uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static esp_err_t st7735_transmit(board_display_t *disp, const uint8_t *data, size_t len, bool is_data)
{
    if (!disp || !disp->spi_handle) return ESP_FAIL;
    if (len == 0) return ESP_OK;

    gpio_set_level(TFT_DC, is_data ? 1 : 0);
    spi_transaction_t trans = {
        .length = len * 8,
        .tx_buffer = data
    };

    return spi_device_transmit(disp->spi_handle, &trans);
}

static esp_err_t st7735_send_command(board_display_t *disp, uint8_t command)
{
    return st7735_transmit(disp, &command, 1, false);
}

static esp_err_t st7735_send_data(board_display_t *disp, const uint8_t *data, size_t len)
{
    return st7735_transmit(disp, data, len, true);
}

static void tft_set_addr_window(board_display_t *disp, uint8_t x0, uint8_t y0, uint8_t x1, uint8_t y1)
{
    const uint8_t col_data[] = {0x00, x0, 0x00, x1};
    const uint8_t row_data[] = {0x00, y0, 0x00, y1};

    st7735_send_command(disp, 0x2A);
    st7735_send_data(disp, col_data, sizeof(col_data));
    st7735_send_command(disp, 0x2B);
    st7735_send_data(disp, row_data, sizeof(row_data));
    st7735_send_command(disp, 0x2C);
}

static void tft_fill_rect(board_display_t *disp, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;

    tft_set_addr_window(disp, x, y, x + w - 1, y + h - 1);

    uint8_t px[2] = {(uint8_t)(color >> 8), (uint8_t)(color & 0xFF)};
    size_t total = w * h;
    
    const size_t buf_size = 256;
    uint8_t line_buf[512]; 
    for(int i=0; i<buf_size; i++) {
        line_buf[i*2] = px[0];
        line_buf[i*2+1] = px[1];
    }

    while(total > 0) {
        size_t batch = (total > buf_size) ? buf_size : total;
        st7735_send_data(disp, line_buf, batch * 2);
        total -= batch;
    }
}

static void tft_fill_screen(board_display_t *disp, uint16_t color)
{
    tft_fill_rect(disp, 0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, color);
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
    if (ch >= 'A' && ch <= 'Z') return chat_font_alnum[ch - 'A'];
    if (ch >= '0' && ch <= '9') return chat_font_alnum[26 + (ch - '0')];
    if (ch == '.') return dot_pattern;
    return NULL;
}

static uint8_t font3x5(char ch, uint8_t row)
{
    const uint8_t *pattern = chat_font_pattern(ch);
    return pattern ? pattern[row] : 0;
}

static void tft_draw_text3x5(board_display_t *disp, uint16_t x, uint16_t y, const char *text, uint16_t fg)
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
                tft_fill_rect(disp, x + col, y + row, run, 1, fg);
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

static void tft_draw_text3x5_scaled(board_display_t *disp, uint16_t x, uint16_t y, const char *text, uint16_t fg, uint8_t scale)
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
                tft_fill_rect(disp, x + col * scale, y + row * scale, run * scale, scale, fg);
                col += run;
            }
        }
        x += 4 * scale;
    }
}

static void tft_draw_ai_overlay_text(board_display_t *disp, const char *text, uint16_t fg, uint8_t scale)
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
            if (line_idx > 0) tft_draw_text3x5_scaled(disp, x, y, line, fg, scale);
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
        tft_draw_text3x5_scaled(disp, x, y, line, fg, scale);
    }
}

static void tft_draw_chat_answer(board_display_t *disp, const char *text)
{
    const uint16_t bg = color565(20, 20, 20);
    const uint16_t fg = color565(200, 200, 200);
    tft_fill_rect(disp, CHAT_RECT_X, CHAT_RECT_Y, CHAT_RECT_W, CHAT_RECT_H, bg);
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
            if (line_idx > 0) tft_draw_text3x5(disp, x, y, line, fg);
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
        tft_draw_text3x5(disp, x, y, line, fg);
    }
}

static void draw_eye(board_display_t *disp, uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color, blink_state_t blink, int8_t off_x, int8_t off_y, int8_t off_size)
{
    const uint16_t bg_color = color565(0, 0, 0);
    tft_fill_rect(disp, x - 3, y - 3, w + 6, h + 6, bg_color);
    uint16_t rw = w + off_size;
    uint16_t rh = h + off_size;
    uint16_t rx = x + off_x;
    uint16_t ry = y + off_y;
    if (blink == BLINK_OPEN) {
        tft_fill_rect(disp, rx, ry, rw, rh, color);
    } else if (blink == BLINK_HALF) {
        tft_fill_rect(disp, rx, ry + rh / 3, rw, rh / 3, color);
    } else if (blink == BLINK_CLOSED) {
        tft_fill_rect(disp, rx, ry + rh / 2 - 2, rw, 4, color);
    }
}

static void draw_robot_face(board_display_t *disp, face_state_t face, blink_state_t blink, int8_t off_x, int8_t off_y, int8_t off_size)
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
    draw_eye(disp, ex1, ey1, ew1, eh1, eye_color, blink, off_x, off_y, off_size);
    draw_eye(disp, ex2, ey2, ew2, eh2, eye_color, blink, off_x, off_y, off_size);
    if (face == FACE_HAPPY) {
        const uint16_t mouth_color = color565(255, 255, 0);
        tft_fill_rect(disp, 44, 100, 40, 5, mouth_color);
        tft_fill_rect(disp, 39, 95, 5, 5, mouth_color);
        tft_fill_rect(disp, 84, 95, 5, 5, mouth_color);
    } else {
        tft_fill_rect(disp, 36, 92, 56, 20, bg_color);
    }
    if (face == FACE_CONFUSED) {
        tft_fill_rect(disp, 54, 114, 20, 4, eye_color);
    } else {
        tft_fill_rect(disp, 54, 114, 20, 4, bg_color);
    }
    if (face == FACE_ERROR && blink == BLINK_OPEN) {
        const uint16_t x_color = color565(255, 50, 50);
        for (int i = 0; i < 15; i++) {
            tft_fill_rect(disp, 26 + i + off_x, 58 + i + off_y, 3, 3, x_color);
            tft_fill_rect(disp, 26 + 15 - i + off_x, 58 + i + off_y, 3, 3, x_color);
            tft_fill_rect(disp, 82 + i + off_x, 58 + i + off_y, 3, 3, x_color);
            tft_fill_rect(disp, 82 + 15 - i + off_x, 58 + i + off_y, 3, 3, x_color);
        }
    }
}

// --- Public API ---

board_display_t *board_display_init(void)
{
    ESP_LOGI(TAG, "TFT init start");

    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << TFT_DC) | (1ULL << TFT_RST)
    };
    gpio_config(&io_conf);

    gpio_config_t touch_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_INPUT,
        .pin_bit_mask = (1ULL << PIN_TOUCH),
        .pull_up_en = GPIO_PULLUP_ENABLE,
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
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Init Fail");
        return NULL;
    }

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = 26000000,
        .mode = 0,
        .spics_io_num = TFT_CS,
        .queue_size = 1
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &g_display_instance.spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Add Device Fail");
        return NULL;
    }

    gpio_set_level(TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(120));

    st7735_send_command(&g_display_instance, 0x01);
    vTaskDelay(pdMS_TO_TICKS(120));
    st7735_send_command(&g_display_instance, 0x11);
    vTaskDelay(pdMS_TO_TICKS(120));

    const uint8_t madctl = 0x00;
    const uint8_t colmod = 0x05;
    st7735_send_command(&g_display_instance, 0x36);
    st7735_send_data(&g_display_instance, &madctl, 1);
    st7735_send_command(&g_display_instance, 0x3A);
    st7735_send_data(&g_display_instance, &colmod, 1);

    const uint8_t col_range[] = {0x00, 0x00, 0x00, 0x7F};
    const uint8_t row_range[] = {0x00, 0x00, 0x00, 0x9F};
    st7735_send_command(&g_display_instance, 0x2A);
    st7735_send_data(&g_display_instance, col_range, sizeof(col_range));
    st7735_send_command(&g_display_instance, 0x2B);
    st7735_send_data(&g_display_instance, row_range, sizeof(row_range));

    st7735_send_command(&g_display_instance, 0x29);
    vTaskDelay(pdMS_TO_TICKS(20));

    tft_fill_screen(&g_display_instance, color565(0,0,0));

    ESP_LOGI(TAG, "TFT init done");
    return &g_display_instance;
}

void board_display_clear(board_display_t *disp)
{
    if (!disp) return;
    tft_fill_screen(disp, color565(0, 0, 0));
}

void board_display_draw_face(board_display_t *disp, face_state_t face, blink_state_t blink, int8_t off_x, int8_t off_y, int8_t off_size)
{
    if (!disp) return;
    draw_robot_face(disp, face, blink, off_x, off_y, off_size);
}

static void tft_draw_net_badge(board_display_t *disp, net_state_t state, bool phase)
{
    const uint16_t bg = color565(0, 0, 0);
    const uint16_t green = color565(0, 255, 0);
    const uint16_t red = color565(255, 50, 50);
    const uint16_t yellow = color565(255, 255, 0);
    const uint16_t text_col = color565(0, 0, 0);

    const uint16_t x0 = 4 + 22 + 2;
    const uint16_t y0 = 5;
    const uint16_t w = 30;
    const uint16_t h = 14;

    tft_fill_rect(disp, x0, y0, w, h, bg);
    
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
    tft_fill_rect(disp, x0, y0, w, h, fill);
    tft_draw_text3x5(disp, x0 + 4, y0 + 5, label, text_col);
}

static void tft_draw_battery_icon(board_display_t *disp, battery_state_t state, bool phase)
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

    tft_fill_rect(disp, x0, y0, w, h, bg);

    const uint16_t outline = fill;
    const uint16_t cap_w = 3;
    const uint16_t cap_h = 6;

    tft_fill_rect(disp, x0, y0 + 2, w - cap_w, 2, outline);
    tft_fill_rect(disp, x0, y0 + h - 4, w - cap_w, 2, outline);
    tft_fill_rect(disp, x0, y0 + 2, 2, h - 4, outline);
    tft_fill_rect(disp, x0 + (w - cap_w) - 2, y0 + 2, 2, h - 4, outline);

    tft_fill_rect(disp, x0 + (w - cap_w), y0 + (h - cap_h) / 2, cap_w, cap_h, outline);

    const uint16_t pad = 3;
    const uint16_t inner_w = (w - cap_w) - pad * 2;
    const uint16_t inner_h = (h - 4) - 2;
    const uint16_t ix = x0 + pad;
    const uint16_t iy = y0 + 3;
    tft_fill_rect(disp, ix, iy, inner_w, inner_h, bg);

    uint16_t fill_w = 0;
    if (level == 1) fill_w = inner_w / 3;
    else if (level == 2) fill_w = (inner_w * 2) / 3;
    else if (level >= 3) fill_w = inner_w;

    if (fill_w > 0) {
        tft_fill_rect(disp, ix, iy, fill_w, inner_h, fill);
    }

    if (state == BAT_CHARGING) {
        const uint16_t bx = x0 + 8;
        const uint16_t by = y0 + 7;

        tft_fill_rect(disp, bx + 0, by - 1, 8, 2, bg);
        tft_fill_rect(disp, bx + 4, by - 4, 8, 2, bg);
        tft_fill_rect(disp, bx + 2, by + 2, 8, 2, bg);

        tft_fill_rect(disp, bx + 6, by - 2, 2, 6, bg);
    }
}

static void tft_draw_ai_overlay_legacy(board_display_t *disp, ai_state_t state, const char *text, bool phase, int64_t wake_until_ms)
{
    // Top-bar badge
    const uint16_t badge_x = 60;
    const uint16_t badge_y = 5;
    const uint16_t badge_w = 36;
    const uint16_t badge_h = 14;
    const uint16_t badge_bg = color565(0, 0, 0);
    const uint16_t badge_fg = color565(0, 0, 0);

    const uint16_t badge_bg_idle = color565(60, 60, 60);
    const uint16_t badge_bg_list = color565(50, 90, 170);
    const uint16_t badge_bg_think = color565(30, 60, 120);
    const uint16_t badge_bg_ans = color565(0, 110, 110);
    const uint16_t badge_bg_err = color565(160, 40, 40);

    uint16_t current_badge_bg = badge_bg_idle;
    const char *label = "IDLE";
    uint16_t text_off_x = 10; // Default for 4 chars (IDLE)

    switch (state) {
        case AI_LISTENING: label = "LIST"; current_badge_bg = badge_bg_list; text_off_x = 10; break;
        case AI_THINKING: label = "THNK"; current_badge_bg = badge_bg_think; text_off_x = 10; break;
        case AI_ANSWERING: label = "SPK"; current_badge_bg = badge_bg_ans; text_off_x = 12; break; // 3 chars
        case AI_ERROR: label = "ERR"; current_badge_bg = badge_bg_err; text_off_x = 12; break; // 3 chars
        default: label = "IDLE"; current_badge_bg = badge_bg_idle; text_off_x = 10; break;
    }

    tft_fill_rect(disp, badge_x, badge_y, badge_w, badge_h, current_badge_bg);
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (1 && wake_until_ms > now_ms) { // Force wake check
        tft_fill_rect(disp, badge_x, badge_y, badge_w, badge_h, color565(0, 200, 200));
        tft_draw_text3x5(disp, badge_x + 10, badge_y + 4, "WAKE", color565(0, 0, 0)); // WAKE is 4 chars -> 10 offset
    } else {
        tft_draw_text3x5(disp, badge_x + text_off_x, badge_y + 4, label, badge_fg);
    }

    const uint16_t bg = color565(0, 0, 0);
    const uint16_t listening = color565(50, 90, 170);
    const uint16_t thinking = color565(30, 60, 120);
    const uint16_t answering = color565(0, 110, 110);
    const uint16_t error = color565(160, 40, 40);
    const uint16_t fg = color565(240, 240, 240);
    const uint16_t accent = color565(255, 255, 0);

    tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, bg);

    if (wake_until_ms > now_ms) {
        tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, 40, 12, color565(0, 200, 200));
        tft_draw_text3x5(disp, AI_RECT_X + 4, AI_RECT_Y + 4, "WAKE", color565(0, 0, 0));
        return;
    }

    switch (state) {
        case AI_IDLE:
            tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, 24, 12, color565(60, 60, 60));
            tft_draw_text3x5(disp, AI_RECT_X + 4, AI_RECT_Y + 4, "IDLE", fg);
            return;
        case AI_LISTENING: {
            tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, listening);
            uint16_t cy = AI_RECT_Y + AI_RECT_H / 2;
            uint16_t cx = AI_RECT_X + AI_RECT_W / 2;
            tft_fill_rect(disp, cx - 22, cy - 8, 14, 14, fg);
            tft_fill_rect(disp, cx + 8, cy - 8, 14, 14, fg);
            break;
        }
        case AI_THINKING: {
            tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, thinking);
            uint16_t cy = AI_RECT_Y + AI_RECT_H / 2;
            uint16_t start_x = AI_RECT_X + AI_RECT_W / 2 - 18;
            uint16_t dot = phase ? fg : accent;
            for (int i = 0; i < 3; i++) {
                tft_fill_rect(disp, start_x + i * 16, cy - 5, 10, 10, dot);
                dot = fg;
            }
            break;
        }
        case AI_ANSWERING: {
            tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, answering);
            tft_draw_ai_overlay_text(disp, text, fg, 2);
            break;
        }
        case AI_ERROR: {
            tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, error);
            const char *msg = (text && text[0]) ? text : "COBA LAGI YA~";
            tft_draw_ai_overlay_text(disp, msg, fg, 2);
            break;
        }
        default:
            break;
    }
}

void board_display_draw_overlay(board_display_t *disp, ai_state_t ai_state, const char *ai_text, 
                                wifi_status_t wifi_status, net_state_t net_state, battery_state_t bat_state, 
                                bool anim_phase, float mic_level, bool wake_accepted,
                                int64_t wake_block_until_ms, const char *wake_block_msg, int64_t ai_wake_until_ms)
{
    if (!disp) return;
    
    // 1. Wifi Icon
    {
        const uint16_t bg = color565(0, 0, 0);
        const uint16_t fg = color565(255, 255, 255);
        const uint16_t dim = color565(60, 60, 60);
        const uint16_t err = color565(255, 50, 50);
        const uint16_t x0 = 4;
        const uint16_t y0 = 4;
        const uint16_t w = 22;
        const uint16_t h = 16;
        tft_fill_rect(disp, x0, y0, w, h, bg);
        const uint16_t base_y = y0 + h - 2;
        const uint16_t bar_w = 3;
        const uint16_t gap = 2;
        const uint16_t bar_h[4] = {3, 6, 9, 12};
        for (int i = 0; i < 4; i++) {
            uint16_t bx = x0 + 2 + i * (bar_w + gap);
            uint16_t by = base_y - bar_h[i];
            tft_fill_rect(disp, bx, by, bar_w, bar_h[i], dim);
        }
        if (wifi_status == WIFI_CONNECTED || wifi_status == WIFI_CONNECTED_STABLE) {
             for (int i = 0; i < 4; i++) {
                uint16_t bx = x0 + 2 + i * (bar_w + gap);
                uint16_t by = base_y - bar_h[i];
                tft_fill_rect(disp, bx, by, bar_w, bar_h[i], fg);
            }
        } else if (wifi_status == WIFI_CONNECTING && anim_phase) {
             for (int i = 0; i < 2; i++) {
                uint16_t bx = x0 + 2 + i * (bar_w + gap);
                uint16_t by = base_y - bar_h[i];
                tft_fill_rect(disp, bx, by, bar_w, bar_h[i], fg);
            }
        } else if (wifi_status == WIFI_ERROR) {
             for (int i = 0; i < (int)w; i++) {
                tft_fill_rect(disp, x0 + i, y0 + (i * h) / w, 1, 1, err);
                tft_fill_rect(disp, x0 + i, y0 + h - 1 - (i * h) / w, 1, 1, err);
            }
        }
    }

    // 2. Net Badge (Legacy)
    tft_draw_net_badge(disp, net_state, anim_phase);

    // 3. AI Overlay & Status Badge (Legacy)
    // Draw AI last so the overlay can overwrite if needed, but the top badge might overlap Net
    // Based on user request, Net is 'left' of Status.
    tft_draw_ai_overlay_legacy(disp, ai_state, ai_text, anim_phase, ai_wake_until_ms);

    // 4. Battery Icon (Legacy)
    tft_draw_battery_icon(disp, bat_state, anim_phase);
}

void board_display_draw_chat(board_display_t *disp, const char *chat_text)
{
    if (!disp) return;
    tft_draw_chat_answer(disp, chat_text);
}

int board_display_get_touch_level(board_display_t *disp)
{
    return gpio_get_level(PIN_TOUCH);
}
