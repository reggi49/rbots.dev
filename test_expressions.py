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
    ("EXCITED", "1. EXCITED (Panel 1: Sunburst rays, paws raised high, sparkling eyes)"),
    ("SURPRISE", "2. SURPRISED (Panel 2: Floating '!!!', wide round eyes, paws at cheeks)"),
    ("SQUINT", "3. SQUINT (Panel 3: Candy & stars, left wink, sweet cat smile)"),
    ("WINK_L", "4. WINK L (Panel 4: Mint/sky gradient, left eye wink, happy open mouth)"),
    ("WINK_R", "5. WINK R (Panel 5: Peach gradient, right eye wink, open smile)"),
    ("CONFUSED", "6. CONFUSED (Panel 6: Lavender, '???', asymmetric gaze, paw at chin)"),
    ("SLEEP", "7. SLEEPY (Panel 7: Midnight indigo, crescent moon, 'zzz', pink pillow)"),
    ("SAD", "8. ERROR SAD (Panel 8: Crimson rose, broken heart, sad tears, quivering pout)"),
    ("HAPPY", "9. HAPPY (Panel 9: Vibrant pink, floating hearts, joyful crescent eyes)"),
    ("RECOVERED", "10. RECOVERED (Panel 10: Spring green leaves, sunny smile, calm eyes)"),
    ("NEUTRAL", "11. NEUTRAL (Peaceful open sparkling eyes, ':3' smile, gentle breathing)"),
    ("LISTEN", "12. LISTENING (Attentive eyes, cyan acoustic soundwaves from headphones)"),
    ("THINK", "13. THINKING (Gazing up-right, rotating orbital thought sparkles)"),
    ("SPEAK", "14. SPEAKING (Dynamic feline mouth modulated with audio level)"),
    ("BLINK", "15. BLINK (Natural smooth eyelid closure and reopen)"),
    ("CURIOUS", "16. CURIOUS (Tilted head illusion, asymmetric eyes, inquisitive)"),
    ("ANGRY", "17. ANGRY (Slanted brows, downward grimace with corner teeth, anger mark)"),
    ("LOVE", "18. LOVE (Heart pupils, floating hearts, glowing rosy blush)"),
    ("PLAYFUL", "19. PLAYFUL (Wink, cute tongue blep :P, paw waving)"),
    ("ERR", "20. ERROR (Concerned expression, broken heart, server error)"),
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


def wait_for_ready(s, timeout=12.0):
    t0 = time.time()
    last_ping = 0.0
    while time.time() - t0 < timeout:
        if time.time() - last_ping > 0.5:
            s.write(b"PING\r\n")
            s.flush()
            last_ping = time.time()
        line = s.readline().decode(errors="replace").strip()
        if "PONG" in line:
            return True
        time.sleep(0.05)
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

