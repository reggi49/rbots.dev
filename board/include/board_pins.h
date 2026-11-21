/**
 * @file board_pins.h
 * @brief Single source of truth for all GPIO assignments (Perfboard Architecture).
 *
 * Target hardware: ESP32-S3 N16R8 (ESP32-S3R8 + 16 MB flash + 8 MB PSRAM)
 *
 * Pin selection rules
 * ───────────────────
 *  • Avoid strapping pins: GPIO0, GPIO3, GPIO45, GPIO46
 *  • Avoid PSRAM (octal-SPI) pins: GPIO35, GPIO36, GPIO37
 *  • Avoid native USB pins: GPIO19, GPIO20
 *  • Mic and speaker use SEPARATE I2S controllers (no shared clocks)
 *
 * ┌───────────────────────────────────────────────────────────┐
 * │  PERIPHERAL            GPIO   DIRECTION / NOTES           │
 * ├───────────────────────────────────────────────────────────┤
 * │  ST7735 TFT (SPI)                                         │
 * │    CS                  1      SPI chip-select             │
 * │    RST                 2      Hardware reset              │
 * │    DC / A0             42     Data/Command select         │
 * │    MOSI / SDA          4      SPI data (MOSI)             │
 * │    SCK                 5      SPI clock (SCK)             │
 * │    BL / LED            -1     Tied to 3.3V (Row 1 Col 10) │
 * │                                                           │
 * │  INMP441 Mic (I2S0 RX)                                    │
 * │    SD  (data→ESP)      41     I2S0 RX Data-In             │
 * │    WS                  15     I2S0 RX Word-Select (LRCLK) │
 * │    BCK                 16     I2S0 RX Bit-Clock (SCK)     │
 * │    L/R                 GND    Left channel mono           │
 * │                                                           │
 * │  MAX98357A Spk (I2S1 TX)                                  │
 * │    WS  (LRC)           8      I2S1 TX Word-Select         │
 * │    BCK                 9      I2S1 TX Bit-Clock           │
 * │    DOUT (DIN)          10     I2S1 TX Data-Out            │
 * │                                                           │
 * │  DRV8833 Motor (LEDC PWM)                                 │
 * │    L_IN1               11     Left Wheel Forward (PWM)    │
 * │    L_IN2               12     Left Wheel Reverse (PWM)    │
 * │    R_IN1               14     Right Wheel Forward (PWM)   │
 * │    R_IN2               13     Right Wheel Reverse (PWM)   │
 * └───────────────────────────────────────────────────────────┘
 */

#pragma once

/* ── ST7735 TFT – SPI ─────────────────────────────────── */
#define PIN_TFT_CS               1
#define PIN_TFT_RST              2
#define PIN_TFT_DC              42
#define PIN_TFT_MOSI             4   /* Soldered to GPIO 4 */
#define PIN_TFT_SCK              5   /* Soldered to GPIO 5 */
#define PIN_TFT_BL              -1   /* Backlight tied to 3.3V */

/* ── INMP441 Microphone – I2S RX (Port 0) ──────────────── */
#define PIN_MIC_I2S_SD          41   /* Serial-Data IN (DOUT) */
#define PIN_MIC_I2S_WS          15   /* Word-Select / LRCLK   */
#define PIN_MIC_I2S_BCK         16   /* Bit-Clock / SCK       */
#define MIC_I2S_PORT_NUM         0   /* ESP32-S3 I2S port 0   */

/* ── MAX98357A Speaker – I2S TX (Port 1) ───────────────── */
#define PIN_SPK_I2S_WS           8   /* Word-Select / LRCLK   */
#define PIN_SPK_I2S_BCK          9   /* Bit-Clock / BCLK      */
#define PIN_SPK_I2S_DOUT        10   /* Serial-Data OUT (DIN) */
#define SPK_I2S_PORT_NUM         1   /* ESP32-S3 I2S port 1   */

/* ── DRV8833 Dual Motor Driver – LEDC PWM ──────────────── */
#define PIN_MOTOR_L_IN1         11   /* Left Motor Fwd (PWM)  */
#define PIN_MOTOR_L_IN2         12   /* Left Motor Rev (PWM)  */
#define PIN_MOTOR_R_IN1         14   /* Right Motor Fwd (PWM) */
#define PIN_MOTOR_R_IN2         13   /* Right Motor Rev (PWM) */

/* ── Touch (Unused / Spare) ────────────────────────────── */
#define PIN_TOUCH               47   /* Unused pin, safe spare */
