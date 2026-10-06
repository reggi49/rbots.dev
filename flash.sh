#!/bin/bash
set -e

echo "================================================================="
echo "⚡ RBOT ESP32-S3 FIRMWARE FLASH SCRIPT"
echo "================================================================="

# 1. Cari port ESP32-S3
PORT=$(ls /dev/cu.usbmodem* 2>/dev/null | head -n 1)
if [ -z "$PORT" ]; then
    PORT=$(ls /dev/cu.usbserial* 2>/dev/null | head -n 1)
fi

if [ -z "$PORT" ]; then
    echo "❌ Error: ESP32-S3 tidak terdeteksi di /dev/cu.usbmodem* atau /dev/cu.usbserial*!"
    echo "   Pastikan kabel USB terpasang ke port USB (native/uart) di board."
    exit 1
fi

echo "[*] Port terdeteksi: $PORT"

# 2. Source ESP-IDF Environment
ESP_IDF_PATH="/Users/reggi/.espressif/v5.5.2/esp-idf/export.sh"
if [ -f "$ESP_IDF_PATH" ]; then
    echo "[*] Mengaktifkan lingkungan ESP-IDF v5.5.2..."
    source "$ESP_IDF_PATH" > /dev/null 2>&1
else
    echo "⚠️ Warning: $ESP_IDF_PATH tidak ditemukan, mencoba melanjutkan..."
fi

# 3. Build Firmware
echo "[*] Melakukan build firmware..."
cd "$(dirname "$0")/board"
idf.py build

# 4. Flashing ke ESP32-S3
echo "[*] Memulai flashing ke $PORT..."
cd build
python -m esptool --chip esp32s3 -p "$PORT" -b 460800 --before default_reset --after hard_reset write_flash "@flash_args"

echo ""
echo "================================================================="
echo "✅ FLASH BERHASIL 100%!"
echo "================================================================="
echo "💡 PENTING (Agar ESP32-S3 booting sempurna):"
echo "   1. Tekan tombol RESET / EN pada board ESP32-S3 Anda sekali,"
echo "      ATAU cabut & pasang kembali kabel USB (cold power cycle)."
echo "   2. Layar LCD ST7735 akan langsung menampilkan wajah & status 'RBOT READY'."
echo "   3. Untuk mencoba rekam -> putar di speaker, jalankan:"
echo "      python parrot_voice.py"
echo "      (atau sentuh pin GPIO 14 ke GND sesaat)"
echo "================================================================="
