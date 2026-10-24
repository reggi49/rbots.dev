# BLE Protocol Specification (v1)

**Project:** R-bot Companion  
**Version:** 1.0.0 (Read-Only Capability and Status Slice)  
**Transport:** Bluetooth Low Energy (BLE) GATT (NimBLE Host Stack)  
**Date:** October 2026  

---

## 1. Custom GATT Service Architecture

Every UUID in the R-bot system adheres to the 128-bit contract defined below. Abbreviated UUIDs are strictly prohibited across implementation and documentation.

| Service / Characteristic | Complete 128-bit UUID | GATT Properties | Access Policy & Behavior |
|:---|:---|:---|:---|
| **R-bot Primary Service** | `7b4a0001-9d3f-4c2e-8a61-2f0c5b7e1000` | Primary Service | Broadcast in BLE advertising payload for client discovery |
| **Command Characteristic** | `7b4a0002-9d3f-4c2e-8a61-2f0c5b7e1000` | Write-with-response | Accepts allowed read-only system commands; bounded to 384 bytes |
| **Status Characteristic** | `7b4a0003-9d3f-4c2e-8a61-2f0c5b7e1000` | Read, Notify | Telemetry and command execution results; `has_real_battery: false` |
| **Configuration Characteristic** | `7b4a0004-9d3f-4c2e-8a61-2f0c5b7e1000` | Read-only | Returns robot identity config; GATT writes rejected (`BLE_ATT_ERR_WRITE_NOT_PERMITTED`) |
| **Event Characteristic** | `7b4a0005-9d3f-4c2e-8a61-2f0c5b7e1000` | Notify | Asynchronous system events and notifications |

---

## 2. Packet Framing & Versioning

Every packet exchanged over the Command and Status characteristics uses standard UTF-8 JSON encoding with an explicit protocol version (`v: 1`) and request tracking ID (`id: <int>`).

- **Maximum Payload Size:** 384 bytes. Payloads exceeding this limit are rejected by the transport.
- **Response Path:** When a command is written with response to the Command characteristic, the resulting response packet is written to the internal status buffer and notified/read via the Status characteristic.

---

## 3. Allowed Read-Only Commands

The Command characteristic strictly accepts only the following four commands in this vertical slice:

### 3.1 PING (Liveness Check)
```json
{
  "v": 1,
  "id": 101,
  "type": "ping",
  "action": "ping"
}
```
**Response:**
```json
{
  "v": 1,
  "id": 101,
  "ok": true,
  "pong": true
}
```

### 3.2 GET_INFO (Hardware & Firmware Metadata)
```json
{
  "v": 1,
  "id": 102,
  "type": "system",
  "action": "get_info"
}
```
**Response:**
```json
{
  "v": 1,
  "id": 102,
  "ok": true,
  "robot_name": "Rbot-Mochi",
  "firmware_version": "v1.0.0",
  "hardware_id": "ESP32-S3-N16R8",
  "build_date": "2026-10-23"
}
```

### 3.3 GET_STATUS (Runtime Telemetry)
```json
{
  "v": 1,
  "id": 103,
  "type": "system",
  "action": "get_status"
}
```
**Response:**
```json
{
  "v": 1,
  "id": 103,
  "ok": true,
  "robot_name": "Rbot-Mochi",
  "firmware_version": "v1.0.0",
  "hardware_id": "ESP32-S3-N16R8",
  "active_face": "unavailable",
  "movement_state": "unavailable",
  "has_real_battery": false,
  "wifi_connected": false,
  "server_connected": "unknown",
  "ota_supported": false
}
```
*(Note: `battery_percent` and `battery_voltage` are omitted because no hardware ADC voltage divider is connected.)*

### 3.4 GET_CAPABILITIES (Truthful Feature Negotiation)
```json
{
  "v": 1,
  "id": 104,
  "type": "system",
  "action": "get_capabilities"
}
```
**Response:**
```json
{
  "v": 1,
  "id": 104,
  "ok": true,
  "capabilities": {
    "motor": false,
    "face": false,
    "voice": false,
    "ota": false,
    "battery_adc": false,
    "config_write": false,
    "watch_face_write": false,
    "wifi_provisioning": false,
    "wifi_ota": false,
    "rollback": false
  }
}
```

---

## 4. Denied Mutation & Administrative Commands

Per the non-negotiable read-only safety boundary, all mutation and administrative commands are intercepted by the dispatcher and safely return `NOT_SUPPORTED`:

- `MOTOR_MOVE`
- `MOTOR_TURN`
- `MOTOR_STOP`
- `SET_CONFIG`
- `SET_NAME`
- `SET_WATCH_FACE`
- `REBOOT`
- `GET_UPDATE_STATUS`
- `START_WIFI_OTA`
- Wi-Fi provisioning commands
- All undocumented commands

### Standard `NOT_SUPPORTED` Response:
```json
{
  "v": 1,
  "id": 105,
  "ok": false,
  "error_code": "NOT_SUPPORTED",
  "error_message": "Command not supported in read-only slice"
}
```

---

## 5. Standard Error Codes

- `NOT_SUPPORTED`: Feature or mutation command disabled in read-only slice.
- `INVALID_COMMAND`: Unknown command type or action.
- `INVALID_ARGUMENT`: Malformed JSON or invalid parameter syntax.
- `UNSUPPORTED_VERSION`: Protocol version `v` is not 1.
- `NOT_CONNECTED`: Client attempted operation while disconnected.
- `TIMEOUT`: Request exceeded bounded protocol timeout (5s).
- `PAYLOAD_TOO_LARGE`: Command exceeded 384 bytes.
- `TRANSPORT_ERROR`: Low-level BLE stack or GATT communication failure.
