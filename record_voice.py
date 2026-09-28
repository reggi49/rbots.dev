#!/usr/bin/env python3
"""
record_voice.py - Capture physical microphone audio from Rbot (INMP441) and save to WAV
========================================================================================
Author   : Rbot AI Audio Specialist
Format   : PCM 16-bit Mono @ 16,000 Hz
Hardware : ESP32-S3 (BCLK=4, WS=1, SD=2)
"""

import sys
import os
import time
import glob
import wave
import struct
import argparse
import numpy as np
import serial

def find_rbot_port():
    ports = glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
    return ports[0] if ports else "/dev/cu.usbmodem2101"

def main():
    parser = argparse.ArgumentParser(description="Record audio from Rbot INMP441 Microphone")
    parser.add_argument("--port", default=None, help="Serial port (default: auto-detect)")
    parser.add_argument("--output", default="recorded_rbot.wav", help="Output WAV filename")
    parser.add_argument("--duration", type=float, default=5.0, help="Duration in seconds (default: 5.0)")
    parser.add_argument("--gain", type=float, default=1.0, help="Digital post-gain multiplier (default: 1.0)")
    parser.add_argument("--normalize", action="store_true", help="Auto-normalize voice peak to -3 dBFS")
    parser.add_argument("--play", action="store_true", help="Play audio immediately on Mac after recording")
    args = parser.parse_args()

    port = args.port or find_rbot_port()
    sample_rate = 16000
    expected_samples = int(args.duration * sample_rate)
    expected_bytes = expected_samples * 2

    print(f"===========================================================")
    print(f"🎙️  RBOT INMP441 MICROPHONE RECORDER")
    print(f"===========================================================")
    print(f"[*] Port Serial : {port}")
    print(f"[*] Durasi      : {args.duration:.1f} detik ({expected_samples:,} sample)")
    print(f"[*] Output File : {args.output}")
    print(f"[*] Target Pin  : BCLK=GPIO4, WS=GPIO1, SD=GPIO2")
    print(f"===========================================================\n")

    try:
        ser = serial.Serial(port, 115200, timeout=0.5)
    except Exception as e:
        print(f"[!] Gagal membuka serial port {port}: {e}")
        sys.exit(1)

    time.sleep(0.3)
    ser.reset_input_buffer()

    print("[*] Mengirim perintah 'CMD_RECORD' ke Rbot...")
    ser.write(b"CMD_RECORD\n")

    # Tunggu handshake penanda START_RECORD
    print("[*] Menunggu sinkronisasi 'START_RECORD' dari Rbot...")
    start_wait = time.time()
    sync_ok = False
    while time.time() - start_wait < 5.0:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if "START_RECORD" in line:
            sync_ok = True
            break
        elif line:
            print(f"    [Rbot] {line}")

    if not sync_ok:
        print("[!] Timeout: Rbot tidak merespons dengan START_RECORD.")
        ser.close()
        sys.exit(1)

    print(f"\n🔴 SEDANG MEREKAM! Silakan bicara ke mikrofon INMP441 ({args.duration:.1f} detik)...")

    raw_bytes = bytearray()
    t_start = time.time()
    last_print = t_start

    while len(raw_bytes) < expected_bytes:
        now = time.time()
        if now - t_start > (args.duration + 5.0):
            print("\n[!] Timeout pembacaan data audio.")
            break

        to_read = min(4096, expected_bytes - len(raw_bytes))
        chunk = ser.read(to_read)
        if chunk:
            raw_bytes.extend(chunk)

        if now - last_print >= 0.3:
            last_print = now
            pct = (len(raw_bytes) / expected_bytes) * 100.0
            bars = int(30 * (pct / 100.0))
            bar_str = "█" * bars + "░" * (30 - bars)
            print(f"\r  [{bar_str}] {pct:5.1f}% ({len(raw_bytes):,}/{expected_bytes:,} B)", end="", flush=True)

    print(f"\r  [{'█'*30}] 100.0% ({len(raw_bytes):,}/{expected_bytes:,} B) - Selesai!\n")

    # Verifikasi penanda END_RECORD
    t_end = time.time()
    while time.time() - t_end < 1.0:
        line = ser.readline().decode("utf-8", errors="ignore").strip()
        if "END_RECORD" in line:
            break

    ser.close()

    if len(raw_bytes) % 2 != 0:
        raw_bytes = raw_bytes[:-1]

    samples = np.frombuffer(raw_bytes, dtype=np.int16)
    if len(samples) == 0:
        print("[!] Tidak ada data audio yang tertangkap.")
        sys.exit(1)

    # Penguatan volume digital opsional (Gain / Normalize)
    if args.normalize:
        peak_curr = np.max(np.abs(samples))
        if peak_curr > 0:
            target_peak = 23170.0  # -3 dBFS
            norm_gain = min(target_peak / peak_curr, 16.0)
            samples = np.clip(samples.astype(np.float64) * norm_gain, -32768, 32767).astype(np.int16)
            print(f"[*] Auto-Normalize: {norm_gain:.1f}x gain (Peak: {np.max(np.abs(samples)):,} / 32767)")
    elif args.gain != 1.0:
        samples = np.clip(samples.astype(np.float64) * args.gain, -32768, 32767).astype(np.int16)
        print(f"[*] Digital post-gain diterapkan: {args.gain:.1f}x (+{20*np.log10(args.gain):.1f} dB)")

    # Simpan sebagai WAV standar
    with wave.open(args.output, "wb") as wf:
        wf.setnchannels(1)      # Mono
        wf.setsampwidth(2)      # 16-bit
        wf.setframerate(sample_rate)
        wf.writeframes(samples.tobytes())

    # Metrik audio
    peak = int(np.max(np.abs(samples)))
    rms = float(np.sqrt(np.mean(samples.astype(np.float64)**2)))
    rms_dbfs = 20 * np.log10(rms / 32767.0) if rms > 0 else -120.0
    peak_dbfs = 20 * np.log10(peak / 32767.0) if peak > 0 else -120.0

    print(f"===========================================================")
    print(f"📊 METRIK REKAMAN AUDIO RBOT")
    print(f"===========================================================")
    print(f"✓ File Tersimpan  : {args.output} ({os.path.getsize(args.output):,} bytes)")
    print(f"✓ Total Sample    : {len(samples):,} ({len(samples)/sample_rate:.2f} s)")
    print(f"✓ Peak Amplitudo  : {peak:,} / 32767 ({peak_dbfs:.2f} dBFS)")
    print(f"✓ RMS Sinyal      : {rms:.1f} ({rms_dbfs:.2f} dBFS)")
    print(f"===========================================================")

    if rms < 50:
        print("⚠️  Sinyal sangat hening. Jika Anda sedang berbicara, periksa kabel INMP441 (BCLK=4, WS=1, SD=2).")
    else:
        print("🎉 Rekaman suara berhasil ditangkap dengan jelas oleh mikrofon INMP441!")

    if args.play:
        print("\n🔊 Memutar hasil rekaman di speaker Mac...")
        os.system(f"afplay '{args.output}'")
    else:
        print(f"\n💡 Untuk mendengarkan rekaman ini di Mac, jalankan:\n   afplay {args.output}")

if __name__ == "__main__":
    main()
