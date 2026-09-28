/**
 * @file esp32_s3_inmp441_responder.ino
 * @brief Arduino C++ Responder Firmware for INMP441 I2S Mic on ESP32-S3
 * @details Protocol compatible with audio_audit.py:
 *          - Listens for "CMD_RECORD\n" via Serial (921600 or 115200)
 *          - Discards pre-roll 100 ms (1600 samples) startup transient
 *          - Sends "START_RECORD\n"
 *          - Applies 1st-Order IIR HPF (@ 90 Hz, alpha=0.96586) to remove low-frequency rumble & DC bias
 *          - Streams raw 16-bit Mono PCM @ 16kHz for 5 seconds (80,000 samples = 160,000 bytes)
 *          - Sends "END_RECORD\n"
 *
 * Hardware Pinout (ESP32-S3):
 *   - GPIO4 : I2S SCK / BCLK
 *   - GPIO1 : I2S WS  / LRCL
 *   - GPIO2 : I2S SD  / DOUT (Data in from Mic)
 *   - VDD   : 3.3V (Tambahkan kapasitor decoupling 100nF + 10uF dekat mic)
 *   - GND   : GND
 *   - L/R   : GND (Left channel mono)
 */

#include <Arduino.h>
#include <driver/i2s.h>

#define I2S_PORT            I2S_NUM_0
#define PIN_I2S_BCLK        4
#define PIN_I2S_WS          1
#define PIN_I2S_DIN         2

#define SAMPLE_RATE         16000
#define RECORD_TIME_SEC     5
#define TOTAL_SAMPLES       (SAMPLE_RATE * RECORD_TIME_SEC) // 80,000 samples
#define BUFFER_SAMPLES      256                             // Chunk size in samples
#define PREROLL_SAMPLES     1600                            // 100ms settling time (1600 samples @ 16kHz)

// 1st-Order IIR High-Pass Filter Coeff (Cutoff fc ≈ 90 Hz @ fs = 16,000 Hz)
// alpha = 1 / (1 + 2*pi*fc/fs) = 1 / (1 + 2*pi*90/16000) ≈ 0.96586f
static const float HPF_ALPHA = 0.96586f;

// Setup I2S Driver (Legacy driver kompatibel ESP32 Arduino Core 2.x & 3.x)
void setup_i2s() {
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = SAMPLE_RATE,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // INMP441 mengirim 24-bit dalam 32-bit slot
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,  // L/R pin dihubungkan ke GND
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_STAND_I2S),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = BUFFER_SAMPLES,
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0
    };

    i2s_pin_config_t pin_config = {
        .bck_io_num = PIN_I2S_BCLK,
        .ws_io_num = PIN_I2S_WS,
        .data_out_num = I2S_PIN_NO_CHANGE,
        .data_in_num = PIN_I2S_DIN
    };

    esp_err_t err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] i2s_driver_install failed: 0x%x\n", err);
        return;
    }

    err = i2s_set_pin(I2S_PORT, &pin_config);
    if (err != ESP_OK) {
        Serial.printf("[ERROR] i2s_set_pin failed: 0x%x\n", err);
        return;
    }

    i2s_zero_dma_buffer(I2S_PORT);
}

void stream_audio_audit() {
    // 1. Bersihkan buffer I2S lama
    i2s_zero_dma_buffer(I2S_PORT);

    int32_t raw_i2s_buffer[BUFFER_SAMPLES];
    int16_t pcm_16_buffer[BUFFER_SAMPLES];

    // 2. Discard Pre-Roll Samples (Muting Awal):
    // Buang 100 ms (1.600 sample) saat inisialisasi stream I2S untuk mengeliminasi lonjakan transien power-up mikrofon MEMS
    size_t preroll_discarded = 0;
    while (preroll_discarded < PREROLL_SAMPLES) {
        size_t bytes_to_read = sizeof(raw_i2s_buffer);
        size_t bytes_read = 0;
        esp_err_t res = i2s_read(I2S_PORT, raw_i2s_buffer, bytes_to_read, &bytes_read, pdMS_TO_TICKS(100));
        if (res == ESP_OK && bytes_read > 0) {
            preroll_discarded += (bytes_read / sizeof(int32_t));
        }
    }

    // 3. Reset State Filter IIR HPF
    float prev_x = 0.0f;
    float prev_y = 0.0f;

    // 4. Kirim frame marker awal
    Serial.println("START_RECORD");
    Serial.flush();
    delay(20);

    size_t samples_captured = 0;

    while (samples_captured < TOTAL_SAMPLES) {
        size_t bytes_to_read = sizeof(raw_i2s_buffer);
        size_t bytes_read = 0;

        esp_err_t res = i2s_read(I2S_PORT, raw_i2s_buffer, bytes_to_read, &bytes_read, pdMS_TO_TICKS(500));
        if (res == ESP_OK && bytes_read > 0) {
            size_t num_samples = bytes_read / sizeof(int32_t);

            for (size_t i = 0; i < num_samples; i++) {
                // INMP441 memetakan 24-bit data pada bit [31:8] di dalam slot 32-bit.
                // Shifting 14-bit secara optimal mengonversi ke 16-bit PCM tanpa kehilangan headroom.
                int32_t sample_shifted = raw_i2s_buffer[i] >> 14;

                // Terapkan Digital 1st-Order IIR High-Pass Filter (HPF @ ~90 Hz)
                // Difference equation: y[n] = alpha * (y[n-1] + x[n] - x[n-1])
                float x = (float)sample_shifted;
                float y = HPF_ALPHA * (prev_y + x - prev_x);
                prev_x = x;
                prev_y = y;

                // Clipping limiter ke batas signed 16-bit
                if (y > 32767.0f) y = 32767.0f;
                else if (y < -32768.0f) y = -32768.0f;

                pcm_16_buffer[i] = (int16_t)y;
            }

            // Stream raw PCM binary ke Serial
            size_t bytes_to_send = num_samples * sizeof(int16_t);
            Serial.write((const uint8_t*)pcm_16_buffer, bytes_to_send);

            samples_captured += num_samples;
        }
    }

    Serial.flush();
    delay(20);
    // Kirim frame marker akhir
    Serial.println("\nEND_RECORD");
    Serial.flush();
}

void setup() {
    // Inisialisasi UART Serial baudrate tinggi (921600 atau 115200)
    Serial.begin(921600);
    while (!Serial && millis() < 3000);

    Serial.println("\n===========================================");
    Serial.println(" ESP32-S3 INMP441 Audio Stream Responder");
    Serial.println(" DSP Chain: 100ms Pre-Roll Discard + 90Hz HPF");
    Serial.println(" Ready for audio_audit.py handshake...");
    Serial.println("===========================================");

    setup_i2s();
}

void loop() {
    if (Serial.available() > 0) {
        String cmd = Serial.readStringUntil('\n');
        cmd.trim();

        if (cmd == "CMD_RECORD" || cmd == "AUDIT_START" || cmd == "RECORD") {
            Serial.println("HANDSHAKE_OK: Starting 5s Audio Stream...");
            stream_audio_audit();
        }
    }
}
