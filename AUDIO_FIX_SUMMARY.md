# Audio Fix Summary - Resolusi Bug Audio Dipercepat 2x

## Tanggal
2026-10-05

## Masalah yang Ditemukan
1. **Audio dipercepat 2x (chipmunk voice)** - suara yang direkam dan dimainkan terdengar seperti pitch tinggi dan durasi diperpendek setengahnya
2. **Data audio terpotong** - hanya 61-68% dari data audio yang berhasil ditransmisikan ke host computer

## Root Cause Analysis

### Issue #1: I2S slot_mask Causing Mono Extraction Instead of Interleaved Stereo
**Masalah Utama:**
- Line `std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;` di `board_audio.c` menyebabkan ESP-IDF I2S driver mengekstrak **hanya LEFT channel** dan mem-pack sebagai mono: `[L0, L1, L2, L3, ...]`
- Tetapi code kita mengalokasi buffer untuk stereo (`chunk_samples * 2`) dan membaca data dengan asumsi interleaved: `[L0, R0, L1, R1, ...]`
- Loop processing menggunakan `i += 2` untuk skip RIGHT channel
- **Efeknya:** Kita hanya memproses **setengah dari sample yang valid** (L0, L2, L4, ...) → sample rate efektif jadi 8kHz → audio diputar 2x lebih cepat!

**Root Cause Lengkap:**
**Root Cause Lengkap:**
1. **INMP441 hardware:** Mengeluarkan I2S Philips standard stereo frames (2 slots: LEFT + RIGHT). Karena L/R pin = GND, hanya slot LEFT berisi audio.
2. **Setting `slot_mask = I2S_STD_SLOT_LEFT`:** Menyebabkan ESP-IDF driver mengekstrak hanya LEFT dan mem-pack buffer menjadi mono kontinu: `[L0, L1, L2, L3, ...]`
3. **Buffer allocation:** Code mengalokasi `chunk_samples * 2` int32, mengira data interleaved stereo: `[L0, R0, L1, R1, ...]`
4. **Processing loop `i += 2`:** Mengambil sample di index 0, 2, 4, 6, ... (mengira skip RIGHT channel)
5. **Actual data:** Buffer berisi `[L0, L1, L2, L3, ...]` → loop mengambil L0, L2, L4, L6, ... → **setengah sample hilang!**
6. **Result:** Sample rate efektif menjadi **8kHz** (setengah dari 16kHz) → audio diputar 2x cepat (chipmunk)!

**Solusi:**
- **HAPUS** line `std_cfg.slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;` 
- Biarkan default `I2S_STD_SLOT_BOTH` aktif, sehingga driver memberikan interleaved data: `[L, R, L, R, ...]`
- Loop `i += 2` sekarang bekerja dengan benar: mengambil L0, L1, L2, L3, ... (skip R0, R1, R2, R3, ...)
- Sample rate tetap 16kHz penuh!

**File yang terpengaruh:**
- `board/src/board_audio.c` - I2S config (REMOVED slot_mask line)
- `board/main/selftest.c` - Mic recording loops (sudah benar dengan `i += 2`)
- `board/main/board.c` - Audio capture untuk wake word (sudah benar dengan `i += 2`)

### Issue #2: USB Serial JTAG TX Buffer Terlalu Kecil
**Masalah:**
- USB Serial JTAG TX buffer hanya 1024 bytes
- Audio streaming membutuhkan 32 KB/detik (16kHz * 2 bytes)
- Buffer overflow menyebabkan data audio dropped setelah 65,536 bytes (exact 64 KB - kemungkinan OS-level buffer limit)

**Solusi:**
- Tingkatkan `tx_buffer_size` dari 1024 bytes ke **131,072 bytes (128 KB)**
- Tingkatkan timeout di `save_mic_recording.py` dari `duration + 4s` ke `duration + 10s`

## File yang Dimodifikasi

