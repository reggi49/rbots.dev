#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_flash.h"
#include "esp_heap_caps.h"

#define TAG "PSRAM_TEST"
#define TARGET_ALLOC_BYTES (7 * 1024 * 1024 + 512 * 1024)  // ~7.5MB
#define EXPECTED_PSRAM_BYTES (8 * 1024 * 1024)
#define EXPECTED_FLASH_BYTES (16 * 1024 * 1024)

static inline uint32_t prng_step(uint32_t *state) {
    *state ^= *state << 13;
    *state ^= *state >> 17;
    *state ^= *state << 5;
    return *state;
}

static void fill_pattern(uint8_t *buffer, size_t length, uint32_t seed) {
    uint32_t prng = seed;
    for (size_t i = 0; i < length; ) {
        prng = prng_step(&prng);
        uint32_t word = prng;
        size_t remaining = length - i;
        size_t chunk = remaining >= 4 ? 4 : remaining;
        for (size_t b = 0; b < chunk; ++b, ++i) {
            buffer[i] = (word >> (8 * b)) & 0xFF;
        }
    }
}

static bool verify_pattern(const uint8_t *buffer, size_t length, uint32_t seed,
                           size_t *bad_index, uint8_t *expected, uint8_t *found) {
    uint32_t prng = seed;
    for (size_t i = 0; i < length; ) {
        prng = prng_step(&prng);
        uint32_t word = prng;
        size_t remaining = length - i;
        size_t chunk = remaining >= 4 ? 4 : remaining;
        for (size_t b = 0; b < chunk; ++b, ++i) {
            uint8_t exp = (word >> (8 * b)) & 0xFF;
            uint8_t got = buffer[i];
            if (exp != got) {
                if (bad_index) *bad_index = i;
                if (expected) *expected = exp;
                if (found) *found = got;
                return false;
            }
        }
    }
    return true;
}

void app_main(void) {
    ESP_LOGI(TAG, "Starting PSRAM stress test (alloc=%zu bytes)", (size_t)TARGET_ALLOC_BYTES);

    uint32_t flash_size = 0;
    esp_err_t flash_err = esp_flash_get_size(NULL, &flash_size);
    if (flash_err == ESP_OK) {
        ESP_LOGI(TAG, "Detected flash size from runtime: %u bytes", flash_size);
    } else {
        ESP_LOGE(TAG, "Flash size query failed: %s", esp_err_to_name(flash_err));
    }

    size_t psram_total = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    ESP_LOGI(TAG, "PSRAM total=%zu bytes, free=%zu bytes", psram_total, psram_free);
    bool psram_size_ok = psram_total >= EXPECTED_PSRAM_BYTES;
    if (!psram_size_ok) {
        ESP_LOGE(TAG, "PSRAM size below expected 8MB for N16R8");
    }

    uint8_t *buffer = heap_caps_malloc(TARGET_ALLOC_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (!buffer) {
        ESP_LOGE(TAG, "PSRAM alloc failed for %zu bytes (free=%zu).", (size_t)TARGET_ALLOC_BYTES, psram_free);
    }

    bool verify_ok = false;
    bool alloc_ok = buffer != NULL;
    uint32_t seed = esp_random();
    size_t bad_index = 0;
    uint8_t expected = 0;
    uint8_t found = 0;

    if (buffer) {
        ESP_LOGI(TAG, "Using pattern seed 0x%08x", seed);
        fill_pattern(buffer, TARGET_ALLOC_BYTES, seed);
        ESP_LOGI(TAG, "Write pass complete, starting verify pass");
        verify_ok = verify_pattern(buffer, TARGET_ALLOC_BYTES, seed, &bad_index, &expected, &found);
        if (!verify_ok) {
            ESP_LOGE(TAG, "Data mismatch at offset %zu (expected=0x%02x, got=0x%02x)", bad_index, expected, found);
        } else {
            ESP_LOGI(TAG, "Verify pass complete, no mismatches detected");
        }
    }

    bool flash_ok = (flash_err == ESP_OK) && (flash_size >= EXPECTED_FLASH_BYTES);
    if (!flash_ok) {
        ESP_LOGE(TAG, "Flash size below 16MB or query failed");
    }

    const bool verdict_ok = alloc_ok && verify_ok && psram_size_ok && flash_ok;
    ESP_LOGW(TAG, "VERDICT: %s", verdict_ok ? "REAL N16R8" : "FAKE/INCORRECT LABEL");

    if (buffer) {
        heap_caps_free(buffer);
    }

    ESP_LOGI(TAG, "Test complete. Holding output for monitor...");
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
