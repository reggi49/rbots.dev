import serial
import time
import sys

PORT = '/dev/cu.usbmodem5A790640561'
BAUD = 115200

print("=" * 70)
print("🤖 RBOT 20 CONSECUTIVE INTERACTION STABILITY & TELEMETRY TEST (V2)")
print("   Testing Dynamic Eye Poses, Asymmetry, Battery States, & Speaker Playback")
print("=" * 70)

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(1.5)
ser.reset_input_buffer()

def send_cmd(cmd, wait_time=0.3):
    ser.write((cmd + '\n').encode('utf-8'))
    ser.flush()
    time.sleep(wait_time)
    lines = []
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            lines.append(line)
    return lines

send_cmd("PING")
metrics_lines = send_cmd("METRICS")
print(f"[*] Initial Baseline Metrics: {' '.join(metrics_lines)}\n")

results = []
NUM_CYCLES = 20

# Varied expression test sequence
poses = [
    ("FACE IDLE", "BAT FULL"),
    ("FACE CURIOUS_L", "BAT MED"),
    ("FACE CURIOUS_R", "BAT CHG"),
    ("FACE WINK_L", "BAT FULL"),
    ("FACE WINK_R", "BAT LOW"),
    ("FACE CONFUSED", "BAT MED"),
    ("FACE EXCITED", "BAT CHG"),
    ("FACE SURPRISE", "BAT FULL"),
    ("FACE HAPPY_BIG", "BAT FULL"),
    ("FACE SQUINT", "BAT MED"),
    ("FACE SLEEP", "BAT FULL"),
    ("FACE IDLE", "BAT CHG"),
    ("FACE CURIOUS_L", "BAT FULL"),
    ("FACE CONFUSED", "BAT MED"),
    ("FACE WINK_R", "BAT FULL"),
    ("FACE EXCITED", "BAT CHG"),
    ("FACE SURPRISE", "BAT FULL"),
    ("FACE CURIOUS_R", "BAT MED"),
    ("FACE HAPPY_BIG", "BAT FULL"),
    ("FACE IDLE", "BAT FULL"),
]

for i in range(1, NUM_CYCLES + 1):
    cycle_start = time.time()
    pre_pose, bat_cmd = poses[i - 1]
    
    # 1. Pre-interaction pose & battery setting
    send_cmd(bat_cmd, 0.15)
    send_cmd(pre_pose, 0.25)
    
    # 2. State: LISTENING
    send_cmd("FACE LISTEN", 0.25)
    
    # 3. State: THINKING
    send_cmd("FACE THINK", 0.3)
    
    # 4. Audio Playback with animated SPEAKING mouth
    ser.write(b"PLAY\n")
    ser.flush()
    play_done = False
    play_start = time.time()
    while time.time() - play_start < 6:
        if ser.in_waiting:
            line = ser.readline().decode('utf-8', errors='ignore').strip()
            if "PLAY_OK" in line or "Speaker human voice test DONE" in line:
                play_done = True
                break
        time.sleep(0.05)
    
    # 5. Post-interaction celebration / return
    if i % 3 == 0:
        send_cmd("FACE HAPPY_BIG", 0.3)
    else:
        send_cmd("FACE HAPPY", 0.3)
        
    send_cmd("FACE IDLE", 0.2)
    
    # 6. Query Telemetry
    m_lines = send_cmd("METRICS", 0.25)
    m_text = " ".join([l for l in m_lines if "METRICS:" in l])
    if not m_text and m_lines:
        m_text = " ".join(m_lines)
    
    cycle_dur = time.time() - cycle_start
    print(f"  Cycle {i:02d}/20 ({pre_pose:14s} | {bat_cmd:8s}): Dur={cycle_dur:.2f}s | Audio={play_done} | {m_text}")
    results.append({
        "cycle": i,
        "play_done": play_done,
        "metrics": m_text,
        "duration": cycle_dur
    })

# Final reset to default
send_cmd("BAT FULL", 0.2)
send_cmd("FACE IDLE", 0.2)
final_metrics = send_cmd("METRICS", 0.3)
ser.close()

print("\n" + "=" * 70)
print("📊 20 CONSECUTIVE INTERACTION TEST SUMMARY")
print("=" * 70)
print(f"Total cycles completed : {len(results)} / 20")
all_audio_ok = all(r["play_done"] for r in results)
print(f"All Audio Playbacks OK : {all_audio_ok}")
print(f"Final Metrics          : {' '.join(final_metrics)}")
print("=" * 70)

if len(results) == NUM_CYCLES and all_audio_ok:
    sys.exit(0)
else:
    sys.exit(1)
