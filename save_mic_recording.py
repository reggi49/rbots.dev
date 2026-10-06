#!/usr/bin/env python3
"""
save_mic_recording.py - Merekam dari INMP441 mic dan menyimpan ke file WAV di komputer
========================================================================================
Usage:  python save_mic_recording.py [--port PORT] [--duration 3] [--output suaraku.wav]

Berbeda dengan parrot_voice.py:
  - parrot_voice.py: Merekam di ESP32 → memutar dari ESP32 (tidak ke komputer)
  - save_mic_recording.py: Merekam di ESP32 → transfer ke komputer → simpan WAV
"""
import sys
import time
import glob
import wave
import struct
import serial

BAUD = 115200


def find_rbot_port():
    ports = sorted(glob.glob("/dev/cu.usbmodem*") + glob.glob("/dev/cu.usbserial*")
                   + glob.glob("/dev/cu.wchusbserial*"))
    return ports[0] if ports else "/dev/cu.usbmodem5A790640561"


def open_port(port):
    s = serial.Serial()
    s.port = port
    s.baudrate = BAUD
    s.timeout = 0.1
    s.dtr = False
    s.rts = False
    s.open()
    return s


def readline(s):
    raw = s.readline()
    if not raw:
        return None
    return raw.decode(errors="replace").strip()


def wait_for_pong(s, timeout=10.0):
    """Send PING until the firmware answers PONG."""
    t0 = time.time()
    last_ping = 0.0
    while time.time() - t0 < timeout:
        if time.time() - last_ping > 1.0:
            s.write(b"PING\\r\\n")
            s.flush()
            last_ping = time.time()
        line = readline(s)
        if line and "PONG" in line:
            return True
    return False


def record_to_file(s, duration_sec, output_wav):
    """Kirim perintah RECORD ke ESP32, terima data PCM binary, simpan ke WAV."""
    sample_rate = 16000
    total_samples = duration_sec * sample_rate
    expected_bytes = total_samples * 2  # 16-bit samples
    
    print(f"\n📝 Mengirim perintah ke ESP32 (durasi: {duration_sec} detik)...")
    s.reset_input_buffer()
    
    # Kirim perintah (sesuai dengan board.c: RECORD atau CMD_RECORD)
    cmd = f"RECORD {duration_sec}\r\n"
    s.write(cmd.encode())
    s.flush()
    
    # Tunggu marker START_RECORD
    print("⏳ Menunggu respons START_RECORD dari ESP32...")
    t0 = time.time()
    sync_ok = False
    while time.time() - t0 < 5.0:
        line = readline(s)
        if not line:
            continue
        if "START_RECORD" in line:
            sync_ok = True
            break
        print(f"  [ESP32] {line}")
        
    if not sync_ok:
        print("\n❌ Timeout: tidak ada START_RECORD dari ESP32")
        return False
        
    print(f"\n🔴 SEDANG MEREKAM! Silakan bicara ke mic INMP441 ({duration_sec} detik)...")
    
    # Tunggu sinyal STREAM_START (ESP32 selesai merekam ke PSRAM dan mulai kirim audio)
    t_rec_start = time.time()
    stream_started = False
    while time.time() - t_rec_start < (duration_sec + 3.0):
        line = readline(s)
        if line and "STREAM_START" in line:
            stream_started = True
            break
        # Tampilkan sisa detik perekaman
        elapsed = time.time() - t_rec_start
        rem = max(0, int(duration_sec - elapsed))
        print(f"  🎙️  Merekam suara... sisa {rem} detik", end="\r", flush=True)
        time.sleep(0.05)
        
    print(f"\n\n📥 Mengunduh data audio dari ESP32 ({expected_bytes:,} bytes)...")
    
    # Baca data raw binary PCM 16-bit yang dikirim ESP32 dari PSRAM
    pcm_data = bytearray()
    t_start = time.time()
    # Timeout transfer (160KB @ 11.5KB/s = ~14 detik + 5 detik buffer)
    transfer_timeout = (expected_bytes / 8000.0) + 10.0
    
    while len(pcm_data) < expected_bytes and (time.time() - t_start) < transfer_timeout:
        to_read = min(4096, expected_bytes - len(pcm_data))
        chunk = s.read(to_read)
        if chunk:
            pcm_data.extend(chunk)
            pct = (len(pcm_data) / expected_bytes) * 100
            print(f"  [Transfer] {len(pcm_data):,} / {expected_bytes:,} bytes ({pct:.1f}%)", end="\r", flush=True)
            
    print(f"\n\n✅ Pengambilan audio selesai: {len(pcm_data):,} bytes diterima.")
    
    if len(pcm_data) < 2000:
        print("❌ Data audio terlalu sedikit!")
        return False
        
    # Hitung RMS dan Peak untuk verifikasi apakah mic benar merekam suara
    samples = struct.unpack(f"<{len(pcm_data)//2}h", pcm_data[:len(pcm_data) - (len(pcm_data)%2)])
    peak = max(abs(s) for s in samples) if samples else 0
    rms = int((sum(s*s for s in samples) / len(samples)) ** 0.5) if samples else 0
    
    print(f"📊 Statistik Audio:")
    print(f"   - RMS  : {rms}")
    print(f"   - Peak : {peak} / 32767")
    if rms < 50:
        print("   ⚠️  Peringatan: Level suara sangat rendah! Periksa wiring INMP441:")
        print("       WS=GPIO1, SCK/BCLK=GPIO4, SD=GPIO2, L/R=GND, VDD=3.3V")
    else:
        print("   🎉 Sinyal terdeteksi! Mic menangkap suara.")
        
    # Simpan file WAV
    with wave.open(output_wav, 'wb') as wf:
        wf.setnchannels(1)
        wf.setsampwidth(2)
        wf.setframerate(sample_rate)
        wf.writeframes(pcm_data)
        
    print(f"💾 File WAV tersimpan di: {output_wav}")
    return True


def main():
    import argparse
    parser = argparse.ArgumentParser(description="Rekam dari INMP441 mic dan simpan ke WAV")
    parser.add_argument("--port", default=None, help="Serial port")
    parser.add_argument("--duration", type=int, default=3, help="Durasi rekaman (detik)")
    parser.add_argument("--output", default="suaraku.wav", help="Output WAV file")
    args = parser.parse_args()
    
    port = args.port or find_rbot_port()
    
    print("=" * 70)
    print("🎙️  RBOT MIC RECORDER - Simpan ke Komputer")
    print("=" * 70)
    print(f"Port    : {port} @ {BAUD}")
    print(f"Durasi  : {args.duration} detik")
    print(f"Output  : {args.output}")
    print("=" * 70)
    
    try:
        s = open_port(port)
    except Exception as e:
        print(f"❌ Cannot open {port}: {e}")
        sys.exit(1)
    
    print("\\nConnecting to Rbot (PING)...", end=" ", flush=True)
    if not wait_for_pong(s):
        print("FAILED")
        print("❌ No PONG from Rbot. Check firmware and port.")
        s.close()
        sys.exit(1)
    print("OK ✅")
    
    try:
        success = record_to_file(s, args.duration, args.output)
        if success:
            print(f"\\n🎉 Selesai! File tersimpan: {args.output}")
            print(f"\\n💡 Untuk putar audio:")
            print(f"   afplay {args.output}")
        else:
            print("\\n⚠️  Recording gagal atau tidak lengkap")
    except KeyboardInterrupt:
        print("\\n\\n⚠️  Dibatalkan oleh user")
    finally:
        s.close()


if __name__ == "__main__":
    main()
