# Reggi Bot (rbot) 🤖🎙️

> **Robot Percakapan Suara Interaktif Berbasis ESP32-S3, Layar Wajah Ekspresif ST7735, Wake Word Lokal WakeNet10, dan Cloud AI Pipeline (Gemini + Azure / Edge-TTS).**

---

## Daftar Isi
1. [Ringkasan Proyek & Arsitektur](#ringkasan-proyek--arsitektur)
2. [Fitur Utama](#fitur-utama)
3. [Teknologi & Stack Perangkat](#teknologi--stack-perangkat)
4. [Skema Pinout Perangkat Keras (Hardware Pinout)](#skema-pinout-perangkat-keras-hardware-pinout)
5. [Struktur Folder & Direktori](#struktur-folder--direktori)
6. [Panduan Menjalankan Sistem (Step-by-Step)](#panduan-menjalankan-sistem-step-by-step)
   - [Langkah 1: Menyiapkan Jaringan Wi-Fi](#langkah-1-menyiapkan-jaringan-wi-fi)
   - [Langkah 2: Menyiapkan dan Menjalankan Backend Server](#langkah-2-menyiapkan-dan-menjalankan-backend-server)
   - [Langkah 3: Konfigurasi Target Firmware](#langkah-3-konfigurasi-target-firmware)
   - [Langkah 4: Kompilasi & Flash Firmware ESP32-S3](#langkah-4-kompilasi--flash-firmware-esp32-s3)
   - [Langkah 5: Monitoring & Verifikasi Interaksi](#langkah-5-monitoring--verifikasi-interaksi)
7. [Spesifikasi Wake Word Lokal (WakeNet10 "Alexa")](#spesifikasi-wake-word-lokal-wakenet10-alexa)
8. [Sistem Animasi Wajah Organik (ST7735 Face Engine)](#sistem-animasi-wajah-organik-st7735-face-engine)
9. [Dokumentasi API Backend](#dokumentasi-api-backend)
10. [Optimasi Audio & Troubleshooting](#optimasi-audio--troubleshooting)

---

## Ringkasan Proyek & Arsitektur

Reggi Bot adalah asisten robot fisik cerdas yang mampu berinteraksi suara dua arah secara alami dalam Bahasa Indonesia. Sistem bekerja dalam mode **setengah dupleks (half-duplex)** dengan pipeline suara terintegrasi dan respons emosi visual real-time melalui layar TFT.

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           REGGI BOT INTERACTION                         │
└─────────────────────────────────────────────────────────────────────────┘
   [Pengguna]
       │ Mengucapkan kata "Alexa" atau menekan tombol GPIO 14
       ▼
 ┌───────────┐      I2S0 (RX)     ┌────────────────────────────────────┐
 │  INMP441  │ ─────────────────> │ ESP32-S3 (ESP-SR WakeNet10)        │
 │ Microphone│                    │ - Deteksi lokal "Alexa" (wn10)     │
 └───────────┘                    │ - Countdown 10 detik & VAD         │
                                  │ - Tampilan wajah "LISTENING"       │
                                  └─────────────────┬──────────────────┘
                                                    │
                                                    │ HTTP POST /chat/voice
                                                    │ (WAV PCM 16kHz Mono)
                                                    ▼
                                  ┌────────────────────────────────────┐
                                  │ BACKEND FASTAPI (Laptop / Server)  │
                                  │ 1. STT: Azure Speech / Gemini      │
                                  │ 2. LLM: Gemini 3.5 Flash Lite      │
                                  │ 3. TTS: Azure Speech / Edge-TTS    │
                                  └─────────────────┬──────────────────┘
                                                    │
                                                    │ Response Audio WAV
                                                    │ (16kHz Mono 16-bit)
                                                    ▼
 ┌───────────┐      I2S1 (TX)     ┌────────────────────────────────────┐
 │ MAX98357A │ <───────────────── │ ESP32-S3 Playback & Display        │
 │ Amplifier │                    │ - Streaming I2S Playback           │
 │     +     │                    │ - Tampilan wajah "SPEAKING"        │
 │  Speaker  │                    │ - Transisi "HAPPY_SOFT" -> "IDLE"  │
 └───────────┘                    └────────────────────────────────────┘
```

---

## Fitur Utama

- **Local Wake Word Akurat:** Menggunakan model **WakeNet10** (`wn10_alexa`) dari Espressif ESP-SR 2.5.5 dengan kuantisasi `w8a16` dan akselerasi vektor hardware ESP32-S3.
- **Pembersihan Noise Akustik:** Dilengkapi software *DC-blocking High-Pass Filter* ($f_c \approx 80\text{ Hz}$) yang mereduksi noise floor INMP441 dari RMS ~1000 ke RMS <45 (-54 dB).
- **Layar Ekspresif Non-Blocking:** Engine animasi wajah berbasis 5 eye-shape families yang berjalan di Core 1 (20 FPS) tanpa membebani atau menghentikan audio streaming I2S.
- **Countdown Interaktif & Auto-Stop:** Perekaman otomatis mendengarkan hingga 10 detik dengan hitung mundur visual di layar, serta otomatis berhenti jika keheningan terdeteksi (Voice Activity Detection / VAD).
- **Dual-Engine Cloud AI:** 
  - STT: Azure Cognitive Speech dengan failover otomatis ke Gemini Transcribe.
  - LLM: Google Gemini 3.5 Flash Lite dengan optimasi respons ramah suara pendek (40–50 kata).
  - TTS: Azure Neural TTS (`id-ID-ArdiNeural`) dengan failover otomatis ke Edge-TTS.

---

## Teknologi & Stack Perangkat

### 1. Hardware
| Komponen | Spesifikasi / Model | Keterangan |
| :--- | :--- | :--- |
| **Microcontroller** | ESP32-S3 DevKit N16R8 | 16 MB SPI Flash, 8 MB Octal PSRAM |
| **Mikrofon** | INMP441 MEMS I2S | Digital I2S Microphone (Port I2S0) |
| **Audio DAC / Amp** | MAX98357A Mono Amplifier | Digital I2S Class D Amp (Port I2S1) |
| **Speaker** | 4Ω 3W Full-Range Speaker | Terhubung ke terminal MAX98357A |
| **Display** | ST7735 1.8" SPI TFT LCD | Resolusi 128x160 RGB, rotasi horizontal |
| **Trigger Tombol** | Tactile Switch / Touch Pad | Push-To-Talk terhubung ke GPIO 14 |

### 2. Embedded Software (Firmware)
- **Framework:** ESP-IDF v5.5.2
- **Komponen AI & DSP:** 
  - `espressif/esp-sr` (v2.5.5)
  - `espressif/esp-dl` (v3.3.13)
  - `espressif/esp-nn` (v1.4.1)
  - `espressif/esp-dsp` (v1.8.2)
  - `espressif/esp-tflite-micro` (v1.4.1)
- **Partisi Flash:** Kustom 4 MB dedicated `model` partition (`0x210000`).

### 3. Backend & Cloud AI
- **Runtime:** Python 3.13
- **Web Framework:** FastAPI + Uvicorn (Asynchronous HTTP)
- **LLM SDK:** `google-genai` (Model: `gemini-3.5-flash-lite`)
- **Speech SDK:** Azure Cognitive Services REST API & `edge-tts`
- **Audio Processing:** `ffmpeg` (konversi format WAV 16kHz mono)

---

## Skema Pinout Perangkat Keras (Hardware Pinout)

File konfigurasi pin utama berada di [`board/include/board_pins.h`](board/include/board_pins.h).

> [!IMPORTANT]
> Mikrofon (I2S0) dan Speaker (I2S1) menggunakan kontroler I2S terpisah untuk menghindari konflik clock generator. Pin strapping (GPIO0, 3, 45, 46) dan Octal PSRAM (GPIO35, 36, 37) tidak digunakan.

### 1. Mikrofon INMP441 (I2S0 RX)
| Pin INMP441 | Pin ESP32-S3 | Fungsi / Deskripsi |
| :--- | :--- | :--- |
| **VDD** | 3.3V | Catu daya modul mikrofon |
| **GND** | GND | Ground bersama |
| **SD** | **GPIO 2** | Serial Data Input |
| **SCK / BCLK** | **GPIO 4** | Continuous Serial Clock |
| **WS / LRCLK** | **GPIO 1** | Word Select (Left/Right clock) |
| **L/R** | **GND** | Konfigurasi kanal kiri (Left Channel) |

### 2. Amplifier MAX98357A (I2S1 TX)
| Pin MAX98357A | Pin ESP32-S3 | Fungsi / Deskripsi |
| :--- | :--- | :--- |
| **VIN** | 5V / 3.3V | Catu daya amplifier (disarankan 5V untuk daya optimal) |
| **GND** | GND | Ground bersama |
| **DIN** | **GPIO 6** | Digital Audio Data Output |
| **BCLK** | **GPIO 11** | Bit Clock Output |
| **LRC / WS** | **GPIO 12** | Left/Right Word Clock Output |
| **GAIN** | GND | Gain default (+9dB) |
| **SD_MODE** | Floating / Pull-up | Mode aktif mono mix |

### 3. Layar ST7735 1.8" TFT (SPI)
| Pin Layar ST7735 | Pin ESP32-S3 | Fungsi / Deskripsi |
| :--- | :--- | :--- |
| **VCC** | 3.3V / 5V | Catu daya modul display |
| **GND** | GND | Ground bersama |
| **CS** | **GPIO 7** | SPI Chip Select |
| **SCK / SCL** | **GPIO 8** | SPI Clock |
| **SDA / MOSI** | **GPIO 9** | SPI Master Out Slave In |
| **A0 / DC** | **GPIO 10** | Data / Command Control |
| **RESET / RST** | **GPIO 16** | Hardware Reset Pin |
| **LED / BL** | **GPIO 15** (atau 3.3V) | Backlight Display Control |

### 4. Tombol Push-To-Talk (PTT)
| Pin Tombol | Pin ESP32-S3 | Fungsi / Deskripsi |
| :--- | :--- | :--- |
| **Terminal 1** | **GPIO 14** | Input digital (Internal Pull-Up aktif) |
| **Terminal 2** | GND | Terhubung ke ground saat tombol ditekan (Active-Low) |

---

## Struktur Folder & Direktori

```
rbot/
├── README.md                      # Dokumentasi utama proyek
├── backend/                       # Layanan Cloud & AI Backend (FastAPI)
│   ├── .env                       # Variabel rahasia (API Keys)
│   ├── requirements.txt           # Dependensi pustaka Python
│   ├── server.py                  # Entry point server audio STT-LLM-TTS
│   └── deploy.sh                  # Skrip deployment service
├── board/                         # Firmware ESP32-S3 (ESP-IDF)
│   ├── CMakeLists.txt             # Konfigurasi CMake tingkat atas
│   ├── dependencies.lock          # Lock file komponen ESP-IDF
│   ├── partitions_singleapp.csv   # Tabel partisi flash (4MB model, 2MB app)
│   ├── sdkconfig                  # Konfigurasi Kconfig kernel & driver
│   ├── include/                   # Header publik firmware
│   │   ├── board_pins.h           # Single source of truth seluruh GPIO
│   │   ├── board_audio.h          # Abstraksi I2S Mic dan Speaker
│   │   ├── board_display.h        # Abstraksi driver ST7735
│   │   ├── board_face.h           # Prototipe engine animasi wajah
│   │   └── board_network.h        # Manajemen Wi-Fi dan HTTP lock
│   ├── src/                       # Implementasi driver perangkat keras
│   │   ├── board_audio.c          # Inisialisasi driver I2S0 dan I2S1
│   │   ├── board_display.c        # Inisialisasi SPI & konfigurasi ST7735
│   │   ├── board_face.c           # Rendering bentuk mata, kedipan, & ekspresi
│   │   └── board_network.c        # Koneksi Wi-Fi STA (SSID & Password)
│   └── main/                      # Logika aplikasi robot percakapan
│       ├── CMakeLists.txt         # Kompilasi modul aplikasi utama
│       ├── idf_component.yml      # Deklarasi komponen (esp-sr 2.5.5, dll)
│       ├── board.c                # app_main(), state machine, CLI handler
│       ├── voice_chat.c           # Pipeline percakapan (record -> HTTP -> play)
│       ├── voice_chat.h           # Definisi URL backend & trigger
│       ├── wake_word_sr.c         # Engine WakeNet10, HPF filter, VAD
│       └── wake_word_sr.h         # API publik kontrol wake word
└── .venv/                         # Lingkungan Python terisolasi
```

---

## Panduan Menjalankan Sistem (Step-by-Step)

Untuk menjalankan seluruh sistem dengan lancar, **selalu jalankan Backend Server terlebih dahulu** sebelum menyalakan atau mem-flash ESP32-S3.

---

### Langkah 1: Menyiapkan Jaringan Wi-Fi

ESP32-S3 dan laptop yang menjalankan backend harus berada di jaringan Wi-Fi lokal yang sama.

Kredensial Wi-Fi terkonfigurasi di [`board/src/board_network.c`](board/src/board_network.c):
```c
#define WIFI_SSID "rara"
#define WIFI_PASS "123456788"
```
*Jika menggunakan Wi-Fi lain, ubah nilai di atas sebelum mengompilasi firmware.*

---

### Langkah 2: Menyiapkan dan Menjalankan Backend Server

#### 1. Masuk ke direktori backend dan siapkan environment
```bash
cd /Users/reggi/Projects/rbot
source .venv/bin/activate
```

#### 2. Konfigurasi file `backend/.env`
Pastikan file `backend/.env` berisi API key yang valid:
```env
# Gemini API Key (Wajib untuk LLM & fallback STT)
AISTUDIO_API_KEY=AIzaSyDAos8...

# Azure Speech Key & Region (Opsional, otomatis fallback ke Gemini & Edge-TTS jika tidak diisi)
AZURE_SPEECH_KEY=BOSKDU4AX5XDhSJR...
AZURE_SPEECH_REGION=southeastasia
```

#### 3. Jalankan server FastAPI
```bash
python backend/server.py
```
Server akan aktif di port `8000`:
```text
INFO:     Started server process [7493]
INFO:     Waiting for application startup.
INFO:     Application startup complete.
INFO:     Uvicorn running on http://0.0.0.0:8000 (Press CTRL+C to quit)
```

#### 4. Dapatkan Alamat IP Lokal Laptop
Buka terminal baru untuk melihat IP lokal laptop di jaringan Wi-Fi:
```bash
ipconfig getifaddr en0
# Contoh output: 10.93.98.240
```

#### 5. Uji endpoint kesehatan backend
```bash
curl http://127.0.0.1:8000/health
```
Respons yang diharapkan:
```json
{"status":"online","service":"rbot-backend","gemini_model":"gemini-3.5-flash-lite","azure_active":false,"azure_speech_region":"southeastasia","voice":"id-ID-ArdiNeural"}
```

---

### Langkah 3: Konfigurasi Target Firmware

Buka file [`board/main/voice_chat.h`](board/main/voice_chat.h) dan pastikan alamat IP backend sesuai dengan IP laptop Anda:
```c
#define DEFAULT_BACKEND_VOICE_URL "http://10.93.98.240:8000/chat/voice"
```
*(Ganti `10.93.98.240` jika IP laptop Anda berbeda).*

---

### Langkah 4: Kompilasi & Flash Firmware ESP32-S3

#### 1. Aktifkan Environment ESP-IDF
```bash
source /Users/reggi/.espressif/v5.5.2/esp-idf/export.sh
cd /Users/reggi/Projects/rbot/board
```

#### 2. Kompilasi Firmware & Model WakeNet10
Perintah ini akan mengompilasi program C/C++ dan secara otomatis mengemas model WakeNet10 ke biner `build/srmodels/srmodels.bin`:
```bash
idf.py build
```

#### 3. Hubungkan ESP32-S3 ke Port USB
Periksa nama port USB yang terdeteksi:
```bash
ls /dev/cu.usb*
# Contoh: /dev/cu.usbmodem5A790640561
```

#### 4. Flash Seluruh Partisi (Bootloader, Partition Table, App, Model)
Gunakan utilitas `esptool`:
```bash
/Users/reggi/.espressif/python_env/idf5.5_py3.13_env/bin/python -m esptool \
  --chip esp32s3 \
  -p /dev/cu.usbmodem5A790640561 \
  -b 460800 \
  --before default_reset \
  --after hard_reset \
  write_flash \
  --flash_mode dio \
  --flash_size 16MB \
  --flash_freq 80m \
  0x8000 build/partition_table/partition-table.bin \
  0x10000 build/rbot.bin \
  0x210000 build/srmodels/srmodels.bin
```

---

### Langkah 5: Monitoring & Verifikasi Interaksi

Buka monitor serial untuk melihat log perangkat:
```bash
idf.py -p /dev/cu.usbmodem5A790640561 monitor
```

Log bootloader yang menandakan sistem siap:
```text
I (3372) MODEL_LOADER: The partition size is 4096 KB
I (3382) WAKE_WORD_SR: Discovered 1 models in partition 'model':
I (3382) WAKE_WORD_SR:   [0] Name: wn10_alexa | Info: wakenet10_tts4h12q16p1_alexa_3_0.6_0.609
I (3392) WAKE_WORD_SR: 🎯 Selected PRIMARY WakeNet10 Model: wn10_alexa (Wake Word: 'Alexa')
I (4342) WAKE_WORD_SR: ✅ ESP-SR AFE initialized with model 'wn10_alexa'
I (4352) WAKE_WORD_SR: ✅ Wake word task started on Core 1 (Listening for 'Alexa')
I (5772) wifi:connected with rara, rssi: -29
*** Rbot ready! Say 'Alexa' or send 'TALK' / touch GPIO 14 ***
```

---

## Spesifikasi Wake Word Lokal (WakeNet10 "Alexa")

1. **Frasa Wake Word:** `"Alexa"`
2. **Model Machine Learning:** `wn10_alexa` (`wakenet10_tts4h12q16p1_alexa_3_0.6_0.609`)
3. **Ambang Batas Deteksi (Threshold):** `0.60` (Sangat sensitif dan toleran terhadap berbagai aksen).
4. **Mode Operasi Tunggal:** Model sekunder dimatikan (`afe_config->wakenet_model_name_2 = NULL`) untuk mencegah perebutan alokasi resource memori dan false-positive dari model lain.
5. **Alternatif Trigger:** Jika berada dalam ruangan sangat bising, percakapan dapat dipicu menggunakan:
   - Tombol PTT fisik pada **GPIO 14**.
   - Perintah UART CLI: ketik `TALK` di serial terminal.

---

## Sistem Animasi Wajah Organik (ST7735 Face Engine)

Layar ST7735 menampilkan wajah robot yang hidup dan responsif secara real-time:

| Status Robot | Ekspresi Wajah | Indikator Visual |
| :--- | :--- | :--- |
| `IDLE` | Santai / Ramah | Mata oval organik dengan animasi berkedip periodik acak (2–6 detik) |
| `LISTENING` | Mata Membuka Lebar | Muncul teks countdown countdown (`LISTENING 10` ... `LISTENING 1`) |
| `THINKING` | Mata Melirik / Berpikir | Animasi orbit dan pandangan berganti |
| `SPEAKING` | Mata Senang & Berkedip Dinamis | Mulut/mata berirama mengikuti jalannya pemutaran audio |
| `HAPPY_SOFT` | Mata Melengkung Bahagia | Ditampilkan selama 1.2 detik setelah robot selesai menjawab |
| `ERROR` | Mata Menyipit / Cemas | Ditampilkan jika terjadi kegagalan jaringan atau HTTP error |

> **Catatan Performa:** Rendering menggunakan buffer memori internal dan transfer DMA SPI non-blocking pada 20 frame per detik, sehingga tidak pernah terjadi stuttering suara saat wajah bergerak.

---

## Dokumentasi API Backend

### 1. `POST /chat/voice` (Endpoint Utama Robot)
- **Content-Type Input:** `audio/wav` atau `application/octet-stream` (WAV/PCM 16 kHz, 16-bit, Mono)
- **Content-Type Output:** `audio/wav` (WAV PCM 16 kHz, 16-bit, Mono)
- **Custom Response Headers:**
  - `X-Transcription`: Teks hasil transkripsi ucapan pengguna.
  - `X-Response-Text`: Teks jawaban yang dihasilkan oleh Gemini.
  - `X-STT-Engine`: Mesin STT yang digunakan (`azure` atau `gemini`).
  - `X-TTS-Engine`: Mesin TTS yang digunakan (`azure` atau `edge_tts`).
  - `X-Total-Latency-Ms`: Waktu total pemrosesan pipeline audio.

### 2. `GET /health`
Mengembalikan status kesiapan server dan konfigurasi model:
```json
{
  "status": "online",
  "service": "rbot-backend",
  "gemini_model": "gemini-3.5-flash-lite",
  "azure_active": false,
  "azure_speech_region": "southeastasia",
  "voice": "id-ID-ArdiNeural"
}
```

### 3. `POST /test/tts`
Menguji sintesis suara TTS langsung:
```bash
curl -X POST http://127.0.0.1:8000/test/tts \
  -H "Content-Type: application/json" \
  -d '{"text": "Halo, ini tes suara robot."}' \
  --output test.wav
```

---

## Optimasi Audio & Troubleshooting

1. **Noise Floor Mikrofon INMP441:**
   Modul INMP441 memiliki bawaan DC-bias offset yang dapat merusak akurasi deteksi neural network. Firmware rbot menyertakan filter digital HPF IIR orde-1:
   $$y[n] = 0.96586 \times (y[n-1] + x[n] - x[n-1])$$
   Filter ini memotong frekuensi di bawah 80 Hz dan menurunkan noise dasar ke -54 dBFS.
2. **Koneksi Wi-Fi Gagal:**
   Pastikan antena onboard ESP32-S3 tidak tertutup logam dan router mengizinkan koneksi 2.4 GHz (ESP32-S3 tidak mendukung band 5 GHz murni).
3. **Suara Speaker Berderau Saat Idle:**
   Pin `SD_MODE` pada MAX98357A dapat dibiarkan mengambang untuk mode mono stereo mix. Pastikan kabel Ground antara mikrofon, amplifier, dan ESP32-S3 terhubung rapat ke titik ground bersama (*star ground*).
4. **Latency Percakapan:**
   Total waktu tunggu jawaban berkisar antara 2.5 hingga 4.5 detik tergantung kecepatan koneksi internet ke Google AI Studio dan Microsoft Azure Speech.
