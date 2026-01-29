// [1.1.48] Simplified Edge Impulse Wake Word Runtime
// Removed unused custom TFLite interpreter path to free ~94KB RAM for EI SDK
static const char *TAG_WW = "WW";

#include <cstdint>
#include <cstddef>
#include "esp_log.h"

// Edge Impulse SDK
#include "edge-impulse-sdk/classifier/ei_run_classifier.h"

static bool ei_initialized = false;

// -------------------------------------------------
// Static int16 PCM buffer pointer for callback
// -------------------------------------------------
static const int16_t *g_pcm_ptr = nullptr;
static size_t g_pcm_samples = 0;

// [1.1.48] Callback for signal_t - converts int16 PCM to float on-the-fly
// This avoids the need for a 64KB static float buffer
static int ei_signal_get_data(size_t offset, size_t length, float *out_ptr) {
    if (!g_pcm_ptr || offset + length > g_pcm_samples) {
        return -1;
    }
    for (size_t i = 0; i < length; i++) {
        out_ptr[i] = (float)g_pcm_ptr[offset + i];
    }
    return 0;
}

// -------------------------------------------------
// Static sliding window buffer (32KB) - used by board.c
// -------------------------------------------------
constexpr int kWindowSamples = 16000;
__attribute__((aligned(16))) static int16_t g_sliding_window[kWindowSamples];

extern "C" int16_t* wake_word_get_window_buffer(void) {
    return g_sliding_window;
}

// -------------------------------------------------
// Edge Impulse Wrapper Functions
// -------------------------------------------------
extern "C" bool ei_wake_word_engine_init(void) {
    if (ei_initialized) return true;
    // [1.1.46] Initialize Edge Impulse internal state
    run_classifier_init();
    ei_initialized = true;
    ESP_LOGI(TAG_WW, "Edge Impulse Wrapper Initialized");
    return true;
}

extern "C" bool ei_wake_word_engine_infer(const int16_t *pcm, size_t samples, float *score_out) {
    if (!ei_initialized) ei_wake_word_engine_init();
    
    // Validate sample count matches model expectation
    if (samples != 16000) {
        ESP_LOGE(TAG_WW, "Invalid sample count: %u (expected 16000)", (unsigned)samples);
        return false;
    }
    
    // Set global pointer for callback
    g_pcm_ptr = pcm;
    g_pcm_samples = samples;
    
    // Create signal with callback-based data retrieval
    signal_t signal_wrapper;
    signal_wrapper.total_length = samples;
    signal_wrapper.get_data = &ei_signal_get_data;

    ei_impulse_result_t result = { 0 };
    EI_IMPULSE_ERROR err = run_classifier(&signal_wrapper, &result, false);
    
    // Clear global pointer after inference
    g_pcm_ptr = nullptr;
    g_pcm_samples = 0;
    
    if (err != EI_IMPULSE_OK) {
        ESP_LOGE(TAG_WW, "EI run failed: %d", err);
        return false;
    }

    // Find the highest classification score
    float max_val = 0.0f;
    for (size_t ix = 0; ix < EI_CLASSIFIER_LABEL_COUNT; ix++) {
        if (result.classification[ix].value > max_val) {
            max_val = result.classification[ix].value;
        }
    }
    
    if (score_out) *score_out = max_val;
    
    ESP_LOGD(TAG_WW, "Score: %.3f", max_val);
    return true;
}
