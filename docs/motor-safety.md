# Motor Safety & Physical Movement Control

**Project:** R-bot Companion Application & Firmware Integration  
**Driver Hardware:** DRV8833 Dual H-Bridge  
**Actuators:** Dual N20 Micro Metal Gearmotors  
**Date:** October 2026  

---

## 1. Safety Principles & Dead-Man Architecture

1. **Firmware Is Authoritative:** The mobile phone UI is an untrusted controller. The ESP32-S3 firmware enforces all speed limits, directional interlocks, and movement timeouts independently of mobile state.
2. **Press-and-Hold Dead-Man Control:**
   - Movement occurs **only while the user holds a directional control**.
   - Releasing the control (`onPointerUp`, `onPointerCancel`, or focus loss) sends an immediate `MOTOR_STOP` command.
3. **500 ms Movement Lease & Hardware Watchdog:**
   - Each movement command carries a lease time (`ttl_ms`, default 500 ms).
   - If phone connection drops, the app crashes, or BLE latency stalls communication, the firmware timer expires at 500 ms and halts both motors automatically.
4. **Immediate STOP Priority:**
   - STOP commands bypass ordinary command queues.
   - STOP sets all DRV8833 inputs to low (or active brake) immediately and clears any active lease timers.
5. **Safe Startup & Reconnect State:**
   - Upon initial boot or BLE reconnection, the motor controller starts in `STOP` mode.
   - Reconnecting to a previously paired robot **never automatically resumes motion**.
   - Safe maximum speed is capped in firmware to 80% duty cycle.

---

## 2. Test Procedures for Hardware Integration

### Bench Test (Wheels Off Ground)
1. Elevate robot wheels so they do not make contact with any surface.
2. Ensure power switch is reachable for emergency manual cutoff.
3. Power on ESP32-S3 and connect via BLE from Android companion app.
4. Hold `FORWARD` for 2 seconds. Verify both wheels rotate forward smoothly.
5. Release `FORWARD`. Verify wheels stop within <50 ms.
6. Hold `FORWARD` and abruptly force-quit the mobile app or disable Bluetooth on the phone.
7. Confirm that wheels stop within 500 ms due to firmware lease expiration.
8. Reopen mobile app and reconnect. Verify motors remain stationary.
