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

def run_pause_reset_test():
    print("=" * 70)
    print("⏸️ RBOT PAUSE RESET TEST (Empirical Device Verification)")
    print("   Testing audio with 1.0s pause in the middle (/tmp/q_pause_reset.wav)")
    print("   Part 1 (2.0s) -> 1.0s Pause -> Part 2 (2.3s)")
    print("   Expected: Silence resets when Part 2 starts, recording does NOT cut off early at 1.0s pause!")
    print("=" * 70)

    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(1.0)
    ser.reset_input_buffer()

    start_time = time.time()
    audio_played = False
    rec_duration_sec = 0.0

    print("\n[*] Sending Wake Word 'Alexa' to robot...")
    threading.Thread(target=play_audio, args=("/tmp/alexa.mp3", 0.5), daemon=True).start()

    while time.time() - start_time < 60:
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
            print(">>> 🎤 ESP32-S3 Turn 1 speech wait. Playing pause audio in 0.8s...")
            threading.Thread(target=play_audio, args=("/tmp/q_pause_reset.wav", 0.8), daemon=True).start()

        if "Recording finished" in line:
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
    # If it survived the 1.0s pause, duration will be > 4.5s!
    if rec_duration_sec >= 4.5:
        print("🎉 SUCCESS: 1.0s pause was tolerated without early cutoff, silence counter reset cleanly!")
    else:
        print(f"⚠️ Premature cutoff detected: Duration was {rec_duration_sec:.2f}s (< 4.5s)")

if __name__ == '__main__':
    run_pause_reset_test()

