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

def run_3turn_test():
    print("=" * 70)
    print("🚀 RBOT MULTI-TURN 3-TURN DIALOGUE TEST (Empirical Device Verification)")
    print("   Turn 1: 'Alexa' -> 'Apa fungsi ESP32?'")
    print("   Turn 2: (No Wake Word) -> 'Kalau dibanding Arduino biasa bagaimana?'")
    print("   Turn 3: (No Wake Word) -> 'Kalau untuk robot ini lebih cocok yang mana?'")
    print("   Turn 4: Silence -> Wait 10s for follow-up timeout & session close")
    print("=" * 70)

    ser = serial.Serial(PORT, BAUD, timeout=0.1)
    time.sleep(1.0)
    ser.reset_input_buffer()

    state = "IDLE_WAIT"
    session_id = None
    start_time = time.time()
    turn1_audio_started = False
    turn2_audio_started = False
    turn3_audio_started = False
    
    # Step 1: Trigger wake word "Alexa"
    print("\n[*] Sending Wake Word 'Alexa' to robot...")
    threading.Thread(target=play_audio, args=("/tmp/alexa.mp3", 0.5), daemon=True).start()

    while time.time() - start_time < 90:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if not line:
            time.sleep(0.01)
            continue
        
        # Print relevant board log lines
        if any(k in line for k in ["WAKE_WORD_SR", "VOICE_CHAT", "STATE", "STATUS", "AUDIO", "BOARD"]):
            print(f"🤖 {line}")

        # Detect Wake Word Detection
        if "WAKE WORD DETECTED" in line:
            print(">>> ✅ Wake word detected on ESP32-S3!")

        # Turn 1 Waiting for Speech
        if "Execute Turn 1" in line and not turn1_audio_started:
            turn1_audio_started = True
            print(">>> 🎤 ESP32-S3 entered Turn 1 speech wait. Playing Question 1 in 0.5s...")
            threading.Thread(target=play_audio, args=("/tmp/q_short.mp3", 0.5), daemon=True).start()

        # Turn 2 Waiting for Speech (In follow-up window!)
        if "Execute Turn 2" in line and not turn2_audio_started:
            turn2_audio_started = True
            print(">>> 🔄 ESP32-S3 entered Turn 2 follow-up window (WITHOUT WAKE WORD)! Playing Question 2 in 1.5s...")
            threading.Thread(target=play_audio, args=("/tmp/q_turn2.mp3", 1.5), daemon=True).start()

        # Turn 3 Waiting for Speech (In follow-up window!)
        if "Execute Turn 3" in line and not turn3_audio_started:
            turn3_audio_started = True
            print(">>> 🔄 ESP32-S3 entered Turn 3 follow-up window (WITHOUT WAKE WORD)! Playing Question 3 in 1.5s...")
            threading.Thread(target=play_audio, args=("/tmp/q_turn3.mp3", 1.5), daemon=True).start()

        # Follow-up Timeout & Session Close
        if "Follow-up window" in line and "expired" in line:
            print(">>> ✅ Follow-up window expired cleanly as expected!")

        if "[SESSION END]" in line:
            print(f">>> 🏁 Session ended successfully: {line}")

        if "Wake word listening RESUMED" in line and turn3_audio_started:
            print(">>> 🎉 WakeNet successfully resumed after multi-turn conversation!")
            time.sleep(1.0)
            break

    ser.close()
    print("\n✅ Multi-turn 3-turn test sequence completed.")

if __name__ == '__main__':
    run_3turn_test()

