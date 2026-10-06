import serial
import time
import sys

PORT = '/dev/cu.usbmodem5A790640561'
BAUD = 115200

print("=" * 65)
print("🤖 RBOT 20 CONSECUTIVE INTERACTION STABILITY & TELEMETRY TEST")
print("=" * 65)

ser = serial.Serial(PORT, BAUD, timeout=1)
time.sleep(1.5)

# Clear any initial buffer
ser.reset_input_buffer()

def send_cmd(cmd, wait_time=0.4):
    ser.write((cmd + '\n').encode('utf-8'))
    ser.flush()
    time.sleep(wait_time)
    lines = []
    while ser.in_waiting:
        line = ser.readline().decode('utf-8', errors='ignore').strip()
        if line:
            lines.append(line)
    return lines

# 0. Initial baseline telemetry
send_cmd("PING")
metrics_lines = send_cmd("METRICS")
print(f"[*] Initial Baseline Metrics: {metrics_lines}")

results = []
NUM_CYCLES = 20

print(f"\n[*] Starting {NUM_CYCLES} consecutive interaction cycles...\n")

for i in range(1, NUM_CYCLES + 1):
    cycle_start = time.time()
    
    # 1. State: LISTENING
    send_cmd("FACE LISTEN", 0.25)
    
    # 2. State: THINKING
    send_cmd("FACE THINK", 0.3)
    
    # 3. Audio Playback with animated SPEAKING mouth
    # Run PLAY command
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
    
    # 4. Trigger Happy
    send_cmd("FACE HAPPY", 0.4)
    
    # 5. Return to Idle
    send_cmd("FACE IDLE", 0.2)
    
    # 6. Query Telemetry
    m_lines = send_cmd("METRICS", 0.3)
    m_text = " ".join(m_lines)
    
    cycle_dur = time.time() - cycle_start
    print(f"  Cycle {i:02d}/20: Duration={cycle_dur:.2f}s | PLAY_OK={play_done} | {m_text}")
    results.append({
        "cycle": i,
        "play_done": play_done,
        "metrics": m_text,
        "duration": cycle_dur
    })

ser.close()

print("\n" + "=" * 65)
print(f"✅ ALL {NUM_CYCLES} CONSECUTIVE INTERACTION TESTS COMPLETED!")
print("=" * 65)
