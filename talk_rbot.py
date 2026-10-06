#!/usr/bin/env python3
"""
talk_rbot.py - Trigger voice chat conversation with Reggi Bot on ESP32-S3.
Mirrors ST7735 LCD status in terminal:
  - TALK IN 3..2..1
  - RECORDING 5..4..3..2..1 (speak your question in Indonesian)
  - THINKING... (transcription & Gemini LLM response generated)
  - SPEAKING... (AI answer played through MAX98357A speaker)
  - RBOT READY

Usage:
  .venv/bin/python talk_rbot.py [--duration 5]
"""

import sys
import time
import glob
import serial
import argparse

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
    s.dtr = False
    s.rts = False
    s.open()
    return s


def readline(s):
    raw = s.readline()
    if not raw:
        return None
    return raw.decode(errors="replace").strip()


def wait_for_pong(s, timeout=5.0):
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


def run_talk(s, duration=5):
    s.reset_input_buffer()
    cmd = f"TALK {duration}\r\n"
    s.write(cmd.encode())
    s.flush()

    t0 = time.time()
    total_timeout = duration + 45.0  # Recording duration + backend/TTS processing timeout
    print(f"\n[*] Mengirim perintah 'TALK {duration}' ke Rbot...\n", flush=True)

    while time.time() - t0 < total_timeout:
        line = readline(s)
        if not line:
            continue

        if line.startswith("STATUS:"):
            text = line[len("STATUS:"):]
            icon = "🎙️ " if "TALK" in text else "🔴" if "RECORD" in text else "🧠" if "THINK" in text else "🔊" if "SPEAK" in text else "✅"
            print(f"  [LCD] {icon} {text}", flush=True)
            if text == "RBOT READY" and (time.time() - t0 > duration + 3):
                return True
        elif "Recording finished" in line:
            print(f"  {line}", flush=True)
        elif "Sending audio to backend" in line:
            print(f"  🌐 {line}", flush=True)
        elif "HTTP Response" in line:
            print(f"  📥 {line}", flush=True)
        elif "Submitted" in line and "audio player" in line:
            print(f"  🔊 {line}", flush=True)
        elif "Voice Chat Turn Finished" in line:
            print(f"  🎉 {line}", flush=True)
            return True
        elif "Wi-Fi not connected" in line or "NO WIFI" in line:
            print(f"  ⚠️  {line}", flush=True)
        elif "ERROR" in line or "FAIL" in line:
            print(f"  ❌ {line}", flush=True)

    print("  ⏱️  Timeout menunggu giliran percakapan selesai.")
    return False


def main():
    parser = argparse.ArgumentParser(description="Trigger Reggi Bot conversational voice chat")
    parser.add_argument("port", nargs="?", default=None, help="Serial port")
    parser.add_argument("--duration", "-d", type=int, default=5, help="Durasi rekaman dalam detik (default 5)")
    args = parser.parse_args()

    port = args.port or find_rbot_port()
    dur = max(3, min(15, args.duration))

    print("=" * 65)
    print("🤖 RBOT CONVERSATIONAL VOICE CHAT (MVP END-TO-END)")
    print("=" * 65)
    print(f"[*] Port Serial : {port} @ {BAUD}")
    print(f"[*] Durasi Tanya: {dur} detik")
    print(f"[*] Backend     : http://10.93.98.240:8000/chat/voice")
    print("=" * 65)

    try:
        s = open_port(port)
    except Exception as e:
        print(f"❌ Gagal membuka serial port {port}: {e}")
        sys.exit(1)

    print("Connecting to Rbot (PING)... ", end="", flush=True)
    if not wait_for_pong(s, timeout=5.0):
        print("FAIL ❌ (Coba cabut & pasang kembali USB atau tekan tombol EN)")
        s.close()
        sys.exit(1)
    print("OK ✅")

    print("Checking Wi-Fi status on Rbot... ", end="", flush=True)
    t0 = time.time()
    wifi_ok = False
    while time.time() - t0 < 8.0:
        line = readline(s)
        if line and ("ONLINE" in line or "sta ip:" in line or "WIFI CONNECTED" in line):
            wifi_ok = True
            break
    print("READY ✅" if wifi_ok else "PROCEEDING (Already connected) ✅")

    success = run_talk(s, duration=dur)
    s.close()

    print("\n" + "=" * 65)
    if success:
        print("✅ Percakapan selesai berhasil!")
    else:
        print("⚠️ Percakapan belum selesai sepenuhnya.")
    print("💡 Catatan: Anda juga dapat menyentuh PIN GPIO 14 (Touch) langsung di board!")
    print("=" * 65)


if __name__ == "__main__":
    main()
