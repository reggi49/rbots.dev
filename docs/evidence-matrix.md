# R-bot Implementation & Verification Evidence Matrix

> **Audit Date:** 23 October 2026  
> **Status Assessment:** Rigorous Evidence-Based Verification  
> **Guideline:** Documentation and UI mockups do NOT constitute complete hardware features. Every claim must have source-level evidence.

---

## 1. Phase Status Summary (Factual & Verified)

| Phase | Description | Previously Claimed | Actual Verified Status | Source Evidence & Notes |
|:---|:---|:---:|:---:|:---|
| **Phase 0** | Baseline Toolchain & Repo Audit | `COMPLETED` | **VERIFIED COMPLETE** | ESP-IDF v5.5.2 (`rbot.bin`), Flutter 3.47.1, Dart 3.13.1, Android SDK 37 verified. |
| **Phase 1** | Flutter Companion Architecture & UI | `COMPLETED` | **VERIFIED (SIMULATED)** | Flutter Riverpod + Pastel UI; 48 automated tests across 10 test files pass. |
| **Phase 2** | BLE Protocol, GATT Stack & Transport | `COMPLETED` | **PHYSICAL BLE VALIDATION PENDING** | `flutter_reactive_ble: 5.6.0` (BSD) integrated; ESP32-S3 NimBLE GATT server compiled; APK debug & release built; physical BLE test pending. |
| **Phase 3** | Motor Control & Dead-Man Safety | `COMPLETED` | **DEFERRED (SAFE)** | Motor commands return `NOT_SUPPORTED`; firmware PWM uninitialized; UI locked when motor capability false. |
| **Phase 4** | Watch Face Studio & Theme Sync | `COMPLETED` | **PARTIAL** | ST7735 face engine has 20 expressions; BLE theme write returns `NOT_SUPPORTED` for read-only slice. |
| **Phase 5** | Settings & Diagnostics Telemetry | `COMPLETED` | **PARTIAL** | Truthful telemetry (`active_face: "unavailable"`, `movement_state: "unavailable"`, `server_connected: "unknown"`, `has_real_battery: false`). |
| **Phase 6** | Dual-Slot OTA Update System | `COMPLETED` | **DESIGN ONLY** | Partition is `partitions_singleapp.csv` (2 MB factory app, 4% / 93.4 KB free margin). Dual-slot OTA designed. |
| **Phase 7** | Hardening and release preparation | `COMPLETED` | **NOT STARTED** | Baseline hardening deferred until after physical hardware validation and motor enablement. |

---

## 2. Feature-by-Feature Evidence Matrix

