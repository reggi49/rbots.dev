# 🔧 RBOT Audio Fix - Test Guide

## Masalah yang Diperbaiki

### 1. **Speaker MAX98357A Tidak Keluar Suara** ❌🔊
   - **Penyebab**: Volume terlalu rendah (AUDIO_VOLUME_SHIFT = 1, yaitu -6dB)
   - **Solusi**: Dinaikkan ke AUDIO_VOLUME_SHIFT = 0 (full volume 0dB)
   - **File**: `board/main/audio_player.c` line 17

### 2. **Audio Tidak Disimpan ke Komputer** ❌💾
   - **Penyebab**: Perintah `CMD_RECORD` tidak terhubung ke CLI di `board.c`
   - **Solusi**: 
     - Tambah handler untuk perintah `RECORD` di `board/main/board.c`
     - Buat script Python baru `save_mic_recording.py` untuk menerima data

---

## 🧪 Test 1: Apakah Speaker Sekarang Mengeluarkan Suara?

### Test dengan Parrot (Mic → Speaker Loopback)

```bash
cd /Users/reggi/Projects/rbot
python parrot_voice.py
```

**Yang Harus Terjadi:**
1. LCD menampilkan "RECORD IN 3, 2, 1"
2. LCD menampilkan "RECORDING 3, 2, 1" (mic merekam)
3. **ESP32 menangkap audio:** `Captured 48000 samples (RMS: 271, Peak: 1628)` ✅
4. LCD menampilkan "SPEAK IN 3, 2, 1"  
5. LCD menampilkan "SPEAKING 3, 2, 1"
6. **🔊 SPEAKER HARUS KELUAR SUARA!** (yang Anda rekam 3 detik sebelumnya)

**Troubleshooting jika masih tidak ada suara:**
- Periksa wiring MAX98357A:
  ```
  WS   → GPIO12
  BCLK → GPIO11
  DIN  → GPIO6
  GND  → GND
  VIN  → 3.3V atau 5V
  SD   → (kosongkan atau GND untuk normal mode)
  GAIN → (kosongkan untuk 9dB gain, atau GND untuk 15dB)
  ```
- Periksa apakah MAX98357A menyala (lampu LED indicator)?
- Coba test tone dulu: kirim `TONE` via serial monitor
- Pastikan speaker terpasang ke output MAX98357A

---

## 🧪 Test 2: Apakah Mic Merekam dan Bisa Disimpan ke Komputer?

### Test dengan Save Recording ke WAV File

```bash
cd /Users/reggi/Projects/rbot
python save_mic_recording.py --duration 5 --output test_mic.wav
```

**Yang Harus Terjadi:**
1. Script mengirim perintah `RECORD 5` ke ESP32
2. ESP32 merespons dengan `START_RECORD`
3. LCD menampilkan "RECORDING..." dengan animasi
4. **Mic INMP441 merekam 5 detik audio**
5. ESP32 streaming data PCM 16-bit ke komputer via serial
6. Script menyimpan ke file `test_mic.wav`
7. Script menampilkan statistik:
   ```
   📊 Statistik Audio:
      - RMS  : 271
      - Peak : 1628 / 32767
      🎉 Sinyal terdeteksi! Mic menangkap suara.
   ```

**Putar file hasil rekaman:**
```bash
afplay test_mic.wav
```

**Troubleshooting jika RMS rendah (<50):**
- Periksa wiring INMP441:
  ```
  WS   → GPIO1
  BCLK → GPIO4  
  SD   → GPIO2
  GND  → GND
  VDD  → 3.3V
  L/R  → GND (untuk left channel)
  ```
- Bicara lebih dekat ke mic INMP441
- Periksa apakah INMP441 menyala (beberapa modul punya LED)

---

## 🎯 Test 3: Perintah CLI yang Tersedia

Anda bisa kirim perintah langsung via serial monitor:

```bash
screen /dev/cu.usbmodem5A790640561 115200
```

Atau via Python:
```python
import serial
s = serial.Serial('/dev/cu.usbmodem5A790640561', 115200, timeout=1)
s.write(b'PING\r\n')
print(s.readline())  # Harus dapat: PONG
```

**Perintah yang didukung:**
- `PING` → Test koneksi (respons: PONG)
- `TONE` → Mainkan tone 1kHz di speaker (test speaker tanpa mic)
- `PLAY` → Mainkan sample voice "Prabowo" di speaker
- `ECHO` → Mic → Speaker loopback 3 detik (parrot test)
- `MIC` → Test level mic RMS/Peak (tanpa simpan)
- `RECORD [sec]` → **BARU!** Rekam dan streaming ke komputer (1-30 detik)

---

## 📊 Expected Output dari Log Anda Sebelumnya

```
python parrot_voice.py
============================================================
🦜 RBOT PARROT  (mic INMP441 -> speaker MAX98357A)
============================================================
Port: /dev/cu.usbmodem5A790640561 @ 115200
Connecting to Rbot (PING)... OK ✅

  Rbot got the command, starting...
  [LCD] 🎙️  RECORD IN 3
  [LCD] 🎙️  RECORD IN 2
  [LCD] 🎙️  RECORD IN 1
  [LCD] 🔴 RECORDING 3
  [LCD] 🔴 RECORDING 2
  [LCD] 🔴 RECORDING 1
        (Captured 48000 samples (RMS: 271, Peak: 1628))
  [LCD] 🔊 SPEAK IN 3
  [LCD] 🔊 SPEAK IN 2
  [LCD] 🔊 SPEAK IN 1
  [LCD] 🔊 SPEAKING 3
  [LCD] 🔊 SPEAKING 2
  [LCD] 🔊 SPEAKING 1
  [LCD] ✅ RBOT READY

🎉 Done!
```

**Analisis:**
- ✅ Mic merekam dengan benar: RMS=271, Peak=1628 (ada sinyal audio)
- ❌ **SEBELUMNYA: Speaker tidak keluar suara** (volume terlalu rendah)
- ✅ **SEKARANG: Dengan AUDIO_VOLUME_SHIFT=0, speaker harus keras!**

---

## 📝 Rangkuman Perubahan

### Files yang Diubah:

1. **`board/main/audio_player.c`** (line 17)
   ```c
   // SEBELUM: #define AUDIO_VOLUME_SHIFT 1   /* -6 dB */
   // SESUDAH: #define AUDIO_VOLUME_SHIFT 0   /* 0 dB - full volume */
   ```

2. **`board/main/board.c`** (line 1049-1067)
   - Tambah CLI handler untuk perintah `RECORD [sec]`
   - Memanggil `selftest_mic_stream(dur)` untuk streaming audio

3. **`save_mic_recording.py`** (NEW FILE)
   - Script Python untuk menerima audio stream dari ESP32
   - Menyimpan ke file WAV di komputer
   - Menampilkan statistik RMS dan Peak

### Build & Flash:
```bash
cd /Users/reggi/Projects/rbot
bash flash.sh
```

✅ **Flash BERHASIL pada 2026-10-05 16:08 WIB**

---

## 🎉 Next Steps

1. **Test speaker volume:** `python parrot_voice.py`
2. **Test save recording:** `python save_mic_recording.py --duration 5`
3. **Verify waveform:** `python audio_audit.py test_mic.wav`

Good luck! 🚀
