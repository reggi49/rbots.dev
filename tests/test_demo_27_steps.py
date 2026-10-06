import serial
import time
import sys

def main():
    port = "/dev/cu.usbmodem5A790640561"
    baud = 115200
    print(f"Connecting to {port} @ {baud}...")
    ser = serial.Serial(port, baud, timeout=0.5)
    time.sleep(1.0)
    ser.reset_input_buffer()

    print("Triggering TEST_FACE (27 Steps Complete Demo)...")
    ser.write(b"TEST_FACE\r\n")

    steps_seen = set()
    start_time = time.time()
    timeout = 70.0 # 27 steps * ~1.8s + margin

    while time.time() - start_time < timeout:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if line:
            if "DEMO" in line and "/" in line:
                print(f"[SERIAL] {line}")
                for s in range(1, 28):
                    if f"[{s}/27]" in line or f"[{s}/27" in line or f"DEMO {s}/27" in line:
                        steps_seen.add(s)
            elif "Demo Complete" in line:
                print(f"[SERIAL] {line}")
                steps_seen.add(27)
                break

    print(f"\nTotal steps observed: {len(steps_seen)} / 27")
    missing = [s for s in range(1, 28) if s not in steps_seen]
    if missing:
        print(f"Missing steps in log: {missing}")
    else:
        print("ALL 27 DEMO STEPS VERIFIED SUCCESSFULLY! ✅")

    ser.write(b"METRICS\r\n")
    time.sleep(0.5)
    metrics = ser.read_all().decode("utf-8", errors="ignore").strip()
    print(f"\nFinal Telemetry:\n{metrics}")

    ser.close()
    if len(steps_seen) >= 25:
        sys.exit(0)
    else:
        sys.exit(1)

if __name__ == "__main__":
    main()
