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

def run_non_exit_test():
    print("=" * 70)
    print("🧪 RBOT NON-EXIT SENTENCE TEST (Empirical Device Verification)")
    print("   Testing sentence containing 'cukup' but as a question:")
    print("   'Apakah tegangan 3 koma 3 volt cukup?' (/tmp/q_non_exit1.mp3)")
    print("   Expected: Backend returns 'Action: CONTINUE', robot answers & enters follow-up window!")
    print("=" * 70)

    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(1.0)
    ser.reset_input_buffer()

    start_time = time.time()
    audio_played = False
    action_continue = False
    follow_up_entered = False

    # Trigger wake word
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
            print(">>> 🎤 ESP32-S3 Turn 1 speech wait. Playing non-exit phrase in 0.8s...")
            threading.Thread(target=play_audio, args=("/tmp/q_non_exit1.mp3", 0.8), daemon=True).start()

        if "Action: CONTINUE" in line:
            action_continue = True
            print(">>> ✅ Correct: Backend returned Action: CONTINUE for non-exit question!")

        if "TANYA LAGI" in line:
            follow_up_entered = True
            print(">>> ✅ Correct: Robot entered follow-up window as expected!")

        if "Wake word listening RESUMED" in line and audio_played:
            print(">>> ✅ WakeNet successfully resumed after follow-up timeout!")
            break

    ser.close()

    if action_continue and follow_up_entered:
        print("\n🎉 SUCCESS: Non-exit sentence containing 'cukup' was NOT misclassified as exit!")
    else:
        print(f"\n⚠️ Result check: action_continue={action_continue}, follow_up_entered={follow_up_entered}")

if __name__ == '__main__':
    run_non_exit_test()

