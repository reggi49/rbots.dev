# CODEX.md — ESP32 AI ENGINEERING CONSTITUTION

## 0. Project Identity
Project Type : ESP32 AI Robot / IoT System
MCU          : ESP32-C3 / ESP32 family
Framework    : ESP-IDF
Language     : C (C99)
Build System : idf.py
Architecture : Backend (board) + Frontend (UI)

---

## 1. Repository Structure (FIX)

Repository ini SELALU terdiri dari dua domain:
- Backend
- Board
- Frontend

RULES:
- Backend =  API, AI, network
- Board = hardware, driver, RTOS, audio,
- Frontend = UI, state, rendering, UX logic

- ❌ DILARANG mencampur logic backend ke frontend atau sebaliknya

---

## 2. 🔒 HARDWARE PIN MAPPING (ABSOLUTE — TIDAK BOLEH DIUBAH)

### 2.1 TFT ST7735 (SPI)
- SCK   → GPIO8
- MOSI  → GPIO9
- DC    → GPIO10
- CS    → GPIO7
- RST   → **BUKAN GPIO21**
  - Gunakan **3V3** atau **GPIO3**

### 2.2 Microphone — INMP441 (I2S INPUT)
- WS / LRCLK → GPIO1
- BCLK       → GPIO4
- SD / DOUT  → GPIO2
- L/R        → GND

### 2.3 Speaker Amp — MAX98357A (I2S OUTPUT)
- LRCLK → GPIO1
- BCLK  → GPIO4
- DIN   → GPIO6

### 2.4 Touch Input
- OUT → GPIO0

🚨 ABSOLUTE RULE:
- ❌ PIN MAPPING **TIDAK BOLEH DIUBAH**
- ❌ JANGAN auto-remap
- ❌ JANGAN buat fallback pin
- ❌ JANGAN optimasi dengan ganti GPIO

Jika ada konflik → STOP dan LAPOR.

---

## 3. HARD RULES (NON-NEGOTIABLE)

❌ DILARANG:
- Mengubah pin mapping
- Menghapus atau menurunkan logging penting
- Refactor besar tanpa task eksplisit

✅ WAJIB:
- Error handling eksplisit
- Early return on failure
- Build & flash harus tetap berhasil

---

## 4. Logging & Evidence Rule (WAJIB ADA BUKTI)

Setiap perubahan HARUS menghasilkan **log signature** yang bisa dipakai sebagai bukti.

### 4.1 Log Signature Examples
Contoh log yang dianggap VALID:

- `WAKE_ACCEPT`
- `WAKE_REJECT`
- `PIPE_STAGE_MIC_OK`
- `PIPE_STAGE_TFLM_OK`
- `PIPE_STAGE_TTS_OK`
- `PIPE_ORDER_OK`
- `NO_LOCK_BUSY`
- `I2S_STREAM_START`
- `I2S_STREAM_STOP`

❌ Perubahan TANPA log signature = INVALID  
❌ Silent fix = DITOLAK

---

## 5. PASS / FAIL Contract

Setiap perubahan harus bisa diuji via log:

| Kondisi | Bukti |
|------|------|
| Success | Log PASS muncul |
| Failure | Log FAIL muncul |
| Deadlock | Log NO_LOCK_BUSY |

Jika tidak bisa dibuktikan lewat log → perubahan dianggap gagal.

---

## 6. Board Rules
- ingat alur ini wifi->wakeword->STT->AI->TTS
- Driver HARUS deterministic
- Jangan blocking tanpa timeout
- Jangan malloc di ISR / audio loop
- Semua init failure HARUS return error

Audio / I2S:
- Shared clock (GPIO1 / GPIO4) HARUS konsisten
- Jangan override config sepihak

---

## 7. Frontend Rules (UI)

- Frontend hanya menerima state
- ❌ Tidak boleh akses GPIO langsung
- ❌ Tidak boleh akses driver backend

Frontend hanya:
- render
- animate
- reflect system state

---

## 8. Build, Flash, Runner Discipline

Untuk SETIAP perubahan besar:
- ✅ Build harus sukses
- ✅ Flash harus sukses
- ❌ Tidak boleh hanya compile-only

Jika perubahan menyebabkan:
- build gagal
- flash gagal
→ PATCH WAJIB diperbaiki sebelum lanjut

---

## 9. Change Discipline

- 1 task = 1 masalah
- Patch sekecil mungkin
- Jangan “sekalian dirapihin”

Jika perubahan berisiko:
➡️ STOP dan MINTA KONFIRMASI

---

## 10. Definition of Done (FINAL)

Perubahan dianggap DONE jika:
- Build sukses
- Flash sukses
- Pin mapping TIDAK berubah
- Log PASS/FAIL muncul
- Tidak ada regression

EOF
