#!/usr/bin/env python3
"""
play_voice.py - Trigger Prabowo voice playback directly on physical Rbot speaker (MAX98357A)
"""
import sys
import time
import glob
import serial

def find_rbot_port():
    ports = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
    if ports:
        return sorted(ports)[0]
    return "/dev/cu.usbmodem5A790640561"

def wait_handshake(ser, max_wait=6.0):
    """Menunggu ESP32 selesai boot dan merespon PING dengan PONG."""
    t0 = time.time()
    while time.time() - t0 < max_wait:
        ser.write(b"PING\r\n")
        ser.flush()
        t_inner = time.time()
        while time.time() - t_inner < 0.4:
            if ser.in_waiting:
                line = ser.readline().decode("utf-8", errors="ignore").strip()
                if "PONG" in line or "READY FOR COMMANDS" in line or "Rbot ready" in line:
                    return True
            time.sleep(0.05)
    return False

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_rbot_port()

    print("=" * 65)
    print("🔊 MEMUTAR SUARA PRABOWO (prabowo_voice.mp3) DI SPEAKER RBOT")
    print("=" * 65)
    print(f"[*] Port Serial : {port}")

    try:
        s = serial.Serial()
        s.port = port
        s.baudrate = 115200
        s.timeout = 0.2
        s.dtr = False
        s.rts = False
        s.open()
    except Exception as e:
        print(f"[!] Gagal membuka port serial {port}: {e}")
        sys.exit(1)

    print("[*] Menghubungkan ke Rbot (menunggu boot & handshake)...")
    time.sleep(1.5)
    s.reset_input_buffer()

    if not wait_handshake(s, max_wait=5.0):
        print("[!] Peringatan: Handshake timeout, mencoba mengirim 'PLAY' langsung...")
    else:
        print("[+] Rbot terhubung!\n")

    s.reset_input_buffer()
    print("[*] Mengirim perintah 'PLAY' ke Rbot...")
    s.write(b"PLAY\r\n")
    s.flush()

    print("[*] Audio sedang diputar melalui speaker MAX98357A...")
    start = time.time()
    while time.time() - start < 6.0:
        line = s.readline().decode("utf-8", errors="ignore").strip()
        if line and ("PLAY_OK" in line or "DONE" in line):
            print(f"    [Rbot] {line}")
            break
        time.sleep(0.1)

    s.close()
    print("\n" + "=" * 65)
    print("✅ Pemutaran audio suara Prabowo selesai!")
    print("=" * 65)

if __name__ == "__main__":
    main()
