# Hardware and Pinout Audit

**Project:** R-bot Companion Application & Embedded Integration  
**Microcontroller:** ESP32-S3 DevKit N16R8 (16 MB SPI Flash, 8 MB Octal PSRAM)  
**Date:** October 2026  

---

## 1. Verified GPIO Mapping

| Subsystem | Signal Name | ESP32-S3 GPIO | Direction / Mode | Verification Source | Status / Conflict Notes |
|:---|:---|---:|:---|:---|:---|
| **INMP441 Mic** | Word Select (WS) | **GPIO 1** | Output (I2S0 WS) | `board_pins.h`, `board_audio.c` | Active, verified |
| **INMP441 Mic** | Serial Data (SD) | **GPIO 2** | Input (I2S0 Data In) | `board_pins.h`, `board_audio.c` | Active, verified |
| **INMP441 Mic** | Bit Clock (BCLK) | **GPIO 4** | Output (I2S0 SCK) | `board_pins.h`, `board_audio.c` | Active, verified |
| **MAX98357A Amp** | Serial Data (DIN) | **GPIO 6** | Output (I2S1 Data Out) | `board_pins.h`, `audio_player.c` | Active, verified |
| **MAX98357A Amp** | Bit Clock (BCLK) | **GPIO 11** | Output (I2S1 SCK) | `board_pins.h`, `audio_player.c` | Active, verified |
| **MAX98357A Amp** | Word Select (WS) | **GPIO 12** | Output (I2S1 WS) | `board_pins.h`, `audio_player.c` | Active, verified |
| **ST7735 TFT** | Chip Select (CS) | **GPIO 7** | Output (SPI CS) | `board_pins.h`, `board_display.c` | Active, verified |
| **ST7735 TFT** | Master Clock (SCK) | **GPIO 8** | Output (SPI CLK) | `board_pins.h`, `board_display.c` | Active, verified |
| **ST7735 TFT** | Data Out (MOSI) | **GPIO 9** | Output (SPI MOSI) | `board_pins.h`, `board_display.c` | Active, verified |
| **ST7735 TFT** | Data/Command (DC) | **GPIO 10** | Output (GPIO) | `board_pins.h`, `board_display.c` | Active, verified |
| **ST7735 TFT** | Backlight (BL) | **GPIO 15** | Output (GPIO high) | `board_pins.h`, `board_display.c` | Active, verified |
| **ST7735 TFT** | Reset (RST) | **GPIO 16** | Output (GPIO) | `board_pins.h`, `board_display.c` | Active, verified |
| **DRV8833 Motor** | Motor A IN1 | **GPIO 13** | Output (LEDC PWM) | README Section 4 | Free (no existing conflict) |
| **DRV8833 Motor** | Motor A IN2 | **GPIO 14** | Output (LEDC PWM) | README Section 4 / `board_pins.h` | **Conflict with PIN_TOUCH** |
| **DRV8833 Motor** | Motor B IN3 | **GPIO 17** | Output (LEDC PWM) | README Section 4 | Free (no existing conflict) |
| **DRV8833 Motor** | Motor B IN4 | **GPIO 18** | Output (LEDC PWM) | README Section 4 | Free (no existing conflict) |

---

## 2. Pin Conflict Analysis & Resolution: GPIO 14

### Observation
- In `board/include/board_pins.h`: `PIN_TOUCH` is defined as `14`.
- In `board/main/board.c`: GPIO 14 is configured as an active-low digital input with internal pull-up to support tactile button pressing (short press to trigger talk, long press to cancel).
- In the project documentation and README: GPIO 14 is documented as **DRV8833 IN2 (Motor A)** because physical touch is replaced with voice-activated WakeNet10 hands-free detection.

### Resolution
1. To ensure backward compatibility and prevent electrical shorts on bench setups where a switch might still be attached to GPIO 14:
   - Provide a build/runtime configuration flag: `CONFIG_RBOT_ENABLE_MOTORS` in `board/main/Kconfig.projbuild`.
   - When motors are enabled (`CONFIG_RBOT_ENABLE_MOTORS=y`), GPIO 14 is exclusively driven as LEDC PWM channel for DRV8833 IN2, and the legacy touch button polling loop is deactivated in favor of WakeNet10 voice activation and BLE commands.
   - When motors are disabled, GPIO 14 remains safe for bench testing with a tactile button.

---

## 3. DRV8833 Truth Table & Driving Architecture

The DRV8833 Dual H-Bridge motor driver supports standard Slow Decay (Braking) and Coast modes:

| IN1 / IN3 | IN2 / IN4 | Mode | Motor Output State |
|:---:|:---:|:---|:---|
| **0** | **0** | Coast / Sleep | High Impedance (Freewheel) |
| **PWM** | **0** | Forward Driving | Forward rotation with regenerative flyback |
| **0** | **PWM** | Reverse Driving | Reverse rotation with regenerative flyback |
| **1** | **1** | Brake | Low side shorted (Dynamic braking) |

### LEDC PWM Specifications
- **Frequency:** 20 kHz (above audible human hearing range to eliminate motor whine).
- **Resolution:** 10-bit (0 – 1023 duty cycle).
- **Safe Limit:** Default clamp to 60% max duty cycle (e.g., 614 / 1023) to prevent thermal overload on N20 gearboxes and high peak current draw from the 18650 cell.
- **Lease Timeout:** 500 ms hardware/software watchdog. Motors coast or brake immediately if no heartbeat/command is received within 500 ms.
