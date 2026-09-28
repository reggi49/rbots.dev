#!/usr/bin/env python3
"""
audio_audit.py - ESP32-S3 + INMP441 I2S Audio Quality & DSP Validation Tool
=============================================================================
Author   : Senior Audio DSP Engineer & Automation Specialist
Platform : macOS (Apple Silicon / Intel)
Hardware : ESP32-S3 (USB CDC/JTAG or Serial) + INMP441 MEMS Microphone
Format   : PCM 16-bit Mono @ 16,000 Hz (Edge Impulse / TinyML Standard)
"""

import os
import sys
import time
import math
import wave
import struct
import argparse
from dataclasses import dataclass, field
from typing import List, Dict, Optional, Tuple

import numpy as np
import scipy.signal
import matplotlib
matplotlib.use("Agg")  # Non-interactive backend for automation / macOS CLI
import matplotlib.pyplot as plt

try:
    import serial
    import serial.tools.list_ports
except ImportError:
    serial = None


# ─────────────────────────────────────────────────────────────────────────────
# Data Structures
# ─────────────────────────────────────────────────────────────────────────────
@dataclass
class AuditConfig:
    port: Optional[str] = None
    baudrate: int = 921600
    sample_rate: int = 16000
    bit_depth: int = 16
    channels: int = 1
    duration_sec: float = 5.0
    output_wav: str = "output_test.wav"
    output_plot: str = "signal_audit.png"
    trigger_cmd: str = "CMD_RECORD\n"
    handshake_timeout: float = 6.0
    read_timeout: float = 10.0
    # Thresholds
    noise_floor_pass_dbfs: float = -48.0
    noise_floor_warn_dbfs: float = -40.0
    dc_offset_max_pct: float = 1.0
    spike_min_freq_hz: float = 4000.0
    spike_prominence_db: float = 12.0
    spike_power_thresh_dbfs: float = -65.0
    # Simulation / Mock
    mock_mode: bool = False
    mock_scenario: str = "clean"  # "clean", "noisy", "dc_offset", "harmonic_spikes"


@dataclass
class SpectralSpike:
    frequency_hz: float
    power_dbfs: float
    prominence_db: float


@dataclass
class AuditMetrics:
    sample_count: int = 0
    duration_sec: float = 0.0
    rms_raw: float = 0.0
    noise_floor_dbfs: float = -120.0
    peak_raw: int = 0
    peak_dbfs: float = -120.0
    dc_offset_raw: float = 0.0
    dc_offset_pct: float = 0.0
    dynamic_range_db: float = 0.0
    snr_estimate_db: float = 0.0
    high_freq_spikes: List[SpectralSpike] = field(default_factory=list)
    dominant_spike_hz: float = 0.0
    dominant_spike_dbfs: float = -120.0
    psd_freqs: np.ndarray = field(default_factory=lambda: np.array([]))
    psd_dbfs: np.ndarray = field(default_factory=lambda: np.array([]))
    verdict_noise_floor: str = "PASS"
    verdict_dc_offset: str = "PASS"
    verdict_spikes: str = "PASS"
    overall_verdict: str = "PASS"
    diagnostic_notes: List[str] = field(default_factory=list)


# ─────────────────────────────────────────────────────────────────────────────
# 1. macOS Serial Port Auto-Detection
# ─────────────────────────────────────────────────────────────────────────────
def detect_esp32_port(preferred_port: Optional[str] = None) -> str:
    """
    Detects ESP32-S3 serial port on macOS dynamically.
    Prioritizes /dev/cu.usbmodem* (ESP32-S3 native USB-CDC/JTAG)
    followed by /dev/cu.usbserial* (UART bridge chips like CP2102/CH340).
    """
    if preferred_port:
        if os.path.exists(preferred_port):
            return preferred_port
        print(f"[WARN] Port spesifik '{preferred_port}' tidak ditemukan di filesystem.", file=sys.stderr)

    if serial is None:
        raise RuntimeError("Library 'pyserial' belum terinstal. Jalankan: pip install pyserial")

    ports = list(serial.tools.list_ports.comports())
    if not ports:
        raise DeviceNotFoundError(
            "Tidak ditemukan port serial yang terhubung! Pastikan ESP32-S3 terhubung via kabel USB data."
        )

    print("\n🔍 Memindai port serial di macOS...")
    candidates = []

    # Espressif USB VID = 0x303a
    ESPRESSIF_VID = 0x303A

    for p in ports:
        device_path = p.device
        desc = p.description or ""
        vid = p.vid
        pid = p.pid

        # Scoring heuristics
        score = 0
        is_cu_modem = "cu.usbmodem" in device_path
        is_cu_serial = "cu.usbserial" in device_path

        if is_cu_modem:
            score += 50
        elif is_cu_serial:
            score += 30

        if vid == ESPRESSIF_VID:
            score += 100
        elif "ESP32" in desc.upper() or "USB JTAG" in desc.upper():
            score += 80
        elif "CP210" in desc.upper() or "CH340" in desc.upper() or "FT232" in desc.upper():
            score += 40

        if score > 0:
            candidates.append((score, device_path, desc, vid, pid))
            vid_str = f"0x{vid:04X}" if vid else "N/A"
            pid_str = f"0x{pid:04X}" if pid else "N/A"
            print(f"  • {device_path:<26} | VID:PID={vid_str}:{pid_str} | Desc: {desc} (Score: {score})")

    if not candidates:
        all_devices = [p.device for p in ports]
        raise DeviceNotFoundError(
            f"Tidak ditemukan port serial ESP32 yang valid. Perangkat terdeteksi: {all_devices}"
        )

    # Sort by highest score first
    candidates.sort(key=lambda x: x[0], reverse=True)
    selected_port = candidates[0][1]
    print(f"✅ Port terpilih: {selected_port}\n")
    return selected_port


