#!/usr/bin/env python3
"""
test_speaker.py - Alat Diagnostik Speaker Fisik Rbot (MAX98357A + 8Ω 2W)
Mengirimkan perintah diagnostik ke ESP32 dan membaca respon serial.
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

def wait_for_handshake(s, max_wait=8.0):
    """Menunggu Rbot selesai boot dan merespon PING dengan PONG."""
    t_start = time.time()
    while time.time() - t_start < max_wait:
        s.write(b"PING\n")
        s.flush()
        t_inner = time.time()
        while time.time() - t_inner < 0.5:
            if s.in_waiting:
                line = s.readline().decode("utf-8", errors="ignore").strip()
                if "PONG" in line:
                    return True
            time.sleep(0.05)
    return False

def main():
    port = sys.argv[1] if len(sys.argv) > 1 else find_rbot_port()

    print("=" * 65)
    print("🔊 RBOT HARDWARE SPEAKER DIAGNOSTIC (MAX98357A + 8Ω 2W)")
    print("=" * 65)
    print(f"[*] Port Serial : {port}")
    print("[*] Target Test : Nada Uji 1 kHz (Sine Wave) & Sampel Suara")
    print("=" * 65 + "\n")

    try:
        s = serial.Serial()
        s.port = port
        s.baudrate = 115200
        s.timeout = 0.5
        s.dtr = False
        s.rts = False
        s.open()
    except Exception as e:
        print(f"[!] Gagal membuka port serial {port}: {e}")
        sys.exit(1)

    print("[*] Menghubungkan ke Rbot (Handshake PING -> PONG)...")
    if wait_for_handshake(s, max_wait=6.0):
        print("[+] Rbot CLI terhubung & siap menerima perintah!\n")
    else:
        print("[!] Peringatan: Handshake tidak merespon dalam 6 detik.")
        print("    Mencoba melanjutkan pengujian...\n")

    s.reset_input_buffer()

    print("[1/2] Mengirim perintah 'TONE' (1000 Hz BEEP Amplitudo Maksimal)...")
    s.write(b"TONE\n")
    s.flush()

    t_start = time.time()
    tone_success = False
    while time.time() - t_start < 5.0:
        line = s.readline().decode("utf-8", errors="ignore").strip()
        if line:
            print(f"    [Rbot] {line}")
            if "TONE_OK" in line or "Speaker tone test DONE" in line:
                tone_success = True
                break

    if tone_success:
        print("\n[+] Sinyal I2S DMA 1 kHz sukses ditransmisikan ke chip MAX98357A!")
    else:
        print("\n[?] Timeout pengiriman sinyal I2S TONE.")

    time.sleep(1.0)
    s.reset_input_buffer()

    print("\n[2/2] Mengirim perintah 'PLAY' (Sampel Suara Vokal)...")
    s.write(b"PLAY\n")
    s.flush()

    t_start = time.time()
    play_success = False
    while time.time() - t_start < 6.0:
        line = s.readline().decode("utf-8", errors="ignore").strip()
        if line:
            print(f"    [Rbot] {line}")
            if "PLAY_OK" in line or "Speaker human voice test DONE" in line:
                play_success = True
                break

    if play_success:
        print("\n[+] Sampel suara vokal selesai ditransmisikan ke I2S!")
    else:
        print("\n[?] Timeout pemutaran suara.")

    s.close()
    print("\n" + "=" * 65)
    print("💡 ANALISIS & PANDUAN:")
    print("   1. Jika di atas tertulis '[+] Sinyal I2S DMA ... sukses'")
    print("      tetapi speaker fisik tetap TIDAK bersuara:")
    print("      - Jalur I2S dari ESP32 (GPIO 11, 12, 6) sudah 100% aktif dan mengirim data.")
    print("      - Masalah berada di sisi modul MAX98357A atau sambungan speaker.")
    print("      - Pastikan pin SD terhubung ke 3.3V (Bukan GND!).")
    print("      - Pastikan Vin MAX98357A terhubung ke 5V dan GND ke GND.")
    print("      - Cek kedua kabel speaker terpasang kuat di terminal screw / solder.")
    print("=" * 65)

if __name__ == "__main__":
    main()
