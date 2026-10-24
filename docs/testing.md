# Testing & Validation Guide

**Project:** R-bot Companion Application  
**Date:** October 2026  

---

## 1. Automated Unit & Widget Tests

Run the full Flutter automated test suite:

```bash
cd rbot_companion
flutter test
```

### Verified Test Cases:
1. `protocol_codec_test.dart`:
   - Serialization of motor move command with direction, speed, and 500 ms TTL.
   - Serialization of emergency STOP command with speed 0 and action stop.
   - Deserialization of success and error responses.
   - Safe parsing of device status telemetry (no fake battery percentage when absent).
2. `motor_controller_test.dart`:
   - Initial stopped state.
   - Safe clamping of speed bounds (20% – 80%).
   - Press-and-hold startMove and immediate stop.
3. `home_screen_widget_test.dart`:
   - Rendering of Kawaii companion home screen, avatar, and navigation cards.

---

## 2. Firmware Compilation Verification

Verify that the ESP-IDF firmware builds cleanly:

```bash
source /Users/reggi/.espressif/v5.5.2/esp-idf/export.sh
cd board
idf.py build
```

Binary output: `build/rbot.bin` (~1.72 MB).