class DeviceNotFoundError(Exception):
    pass


class SyncTimeoutError(Exception):
    pass


# ─────────────────────────────────────────────────────────────────────────────
# 2. Serial Synchronization & Binary PCM Recording
# ─────────────────────────────────────────────────────────────────────────────
def capture_serial_audio(cfg: AuditConfig) -> np.ndarray:
    """
    Connects to ESP32-S3 via Serial, initiates handshake, and captures raw 16-bit PCM mono stream.
    Expected protocol:
      1. Host sends: `CMD_RECORD\\n`
      2. ESP32 responds: (Optional logs) followed by `START_RECORD\\n`
      3. ESP32 streams raw PCM 16-bit signed LE mono data
      4. ESP32 ends with: `END_RECORD\\n`
    """
    if cfg.mock_mode:
        print(f"[MOCK] Menghasilkan audio sintetis skenario: '{cfg.mock_scenario}'...")
        return generate_synthetic_audio(cfg)

    port = cfg.port or detect_esp32_port()
    total_samples = int(cfg.duration_sec * cfg.sample_rate)
    expected_payload_bytes = total_samples * (cfg.bit_depth // 8) * cfg.channels

    print(f"📡 Membuka koneksi serial ke {port} @ {cfg.baudrate} baud...")
    
    try:
        ser = serial.Serial(
            port=port,
            baudrate=cfg.baudrate,
            timeout=1.0,
            write_timeout=2.0
        )
    except serial.SerialException as e:
        raise RuntimeError(f"Gagal membuka port serial {port}: {e}")

    try:
        # Menstabilkan DTR/RTS di macOS agar tidak mereset ESP32 secara tidak disengaja
        ser.dtr = False
        ser.rts = False
        time.sleep(0.3)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        print(f"👉 Mengirim perintah trigger: {cfg.trigger_cmd.strip()}...")
        ser.write(cfg.trigger_cmd.encode("utf-8"))
        ser.flush()

        print("⏳ Menunggu sinkronisasi frame 'START_RECORD' dari ESP32...")
        start_sync_time = time.time()
        start_record_found = False

        while time.time() - start_sync_time < cfg.handshake_timeout:
            line = ser.readline()
            if not line:
                continue
            try:
                line_str = line.decode("utf-8", errors="ignore").strip()
            except Exception:
                continue

            if line_str:
                print(f"  [ESP32] {line_str}")

            if "START_RECORD" in line_str:
                start_record_found = True
                print("🎯 Penanda 'START_RECORD' terdeteksi! Memulai penangkapan stream PCM...")
                break

        if not start_record_found:
            raise SyncTimeoutError(
                f"Timeout ({cfg.handshake_timeout}s): ESP32 tidak mengirimkan penanda 'START_RECORD'."
            )

        # Merekam raw payload PCM
        raw_pcm_bytes = bytearray()
        record_start_time = time.time()
        last_progress_time = record_start_time

        print(f"🎙️  Menangkap {expected_payload_bytes:,} bytes audio ({cfg.duration_sec}s @ {cfg.sample_rate}Hz 16-bit)...")

        while len(raw_pcm_bytes) < expected_payload_bytes:
            now = time.time()
            if now - record_start_time > cfg.read_timeout:
                print(f"\n[WARN] Timeout pembacaan data serial tercapai ({cfg.read_timeout}s).", file=sys.stderr)
                break

            bytes_to_read = min(4096, expected_payload_bytes - len(raw_pcm_bytes))
            chunk = ser.read(bytes_to_read)
            if chunk:
                raw_pcm_bytes.extend(chunk)

            # Update progress bar
            if now - last_progress_time >= 0.5:
                last_progress_time = now
                pct = (len(raw_pcm_bytes) / expected_payload_bytes) * 100.0
                bar_len = 30
                filled = int(bar_len * (pct / 100.0))
                bar = "█" * filled + "░" * (bar_len - filled)
                print(f"\r  [{bar}] {pct:5.1f}% ({len(raw_pcm_bytes):,}/{expected_payload_bytes:,} bytes)", end="", flush=True)

        print(f"\r  [{'█'*30}] 100.0% ({len(raw_pcm_bytes):,}/{expected_payload_bytes:,} bytes) - Selesai!\n")

        # Cek penanda END_RECORD
        end_wait_start = time.time()
        while time.time() - end_wait_start < 1.0:
            tail_line = ser.readline().decode("utf-8", errors="ignore").strip()
            if "END_RECORD" in tail_line:
                print(f"🏁 Penanda 'END_RECORD' berhasil diverifikasi.")
                break

    finally:
        if ser.is_open:
            ser.close()
            print(f"🔒 Port serial {port} telah ditutup dengan aman.")

    if len(raw_pcm_bytes) < 3200:
        raise ValueError(f"Ukuran payload audio terlalu kecil ({len(raw_pcm_bytes)} bytes) untuk dianalisis.")

    # Potong jika byte ganjil (16-bit butuh 2 byte per sample)
    if len(raw_pcm_bytes) % 2 != 0:
        raw_pcm_bytes = raw_pcm_bytes[:-1]

    # Konversi ke NumPy array int16 Little-Endian
    samples = np.frombuffer(raw_pcm_bytes, dtype=np.int16)
    return samples


def save_wav_file(samples: np.ndarray, file_path: str, sample_rate: int = 16000) -> None:
    """
    Saves raw int16 PCM samples as standard RIFF/WAV format.
    """
    with wave.open(file_path, "wb") as wf:
        wf.setnchannels(1)       # Mono
        wf.setsampwidth(2)       # 16-bit (2 bytes)
        wf.setframerate(sample_rate)
        wf.writeframes(samples.tobytes())
    file_size = os.path.getsize(file_path)
    print(f"💾 File audio tersimpan: '{file_path}' ({file_size:,} bytes, {len(samples)/sample_rate:.2f} detik)")


# ─────────────────────────────────────────────────────────────────────────────
# 3. DSP Metrics Analysis
# ─────────────────────────────────────────────────────────────────────────────
def analyze_audio_dsp(samples: np.ndarray, cfg: AuditConfig) -> AuditMetrics:
    """
    Executes professional audio DSP measurements:
      - Noise Floor (dBFS) via RMS in silence
      - DC Offset (% Full Scale)
      - Power Spectral Density (PSD) & FFT Artifact Detection (> 4 kHz)
      - Dynamic Range / SNR estimation
      - Pass/Warning/Fail criteria evaluation
    """
    metrics = AuditMetrics()
    n_samples = len(samples)
    metrics.sample_count = n_samples
    metrics.duration_sec = n_samples / cfg.sample_rate

    # Convert to float array for numerical precision
    s_float = samples.astype(np.float64)

    # 1. DC Offset Analysis
    # Formula: Mean amplitude / Full-Scale (32768.0) * 100%
    dc_offset_raw = float(np.mean(s_float))
    dc_offset_pct = (abs(dc_offset_raw) / 32768.0) * 100.0
    metrics.dc_offset_raw = dc_offset_raw
    metrics.dc_offset_pct = dc_offset_pct

    # 2. RMS & Noise Floor (dBFS)
    # Formula: 20 * log10(RMS / 32767.0)
    rms_raw = float(np.sqrt(np.mean(s_float ** 2)))
    metrics.rms_raw = rms_raw

    # Peak amplitude
    peak_raw = int(np.max(np.abs(samples)))
    metrics.peak_raw = peak_raw

    eps = 1e-12
    if rms_raw > eps:
        noise_floor_dbfs = 20.0 * math.log10(rms_raw / 32767.0)
    else:
        noise_floor_dbfs = -120.0
    metrics.noise_floor_dbfs = noise_floor_dbfs

    if peak_raw > 0:
        peak_dbfs = 20.0 * math.log10(peak_raw / 32767.0)
    else:
        peak_dbfs = -120.0
    metrics.peak_dbfs = peak_dbfs

    # 3. Dynamic Range & SNR Estimator
    # Pada uji hening (silence test), dynamic range teoretis ke full-scale adalah |Noise Floor|
    metrics.dynamic_range_db = abs(noise_floor_dbfs)
    metrics.snr_estimate_db = peak_dbfs - noise_floor_dbfs

    # 4. Frequency Domain & Artifact Detection (> 4 kHz)
    # Gunakan Welch's Power Spectral Density dengan Hann window (scaling='spectrum' -> 0 dBFS peak ref)
    # Normalisasi sinyal ke range [-1.0, 1.0]
    s_norm = s_float / 32768.0
    nperseg = min(2048, n_samples)
    freqs, psd = scipy.signal.welch(
        s_norm,
        fs=cfg.sample_rate,
        window="hann",
        nperseg=nperseg,
        scaling="spectrum"
    )

    # Konversi ke dBFS
    psd_dbfs = 10.0 * np.log10(psd + 1e-15)
    metrics.psd_freqs = freqs
    metrics.psd_dbfs = psd_dbfs

    # Filter frekuensi di atas 4 kHz
    high_freq_mask = freqs >= cfg.spike_min_freq_hz
    hf_freqs = freqs[high_freq_mask]
    hf_psd = psd_dbfs[high_freq_mask]

    detected_spikes: List[SpectralSpike] = []
    if len(hf_psd) > 5:
        # Hitung median noise level di band frekuensi tinggi
        hf_median_noise = float(np.median(hf_psd))

        # Deteksi peak anomalik
        # Kriteria: memiliki prominence relatif terhadap noise sekitar
        peaks, properties = scipy.signal.find_peaks(
            hf_psd,
            prominence=cfg.spike_prominence_db,
            height=cfg.spike_power_thresh_dbfs
        )

        for p_idx in peaks:
            f_val = float(hf_freqs[p_idx])
            p_val = float(hf_psd[p_idx])
            prom = float(properties["prominences"][np.where(peaks == p_idx)[0][0]])
            detected_spikes.append(SpectralSpike(frequency_hz=f_val, power_dbfs=p_val, prominence_db=prom))

    metrics.high_freq_spikes = detected_spikes

    if detected_spikes:
        # Ambil spike terkuat
        strongest = max(detected_spikes, key=lambda s: s.power_dbfs)
        metrics.dominant_spike_hz = strongest.frequency_hz
        metrics.dominant_spike_dbfs = strongest.power_dbfs
    else:
        metrics.dominant_spike_hz = 0.0
        metrics.dominant_spike_dbfs = -120.0

    # 5. Pass/Fail Decision Logic
    # Kriteria Noise Floor:
    # PASS: <= -48 dBFS
    # WARNING: -48 dBFS s.d. -40 dBFS
    # FAIL: > -40 dBFS
    if noise_floor_dbfs <= cfg.noise_floor_pass_dbfs:
        metrics.verdict_noise_floor = "PASS"
    elif noise_floor_dbfs <= cfg.noise_floor_warn_dbfs:
        metrics.verdict_noise_floor = "WARNING"
        metrics.diagnostic_notes.append(
            f"Noise floor ({noise_floor_dbfs:.1f} dBFS) agak tinggi. Pastikan mikrofon berada di lingkungan hening saat kalibrasi."
        )
    else:
        metrics.verdict_noise_floor = "FAIL"
        metrics.diagnostic_notes.append(
            f"Noise floor ({noise_floor_dbfs:.1f} dBFS) melebihi batas toleransi (-40 dBFS). Periksa grounding L/R pin dan decoupling VDD INMP441."
        )

    # Kriteria DC Offset:
    # PASS: < 1.0%
    # FAIL: >= 1.0%
    if dc_offset_pct < cfg.dc_offset_max_pct:
        metrics.verdict_dc_offset = "PASS"
    else:
        metrics.verdict_dc_offset = "FAIL"
        metrics.diagnostic_notes.append(
            f"DC Offset ({dc_offset_pct:.2f}% FS) melebihi 1.0%. Diperlukan IIR High-Pass Filter (cut-off ~20-50 Hz) pada DSP firmware."
        )

    # Kriteria Spike Frekuensi Tinggi:
    if detected_spikes:
        # Jika ada spike dengan power > -55 dBFS atau sangat prominen (> 18 dB)
        severe_spikes = [s for s in detected_spikes if s.power_dbfs > -55.0 or s.prominence_db >= 18.0]
        if severe_spikes:
            metrics.verdict_spikes = "FAIL"
            metrics.diagnostic_notes.append(
                f"Terdeteksi {len(severe_spikes)} harmonic spike tajam >4 kHz (puncak: {metrics.dominant_spike_hz:.1f} Hz @ {metrics.dominant_spike_dbfs:.1f} dBFS). "
                "Mengindikasikan switching noise regulator DC-DC, ripple 5V USB, atau I2S clock jitter/slip."
            )
        else:
            metrics.verdict_spikes = "WARNING"
            metrics.diagnostic_notes.append(
                f"Terdeteksi spike minor >4 kHz (puncak: {metrics.dominant_spike_hz:.1f} Hz @ {metrics.dominant_spike_dbfs:.1f} dBFS). Disarankan menambah kapasitor decoupling 100nF paralel 10uF di pin VDD-GND mic."
            )
    else:
        metrics.verdict_spikes = "PASS"

    # Evaluasi Keseluruhan
    verdicts = [metrics.verdict_noise_floor, metrics.verdict_dc_offset, metrics.verdict_spikes]
    if "FAIL" in verdicts:
        metrics.overall_verdict = "FAIL"
    elif "WARNING" in verdicts:
        metrics.overall_verdict = "WARNING"
    else:
        metrics.overall_verdict = "PASS"

    return metrics


# ─────────────────────────────────────────────────────────────────────────────
# 4. Professional Visualization (Time & Frequency Domain)
# ─────────────────────────────────────────────────────────────────────────────
def generate_audit_plots(samples: np.ndarray, metrics: AuditMetrics, cfg: AuditConfig) -> None:
    """
    Renders a high-resolution, dual-subplot engineering figure:
      - Subplot 1 (Top): Time Domain Waveform & DC Bias line
      - Subplot 2 (Bottom): Frequency Domain Welch PSD with high-freq artifact highlight zone
    """
    plt.style.use("seaborn-v0_8-whitegrid" if "seaborn-v0_8-whitegrid" in plt.style.available else "default")
    fig, (ax_time, ax_freq) = plt.subplots(2, 1, figsize=(12, 8), dpi=150)
    fig.patch.set_facecolor("#FAFAFC")

    # Waktu
    time_axis = np.linspace(0.0, metrics.duration_sec, len(samples), endpoint=False)

    # ── Subplot 1: Time Domain ───────────────────────────────────────────────
    ax_time.set_facecolor("#FFFFFF")
    # Tampilkan dalam normalisasi -1.0 s.d. +1.0
    norm_samples = samples / 32768.0
    ax_time.plot(time_axis, norm_samples, color="#1F77B4", alpha=0.75, linewidth=0.6, label="PCM Waveform")

    # Garis DC Offset
    dc_norm = metrics.dc_offset_raw / 32768.0
    ax_time.axhline(0.0, color="#7F7F7F", linestyle="--", linewidth=0.8, alpha=0.6)
    ax_time.axhline(dc_norm, color="#D62728", linestyle=":", linewidth=1.2,
                    label=f"DC Offset: {metrics.dc_offset_raw:.1f} ({metrics.dc_offset_pct:.2f}%)")

    # Moving RMS Envelope
    frame_len = int(cfg.sample_rate * 0.05)  # 50 ms window
    if frame_len > 1 and len(norm_samples) > frame_len:
        hop = frame_len // 2
        rms_env = [
            np.sqrt(np.mean(norm_samples[i : i + frame_len] ** 2))
            for i in range(0, len(norm_samples) - frame_len, hop)
        ]
        rms_time = [time_axis[i + frame_len // 2] for i in range(0, len(norm_samples) - frame_len, hop)]
        ax_time.plot(rms_time, rms_env, color="#FF7F0E", linewidth=1.5, alpha=0.9, label="RMS Envelope")
        ax_time.plot(rms_time, -np.array(rms_env), color="#FF7F0E", linewidth=1.5, alpha=0.9)

    ax_time.set_title("Time Domain: PCM Audio Waveform & DC Bias Profile", fontsize=12, fontweight="bold", pad=8)
    ax_time.set_xlabel("Time (seconds)", fontsize=10)
    ax_time.set_ylabel("Normalized Amplitude (FS)", fontsize=10)
    ax_time.set_xlim(0, metrics.duration_sec)
    ax_time.set_ylim(-1.05, 1.05)
    ax_time.legend(loc="upper right", frameon=True, fontsize=9)
    ax_time.grid(True, linestyle="--", alpha=0.5)

    # ── Subplot 2: Frequency Domain (Welch PSD) ──────────────────────────────
    ax_freq.set_facecolor("#FFFFFF")
    freqs = metrics.psd_freqs
    psd_dbfs = metrics.psd_dbfs

    ax_freq.plot(freqs, psd_dbfs, color="#2CA02C", linewidth=1.0, label="Power Spectral Density (Welch)")

    # Highlight zona inspeksi > 4 kHz
    ax_freq.axvspan(cfg.spike_min_freq_hz, cfg.sample_rate / 2.0, color="#FF9896", alpha=0.2,
                    label="Inspection Zone (> 4 kHz, Switching Noise / Jitter)")

    # Ambang batas Noise Floor target (-48 dBFS)
    ax_freq.axhline(cfg.noise_floor_pass_dbfs, color="#17BECF", linestyle="--", linewidth=1.2,
                    label=f"Pass Target ({cfg.noise_floor_pass_dbfs:.0f} dBFS)")

    # Tandai spike frekuensi tinggi yang terdeteksi
    if metrics.high_freq_spikes:
        spike_f = [s.frequency_hz for s in metrics.high_freq_spikes]
        spike_p = [s.power_dbfs for s in metrics.high_freq_spikes]
        ax_freq.scatter(spike_f, spike_p, color="#D62728", s=60, zorder=5, marker="^", label="Detected Spikes")

        # Label untuk spike dominan
        if metrics.dominant_spike_hz > 0:
            ax_freq.annotate(
                f"Spike: {metrics.dominant_spike_hz:.0f} Hz\n({metrics.dominant_spike_dbfs:.1f} dBFS)",
                xy=(metrics.dominant_spike_hz, metrics.dominant_spike_dbfs),
                xytext=(metrics.dominant_spike_hz - 1200, metrics.dominant_spike_dbfs + 10),
                arrowprops=dict(facecolor="#D62728", shrink=0.08, width=1, headwidth=6),
                fontsize=8,
                fontweight="bold",
                color="#8C1D18",
                bbox=dict(boxstyle="round,pad=0.2", facecolor="#FFEEEE", edgecolor="#D62728", alpha=0.9)
            )

    ax_freq.set_title("Frequency Domain: Power Spectral Density (PSD) & Artifact Detection", fontsize=12, fontweight="bold", pad=8)
    ax_freq.set_xlabel("Frequency (Hz)", fontsize=10)
    ax_freq.set_ylabel("Magnitude (dBFS)", fontsize=10)
    ax_freq.set_xlim(0, cfg.sample_rate / 2.0)
    ax_freq.set_ylim(max(-120.0, np.min(psd_dbfs) - 10.0), max(-20.0, np.max(psd_dbfs) + 10.0))
    ax_freq.legend(loc="upper right", frameon=True, fontsize=9)
    ax_freq.grid(True, linestyle="--", alpha=0.5)

    # ── Header Banner & Verdict Badge ────────────────────────────────────────
    badge_colors = {
        "PASS": ("#E8F5E9", "#2E7D32"),
        "WARNING": ("#FFF8E1", "#F57F17"),
        "FAIL": ("#FFEBEE", "#C62828")
    }
    bg_col, txt_col = badge_colors.get(metrics.overall_verdict, ("#EEEEEE", "#222222"))

    summary_text = (
        f"Verdict: {metrics.overall_verdict}  |  "
        f"Noise Floor: {metrics.noise_floor_dbfs:.1f} dBFS  |  "
        f"DC Offset: {metrics.dc_offset_pct:.2f}%  |  "
        f"Dynamic Range: {metrics.dynamic_range_db:.1f} dB  |  "
        f"Spikes >4kHz: {len(metrics.high_freq_spikes)}"
    )

    fig.suptitle(
        f"ESP32-S3 + INMP441 Audio Quality Audit (16kHz / 16-bit Mono)\n{summary_text}",
        fontsize=13,
        fontweight="bold",
        color=txt_col,
        y=0.98
    )

    plt.tight_layout(rect=[0, 0.02, 1, 0.94])
    plt.savefig(cfg.output_plot, dpi=200, facecolor=fig.get_facecolor())
    plt.close(fig)
    print(f"📊 Grafik audit tersimpan: '{cfg.output_plot}' (200 DPI)")


# ─────────────────────────────────────────────────────────────────────────────
# 5. Formatted Terminal Console Reporting
# ─────────────────────────────────────────────────────────────────────────────
def print_audit_report(metrics: AuditMetrics, cfg: AuditConfig) -> None:
    """
    Prints a clean, colorized ANSI diagnostic report to the terminal.
    """
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    RED = "\033[91m"
    CYAN = "\033[96m"
    BOLD = "\033[1m"
    RESET = "\033[0m"

    def colorize_verdict(v: str) -> str:
        if v == "PASS":
            return f"{GREEN}{BOLD}PASS{RESET}"
        elif v == "WARNING":
            return f"{YELLOW}{BOLD}WARNING{RESET}"
        return f"{RED}{BOLD}FAIL{RESET}"

    sep = "═" * 74
    thin_sep = "─" * 74

    print(f"\n{BOLD}{CYAN}{sep}{RESET}")
    print(f"{BOLD}{CYAN}      ESP32-S3 + INMP441 AUDIO QUALITY & DSP AUDIT REPORT{RESET}")
    print(f"{BOLD}{CYAN}{sep}{RESET}")
    print(f" Target Standard     : 16,000 Hz, 16-bit PCM Mono (Edge Impulse / TinyML)")
    print(f" Total Samples       : {metrics.sample_count:,} samples ({metrics.duration_sec:.2f} s)")
    print(f" Audio WAV Artifact  : {cfg.output_wav}")
    print(f" Spectrogram Plot    : {cfg.output_plot}")
    print(f"{thin_sep}")
    print(f" {'PARAMETER':<25} | {'NILAI UKUR':<16} | {'TARGET AMBANG':<15} | {'STATUS':<8}")
    print(f"{thin_sep}")

    # Row 1: Noise Floor
    nf_status = colorize_verdict(metrics.verdict_noise_floor)
    print(f" {'Noise Floor (dBFS)':<25} | {metrics.noise_floor_dbfs:>10.2f} dBFS  | {'<= -48.0 dBFS':<15} | {nf_status:<8}")

    # Row 2: DC Offset
    dc_status = colorize_verdict(metrics.verdict_dc_offset)
    dc_str = f"{metrics.dc_offset_pct:6.2f}% ({metrics.dc_offset_raw:+.1f})"
    print(f" {'DC Offset':<25} | {dc_str:>16} | {'< 1.00%':<15} | {dc_status:<8}")

    # Row 3: Artifacts / High Frequency Spikes
    spk_status = colorize_verdict(metrics.verdict_spikes)
    spk_str = f"{len(metrics.high_freq_spikes)} spike(s)" if metrics.high_freq_spikes else "None (Clean)"
    print(f" {'Spike Artefak (>4 kHz)':<25} | {spk_str:>16} | {'No High Spikes':<15} | {spk_status:<8}")

    # Row 4: Dynamic Range
    print(f" {'Dynamic Range (Silence)':<25} | {metrics.dynamic_range_db:>10.2f} dB    | {'>= 48.0 dB':<15} | {'INFO':<8}")

    # Row 5: Peak Amplitude
    peak_str = f"{metrics.peak_dbfs:>10.2f} dBFS"
    print(f" {'Peak Amplitude':<25} | {peak_str:>16} | {'N/A (Silence)':<15} | {'INFO':<8}")

    print(f"{thin_sep}")
    overall_color = GREEN if metrics.overall_verdict == "PASS" else (YELLOW if metrics.overall_verdict == "WARNING" else RED)
    print(f" {BOLD}OVERALL AUDIT VERDICT: {overall_color}[ {metrics.overall_verdict} ]{RESET}")
    print(f"{CYAN}{sep}{RESET}")

    # Diagnostic Recommendations
    if metrics.diagnostic_notes:
        print(f"\n{BOLD}🔧 REKOMENDASI TEKNIKAL & HARDWARE DSP:{RESET}")
        for idx, note in enumerate(metrics.diagnostic_notes, 1):
            print(f"  {idx}. {note}")

    if metrics.high_freq_spikes:
        print(f"\n{BOLD}🔍 DETAIL SPEKTRUM SPIKE ANOMALIK (> 4 kHz):{RESET}")
        for s in metrics.high_freq_spikes:
            print(f"  • Frekuensi: {s.frequency_hz:6.1f} Hz | Magnitude: {s.power_dbfs:6.1f} dBFS | Prominence: {s.prominence_db:5.1f} dB")
    print()


# ─────────────────────────────────────────────────────────────────────────────
# 6. Synthetic Mock Generator (For automated CI / Testing without hardware)
# ─────────────────────────────────────────────────────────────────────────────
def generate_synthetic_audio(cfg: AuditConfig) -> np.ndarray:
    """
    Generates deterministic synthetic audio for local test validation:
      - 'clean': -55 dBFS thermal noise, DC offset 0.1% (PASS)
      - 'noisy': -38 dBFS loud room/electrical noise (FAIL)
      - 'dc_offset': DC bias at 4% full scale (FAIL)
      - 'harmonic_spikes': Clean noise + 5.2 kHz and 6.4 kHz switching spikes (FAIL)
    """
    np.random.seed(42)
    n_samples = int(cfg.duration_sec * cfg.sample_rate)
    t = np.linspace(0.0, cfg.duration_sec, n_samples, endpoint=False)

    if cfg.mock_scenario == "noisy":
        # RMS around 400 (-38 dBFS)
        noise = np.random.normal(0, 420.0, n_samples)
        dc = 50.0
        signal = noise + dc

    elif cfg.mock_scenario == "dc_offset":
        # DC offset around 1500 (> 4% of 32768)
        noise = np.random.normal(0, 50.0, n_samples)
        dc = 1400.0
        signal = noise + dc

    elif cfg.mock_scenario == "harmonic_spikes":
        # Low noise (-54 dBFS) with high-frequency switching regulator tones
        noise = np.random.normal(0, 60.0, n_samples)
        # 5.2 kHz switching harmonic sub-carrier (amplitude 800)
        spike1 = 850.0 * np.sin(2.0 * np.pi * 5200.0 * t)
        # 6.4 kHz clock jitter ripple (amplitude 600)
        spike2 = 600.0 * np.sin(2.0 * np.pi * 6400.0 * t)
        dc = 20.0
        signal = noise + spike1 + spike2 + dc

    else:  # "clean"
        # Typical INMP441 quiet floor: ~ -54 dBFS (RMS ~ 65)
        noise = np.random.normal(0, 65.0, n_samples)
        dc = 15.0  # ~0.04% FS
        signal = noise + dc

    # Clip to int16 boundaries
    signal = np.clip(signal, -32768, 32767).astype(np.int16)
    return signal


# ─────────────────────────────────────────────────────────────────────────────
# Main Entry Point
# ─────────────────────────────────────────────────────────────────────────────
def main() -> int:
    parser = argparse.ArgumentParser(
        description="ESP32-S3 + INMP441 I2S Audio Quality & DSP Validation Tool (macOS)",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter
    )
    parser.add_argument("--port", type=str, default=None,
                        help="Port serial ESP32-S3 (contoh: /dev/cu.usbmodem2101). Kosongkan untuk auto-detect.")
    parser.add_argument("--baud", type=int, default=921600,
                        help="Baudrate serial communication (default: 921600, alternatif: 115200)")
    parser.add_argument("--duration", type=float, default=5.0,
                        help="Durasi perekaman audio dalam detik")
    parser.add_argument("--sample-rate", type=int, default=16000,
                        help="Sample rate target dalam Hz")
    parser.add_argument("--output-wav", type=str, default="output_test.wav",
                        help="Path output file audio WAV")
    parser.add_argument("--output-plot", type=str, default="signal_audit.png",
                        help="Path output file visualisasi PNG")
    parser.add_argument("--trigger-cmd", type=str, default="CMD_RECORD\n",
                        help="Perintah serial trigger uji coba yang dikirim ke ESP32")
    parser.add_argument("--mock", action="store_true",
                        help="Gunakan sinyal audio sintetis untuk pengujian otomasi tanpa hardware")
    parser.add_argument("--mock-scenario", type=str, default="clean",
                        choices=["clean", "noisy", "dc_offset", "harmonic_spikes"],
                        help="Skenario sinyal sintetis jika menggunakan flag --mock")

    args = parser.parse_args()

    config = AuditConfig(
        port=args.port,
        baudrate=args.baud,
        sample_rate=args.sample_rate,
        duration_sec=args.duration,
        output_wav=args.output_wav,
        output_plot=args.output_plot,
        trigger_cmd=args.trigger_cmd if args.trigger_cmd.endswith("\n") else args.trigger_cmd + "\n",
        mock_mode=args.mock,
        mock_scenario=args.mock_scenario
    )

    try:
        # Step 1: Capture Audio via Serial or Mock
        samples = capture_serial_audio(config)

        # Step 2: Save as valid WAV file
        save_wav_file(samples, config.output_wav, config.sample_rate)

        # Step 3: Run DSP Analysis
        print("🧠 Menjalankan analisis DSP (RMS, DC Offset, Welch PSD & Spikes)...")
        metrics = analyze_audio_dsp(samples, config)

        # Step 4: Render Plots
        generate_audit_plots(samples, metrics, config)

        # Step 5: Terminal Diagnostic Report
        print_audit_report(metrics, config)

        # Return code: 0 for PASS, 1 for WARNING, 2 for FAIL
        if metrics.overall_verdict == "PASS":
            return 0
        elif metrics.overall_verdict == "WARNING":
            return 1
        else:
            return 2

    except KeyboardInterrupt:
        print("\n\n[ABORT] Operasi dibatalkan oleh pengguna (SIGINT).", file=sys.stderr)
        return 130
    except Exception as err:
        print(f"\n❌ [ERROR] Kegagalan audit audio: {err}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
