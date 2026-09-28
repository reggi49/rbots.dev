/**
 * @file board_pins.h
 * @brief Single source of truth for all GPIO assignments.
 *
 * Target hardware: ESP32-S3 N16R8  (ESP32-S3R8 + 16 MB flash + 8 MB PSRAM)
 *
 * Pin selection rules
 * ───────────────────
 *  • Avoid strapping pins: GPIO0, GPIO3, GPIO45, GPIO46
 *  • Avoid PSRAM (octal-SPI) pins: GPIO35, GPIO36, GPIO37
 *  • Mic and speaker use SEPARATE I2S controllers (no shared clocks)
 *
 * ┌───────────────────────────────────────────────────────┐
 * │  PERIPHERAL         GPIO   DIRECTION / NOTES          │
 * ├───────────────────────────────────────────────────────┤
 * │  Touch sensor       14     Digital input (active-low) │
 * │                                                       │
 * │  INMP441 Mic (RX)                                     │
 * │    WS               1      I2S0 RX Word-Select        │
 * │    BCK              4      I2S0 RX Bit-Clock          │
 * │    SD  (data→ESP)   2      I2S0 RX Data-In            │
 * │                                                       │
 * │  MAX98357A Spk (TX)                                   │
 * │    WS               12     I2S1 TX Word-Select        │
 * │    BCK              11     I2S1 TX Bit-Clock          │
 * │    DIN (ESP→amp)    6      I2S1 TX Data-Out           │
 * │                                                       │
 * │  ST7735 TFT (SPI)                                     │
 * │    CS               7      SPI chip-select            │
 * │    SCK              8      SPI clock                  │
 * │    MOSI             9      SPI data                   │
 * │    DC               10     Data/Command select        │
 * │    RST              16     Hardware reset              │
 * │    BL               15     Backlight (PWM optional)   │
 * └───────────────────────────────────────────────────────┘
 */

#pragma once

/* ── Touch ─────────────────────────────────────────────── */
#define PIN_TOUCH               14

/* ── INMP441 Microphone – I2S RX (Port 0) ──────────────── */
#define PIN_MIC_I2S_WS           1   /* Word-Select / LRCLK  */
#define PIN_MIC_I2S_BCK          4   /* Bit-Clock / SCK      */
#define PIN_MIC_I2S_SD           2   /* Serial-Data IN       */
#define MIC_I2S_PORT_NUM         0   /* ESP32-S3 I2S port 0  */

/* ── MAX98357A Speaker – I2S TX (Port 1) ───────────────── */
#define PIN_SPK_I2S_WS          12   /* Word-Select / LRCLK  */
#define PIN_SPK_I2S_BCK         11   /* Bit-Clock / SCK      */
#define PIN_SPK_I2S_DOUT         6   /* Serial-Data OUT      */
#define SPK_I2S_PORT_NUM         1   /* ESP32-S3 I2S port 1  */

/* ── ST7735 TFT – SPI ─────────────────────────────────── */
#define PIN_TFT_CS               7
#define PIN_TFT_SCK              8
#define PIN_TFT_MOSI             9
#define PIN_TFT_DC              10
#define PIN_TFT_RST             16   /* Connected to GPIO16 now */
#define PIN_TFT_BL              15   /* Backlight (optional) */
