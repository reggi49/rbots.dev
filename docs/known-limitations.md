# Known Limitations & Hardware Assumptions

**Project:** R-bot Companion Application & ESP32-S3 Firmware  
**Date:** 23 October 2026  
**Status:** Canonical Reference of Current Boundaries  

---

## 1. Physical BLE Hardware Verification Pending
- **Status:** The read-only BLE slice (NimBLE GATT server + Flutter `ReactiveBleTransport`) compiles cleanly and passes all automated tests (48/48 passing across 10 test files).
- **Limitation:** Physical end-to-end communication has not yet been verified over the air because no ESP32-S3 board is currently connected via USB (`/dev/cu.usb*` absent).
- **Phase Status:** Phase 2 is strictly classified as **physical BLE validation pending** and will not be marked as complete until live communication on target hardware is physically demonstrated.

## 2. Motor Movement Intentionally Disabled & Capability-Locked
- **Status:** Motor commands (`MOTOR_MOVE`, `MOTOR_STOP`) return `NOT_SUPPORTED`.
- **UI Safety:** Flutter companion UI locks directional buttons, displays a persistent disabled warning banner, and blocks command emission at the controller level when `motor` capability is reported as inactive (`false`).
- **Firmware Guard:** Firmware motor PWM (LEDC) is intentionally kept uninitialized to preserve physical safety until BLE link stability, command parsing, and telemetry are physically confirmed.

## 3. Configuration & Watch Face Writes Disabled
- **Status:** Configuration writes (`SET_CONFIG`, `SET_NAME`) and watch-face writes (`SET_WATCH_FACE`) return `NOT_SUPPORTED`.
- **Reason:** Configuration persistence to NVS and face-engine command synchronization are deferred to subsequent phases after read-only communication is proven on physical hardware.

## 4. Truthful Telemetry & Battery Reality
- **Battery Reality:** The ESP32-S3 DevKit hardware currently lacks an ADC voltage divider wired to an analog GPIO for measuring 18650 cell voltage. Telemetry is reported truthfully: `has_real_battery: false`, `battery_percent: null`, `battery_voltage: null`.
- **Runtime State Reality:** Firmware and client report `active_face: "unavailable"`, `movement_state: "unavailable"`, and `server_connected: "unknown"` rather than fabricating simulated states.

## 5. ATT MTU & Single-Packet Delivery Envelope
- **ATT MTU Baseline:** Without MTU negotiation, default ATT MTU is 23 bytes (effective application payload 20 bytes). All current protocol v1 JSON requests (56–70 B) and responses (48–284 B) exceed 20 bytes.
- **Negotiated MTU:** Stack configures `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512`. Android client explicitly negotiates MTU 512 upon connection, yielding an unfragmented application payload of 509 bytes ($512 - 3$).
- **Framing Constraint:** Neither client nor firmware implements multi-packet streaming or reassembly; all transfers are single-packet atomic ATT PDUs.

## 6. Flash Partition Margin & Single-App Layout
- **Status:** The active partition table is `partitions_singleapp.csv` (factory app partition size: 0x200000 / 2 MB; WakeNet10 model partition size: 0x400000 / 4 MB at offset 0x210000).
- **Firmware Size:** Current compiled binary size is `0x1e9310` bytes (2,003,728 bytes = 1.91 MB).
- **Remaining Margin:** Only `0x16cf0` bytes (93,424 bytes / 4.45% free space) remain in the 2 MB app slot.
- **Impact:** Any substantial firmware expansion requires migrating to a dual-app layout (`ota_0`, `ota_1`, `otadata`) taking advantage of the physical 16 MB flash hardware.
- **OTA Limitation:** OTA updates through Wi-Fi or BLE remain disabled until partition migration is executed via physical USB flashing.
