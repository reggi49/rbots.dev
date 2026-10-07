import time
import subprocess
import serial
import threading
import os

PORT = '/dev/cu.usbmodem5A790640561'
BAUD = 115200

def play_audio(filepath, delay=0.0):
    if delay > 0:
        time.sleep(delay)
    print(f"\n📢 [SPEAKER] Playing audio: {os.path.basename(filepath)} (afplay)...")
    subprocess.run(["afplay", filepath], check=True)
    print(f"📢 [SPEAKER] Finished playing: {os.path.basename(filepath)}\n")

def run_long_question_test():
    print("=" * 70)
    print("📏 RBOT LONG QUESTION (9 SECONDS) RECORDING TEST")
    print("   Testing continuous speech for 9 seconds (/tmp/q_long.mp3)")
    print("   Verifying that recorder does NOT cut off prematurely")
    print("   and records full speech duration (~9-10s)")
    print("=" * 70)

    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(1.0)
    ser.reset_input_buffer()

    start_time = time.time()
    audio_played = False
    rec_duration_sec = 0.0

    print("\n[*] Sending Wake Word 'Alexa' to robot...")
    threading.Thread(target=play_audio, args=("/tmp/alexa.mp3", 0.5), daemon=True).start()

    while time.time() - start_time < 70:
        try:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
        except Exception:
            time.sleep(0.05)
            continue
        if not line:
            time.sleep(0.01)
            continue

        if any(k in line for k in ["WAKE_WORD_SR", "VOICE_CHAT", "STATE", "STATUS"]):
            print(f"🤖 {line}")

        if "Execute Turn 1" in line and not audio_played:
            audio_played = True
            print(">>> 🎤 ESP32-S3 Turn 1 speech wait. Playing long 9s question in 0.8s...")
            threading.Thread(target=play_audio, args=("/tmp/q_long.mp3", 0.8), daemon=True).start()

        if "Recording finished" in line:
            # e.g.: Recording finished: 153600 samples (RMS: 184, Peak: 1283, Dur: 9600 ms)
            print(f">>> 📊 Recorded result: {line}")
            if "Dur:" in line:
                try:
                    dur_part = line.split("Dur:")[1].split("ms")[0].strip()
                    rec_duration_sec = float(dur_part) / 1000.0
                except Exception:
                    pass

        if "Turn 1 Completed Successfully" in line:
            break

    ser.close()

    print(f"\nFinal Recorded Duration: {rec_duration_sec:.2f} seconds")
    if rec_duration_sec >= 8.0:
        print("🎉 SUCCESS: Long speech was recorded completely without premature cutoff!")
    else:
        print(f"⚠️ Warning: Recorded duration was {rec_duration_sec:.2f}s (< 8.0s)")

if __name__ == '__main__':
    run_long_question_test()

