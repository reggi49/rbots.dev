# Implementation Audit & Baseline Report

**Project:** R-bot Companion Application & Firmware Integration  
**Date:** October 2026  
**Auditor:** Senior Embedded Systems, Flutter, and Firmware Engineer  

---

## 1. Executive Summary

This report establishes the baseline architecture, existing codebase status, hardware pinout, partition structure, and feasibility analysis prior to creating the Flutter companion application and integrating it with the ESP-IDF firmware.

The existing codebase is functional and builds successfully with ESP-IDF v5.5.2. It features an expressive 20-expression Kawaii Cat face engine on a 128×160 ST7735 display, a WakeNet10 local wake-word engine ("Alexa"), half-duplex voice conversation with a FastAPI backend, and preliminary documentation for DRV8833 motor control.

---

## 2. Environment & Tooling Verification

| Component | Target Version | Actual System Version | Status |
|:---|:---|:---|:---|
| **Host OS** | macOS (Darwin ARM64) | Darwin 24.6.0 (ARM64) | Verified |
| **ESP-IDF** | v5.5.2 | v5.5.2-dirty (`/Users/reggi/.espressif/v5.5.2/esp-idf`) | Verified |
| **Python** | Python 3.11 - 3.13 | Python 3.13.3 (`idf5.5_py3.13_env`) | Verified |
| **Flutter SDK** | 3.x+ | Flutter 3.47.1 (stable channel) | Verified |
| **Dart SDK** | 3.x+ | Dart 3.13.1 | Verified |
| **Firmware Build** | Ninja / CMake | Built clean (`rbot.bin` size 0x1B9720 / ~1.72 MB) | Verified |
| **Git Working Tree** | Clean or inspected | Uncommitted modifications in 5 files (README, backend, board_face.c, etc.) - inspected & preserved | Verified |

---

## 3. Subsystem Breakdown & Current Implementation Status

### 3.1 Audio & Voice Pipeline (Preserved)
- **Input:** INMP441 MEMS digital microphone on I2S0 (RX).
  - WS: GPIO 1, BCK: GPIO 4, SD: GPIO 2.
  - Software DC-blocking IIR high-pass filter ($f_c \approx 80\text{ Hz}$) running on sampled PCM.
- **Output:** MAX98357A I2S Class-D amplifier on I2S1 (TX).
  - WS: GPIO 12, BCK: GPIO 11, DIN: GPIO 6.
- **Wake Word:** Local WakeNet10 (`wn10_alexa`) loaded from dedicated flash partition at `0x210000`.
- **Status:** Fully functional. Must not be altered or starved by BLE.

### 3.2 Visual Display & Face Engine (Preserved)
- **Panel:** ST7735 128×160 RGB565 SPI display.
  - CS: GPIO 7, SCK: GPIO 8, MOSI: GPIO 9, DC: GPIO 10, RST: GPIO 16, BL: GPIO 15.
- **Engine:** Kawaii Cat face renderer running at 20 FPS on Core 1 in a dedicated task (`face_task`).
  - 40 KB DMA framebuffer.
  - 20 emotional expressions: `EXCITED`, `SURPRISED`, `SQUINT`, `WINK_LEFT`, `WINK_RIGHT`, `CONFUSED`, `SLEEPY`, `SAD`, `HAPPY`, `HAPPY_BIG`, `RECOVERED`, `NEUTRAL`, `LISTENING`, `THINKING`, `SPEAKING`, `BLINK`, `CURIOUS_L`, `CURIOUS_R`, `ANGRY`, `LOVE`, `PLAYFUL`.
- **Status:** Fully functional. Currently driven by runtime events and interactive CLI.

### 3.3 Bluetooth Low Energy (BLE)
- **Current State:** **Implemented and Build-Verified (Read-Only Slice)**.
- **Firmware Stack:** ESP-IDF NimBLE (`CONFIG_BT_NIMBLE_ENABLED=y`, `CONFIG_SW_COEXIST_ENABLE=y`) on Core 0 with custom GATT Service `7b4a0001-9d3f-4c2e-8a61-2f0c5b7e1000`.
- **Mobile Stack:** `flutter_reactive_ble: 5.6.0` (BSD-3-Clause) with clean `BleTransport` abstraction and strict production/mock separation.
- **Allowed Commands:** `PING`, `GET_INFO`, `GET_STATUS`, `GET_CAPABILITIES`. All mutation commands return `NOT_SUPPORTED`.
- **Status:** Automated test and firmware build verified. Pending physical hardware flashing.

### 3.4 Motor Control & Physical Safety
- **Target Hardware:** DRV8833 dual H-bridge driving two N20 gearmotors.
- **Assigned Pins:**
  - IN1: GPIO 13 (Motor A / Left)
  - IN2: GPIO 14 (Motor A / Left) - *Note: previously shared with touch sensor input*
  - IN3: GPIO 17 (Motor B / Right)
  - IN4: GPIO 18 (Motor B / Right)
- **Current State:** Documented in README and pin tables, but **no active driver or PWM code exists** in `board/src/`. GPIO 14 is still configured as digital input for tactile touch.
- **Resolution Plan:**
  - Introduce `board_motor.h/c` utilizing ESP32 LEDC PWM.
  - Provide a config guard to transition GPIO 14 cleanly from touch input to motor IN2 when motor control is active.

### 3.5 Flash Partitions & OTA Readiness
- **Current Partition Table (`partitions_singleapp.csv`):**
  - `nvs`: 0x9000 (20 KB)
  - `phy_init`: 0xF000 (4 KB)
  - `factory`: 0x10000 (2 MB)
  - `model`: 0x210000 (4 MB)
- **Flash Hardware:** 16 MB Octal Flash (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`). Total mapped space is only ~6.2 MB out of 16 MB.
- **OTA Feasibility:** Safe dual-app OTA (`ota_0`, `ota_1`, `otadata`) is **fully possible** in 16 MB flash without shrinking the 4 MB model partition. See `docs/ota-partition-audit.md` for complete partition layout analysis.

---

## 4. Integration Roadmap & Safeguards

1. **Phase 1:** Build standalone Flutter Companion App (`rbot_companion/`) using Flutter 3.47.1 + Riverpod + Pastel Kawaii design system.
2. **Phase 2:** Define GATT UUIDs and unified JSON protocol v1; implement NimBLE server in firmware; build BLE manager with mock fallback in Flutter.
3. **Phase 3:** Implement DRV8833 LEDC PWM driver in firmware with dead-man timeout (500 ms lease); implement press-and-hold controls in Flutter.
4. **Phase 4:** Map watch face theme requests to firmware face engine; persist preferences in NVS.
5. **Phase 5:** Expose real telemetry and diagnostics.
6. **Phase 6:** Present safe dual-app OTA partition migration and HTTPS update workflow.
