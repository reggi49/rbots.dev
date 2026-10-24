# BLE Dependency Migration & Evaluation Report

> **Document Type:** Engineering Evaluation & Compliance Record  
> **Date:** 23 October 2026  
> **Project:** R-bot Companion (Mobile Android Target)  
> **Migration Status:** COMPLETED

---

## 1. Mandatory Migration Decision

The R-bot Companion application has executed a mandatory migration from `flutter_blue_plus` to `flutter_reactive_ble`.

### Rationale
1. **Commercial Independence & Clean IP:**
   - The R-bot project may transition into a commercial product or open-source community distribution.
   - `flutter_blue_plus` introduced proprietary / commercial licensing terms and required runtime license declarations (e.g., `license: License.nonprofit`) for non-paying users.
   - Retaining such requirements in a production codebase creates business risk and licensing contamination.
2. **Permissive Open-Source Licensing:**
   - `flutter_reactive_ble` is licensed under the **BSD-3-Clause** license (authored by Signify / Philips Hue).
   - BSD-3-Clause is universally recognized as a permissive, commercially friendly license that requires only the retention of copyright notices and disclaimers without requiring royalty payments, runtime declarations, or source disclosure.
3. **Architectural Isolation:**
   - The application enforces a strict `BleTransport` abstraction layer. Replacing the concrete package implementation did not require changes to protocol codecs, presentation views, or domain models.

---

## 2. Dependency Changes

| Attribute | Previous Dependency | Replacement Dependency |
|:---|:---|:---|
| **Package Name** | `flutter_blue_plus` | `flutter_reactive_ble` |
| **Installed Version** | `^2.3.14` (purged) | `5.6.0` (stable) |
| **License Type** | Custom Commercial / Tiered License | **BSD-3-Clause** |
| **Runtime License Declaration** | `License.nonprofit` | **None** (unrestricted permissive) |
| **Package Author** | `chip` / `boskokg` | Philips Hue / Signify B.V. |
| **Platform Target** | Android (API 21+) | Android (API 21+) |

### Transitive Dependency Tree of `flutter_reactive_ble: 5.6.0`
- `functional_data: 1.2.0` (MIT License)
- `protobuf: 6.1.0` (BSD-3-Clause)
- `reactive_ble_mobile: 5.6.0` (BSD-3-Clause)
- `reactive_ble_platform_interface: 5.6.0` (BSD-3-Clause)

---

## 3. Audit of Removed License Declarations

Every previous occurrence of `License.nonprofit` was audited and removed:
1. `Frontend/lib/data/ble/ble_transport.dart`:
   - Line 101: `await device.connect(license: License.nonprofit, ...)` was completely removed.
   - All references to `FlutterBluePlus`, `BluetoothDevice`, `BluetoothCharacteristic`, and `Guid` were removed.
2. `Frontend/pubspec.yaml`:
   - `flutter_blue_plus: ^2.3.14` removed.
3. `Frontend/pubspec.lock`:
   - `flutter_blue_plus`, `flutter_blue_plus_android`, `flutter_blue_plus_darwin`, `flutter_blue_plus_linux`, `flutter_blue_plus_platform_interface`, `flutter_blue_plus_web`, `flutter_blue_plus_winrt` completely purged.

---

## 4. BSD-3-Clause Compliance Obligations

For `flutter_reactive_ble: 5.6.0` and its transitive dependencies, the BSD-3-Clause license requires:
1. **Source Code Distribution:** Retention of the copyright notice, conditions, and disclaimer.
2. **Binary Distribution:** Reproduction of the copyright notice, conditions, and disclaimer in documentation or user materials distributed with the package.
3. **Endorsement Prohibition:** Neither the name of the copyright holder nor the names of contributors may be used to endorse or promote products derived from this software without specific prior written permission.

All required attribution texts have been compiled into [`docs/third-party-licenses.md`](file:///Users/reggi/Projects/rbot/docs/third-party-licenses.md).

---

## 5. Platform Compatibility & Differences Affecting R-bot

| Feature | `flutter_blue_plus` | `flutter_reactive_ble` | R-bot Implementation Strategy |
|:---|:---|:---|:---|
| **Scanning** | `FlutterBluePlus.startScan(withServices: [...])` | `_ble.scanForDevices(withServices: [...])` | Stream-based scan with duplicate suppression by device ID and RSSI update. |
| **Service Discovery** | `device.discoverServices()` | `_ble.discoverAllServices(id)` followed by `_ble.getDiscoveredServices(id)` | Explicit validation of Service UUID `7b4a0001-...` and characteristic UUIDs. |
| **Command Write** | `char.write(bytes, withoutResponse: false)` | `_ble.writeCharacteristicWithResponse(char, value: bytes)` | Write-with-response enforced on Command characteristic `7b4a0002-...`. |
| **Status Read** | `char.read()` | `_ble.readCharacteristic(char)` | Direct asynchronous read from Status characteristic `7b4a0003-...`. |
| **Notifications** | `char.setNotifyValue(true)` + `onValueReceived` | `_ble.subscribeToCharacteristic(char)` | Subscription stream cancelled on disconnect. |
| **Adapter State** | `FlutterBluePlus.adapterState` | `_ble.statusStream` / `_ble.status` | Mapped to domain `BleAdapterState` (unknown, unsupported, unauthorized, poweredOff, ready). |
| **MTU Handling** | `device.requestMtu(512)` | Automatic platform negotiation | Default MTU safe limit capped at 384 bytes; all read-only commands fit in < 70 bytes. |

---

## 6. Migration Risks & Mitigations

1. **Android Location Services Behavior:**
   - On Android 11 and earlier, BLE scanning requires location services enabled.
   - On Android 12+ (API 31+), `BLUETOOTH_SCAN` with `android:usesPermissionFlags="neverForLocation"` avoids location prompts.
   - *Mitigation:* `AndroidManifest.xml` specifies `neverForLocation` for Android 12+, with legacy location permissions scoped to `maxSdkVersion="30"`.
2. **Simultaneous Connections:**
   - Multiple connection attempts to the same or multiple peripherals can cause stack faults on Android BLE.
   - *Mitigation:* `ReactiveBleTransport` enforces single-connection state guards (`if (_connectionState == BleConnectionState.connecting) return;`) and stops scanning before connecting.
3. **Dead-Man Timer & Heartbeat:**
   - BLE transport latencies must remain within the 500 ms firmware lease once motors are enabled in Phase 3.
   - *Mitigation:* All motor commands remain strictly rejected with `NOT_SUPPORTED` during this read-only phase.

