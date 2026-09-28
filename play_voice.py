#!/usr/bin/env python3
"""
play_voice.py - Trigger human voice playback directly on physical Rbot speaker (MAX98357A)
"""
import sys
import time
import glob
import serial

def find_rbot_port():
    ports = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
    if ports:
        return ports[0]
    return "/dev/cu.usbmodem2101"

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_rbot_port()
    print(f"[*] Connecting to Rbot on {port}...")

    try:
        s = serial.Serial(port, 115200, timeout=0.2)
    except Exception as e:
        print(f"[!] Error opening serial port {port}: {e}")
        sys.exit(1)

    time.sleep(0.3)
    print("[*] Sending 'PLAY' command to Rbot...")
    s.write(b"PLAY\n")

    start = time.time()
    played = False
    while time.time() - start < 5.0:
        line = s.readline()
        if line:
            text = line.decode("utf-8", errors="replace").rstrip()
            if text:
                print(f"    [Rbot] {text}")
            if "PLAY_OK" in text or "DONE" in text:
                played = True

    s.close()
    if played:
        print("\n[+] Suara berhasil diputar melalui speaker fisik Rbot (MAX98357A)!")
    else:
        print("\n[?] Perintah terkirim. Pastikan kabel speaker MAX98357A terhubung dengan benar.")

if __name__ == "__main__":
    main()
