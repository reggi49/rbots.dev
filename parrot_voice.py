#!/usr/bin/env python3
"""
parrot_voice.py - Rbot "parrot": record from the INMP441 mic, then play it back
on the MAX98357A speaker. The terminal mirrors exactly what the ST7735 LCD shows
(the firmware prints "STATUS:<text>" every time it updates the screen).

Usage:  python parrot_voice.py [PORT] [--loop]
"""
import sys
import time
import glob
import serial

BAUD = 115200


def find_rbot_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
                   + glob.glob("/dev/cu.wchusbserial*"))
    return ports[0] if ports else "/dev/cu.usbmodem5A790640561"


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0.1
    # Keep DTR/RTS released so opening the port does NOT reset the ESP32-S3
    s.dtr = False
    s.rts = False
    s.open()
    return s


def readline(s):
    raw = s.readline()
    if not raw:
        return None
    return raw.decode(errors="replace").strip()


def wait_for_pong(s, timeout=10.0):
    """Send PING until the firmware answers PONG (handles a fresh boot)."""
    t0 = time.time()
    last_ping = 0.0
    while time.time() - t0 < timeout:
        if time.time() - last_ping > 1.0:
            s.write(b"PING\r\n")
            s.flush()
            last_ping = time.time()
        line = readline(s)
        if line and "PONG" in line:
            return True
    return False


def run_parrot(s, duration=3):
    s.reset_input_buffer()
    cmd = f"ECHO {duration}\r\n"
    s.write(cmd.encode())
    s.flush()

    t0 = time.time()
    # Dynamic timeout based on duration: 3s prep + duration rec + 3s speak prep + duration speak + 10s buffer
    total_timeout = (duration * 2) + 16.0
    while time.time() - t0 < total_timeout:
        line = readline(s)
        if not line:
            continue
        if line.startswith("STATUS:"):
            text = line[len("STATUS:"):]
            icon = "🎙️ " if text.startswith("RECORD") else "🔊" if text.startswith("SPEAK") else "✅"
            if text.startswith("RECORDING"):
                icon = "🔴"
            print(f"  [LCD] {icon} {text}", flush=True)
        elif line.startswith("ECHO_START"):
            print("  Rbot got the command, starting...", flush=True)
        elif line.startswith("ECHO_ERR"):
            print(f"  ❌ Firmware error: {line}", flush=True)
            return False
        elif line.startswith("ECHO_DONE"):
            return True
        elif "Recording done" in line:
            print(f"        ({line.split('Recording done!')[-1].strip()})", flush=True)
    print("  ⏱️  Timeout: no ECHO_DONE received.")
    return False


def main():
    import argparse
    parser = argparse.ArgumentParser(description="RBOT PARROT (mic -> speaker loopback)")
    parser.add_argument("port", nargs="?", default=None, help="Serial port")
    parser.add_argument("--duration", "-d", type=int, default=3, help="Durasi rekaman dalam detik (contoh: 5, 10, 15)")
    parser.add_argument("--loop", action="store_true", help="Ulangi loop parrot terus-menerus")
    args = parser.parse_args()

    port = args.port or find_rbot_port()
    duration = max(1, min(60, args.duration))

    print("=" * 60)
    print("🦜 RBOT PARROT  (mic INMP441 -> speaker MAX98357A)")
    print("=" * 60)
    print(f"Port    : {port} @ {BAUD}")
    print(f"Durasi  : {duration} detik")
    print("=" * 60)

    try:
        s = open_port(port)
    except Exception as e:
        print(f"❌ Cannot open {port}: {e}")
        sys.exit(1)

    print("Connecting to Rbot (PING)...", end=" ", flush=True)
    if not wait_for_pong(s):
        print("FAILED")
        print("❌ No PONG from Rbot. Check: firmware flashed with UART console,")
        print("   cable on the 'COM/UART' USB port, and nothing else using the port.")
        s.close()
        sys.exit(1)
    print("OK ✅\n")

    try:
        while True:
            ok = run_parrot(s, duration)
            print("\n🎉 Done!" if ok else "\n⚠️  Parrot did not finish.")
            if not args.loop:
                break
            input("\nPress Enter to parrot again (Ctrl+C to quit)...")
            print()
    except KeyboardInterrupt:
        pass
    finally:
        s.close()


if __name__ == "__main__":
    main()
