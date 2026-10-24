# Wi-Fi OTA Architecture & Security

**Project:** R-bot Companion  
**Date:** October 2026  

---

## 1. High-Level Flow
1. Mobile app queries robot firmware version & capabilities via BLE `GET_INFO`.
2. Mobile app checks remote HTTPS endpoint for signed update manifest.
3. Mobile app presents release notes, size, and version to user.
4. User grants explicit confirmation.
5. Mobile app sends `START_WIFI_OTA` with manifest URL and SHA-256 digest.
6. Robot stops motors, enters safe state, connects to Wi-Fi, and downloads binary over HTTPS directly to the inactive OTA partition.
7. Firmware verifies cryptographic SHA-256 and ESP-IDF image header before marking partition active.
8. Robot reboots into new image.
9. Self-test runs on first boot. If successful, firmware calls `esp_ota_mark_app_valid_cancel_rollback()`. If it crashes or loops, the bootloader automatically rolls back to the previous known-good partition.

---

## 2. Partition Prerequisites
- Requires dual application slots (`ota_0`, `ota_1`) and `otadata`.
- See `docs/ota-partition-audit.md` for the complete 16 MB partition migration plan.
- The single-application partition (`partitions_singleapp.csv`) is preserved as the default baseline to prevent unintended flash overwrites.