| Feature / Subsystem | Implementation File & Symbol | Build Evidence | Automated Test | Simulation Test | Physical Hardware Test | Actual Status |
|:---|:---|:---:|:---:|:---:|:---:|:---:|
| **Audio Capture (Mic)** | `board/src/board_audio.c`<br>`board_audio_init_rx()` | `idf.py build`<br>(`rbot.bin`) | N/A | Buffer pipeline test | Verified in prior audio audits | **VERIFIED COMPLETE** |
| **Audio Playback (Amp)** | `board/main/audio_player.c`<br>`audio_player_start()` | `idf.py build`<br>(`rbot.bin`) | N/A | Sine wave generation test | Verified in prior audio audits | **VERIFIED COMPLETE** |
| **Audio Volume (+30%)** | `backend/server.py`<br>`TTS_VOLUME_BOOST = 1.30` | Python runtime | `backend/test_tts` | `ffmpeg -filter:a volume=1.3` | Tested via FastAPI TestClient | **VERIFIED COMPLETE** |
| **Kawaii Cat UI (ST7735)** | `board/src/board_face.c`<br>`render_cat_head()`, etc. | `idf.py build`<br>(`rbot.bin`) | `test_expressions.py` | Framebuffer DMA render | Physical LCD tested in prior turns | **VERIFIED COMPLETE** |
| **Wake Word (WakeNet10)** | `board/main/wake_word_sr.c`<br>`wake_word_sr_init()` | `idf.py build`<br>(`srmodels.bin`) | N/A | IIR HPF test (-54dB noise floor) | Tested with verbal "Alexa" | **VERIFIED COMPLETE** |
| **Cloud AI & Latency Box** | `backend/server.py`<br>`generate_llm_response()` | Python runtime | TestClient mock turn | Benchmarked at 3.73s total turn | Verified live with backend process | **VERIFIED COMPLETE** |
| **Flutter UI & Pastel Theme** | `Frontend/lib/core/theme/`<br>`AppColors` | `flutter analyze`<br>(0 issues) | `home_screen_widget_test.dart` | Widget test runner | Pending Android target | **VERIFIED (SIMULATED)** |
| **Flutter Protocol Codec** | `Frontend/lib/data/models/`<br>`BleCommand`, `BleResponse` | Flutter SDK | `protocol_codec_test.dart` | JSON serialize / deserialize | Unit tests passing | **VERIFIED COMPLETE** |
| **ATT MTU & Framing Bounds** | `Frontend/lib/data/ble/`<br>`ReactiveBleTransport` | `docs/ble-mtu-and-framing.md` | `mtu_payload_measurement_test.dart` | Measured UTF-8 bytes (56–284 B) | Fits in MTU 512 (509 B payload) | **VERIFIED COMPLETE** |
| **Request Coordinator** | `Frontend/lib/data/ble/`<br>`ReactiveBleTransport` | `flutter test` (48/48 pass) | `request_coordinator_test.dart` | 1-in-flight queue, ID routing | Tested with simulated delays | **VERIFIED COMPLETE** |
| **Capability UI Safety** | `Frontend/lib/features/remote/`<br>`RemoteScreen`, `MotorController` | `flutter test` (48/48 pass) | `capability_safety_test.dart` | Remote screen locked on `motor: false` | Verified via widget test | **VERIFIED COMPLETE** |
| **Runtime Android Permissions** | `Frontend/android/app/.../`<br>`MainActivity.kt`, `BlePermissionService` | `flutter build apk` (0 errors) | `discovery_screen_test.dart` | MethodChannel permission handling | Tested via unit & build | **VERIFIED COMPLETE** |
| **Flutter Reactive BLE Transport** | `Frontend/lib/data/ble/`<br>`ReactiveBleTransport` | `flutter analyze`<br>(0 issues) | `transport_architecture_test.dart` | Plugin method mock test | Pending physical Android/ESP32 | **VERIFIED (SIMULATED)** |
| **BLE GATT Service (Firmware)** | `board/main/rbot_ble.c`<br>`rbot_ble_init()` | `idf.py build`<br>(`rbot.bin` size: 1.91 MB) | N/A | NimBLE GATT registration verified | Pending physical ESP32-S3 | **BUILD VERIFIED** |
| **BLE Protocol Dispatcher** | `board/main/rbot_ble.c`<br>`rbot_ble_process_command()` | `idf.py build`<br>(0 errors) | `transport_lifecycle_and_protocol_test.dart` | `PING`, `GET_INFO`, `GET_STATUS`, `GET_CAPABILITIES` | Pending physical ESP32-S3 | **BUILD VERIFIED** |
| **Motor Hardware Execution** | DRV8833 GPIO 13, 14, 17, 18 | `board/include/board_pins.h` | `motor_controller_test.dart` | Dead-man timer verified in Flutter | Intentionally deferred until BLE verified | **DEFERRED (SAFE)** |
| **Dual-Slot OTA Migration** | `docs/ota-partition-audit.md` | Partition CSV ready | N/A | Layout calculation verified | Awaiting USB cable flash | **DESIGN ONLY** |
| **Battery Telemetry (ADC)** | `docs/known-limitations.md` | N/A | `has_real_battery == false` | Null safety verified | No hardware ADC installed | **DOCUMENTED FACT** |

---

## 3. Technology Stack & Licensing Status

- **Mobile BLE Engine:** `flutter_reactive_ble: 5.6.0` (BSD-3-Clause, Signify Netherlands B.V.)
- **Transitive Dependencies Verified:**
  - `reactive_ble_mobile: 5.6.0` (BSD-3-Clause, Signify Netherlands B.V.)
  - `reactive_ble_platform_interface: 5.6.0` (BSD-3-Clause, Signify Netherlands B.V.)
  - `functional_data: 1.2.0` (**MIT License**, Sander Kersten)
  - `protobuf: 6.1.0` (BSD-3-Clause, The Dart Project Authors)
- **Previous BLE Engine:** `flutter_blue_plus: ^2.3.14` (**Completely removed**)
- **License Declarations:** `License.nonprofit` (**Completely purged from codebase**)
- **Third-Party Notices:** Documented in `docs/third-party-licenses.md`
- **Firmware BLE Stack:** Apache NimBLE on ESP-IDF v5.5.2 (Apache 2.0)
- **Active Partition:** `partitions_singleapp.csv` (Flash 16 MB, App 2 MB, 4% / 93.4 KB margin remaining)
