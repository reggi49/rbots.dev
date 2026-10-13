#!/usr/bin/env python3
"""
test_expressions.py - Script pengujian seluruh ekspresi wajah R-bot.

Mengirimkan perintah pengujian ekspresi wajah ke firmware ESP32-S3 via Serial/UART.
Bisa menjalankan demo berurutan otomatis atau memilih ekspresi tertentu.

Penggunaan:
    python test_expressions.py             # Menjalankan demonstrasi berurutan seluruh ekspresi
    python test_expressions.py --demo      # Menjalankan built-in 27-step firmware demo
    python test_expressions.py HAPPY_BIG   # Menjalankan satu ekspresi tertentu
"""

import sys
import time
import glob
import argparse
import serial

BAUD = 115200

EXPRESSIONS = [
    ("IDLE", "1. Standby / Siap (Mata cyan ramah, kedip berkala)"),
    ("LISTEN", "2. Listening (Mata membesar, fokus mendengarkan user)"),
    ("THINK", "3. Thinking (Mata melirik ke atas-kanan/kiri + partikel orbit kuning)"),
    ("SPEAK", "4. Speaking (Mata aktif + gelombang audio modulasi di mulut)"),
    ("HAPPY", "5. Happy Soft (Senyum kecil + mata lembut setelah menjawab)"),
    ("HAPPY_BIG", "6. Happy Big (Mata busur bulan sabit gembira + senyum)"),
    ("CURIOUS_L", "7. Curious Kiri (Mata kiri membesar + alis miring quizzical)"),
    ("CURIOUS_R", "8. Curious Kanan (Mata kanan membesar + melirik)"),
    ("EXCITED", "9. Excited (Mata melonjong antusias, animasi squash & stretch)"),
    ("SURPRISE", "10. Surprised (Mata membulat tinggi terkejut)"),
    ("CONFUSED", "11. Confused (Alis mengernyit ragu / bingung)"),
    ("WINK_L", "12. Kedip Kiri (Playful wink mata kiri)"),
    ("WINK_R", "13. Kedip Kanan (Playful wink mata kanan)"),
    ("SLEEP", "14. Sleepy / Mengantuk (Kelopak mata turun 60% + layar redup 50%)"),
    ("ERR", "15. Error Sad (Mata merah murung melengkung ke bawah)"),
    ("OFFLINE", "16. Offline / Disconnected (Mata oranye redup tanpa koneksi)"),
    ("BOOT", "17. Booting (Animasi mata terbuka pertama kali)"),
]


def find_rbot_port():
    ports = sorted(
        glob.glob("/dev/cu.usbmodem*")
        + glob.glob("/dev/cu.usbserial*")
        + glob.glob("/dev/cu.wchusbserial*")
    )
    return ports[0] if ports else "/dev/cu.usbmodem5A790640561"


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0.1
    # Jaga RTS/DTR agar ESP32-S3 tidak ter-reset saat port dibuka
    s.dtr = False
    s.rts = False
    s.open()
    return s


def send_command(s, cmd):
    s.reset_input_buffer()
    payload = f"{cmd}\r\n".encode()
    s.write(payload)
    s.flush()
    time.sleep(0.05)


def wait_for_ready(s, timeout=5.0):
    t0 = time.time()
    last_ping = 0.0
    while time.time() - t0 < timeout:
        if time.time() - last_ping > 0.8:
            s.write(b"PING\r\n")
            s.flush()
            last_ping = time.time()
        line = s.readline().decode(errors="replace").strip()
        if "PONG" in line:
            return True
    return False


def main():
    parser = argparse.ArgumentParser(description="Test ekspresi wajah R-bot")
    parser.add_argument("expression", nargs="?", default=None, help="Nama ekspresi (misal: HAPPY, WINK_L, SLEEP, dll)")
    parser.add_argument("--demo", action="store_true", help="Jalankan built-in 27-step firmware demo")
    parser.add_argument("--port", default=None, help="Port serial ESP32-S3")
    parser.add_argument("--delay", type=float, default=2.5, help="Durasi tayang tiap ekspresi (detik)")
    args = parser.parse_args()

    port = args.port or find_rbot_port()
    print("=" * 65)
    print("🤖 RBOT FACE EXPRESSIONS TEST SUITE")
    print(f"[*] Menghubungkan ke: {port}")
    print("=" * 65)

    try:
        s = open_port(port)
    except Exception as e:
        print(f"❌ Gagal membuka serial port {port}: {e}")
        sys.exit(1)

    print("[*] Melakukan handshake PING -> PONG...")
    if not wait_for_ready(s):
        print("⚠️ Warning: Firmware tidak merespons PONG, tetap mencoba mengirim perintah...")
    else:
        print("✅ Terhubung ke R-bot!\n")

    if args.demo:
        print("[*] Menjalankan firmware built-in 27-step animation demo...")
        send_command(s, "DEMO")
        print("✅ Demo aktif di layar ST7735! Perhatikan transisi di layar.")
        s.close()
        return

    if args.expression:
        cmd = f"FACE {args.expression.upper()}"
        print(f"[*] Mengirim ekspresi spesifik: {cmd}")
        send_command(s, cmd)
        print("✅ Selesai!")
        s.close()
        return

    print("Mulai pengujian seluruh ekspresi secara berurutan:\n")
    for name, desc in EXPRESSIONS:
        print(f"▶️  Menampilkan: {desc}")
        send_command(s, f"FACE {name}")
        time.sleep(args.delay)

    print("\n[*] Mengembalikan wajah ke status READY (IDLE)...")
    send_command(s, "FACE IDLE")
    print("✅ Seluruh ekspresi selesai diuji coba 100%!")
    s.close()


if __name__ == "__main__":
    main()
