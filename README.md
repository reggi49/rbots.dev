# R-bot 🤖🐱🎙️

> **Robot Pendamping Interaktif Berbasis ESP32-S3: Antarmuka Kawaii Cat ST7735, Penggerak Roda Dual N20 (DRV8833), Manajemen Daya Baterai 18650 + BMS, Wake Word Lokal WakeNet10, dan Cloud AI Pipeline (Gemini 3.5 + Edge-TTS / Azure).**

---

## Daftar Isi
1. [Ringkasan Proyek & Arsitektur](#ringkasan-proyek--arsitektur)
2. [Fitur Utama](#fitur-utama)
3. [Spesifikasi Hardware & Komponen](#spesifikasi-hardware--komponen)
4. [Skema Pinout & Pengkabelan Lengkap](#skema-pinout--pengkabelan-lengkap)
   - [1. Audio Input: Mikrofon INMP441 (I2S0)](#1-audio-input-mikrofon-inmp441-i2s0)
   - [2. Audio Output: Amplifier MAX98357A (I2S1)](#2-audio-output-amplifier-max98357a-i2s1)
   - [3. Visual Display: ST7735 1.8" TFT (SPI)](#3-visual-display-st7735-18-tft-spi)
   - [4. Penggerak Motor: DRV8833 + Dual N20 Gearmotors](#4-penggerak-motor-drv8833--dual-n20-gearmotors)
   - [5. Sistem Daya: Baterai 18650 + Modul BMS](#5-sistem-daya-sistem-baterai-18650--modul-bms)
5. [Tabel Logika Kontrol Motor N20](#tabel-logika-kontrol-motor-n20)
6. [Struktur Folder & Direktori](#struktur-folder--direktori)
7. [Panduan Menjalankan Sistem (Step-by-Step)](#panduan-menjalankan-sistem-step-by-step)
   - [Langkah 1: Menyiapkan Jaringan Wi-Fi](#langkah-1-menyiapkan-jaringan-wi-fi)
   - [Langkah 2: Menyiapkan dan Menjalankan Backend Server](#langkah-2-menyiapkan-dan-menjalankan-backend-server)
   - [Langkah 3: Konfigurasi Target Firmware](#langkah-3-konfigurasi-target-firmware)
   - [Langkah 4: Kompilasi & Flash Firmware ESP32-S3](#langkah-4-kompilasi--flash-firmware-esp32-s3)
   - [Langkah 5: Monitoring & Verifikasi Interaksi](#langkah-5-monitoring--verifikasi-interaksi)
8. [Sistem Animasi Wajah Kawaii Cat (ST7735 20 Ekspresi)](#sistem-animasi-wajah-kawaii-cat-st7735-20-ekspresi)
9. [Dokumentasi API & Panel Analisis Latensi](#dokumentasi-api--panel-analisis-latensi)
10. [Optimasi Audio, Daya & Troubleshooting](#optimasi-audio-daya--troubleshooting)

---

## Ringkasan Proyek & Arsitektur

R-bot adalah robot pendamping cerdas berbentuk kucing robotik (*kawaii cat*) yang mampu bergerak mandiri, merespons suara pengguna secara *hands-free* lewat pengenalan kata pemicu (*wake word*) lokal, dan bercakap-cakap santai dalam Bahasa Indonesia.

Sistem tidak lagi menggunakan tombol sentuh (*touch sensor*) fisik, melainkan murni dikendalikan oleh suara (*Voice-Activated*) dan perintah UART, membebaskan GPIO untuk kontrol motor penggerak roda.

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                           ARSITEKTUR R-BOT                                  │
└─────────────────────────────────────────────────────────────────────────────┘

                  [Pengguna Bicara: "Alexa ..."]
                               │
                               ▼
 ┌───────────┐           ┌────────────────────────────────────────────────┐
 │  INMP441  │ I2S0 (RX) │ ESP32-S3 DevKit N16R8                          │
 │ Mikrofon  │──────────>│ • WakeNet10 Local Model ("Alexa")              │
 └───────────┘           │ • DC-Blocking IIR High-Pass Filter             │
                         │ • Energy VAD & Countdown 10 Detik              │
                         │ • Kawaii Cat UI Engine (20 FPS di Core 1)      │
                         └──────────────┬──────────────────┬──────────────┘
                                        │                  │
                ┌───────────────────────┘                  └──────────────────────┐
                │ HTTP POST /chat/voice                                           │ PWM / GPIO
                ▼                                                                 ▼
 ┌───────────────────────────────────────┐                        ┌─────────────────────────────────┐
 │ BACKEND AI (FastAPI / Laptop)         │                        │ DRV8833 Dual H-Bridge Driver    │
 │ 1. STT: Gemini / Azure (Minimal Think)│                        │ • IN1 (GPIO13)  IN2 (GPIO14)    │
 │ 2. LLM: Gemini 3.5 Flash Lite         │                        │ • IN3 (GPIO17)  IN4 (GPIO18)    │
 │    + Brave Search (Konteks Terkini)   │                        └───────────────┬─────────────────┘
 │ 3. TTS: Edge-TTS / Azure / gTTS       │                                        │
 │    + 30% Volume Boost Audio Filter    │                                        ▼
 └──────────────────┬────────────────────┘                        ┌─────────────────────────────────┐
                    │                                             │ 2x N20 Micro DC Gearmotors      │
                    │ Response Audio WAV                          │ (Roda Kiri & Roda Kanan)        │
                    ▼                                             └─────────────────────────────────┘
 ┌───────────────────────────────────────┐
 │ ESP32-S3 Audio & Visual Feedback      │
 │ • I2S1 (TX) ──> MAX98357A ──> Speaker │
 │ • SPI ────────> ST7735 1.8" Cat Face  │
 └───────────────────────────────────────┘
```

---

## Fitur Utama

- **Mobilitas Roda Dual N20 (DRV8833):** Penggerak roda diferensial berbasis dual motor DC N20 dengan driver MOSFET DRV8833 hemat daya, memungkinkan robot maju, mundur, bermanuver, dan berputar.
- **Manajemen Daya Terintegrasi (Baterai 18650 + BMS):** Menggunakan baterai Li-ion 18650 rechargeable yang diproteksi oleh modul BMS (Battery Management System) terhadap *over-charge*, *over-discharge*, dan korsleting arus pendek.
- **Hands-Free Wake Word Lokal:** Menggunakan model **WakeNet10** (`wn10_alexa`) dari Espressif ESP-SR 2.5.5 dengan kuantisasi `w8a16` dan akselerasi vektor perangkat keras ESP32-S3. Tanpa ketergantungan pada tombol sentuh fisik.
- **Tampilan Wajah Kawaii Cat Organik (ST7735):** 20 ekspresi emosi kucing kawaii (pupil berlapis gradien cyan-violet, telinga kucing 3D, pita pink, lonceng emas, pipi merona, kumis, dan status banner) yang dirender dinamis di Core 1.
- **Dual-Engine Cloud AI dengan Konteks Waktu Nyata:**
  - **STT:** Azure Cognitive Speech dengan fallback otomatis ke Google Gemini STT (`thinking_level="minimal"` untuk latensi ultra-cepat ~1.6 detik).
  - **LLM:** Google Gemini 3.5 Flash Lite yang terintegrasi dengan **Brave Web Search API** untuk menjawab fakta terkini (nama pejabat, berita, cuaca, dsb).
  - **TTS:** Microsoft Edge-TTS (`id-ID-GadisNeural` / `ms-MY-YasminNeural`) dengan fallback ke gTTS (Google) dan Azure Speech.
- **Penguatan Audio Digital (+30% Volume Boost):** Volume suara speaker ditingkatkan sebesar 30% menggunakan filter *soft limiting* ffmpeg pada backend, menghasilkan suara yang lantang dan jernih tanpa distorsi.
- **Panel Analisis Latensi & Bottleneck:** Backend secara otomatis mengukur dan mencetak proporsi waktu proses STT, LLM, dan TTS untuk mendeteksi bottleneck sistem secara transparan.

---

## Spesifikasi Hardware & Komponen

| Komponen | Tipe / Model | Peran & Keterangan |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-S3 DevKit N16R8 | Dual Core Xtensa LX7, 16 MB Flash, 8 MB Octal PSRAM |
| **Driver Motor** | DRV8833 Dual H-Bridge (Modul Hitam) | Efisiensi tinggi, kontrol arah & kecepatan PWM 2 motor |
| **Motor Penggerak** | 2x N20 Micro Metal Gearmotor | Motor DC torsi presisi untuk roda robot (Kiri & Kanan) |
| **Baterai** | 18650 Li-ion 3.7V (~2500–3400 mAh) | Sumber daya utama yang dapat diisi ulang |
| **BMS / Charger** | Modul 1S BMS / TP4056 dengan Proteksi | Pengisian via USB-C, proteksi overcharge & under-voltage |
| **Mikrofon** | INMP441 MEMS Digital I2S | Input suara 16-bit 16 kHz (Port I2S0 RX) |
| **Audio Amp** | MAX98357A I2S Class D Amplifier | Output audio digital ke analog (Port I2S1 TX) |
| **Speaker** | 4Ω 3W Full-Range Speaker | Pengeras suara vokal robot |
| **Layar** | ST7735 1.8" SPI TFT LCD | Resolusi 128x160 piksel, rendering wajah robot kucing |
| *Touch Sensor* | *(Ditiadakan)* | *Pin GPIO 14 dialokasikan ulang untuk kontrol motor* |

---

## Skema Pinout & Pengkabelan Lengkap

File konfigurasi pin utama didefinisikan di [`board/include/board_pins.h`](board/include/board_pins.h).

> [!IMPORTANT]
> - Seluruh pin ground (**GND**) pada ESP32-S3, modul DRV8833, amplifier MAX98357A, dan output BMS **wajib terhubung bersama (Common Ground)**.
> - Hindari mengambil tegangan motor dari pin `3V3` ESP32-S3. Gunakan output baterai langsung/BMS ke pin `VCC` DRV8833 untuk mencegah penurunan tegangan (*brownout reset*).

---

### 1. Audio Input: Mikrofon INMP441 (I2S0)
| Pin INMP441 | Pin ESP32-S3 | Keterangan |
| :--- | :--- | :--- |
| **VDD** | 3.3V | Catu daya mikrofon |
| **GND** | GND | Ground bersama |
| **SD** | **GPIO 2** | Serial Data Input |
| **SCK / BCLK** | **GPIO 4** | Continuous Serial Clock |
| **WS / LRCLK** | **GPIO 1** | Word Select (Left/Right clock) |
| **L/R** | GND | Konfigurasi Left Channel |

---

### 2. Audio Output: Amplifier MAX98357A (I2S1)
| Pin MAX98357A | Pin ESP32-S3 | Keterangan |
| :--- | :--- | :--- |
| **VIN** | 5V / V_BAT | Catu daya amplifier (disarankan 5V atau tegangan baterai) |
| **GND** | GND | Ground bersama |
| **DIN** | **GPIO 6** | Digital Audio Data Output |
| **BCLK** | **GPIO 11** | Bit Clock Output |
| **LRC / WS** | **GPIO 12** | Left/Right Word Clock Output |
| **GAIN** | GND | Gain default (+9dB) |
| **SD_MODE** | Floating | Mode stereo-to-mono mix |

---

### 3. Visual Display: ST7735 1.8" TFT (SPI)
| Pin Layar ST7735 | Pin ESP32-S3 | Keterangan |
| :--- | :--- | :--- |
| **VCC** | 3.3V / 5V | Catu daya modul display |
| **GND** | GND | Ground bersama |
| **CS** | **GPIO 7** | SPI Chip Select |
| **SCK / SCL** | **GPIO 8** | SPI Master Clock |
| **SDA / MOSI** | **GPIO 9** | SPI Master Out Slave In |
| **A0 / DC** | **GPIO 10** | Data / Command Control |
| **RESET / RST** | **GPIO 16** | Hardware Reset Pin |
| **LED / BL** | **GPIO 15** (atau 3.3V) | Kontrol kecerahan backlight |

---

### 4. Penggerak Motor: DRV8833 + Dual N20 Gearmotors
Modul DRV8833 menghubungkan ESP32-S3 dengan dua buah motor N20 (Roda Kiri & Roda Kanan):

| Pin DRV8833 (Hitam) | Terhubung Ke | Deskripsi Fungsi |
| :--- | :--- | :--- |
| **VCC** | **(+) Baterai / BMS OUT+** | Catu daya motor (3.7V - 6V) |
| **GND** | **(-) Baterai & GND ESP32** | Ground bersama (*Common Ground*) |
| **IN1** | **ESP32-S3 GPIO 13** | Kontrol Roda Kiri (Maju / PWM) |
| **IN2** | **ESP32-S3 GPIO 14** | Kontrol Roda Kiri (Mundur / PWM) *(Bekas pin touch)* |
| **IN3** | **ESP32-S3 GPIO 17** | Kontrol Roda Kanan (Maju / PWM) |
| **IN4** | **ESP32-S3 GPIO 18** | Kontrol Roda Kanan (Mundur / PWM) |
| **ULT / EEP** | 3.3V / Dibiarkan | Pin Sleep (*biasanya sudah terpasang pull-up internal*) |
| **MOTOR A (OUT1, OUT2)**| Kabel Motor N20 Kiri | Terminal Roda Kiri |
| **MOTOR B (OUT3, OUT4)**| Kabel Motor N20 Kanan | Terminal Roda Kanan |

---

### 5. Sistem Daya: Baterai 18650 + Modul BMS
```
 ┌──────────────────────┐
 │  Baterai 18650 3.7V  │
 │  [ + ]        [ - ]  │
 └───┬─────────────┬────┘
     │             │
     │             │
 ┌───▼─────────────▼──────────────────────────────┐
 │ MODUL BMS / CHARGER (Misal: TP4056 Proteksi)   │
 │ • B+  <-- Kutub Positif Baterai                │
 │ • B-  <-- Kutub Negatif Baterai                │
 │ • IN+ / IN- <-- Port Pengisian USB-C           │
 └───┬─────────────────────────────┬──────────────┘
     │ OUT+ (VCC)                  │ OUT- (GND)
     ▼                             ▼
 [Sakelar On/Off]            ┌───────────────┐
     │                       │ COMMON GROUND │
     ├───────────────────────┤ (GND Bersama) │
     │                       └───────┬───────┘
     │                               │
     ├─> VCC DRV8833 Motor           ├─> GND DRV8833 Motor
     │                               ├─> GND ESP32-S3
     └─> 5V / VIN ESP32-S3           ├─> GND Layar ST7735
         (atau via Step-Up 5V)       ├─> GND Mic INMP441
                                     └─> GND Amp MAX98357A
```

---

## Tabel Logika Kontrol Motor N20

Kombinasi logika digital/PWM pada pin `IN1-IN4` untuk mengatur manuver pergerakan robot:

| Aksi Manuver Robot | IN1 (GPIO 13) | IN2 (GPIO 14) | IN3 (GPIO 17) | IN4 (GPIO 18) | Keterangan Gerak |
| :--- | :---: | :---: | :---: | :---: | :--- |
| **Maju Lurus** | **HIGH (PWM)**| LOW | **HIGH (PWM)**| LOW | Kedua roda berputar maju bersamaan |
| **Mundur Lurus** | LOW | **HIGH (PWM)**| LOW | **HIGH (PWM)**| Kedua roda berputar mundur bersamaan |
| **Belok Kiri Halus** | LOW | LOW | **HIGH (PWM)**| LOW | Roda kiri diam, roda kanan maju |
| **Belok Kanan Halus**| **HIGH (PWM)**| LOW | LOW | LOW | Roda kiri maju, roda kanan diam |
| **Putar Kiri di Tempat** | LOW | **HIGH (PWM)**| **HIGH (PWM)**| LOW | Roda kiri mundur, roda kanan maju |
| **Putar Kanan di Tempat**| **HIGH (PWM)**| LOW | LOW | **HIGH (PWM)**| Roda kiri maju, roda kanan mundur |
| **Pengereman (Brake)**| **HIGH** | **HIGH** | **HIGH** | **HIGH** | H-Bridge short-circuit dinamis (rem cepat) |
| **Meluncur / Standby** | LOW | LOW | LOW | LOW | Motor meluncur bebas (*Coast / Idle*) |

---

## Struktur Folder & Direktori

```
rbot/
├── README.md                      # Dokumentasi utama proyek
├── backend/                       # Layanan Cloud & AI Backend (FastAPI)
│   ├── .env                       # Variabel rahasia (API Keys & Pengaturan Suara)
│   ├── requirements.txt           # Dependensi Python
│   ├── server.py                  # Entry point server audio STT-LLM-TTS + Latency Box
│   └── deploy.sh                  # Skrip deployment
├── board/                         # Firmware ESP32-S3 (ESP-IDF)
│   ├── CMakeLists.txt             # Konfigurasi build CMake
│   ├── partitions_singleapp.csv   # Tabel partisi flash (4MB model, 2MB app)
│   ├── sdkconfig                  # Konfigurasi Kconfig ESP-IDF
│   ├── include/                   # Header publik firmware
│   │   ├── board_pins.h           # Single source of truth seluruh GPIO
│   │   ├── board_audio.h          # Abstraksi I2S Mic dan Speaker
│   │   ├── board_display.h        # Abstraksi driver ST7735
│   │   ├── board_face.h           # Prototipe antarmuka wajah Kawaii Cat
│   │   └── board_network.h        # Manajemen koneksi Wi-Fi
│   ├── src/                       # Implementasi driver perangkat keras
│   │   ├── board_audio.c          # Inisialisasi driver I2S0 dan I2S1
│   │   ├── board_display.c        # Inisialisasi SPI & konfigurasi display
│   │   ├── board_face.c           # Rendering Kawaii Cat 20 ekspresi (Layered Eyes & Paws)
│   │   └── board_network.c        # Koneksi Wi-Fi STA
│   └── main/                      # Logika aplikasi robot percakapan
│       ├── board.c                # app_main(), state machine, CLI handler
│       ├── voice_chat.c           # Pipeline percakapan (record -> HTTP -> play)
│       ├── voice_chat.h           # URL endpoint backend & trigger
│       ├── wake_word_sr.c         # Engine WakeNet10, HPF filter, VAD
│       └── wake_word_sr.h         # API kontrol wake word
└── test_expressions.py           # Skrip otomatis uji coba visual 20 ekspresi
```

---

## Panduan Menjalankan Sistem (Step-by-Step)

### Langkah 1: Menyiapkan Jaringan Wi-Fi
Pastikan ESP32-S3 dan komputer backend berada di jaringan Wi-Fi lokal (SSID 2.4 GHz) yang sama.
Ubah SSID & kata sandi pada [`board/src/board_network.c`](board/src/board_network.c) jika diperlukan:
```c
#define WIFI_SSID "rara"
#define WIFI_PASS "123456788"
```

---

### Langkah 2: Menyiapkan dan Menjalankan Backend Server
1. Masuk ke direktori backend dan aktifkan virtual environment:
   ```bash
   cd /Users/reggi/Projects/rbot
   source .venv/bin/activate
   ```
2. Pastikan file `backend/.env` telah terisi:
   ```env
   AISTUDIO_API_KEY=AIzaSyDAos8...
   BRAVE_API_KEY=BSAalJzmn-Tu6...
   TTS_VOICE=id-ID-GadisNeural
   TTS_VOLUME_BOOST=1.30
   ```
3. Jalankan server FastAPI:
   ```bash
   python backend/server.py
   ```
4. Cek alamat IP lokal komputer Anda:
   ```bash
   ipconfig getifaddr en0
   # Contoh output: 10.93.98.240
   ```

---

### Langkah 3: Konfigurasi Target Firmware
Buka file [`board/main/voice_chat.h`](board/main/voice_chat.h) dan sesuaikan alamat IP backend dengan IP komputer Anda:
```c
#define DEFAULT_BACKEND_VOICE_URL "http://10.93.98.240:8000/chat/voice"
```

---

### Langkah 4: Kompilasi & Flash Firmware ESP32-S3
1. Muat environment ESP-IDF v5.5.2:
   ```bash
   source /Users/reggi/.espressif/v5.5.2/esp-idf/export.sh
   cd /Users/reggi/Projects/rbot/board
   ```
2. Lakukan kompilasi:
   ```bash
   idf.py build
   ```
3. Flash ke ESP32-S3:
   ```bash
   idf.py -p /dev/cu.usbmodem5A790640561 flash
   ```

---

### Langkah 5: Monitoring & Verifikasi Interaksi
Jalankan serial monitor:
```bash
idf.py -p /dev/cu.usbmodem5A790640561 monitor
```
Ketika sistem berhasil terhubung ke Wi-Fi dan modul WakeNet aktif, robot siap merespons:
```text
I (4352) WAKE_WORD_SR: ✅ Wake word task started on Core 1 (Listening for 'Alexa')
I (5772) wifi:connected with rara, rssi: -29
*** Rbot ready! Say 'Alexa' or send 'TALK' ***
```

---

## Sistem Animasi Wajah Kawaii Cat (ST7735 20 Ekspresi)

Wajah robot mengusung desain **Kawaii Robot Cat** beresolusi 128x160 piksel dengan kedalaman warna RGB565:
- **Mata Anime Berlapis:** Gradien pupil cyan-violet, refleksi ganda (*layered highlights*), dan kelopak mata lentur.
- **Aksesoris Karakter:** Telinga kucing 3D bersudut halus, pita merah muda (*volumetric bow*), dan lonceng emas di leher.
- **Gesture Cakar (Paws):** Sepasang cakar kucing putih yang beranimasi menopang dagu, melambai, atau memegang pipi sesuai kondisi emosi.
- **Status Header & Banner:** Indikator bar Wi-Fi, globe koneksi backend, persentase baterai, dan pill banner status di bagian bawah layar.

### 20 Daftar Ekspresi Karakter:
1. `EXCITED` – Mata berbinar bintang, melompat riang.
2. `SURPRISED` – Pupil membesar dengan mulut terbuka bulat.
3. `SQUINT` – Menyipitkan mata dengan cakar menyilang.
4. `WINK_LEFT` – Mengedipkan mata kiri.
5. `WINK_RIGHT` – Mengedipkan mata kanan.
6. `CONFUSED` – Alis miring asimetris dengan cakar memegang dagu.
7. `SLEEPY` – Kelopak terkantuk (layar otomatis meredup 50% untuk hemat daya).
8. `SAD` – Mata sayu meneteskan air mata kecil.
9. `HAPPY` – Senyum manis ramah.
10. `HAPPY_BIG` – Mata melengkung bulan sabit dengan tawa gembira.
11. `RECOVERED` – Tarikan nafas lega setelah kendala teratasi.
12. `NEUTRAL` – Tatapan ramah santai dalam kondisi siaga.
13. `LISTENING` – Telinga terangkat fokus dengan indikator gelombang suara.
14. `THINKING` – Pandangan melirik ke atas dengan kilauan inspirasi.
15. `SPEAKING` – Mulut bergerak dinamis sinkron dengan ritme suara audio.
16. `BLINK` – Kedipan alami periodik.
17. `CURIOUS_L` – Menoleh penasaran ke arah kiri.
18. `CURIOUS_R` – Menoleh penasaran ke arah kanan.
19. `ANGRY` – Alis mengerut tajam dengan rona pipi kemerahan.
20. `LOVE` – Pupil mata berbentuk hati berkilau penuh kasih.

---

## Dokumentasi API & Panel Analisis Latensi

### 1. Endpoint Utama: `POST /chat/voice`
- **Input:** Audio WAV / PCM 16 kHz Mono 16-bit dari rekaman ESP32-S3.
- **Output:** Audio WAV hasil respons AI yang telah diperkuat (+30% digital gain).
- **Format Header Respons:**
  - `X-Transcription`: Kalimat yang diucapkan pengguna.
  - `X-Response-Text`: Teks jawaban dari asisten AI.
  - `X-STT-Engine`: Mesin pengenal ucapan (`azure` / `gemini`).
  - `X-TTS-Engine`: Mesin sintesis suara (`edge_tts` / `azure` / `gtts`).
  - `X-Total-Latency-Ms`: Waktu total siklus dari request diterima hingga respons dikirim.

### 2. Panel Analisis Latensi Otomatis
Setiap selesai memproses ucapan, backend mencetak laporan profil performa di terminal:
```text
┌────────────────────────────────────────────────────────┐
│ ⏱️  RBOT PIPELINE LATENCY BENCHMARK                    │
├────────────────────────────────────────────────────────┤
│ 🎙️  STT  (GEMINI) :  1,620 ms  [  38.5% ]            │
│ 🧠  LLM  (GEMINI) :  1,450 ms  [  34.4% ]            │
│ 🔊  TTS  (EDGE_TTS) :  1,140 ms  [  27.1% ] (vol +30%)  │
├────────────────────────────────────────────────────────┤
│ 🚀  TOTAL DURATION:  4,210 ms                         │
│ ⚠️  BOTTLENECK    : STT (1,620 ms, 38.5%)               │
└────────────────────────────────────────────────────────┘
```

---

## Optimasi Audio, Daya & Troubleshooting

1. **Uji Mandiri Motor N20 dengan Baterai 18650:**
   Motor N20 dapat diuji coba secara langsung dengan menempelkan kedua kabel terminalnya ke kutub $(+)$ dan $(-)$ baterai 18650. Motor akan berputar lancar; membalikkan kabel akan mengubah arah putaran (maju vs mundur).
2. **Pencegahan Brownout & Reset ESP32-S3:**
   Arus lonjakan saat motor berakselerasi (*stall current*) dapat menyebabkan tegangan anjlok. Pastikan jalur daya motor mengambil arus langsung dari output baterai/BMS ke pin `VCC` DRV8833, bukan dari pin `3V3` regulator ESP32-S3.
3. **Pembersihan Derau Mikrofon (DC-Blocking Filter):**
   Filter digital IIR orde-1 memotong frekuensi liar di bawah 80 Hz, mereduksi deru motor dan kebisingan lantai ke level aman (-54 dBFS).
4. **Volume Speaker Kurang Keras:**
   Tingkat penguatan digital telah dinaikkan +30% secara default di backend (`TTS_VOLUME_BOOST=1.30`). Anda dapat menaikkannya lebih lanjut melalui `.env` (misalnya `TTS_VOLUME_BOOST=1.50`).
