#include "board_display.h"
#include "board_pins.h"            /* Single source of truth for GPIOs */
#include "esp_log.h"
#include "sdkconfig.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "esp_timer.h"
#include "esp_attr.h"
#include "board_types.h"
#include "board_face.h"

#define TAG "BD_DISP"

#define SCREEN_WIDTH   128
#define SCREEN_HEIGHT  160

#ifndef ST7735_XSTART
#define ST7735_XSTART 0
#endif

#ifndef ST7735_YSTART
#define ST7735_YSTART 0
#endif

#ifndef ST7735_MADCTL
#define ST7735_MADCTL 0xA0
#endif

#ifndef ST7735_COLMOD
#define ST7735_COLMOD 0x05
#endif

#ifndef ST7735_USE_NORON
#define ST7735_USE_NORON 1
#endif

#ifndef ST7735_USE_INVON
#define ST7735_USE_INVON 0
#endif

/* All TFT / touch pin aliases come from board_pins.h:
 *   PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST, PIN_TFT_SCK, PIN_TFT_MOSI,
 *   PIN_TFT_BL, PIN_TOUCH                                              */

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
static SemaphoreHandle_t s_disp_spi_mutex = NULL;

static inline uint16_t color565(uint8_t r, uint8_t g, uint8_t b)
{
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

static esp_err_t st7735_transmit(board_display_t *disp, const uint8_t *data, size_t len, bool is_data)
{
    if (!disp || !disp->spi_handle) return ESP_FAIL;
    if (len == 0) return ESP_OK;

    if (s_disp_spi_mutex) {
        xSemaphoreTake(s_disp_spi_mutex, portMAX_DELAY);
    }

    gpio_set_level(PIN_TFT_DC, is_data ? 1 : 0);
    spi_transaction_t trans = {0};
    trans.length = len * 8;
    if (len <= 4) {
        trans.flags = SPI_TRANS_USE_TXDATA;
        memcpy(trans.tx_data, data, len);
    } else {
        trans.tx_buffer = data;
    }

    esp_err_t ret = spi_device_transmit(disp->spi_handle, &trans);

    if (s_disp_spi_mutex) {
        xSemaphoreGive(s_disp_spi_mutex);
    }

    return ret;
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
    uint16_t xs0 = (uint16_t)x0 + ST7735_XSTART;
    uint16_t xs1 = (uint16_t)x1 + ST7735_XSTART;
    uint16_t ys0 = (uint16_t)y0 + ST7735_YSTART;
    uint16_t ys1 = (uint16_t)y1 + ST7735_YSTART;

    const uint8_t col_data[] = {
        (uint8_t)(xs0 >> 8), (uint8_t)(xs0 & 0xFF),
        (uint8_t)(xs1 >> 8), (uint8_t)(xs1 & 0xFF)
    };
    const uint8_t row_data[] = {
        (uint8_t)(ys0 >> 8), (uint8_t)(ys0 & 0xFF),
        (uint8_t)(ys1 >> 8), (uint8_t)(ys1 & 0xFF)
    };

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
    static DMA_ATTR uint8_t line_buf[512]; 
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
    if (scale == 0) scale = 2;
    char prepared[AI_TEXT_MAX];
    chat_prepare_text(prepared, text);

    char line1[32] = {0};
    char line2[32] = {0};

    char *newline_pos = strchr(prepared, '\n');
    if (newline_pos) {
        size_t len1 = newline_pos - prepared;
        if (len1 >= sizeof(line1)) len1 = sizeof(line1) - 1;
        strncpy(line1, prepared, len1);
        line1[len1] = '\0';
        strncpy(line2, newline_pos + 1, sizeof(line2) - 1);
        line2[sizeof(line2) - 1] = '\0';
    } else {
        size_t len = strlen(prepared);
        if (len <= 13) {
            strncpy(line1, prepared, sizeof(line1) - 1);
        } else {
            int split_idx = -1;
            for (int i = 0; i < (int)len && i <= 12; i++) {
                if (prepared[i] == ' ') split_idx = i;
            }
            if (split_idx > 0) {
                strncpy(line1, prepared, split_idx);
                line1[split_idx] = '\0';
                const char *p2 = prepared + split_idx + 1;
                while (*p2 == ' ') p2++;
                strncpy(line2, p2, sizeof(line2) - 1);
            } else {
                strncpy(line1, prepared, 12);
                line1[12] = '\0';
                strncpy(line2, prepared + 12, sizeof(line2) - 1);
            }
        }
    }

    for (int l = 0; l < 2; l++) {
        char *target = (l == 0) ? line1 : line2;
        int sl = strlen(target);
        while (sl > 0 && target[sl - 1] == ' ') {
            target[sl - 1] = '\0';
            sl--;
        }
    }

    uint8_t num_lines = (line2[0] != '\0') ? 2 : 1;
    uint16_t char_w = 4 * scale;
    uint16_t line_h = 6 * scale;
    uint16_t total_h = num_lines * line_h;
    uint16_t start_y = AI_RECT_Y + (AI_RECT_H - total_h) / 2;

    if (line1[0] != '\0') {
        int l1_len = strlen(line1);
        int l1_w = l1_len * char_w - (1 * scale);
        int l1_x = AI_RECT_X + (AI_RECT_W - l1_w) / 2;
        if (l1_x < AI_RECT_X + 2) l1_x = AI_RECT_X + 2;
        tft_draw_text3x5_scaled(disp, l1_x, start_y, line1, fg, scale);
    }

    if (num_lines == 2 && line2[0] != '\0') {
        int l2_len = strlen(line2);
        int l2_w = l2_len * char_w - (1 * scale);
        int l2_x = AI_RECT_X + (AI_RECT_W - l2_w) / 2;
        if (l2_x < AI_RECT_X + 2) l2_x = AI_RECT_X + 2;
        tft_draw_text3x5_scaled(disp, l2_x, start_y + line_h, line2, fg, scale);
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
    if (g_display_instance.spi_handle != NULL) {
        return &g_display_instance;
    }

    if (!s_disp_spi_mutex) {
        s_disp_spi_mutex = xSemaphoreCreateMutex();
    }

    ESP_LOGI(TAG, "TFT init start");

#if PIN_TFT_BL >= 0
    gpio_config_t bl_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = (1ULL << PIN_TFT_BL)
    };
    gpio_config(&bl_conf);
    gpio_set_level(PIN_TFT_BL, 1);
#endif

    uint64_t io_mask = (1ULL << PIN_TFT_DC);
#if PIN_TFT_RST >= 0
    io_mask |= (1ULL << PIN_TFT_RST);
#endif
    gpio_config_t io_conf = {
        .intr_type = GPIO_INTR_DISABLE,
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = io_mask
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

    ESP_LOGI(TAG, "Configuring SPI with: MOSI=%d, SCK=%d, CS=%d, DC=%d, RST=%d", 
             PIN_TFT_MOSI, PIN_TFT_SCK, PIN_TFT_CS, PIN_TFT_DC, PIN_TFT_RST);

    spi_bus_config_t buscfg = {
        .miso_io_num = -1,
        .mosi_io_num = PIN_TFT_MOSI,
        .sclk_io_num = PIN_TFT_SCK,
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
        .clock_speed_hz =
    #if (defined(CONFIG_TFT_MINIMAL_TEST) && CONFIG_TFT_MINIMAL_TEST) || defined(TFT_MINIMAL_TEST)
                400000,
    #else
            26000000,
    #endif
        .mode = 0,               // Try SPI Mode 0 (CPOL=0, CPHA=0)
        .spics_io_num = PIN_TFT_CS,
        .queue_size = 1
    };

    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &g_display_instance.spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI Add Device Fail");
        return NULL;
    }

    // Give the hardware time to stabilize after power-up
    vTaskDelay(pdMS_TO_TICKS(500));

#if PIN_TFT_RST >= 0
    gpio_set_level(PIN_TFT_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(100));
    gpio_set_level(PIN_TFT_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
#else
    // If Hardwired RST, we rely on SW Reset
    st7735_send_command(&g_display_instance, 0x01); // SWRESET
    vTaskDelay(pdMS_TO_TICKS(150));
#endif

    // --- ST7735 Init Sequence ---
    ESP_LOGI(TAG, "Starting ST7735 Initialization with RST=%d", PIN_TFT_RST);
    
    // 1. SWRESET
    st7735_send_command(&g_display_instance, 0x01);
    vTaskDelay(pdMS_TO_TICKS(150));

    // 2. SLPOUT
    st7735_send_command(&g_display_instance, 0x11);
    vTaskDelay(pdMS_TO_TICKS(255));

    // 3. FRMCTR1
    const uint8_t frmctr1[] = {0x01, 0x2C, 0x2D};
    st7735_send_command(&g_display_instance, 0xB1);
    st7735_send_data(&g_display_instance, frmctr1, 3);

    // 4. FRMCTR2
    st7735_send_command(&g_display_instance, 0xB2);
    st7735_send_data(&g_display_instance, frmctr1, 3);

    // 5. FRMCTR3
    const uint8_t frmctr3[] = {0x01, 0x2C, 0x2D, 0x01, 0x2C, 0x2D};
    st7735_send_command(&g_display_instance, 0xB3);
    st7735_send_data(&g_display_instance, frmctr3, 6);

    // 6. INVCTR
    const uint8_t invctr[] = {0x07};
    st7735_send_command(&g_display_instance, 0xB4);
    st7735_send_data(&g_display_instance, invctr, 1);

    // 7. PWCTR1
    const uint8_t pwctr1[] = {0xA2, 0x02, 0x84};
    st7735_send_command(&g_display_instance, 0xC0);
    st7735_send_data(&g_display_instance, pwctr1, 3);

    // 8. PWCTR2
    const uint8_t pwctr2[] = {0xC5};
    st7735_send_command(&g_display_instance, 0xC1);
    st7735_send_data(&g_display_instance, pwctr2, 1);

    // 9. PWCTR3
    const uint8_t pwctr3[] = {0x0A, 0x00};
    st7735_send_command(&g_display_instance, 0xC2);
    st7735_send_data(&g_display_instance, pwctr3, 2);

    // 10. PWCTR4
    const uint8_t pwctr4[] = {0x8A, 0x2A};
    st7735_send_command(&g_display_instance, 0xC3);
    st7735_send_data(&g_display_instance, pwctr4, 2);

    // 11. PWCTR5
    const uint8_t pwctr5[] = {0x8A, 0xEE};
    st7735_send_command(&g_display_instance, 0xC4);
    st7735_send_data(&g_display_instance, pwctr5, 2);

    // 12. VMCTR1
    const uint8_t vmctr1[] = {0x0E};
    st7735_send_command(&g_display_instance, 0xC5);
    st7735_send_data(&g_display_instance, vmctr1, 1);

    // 13. INVOFF
    st7735_send_command(&g_display_instance, 0x20);

    // 14. MADCTL
    const uint8_t madctl_val = 0xC0;
    st7735_send_command(&g_display_instance, 0x36);
    st7735_send_data(&g_display_instance, &madctl_val, 1);

    // 15. COLMOD
    const uint8_t colmod_val = 0x05;
    st7735_send_command(&g_display_instance, 0x3A);
    st7735_send_data(&g_display_instance, &colmod_val, 1);

    // 16. GMCTRP1
    const uint8_t gamma_p[] = {0x02, 0x1C, 0x07, 0x12, 0x37, 0x32, 0x29, 0x2D, 0x29, 0x25, 0x2B, 0x39, 0x00, 0x01, 0x03, 0x10};
    st7735_send_command(&g_display_instance, 0xE0);
    st7735_send_data(&g_display_instance, gamma_p, 16);

    // 17. GMCTRN1
    const uint8_t gamma_n[] = {0x03, 0x1D, 0x07, 0x06, 0x2E, 0x2C, 0x29, 0x2D, 0x2E, 0x2E, 0x37, 0x3F, 0x00, 0x00, 0x02, 0x10};
    st7735_send_command(&g_display_instance, 0xE1);
    st7735_send_data(&g_display_instance, gamma_n, 16);

    // 18. NORON
    st7735_send_command(&g_display_instance, 0x13);
    vTaskDelay(pdMS_TO_TICKS(10));

    // 19. DISPON
    st7735_send_command(&g_display_instance, 0x29);
    vTaskDelay(pdMS_TO_TICKS(100));

    tft_fill_screen(&g_display_instance, color565(0,0,0)); // Start BLACK

    // --- End Init Sequence ---

    ESP_LOGI(TAG, "TFT init done");
    return &g_display_instance;
}

void board_display_clear(board_display_t *disp)
{
    if (!disp) return;
    tft_fill_screen(disp, color565(0, 0, 0));
}

void board_display_fill_color(board_display_t *disp, uint16_t rgb565)
{
    if (!disp) return;
    tft_fill_screen(disp, rgb565);
}

void board_display_draw_face(board_display_t *disp, face_state_t face, blink_state_t blink, int8_t off_x, int8_t off_y, int8_t off_size)
{
    (void)disp; (void)blink; (void)off_x; (void)off_y; (void)off_size;
    board_face_set_state(face);
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
    const uint16_t badge_x = 54;
    const uint16_t badge_y = 5;
    const uint16_t badge_w = 44;
    const uint16_t badge_h = 14;
    const uint16_t badge_fg = color565(255, 255, 255);

    const uint16_t badge_bg_idle = color565(50, 60, 70);
    const uint16_t badge_bg_list = color565(30, 80, 180);
    const uint16_t badge_bg_think = color565(40, 60, 140);
    const uint16_t badge_bg_ans = color565(0, 140, 120);
    const uint16_t badge_bg_err = color565(180, 40, 40);

    uint16_t current_badge_bg = badge_bg_idle;
    const char *label = "READY";
    uint16_t text_off_x = 12;

    switch (state) {
        case AI_LISTENING: label = "LISTEN"; current_badge_bg = badge_bg_list; text_off_x = 10; break;
        case AI_THINKING: label = "THINK"; current_badge_bg = badge_bg_think; text_off_x = 12; break;
        case AI_ANSWERING: label = "SPEAK"; current_badge_bg = badge_bg_ans; text_off_x = 12; break;
        case AI_ERROR: label = "ERR"; current_badge_bg = badge_bg_err; text_off_x = 16; break;
        default: label = "READY"; current_badge_bg = badge_bg_idle; text_off_x = 12; break;
    }

    tft_fill_rect(disp, badge_x, badge_y, badge_w, badge_h, current_badge_bg);
    int64_t now_ms = esp_timer_get_time() / 1000;
    if (wake_until_ms > now_ms) {
        tft_fill_rect(disp, badge_x, badge_y, badge_w, badge_h, color565(0, 200, 200));
        tft_draw_text3x5(disp, badge_x + 14, badge_y + 4, "WAKE", color565(0, 0, 0));
    } else {
        tft_draw_text3x5(disp, badge_x + text_off_x, badge_y + 4, label, badge_fg);
    }

    const uint16_t bg = color565(0, 0, 0);
    const uint16_t listening = color565(20, 60, 140);
    const uint16_t thinking = color565(30, 40, 100);
    const uint16_t answering = color565(0, 110, 95);
    const uint16_t error = color565(160, 40, 40);
    const uint16_t fg = color565(255, 255, 255);
    const uint16_t accent = color565(255, 255, 0);

    tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, bg);

    if (wake_until_ms > now_ms) {
        tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, color565(0, 140, 150));
        tft_draw_ai_overlay_text(disp, "WAKE ACCEPTED", color565(0, 0, 0), 2);
        return;
    }

    switch (state) {
        case AI_IDLE: {
            if (text && text[0]) {
                tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, color565(15, 22, 32));
                tft_draw_ai_overlay_text(disp, text, color565(180, 220, 255), 2);
            } else {
                tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, color565(15, 22, 32));
                tft_draw_ai_overlay_text(disp, "RBOT READY", color565(180, 220, 255), 2);
            }
            return;
        }
        case AI_LISTENING: {
            tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, listening);
            const char *msg = (text && text[0]) ? text : "RBOT LISTENING";
            tft_draw_ai_overlay_text(disp, msg, fg, 2);
            break;
        }
        case AI_THINKING: {
            tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, thinking);
            const char *msg = (text && text[0]) ? text : "RBOT THINKING";
            tft_draw_ai_overlay_text(disp, msg, accent, 2);
            break;
        }
        case AI_ANSWERING: {
            tft_fill_rect(disp, AI_RECT_X, AI_RECT_Y, AI_RECT_W, AI_RECT_H, answering);
            const char *msg = (text && text[0]) ? text : "RBOT SPEAKING";
            tft_draw_ai_overlay_text(disp, msg, fg, 2);
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
    (void)disp; (void)net_state; (void)anim_phase; (void)mic_level; (void)wake_accepted;
    (void)wake_block_until_ms; (void)wake_block_msg; (void)ai_wake_until_ms;

    switch (ai_state) {
        case AI_LISTENING: board_face_set_state(FACE_LISTENING); break;
        case AI_THINKING:  board_face_set_state(FACE_THINKING);  break;
        case AI_ANSWERING: board_face_set_state(FACE_SPEAKING);  break;
        case AI_ERROR:     board_face_set_error(FACE_ERR_SERVER, ai_text); break;
        case AI_IDLE:      board_face_set_state(FACE_IDLE);      break;
    }
    if (ai_text && ai_text[0]) {
        board_face_set_status_text(ai_text);
    }
    board_face_set_wifi_status(wifi_status);
    board_face_set_battery_state(bat_state);
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

esp_err_t board_display_send_bitmap(board_display_t *disp, uint16_t x, uint16_t y, uint16_t w, uint16_t h, const uint16_t *bitmap)
{
    if (!disp || !disp->spi_handle || !bitmap) return ESP_FAIL;
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return ESP_OK;
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w == 0 || h == 0) return ESP_OK;

    tft_set_addr_window(disp, x, y, x + w - 1, y + h - 1);

    const uint8_t *data = (const uint8_t *)bitmap;
    size_t remaining_bytes = (size_t)w * h * sizeof(uint16_t);
    const size_t max_chunk = 4096;

    while (remaining_bytes > 0) {
        size_t chunk = (remaining_bytes > max_chunk) ? max_chunk : remaining_bytes;
        esp_err_t err = st7735_send_data(disp, data, chunk);
        if (err != ESP_OK) return err;
        data += chunk;
        remaining_bytes -= chunk;
    }
    return ESP_OK;
}
