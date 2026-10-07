import time
import subprocess
import serial
import sys
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

def run_explicit_exit_test():
    print("=" * 70)
    print("🛑 RBOT EXPLICIT VERBAL EXIT TEST (Empirical Device Verification)")
    print("   1. Wake word: 'Alexa'")
    print("   2. User says: 'Cukup, terima kasih banyak.' (/tmp/q_exit.mp3)")
    print("   3. Backend detects exit intent -> returns 'X-Session-Action: end'")
    print("   4. ESP32 plays farewell -> terminates session IMMEDIATELY (no follow-up window)")
    print("   5. WakeNet resumes immediately")
    print("=" * 70)

    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(1.0)
    ser.reset_input_buffer()

    start_time = time.time()
    audio_played = False
    exit_action_detected = False
    follow_up_entered = False
    session_ended = False

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
            print(">>> 🎤 ESP32-S3 Turn 1 speech wait. Playing exit phrase 'Cukup, terima kasih banyak.' in 0.8s...")
            threading.Thread(target=play_audio, args=("/tmp/q_exit.mp3", 0.8), daemon=True).start()

        if "Action: END" in line or "action=end" in line or "X-Session-Action: end" in line:
            exit_action_detected = True
            print(">>> ✅ Backend Action: END detected by ESP32-S3!")

        if "TANYA LAGI" in line:
            follow_up_entered = True
            print(">>> ❌ WARNING: Robot entered follow-up window despite exit command!")

        if "[SESSION END]" in line:
            session_ended = True
            print(">>> 🏁 Session closed cleanly!")

        if "Wake word listening RESUMED" in line and audio_played:
            print(">>> ✅ WakeNet successfully resumed!")
            time.sleep(1.0)
            break

    ser.close()

    if exit_action_detected and session_ended and not follow_up_entered:
        print("\n🎉 SUCCESS: Explicit verbal exit cleanly terminated the session without entering follow-up window!")
    else:
        print(f"\n⚠️ Result check: exit_detected={exit_action_detected}, session_ended={session_ended}, follow_up_entered={follow_up_entered}")

if __name__ == '__main__':
    run_explicit_exit_test()

