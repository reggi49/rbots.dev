# OTA & Partition Table Audit

**Project:** R-bot Companion Application & Firmware Integration  
**Hardware Flash Size:** 16 MB SPI Flash (`CONFIG_ESPTOOLPY_FLASHSIZE_16MB=y`)  
**Date:** October 2026  

---

## 1. Current Flash Partition Table Analysis

The active partition configuration is stored in `board/partitions_singleapp.csv`:

```csv
# Name,   Type, SubType, Offset,  Size, Flags
nvs,      data, nvs,     0x9000,  0x5000,
phy_init, data, phy,     0xF000,  0x1000,
factory,  app,  factory, 0x10000, 2M,
model,    data, ,        ,        4M,
```

### Partition Map & Offsets
- **Bootloader:** `0x0000` to `0x7FFF` (32 KB)
- **Partition Table:** `0x8000` to `0x8FFF` (4 KB)
- **NVS (`nvs`):** `0x9000` to `0xDFFF` (20 KB)
- **PHY Init (`phy_init`):** `0xF000` to `0xFFFF` (4 KB)
- **Factory App (`factory`):** `0x10000` to `0x20FFFF` (2 MB = 2,097,152 bytes)
- **Model Partition (`model`):** `0x210000` to `0x60FFFF` (4 MB = 4,194,304 bytes)
- **Total In-Use Flash:** `0x610000` (~6.06 MB / 6,356,992 bytes)
- **Unused Available Flash:** `0x610000` to `0xFFFFFF` (**~9.94 MB free space**)

---

## 2. OTA Readiness Assessment

### Current Limitations
1. **Single Application Slot:** The current table only defines `factory` (subtype `factory`). It has no secondary application partition (`ota_1`) and no `otadata` partition.
2. **Standard ESP-IDF OTA Incompatibility:** Standard `esp_ota_begin()` requires an active `otadata` partition and at least two app slots (`ota_0` and `ota_1`). Attempting to run standard ESP-IDF OTA on this table will fail immediately with `ESP_ERR_NOT_FOUND` (no OTA partition found).

### Crucial Hardware Opportunity
Because the ESP32-S3 module on R-bot is an **N16R8 (16 MB Flash)**, there is almost **10 MB of unallocated flash space** after the 4 MB model partition! We do **not** need to shrink the WakeNet10 model partition to achieve dual-app OTA.

---

## 3. Recommended Safe OTA Migration Partition Table

When ready for OTA implementation (Phase 6), the partition table can be updated to:

```csv
# Name,    Type, SubType, Offset,   Size,     Flags
nvs,       data, nvs,     0x9000,   0x5000,
otadata,   data, ota,     0xE000,   0x2000,
phy_init,  data, phy,     0x10000,  0x1000,
ota_0,     app,  ota_0,   0x20000,  0x280000,   # 2.5 MB (Current app is 1.72 MB)
ota_1,     app,  ota_1,   0x2A0000, 0x280000,   # 2.5 MB (Secondary OTA slot)
model,     data, ,        0x520000, 0x400000,   # 4.0 MB (WakeNet10 model intact)
storage,   data, spiffs,  0x920000, 0x600000,   # 6.0 MB (Optional assets / logs)
```

### Safety & Backward Compatibility Rules:
1. **Never alter partition tables during normal runtime without explicit user permission.**
2. In Phase 0-5, continue building with `partitions_singleapp.csv` to ensure all existing bench tests and flash scripts work without disruption.
3. The Flutter app's OTA module will query the robot's partition capability via `GET_INFO` / `GET_CAPABILITIES`. If the device reports `ota_supported: false` (running singleapp), the mobile app will clearly explain that the robot requires a one-time flash update to enable dual-slot OTA.
