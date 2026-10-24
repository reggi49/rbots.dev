# BLE MTU and Framing Specification

This document provides the authoritative technical audit and specification of BLE ATT MTU negotiation, payload dimensions, and framing behaviors across the ESP32-S3 NimBLE firmware and the Flutter mobile companion app (`flutter_reactive_ble` on Android).

---

## 1. Core BLE ATT MTU Fundamentals

In Bluetooth Low Energy (BLE), all attribute exchanges (writes, reads, notifications, indications) operate on the Attribute Protocol (ATT).
- **Default ATT MTU:** 23 bytes (Bluetooth Core Specification 4.0+ baseline).
- **ATT Header Overhead:** 3 bytes:
  - 1 byte Opcode (e.g., `0x12` for Write Request, `0x1B` for Handle Value Notification)
  - 2 bytes Attribute Handle
- **Default Application Payload:** $23 - 3 = \mathbf{20\text{ bytes}}$.

---

## 2. Platform and Stack Configurations

### 2.1 ESP32-S3 NimBLE Configuration
- Firmware stack: Apache NimBLE integrated with ESP-IDF v5.5.2.
- `CONFIG_BT_NIMBLE_ATT_PREFERRED_MTU=512` (configured in `board/sdkconfig` and `board/sdkconfig.defaults`).
- When an ATT MTU Exchange Request is received from a central device (Android), the NimBLE stack negotiates up to 512 bytes.
- Firmware characteristic buffers:
  - `s_cmd_buf`: 384 bytes.
  - `s_status_buf`: 384 bytes.

### 2.2 Android & `flutter_reactive_ble` 5.6.0
- On Android, the BLE stack connects initially at the default ATT MTU (23 bytes).
- Android's native `BluetoothGatt.requestMtu(int mtu)` initiates the ATT MTU Exchange.
- `flutter_reactive_ble` exposes `requestMtu({required String deviceId, required int mtu})`.
- The negotiated ATT MTU is calculated as:
  $$\text{ATT\_MTU}_{\text{effective}} = \min(\text{MTU}_{\text{client\_requested}}, \text{MTU}_{\text{server\_preferred}})$$
  With client requesting 512 and server offering 512, the negotiated ATT MTU is **512 bytes**.
- **Effective Maximum Application Payload:**
  $$\mathbf{512 - 3 = 509\text{ bytes}}$$

---

## 3. Payload Audit & Byte Measurements

All protocol v1 requests and responses use UTF-8 encoded JSON. Below are empirical UTF-8 byte measurements for all protocol messages (using 13-digit Unix millisecond timestamps for request/response IDs, e.g. `1729665000000`):

| Message Type | Direction | Sample Payload | Length (Bytes) | Fits Default MTU (20 B)? | Fits Negotiated MTU (509 B)? |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **PING (Req)** | App $\rightarrow$ Robot | `{"v":1,"id":1729665000000,"type":"ping","action":"ping"}` | 56 B | **NO** (exceeds by 36 B) | **YES** (453 B margin) |
| **PING (Resp)** | Robot $\rightarrow$ App | `{"v":1,"id":1729665000000,"ok":true,"pong":true}` | 48 B | **NO** (exceeds by 28 B) | **YES** (461 B margin) |
| **GET_INFO (Req)** | App $\rightarrow$ Robot | `{"v":1,"id":1729665000000,"type":"system","action":"get_info"}` | 62 B | **NO** (exceeds by 42 B) | **YES** (447 B margin) |
| **GET_INFO (Resp)** | Robot $\rightarrow$ App | `{"v":1,"id":1729665000000,"ok":true,"robot_name":"Rbot-DEV","firmware_version":"3e0559c-dirty","hardware_id":"ESP32-S3-N16R8","build_date":"Oct 23 2026 14:18:00"}` | 161 B | **NO** (exceeds by 141 B) | **YES** (348 B margin) |
| **GET_STATUS (Req)** | App $\rightarrow$ Robot | `{"v":1,"id":1729665000000,"type":"system","action":"get_status"}` | 64 B | **NO** (exceeds by 44 B) | **YES** (445 B margin) |
| **GET_STATUS (Resp)** | Robot $\rightarrow$ App | `{"v":1,"id":1729665000000,"ok":true,"robot_name":"Rbot-DEV","firmware_version":"3e0559c-dirty","hardware_id":"ESP32-S3-N16R8","active_face":"unavailable","movement_state":"unavailable","has_real_battery":false,"wifi_connected":false,"server_connected":"unknown","ota_supported":false}` | 284 B | **NO** (exceeds by 264 B) | **YES** (225 B margin) |
| **GET_CAPABILITIES (Req)** | App $\rightarrow$ Robot | `{"v":1,"id":1729665000000,"type":"system","action":"get_capabilities"}` | 70 B | **NO** (exceeds by 50 B) | **YES** (439 B margin) |
| **GET_CAPABILITIES (Resp)** | Robot $\rightarrow$ App | `{"v":1,"id":1729665000000,"ok":true,"capabilities":{"motor":false,"face":false,"voice":false,"ota":false,"battery_adc":false,"config_write":false,"watch_face_write":false,"wifi_provisioning":false,"wifi_ota":false,"rollback":false}}` | 216 B | **NO** (exceeds by 196 B) | **YES** (293 B margin) |
| **NOT_SUPPORTED (Resp)** | Robot $\rightarrow$ App | `{"v":1,"id":1729665000000,"ok":false,"error_code":"NOT_SUPPORTED","error_message":"Command not supported in read-only slice"}` | 125 B | **NO** (exceeds by 105 B) | **YES** (384 B margin) |
| **MALFORMED (Resp)** | Robot $\rightarrow$ App | `{"v":1,"id":0,"ok":false,"error_code":"INVALID_ARGUMENT","error_message":"Invalid JSON format"}` | 95 B | **NO** (exceeds by 75 B) | **YES** (414 B margin) |

---

## 4. Framing, Fragmentation, and Atomicity

1. **Absence of Transport Fragmentation:**
   - Neither client nor firmware implements multi-packet SLIP, COBS, length-prefixed streaming, or multi-frame reassembly.
   - Every BLE write (`writeCharacteristicWithResponse`) and notification (`ble_gatts_notify_custom`) is dispatched as a single, atomic ATT PDU.
2. **Payload Envelope Analysis:**
   - With initial ATT MTU (23 bytes, effective payload 20 bytes), **ALL** requests and responses exceed the capacity.
   - With negotiated ATT MTU 512 (effective payload 509 bytes), **ALL** current protocol v1 requests ($\le 70$ bytes) and responses ($\le 284$ bytes) fit comfortably within a single PDU, leaving an unfragmented safety margin of at least 225 bytes.
3. **Mandatory Post-Connection MTU Exchange:**
   - Because all commands and responses exceed the default 20-byte payload limit, communication will fail if MTU exchange is omitted.
   - Therefore, the client connection pipeline MUST perform MTU negotiation (`requestMtu(512)`) immediately after connection establishment and prior to dispatching any command writes.