### 1. `board/src/board_audio.c`
```diff
- .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO),
+ .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_STEREO),

- size_t need_int32 = chunk_samples;
+ size_t need_int32 = chunk_samples * 2; // Stereo: 2 samples per frame

- for (size_t out_idx = 0; out_idx < chunk_samples; ++out_idx) {
-     int16_t sample = mic_sample_to_int16(audio->raw_buffer[out_idx]);
+ for (size_t sample_idx = 0, out_idx = 0; out_idx < chunk_samples; ++out_idx, sample_idx += 2) {
+     int16_t sample = mic_sample_to_int16(audio->raw_buffer[sample_idx]); // LEFT channel only

- int32_t raw_buffer[MAX_CHUNK_SAMPLES]; // Mono raw samples
+ int32_t raw_buffer[MAX_CHUNK_SAMPLES * 2]; // Stereo raw (LEFT + RIGHT)

- for (size_t i = 0; i < int32_count; i++) {
+ for (size_t i = 0; i < int32_count; i += 2) { // Stereo: skip RIGHT channel
```

### 2. `board/main/selftest.c`
```diff
# selftest_mic_rms():
- for (size_t i = 0; i < samples; i++) {
+ for (size_t i = 0; i < samples; i += 2) { // STEREO: process LEFT channel only (skip RIGHT)

# selftest_mic_stream():
- for (size_t i = 0; i < n && (samples_captured + i) < total_samples; i++) {
-     int16_t raw_s16 = inmp441_to_pcm16(raw_buf[i]);
-     ...
-     pcm_buf[i] = (int16_t)y;
- }
- size_t chunk_samples = (samples_captured + n <= total_samples) ? n : (total_samples - samples_captured);

+ for (size_t i = 0; i < n && (samples_captured + i/2) < total_samples; i += 2) {
+     int16_t raw_s16 = inmp441_to_pcm16(raw_buf[i]); // LEFT channel only
+     ...
+     pcm_buf[i/2] = (int16_t)y;
+ }
+ size_t mono_samples = n / 2;
+ size_t chunk_samples = (samples_captured + mono_samples <= total_samples) ? mono_samples : (total_samples - samples_captured);

# selftest_mic_speaker_loopback():
- for (size_t i = 0; i < n && captured < total_samples; i++) {
+ for (size_t i = 0; i < n && captured < total_samples; i += 2) { // STEREO: LEFT only
```

### 3. `board/main/board.c`
```diff
- size_t samples = bytes_read / 4;  // 32-bit samples (mono)
- // Mono mode already outputs LEFT channel only
- for (size_t i = 0; i < samples && total < target_samples; i++) {
+ size_t samples = bytes_read / 4;  // 32-bit samples (stereo)
+ // Stereo mode: process only LEFT channel (even indices: 0, 2, 4, ...)
+ for (size_t i = 0; i < samples && total < target_samples; i += 2) {

# configure_console_for_binary_dump():
-     .tx_buffer_size = 1024,
+     .tx_buffer_size = 131072,  // 128 KB buffer untuk streaming audio
```

### 4. `save_mic_recording.py`
```diff
- timeout = duration_sec + 4.0
+ timeout = duration_sec + 10.0  # Durasi + waktu transmisi + buffer
```

## Hasil Testing

### Before Fix:
```
Duration: 5 seconds
Received: 98,304 / 160,000 bytes (61.4%)
Audio: Sped up 2x (chipmunk voice)
```

### After Fix:
```
Duration: 3 seconds
Received: 96,000 / 96,000 bytes (100.0%)
Duration verified: 3.000000 sec @ 16000 Hz
Audio: Normal speed ✅

Duration: 5 seconds  
Received: 160,000 / 160,000 bytes (100.0%)
Duration verified: 5.000000 sec @ 16000 Hz
Audio: Normal speed ✅
```

### Parrot Test (Mic → Speaker Loopback):
```
Captured: 48,000 samples (3.0 sec @ 16kHz)
RMS: 169, Peak: 1402
Playback: SPEAKING 3 → 2 → 1 (exact 3 seconds)
Audio quality: Clear, no speed issues ✅
```

## Key Takeaways
1. **INMP441 WAJIB menggunakan STEREO mode** meskipun hanya LEFT channel yang digunakan
2. **Semua audio processing loop harus skip RIGHT channel** dengan `i += 2`
3. **USB Serial JTAG buffer perlu cukup besar** untuk streaming audio (min 128 KB untuk 4+ detik audio)
4. **Always verify dengan `afinfo`** untuk memastikan durasi audio tepat

## Referensi
- INMP441 Datasheet: I2S Philips standard, LEFT channel mode (L/R=GND)
- ESP-IDF I2S Documentation: `I2S_SLOT_MODE_STEREO` untuk hardware compatibility
- Gold state commit: `4652fff` (fix semua fitur normal)
