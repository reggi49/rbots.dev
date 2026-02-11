// [1.1.50] Edge Impulse Wake Word Runtime - 4-Class EON Compiler Support
// Model classes: { "no", "noise", "unknown", "yes" } -> indices [0, 1, 2, 3]
static const char *TAG_WW = "WW";

#include <cstdint>
#include <cstddef>
#include "esp_log.h"
#include "wake_word_runtime.h"

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
// Static sliding window buffer (~30KB) - used by board.c
// -------------------------------------------------
// [1.1.49] Match model's EI_CLASSIFIER_RAW_SAMPLE_COUNT (15488 samples = ~968ms at 16kHz)
constexpr int kWindowSamples = EI_WW_WINDOW_SAMPLES;
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
    ESP_LOGI(TAG_WW, "Edge Impulse Wrapper Initialized (4-Class: no/noise/unknown/yes)");
    return true;
}

// [1.1.50] 4-Class Inference - Returns all class scores
extern "C" bool ei_wake_word_engine_infer_4class(const int16_t *pcm, size_t samples, ei_ww_scores_t *scores_out) {
    if (!ei_initialized) ei_wake_word_engine_init();
    
    // Validate sample count matches model's EI_CLASSIFIER_RAW_SAMPLE_COUNT
    if (samples != EI_WW_WINDOW_SAMPLES) {
        ESP_LOGE(TAG_WW, "Invalid sample count: %u (expected %d)", (unsigned)samples, EI_WW_WINDOW_SAMPLES);
        return false;
    }
    
    // Set global pointer for callback
    g_pcm_ptr = pcm;
    g_pcm_samples = samples;
    
    // Create signal with callback-based data retrieval
    signal_t signal_wrapper;
    signal_wrapper.total_length = samples;
    signal_wrapper.get_data = &ei_signal_get_data;

    ei_impulse_result_t result = {};
    EI_IMPULSE_ERROR err = run_classifier(&signal_wrapper, &result, false);
    
    // Clear global pointer after inference
    g_pcm_ptr = nullptr;
    g_pcm_samples = 0;
    
    if (err != EI_IMPULSE_OK) {
        ESP_LOGE(TAG_WW, "EI run failed: %d", err);
        return false;
    }

    // [1.1.50] Extract 4-class scores based on model_variables.h indices
    // Categories: { "no", "noise", "unknown", "yes" } -> [0, 1, 2, 3]
    if (scores_out) {
        scores_out->no      = result.classification[0].value;
        scores_out->noise   = result.classification[1].value;
        scores_out->unknown = result.classification[2].value;
        scores_out->yes     = result.classification[3].value;
    }
    
    return true;
}

// [DEPRECATED] Single-score inference - kept for compatibility
extern "C" bool ei_wake_word_engine_infer(const int16_t *pcm, size_t samples, float *score_out) {
    ei_ww_scores_t scores;
    if (!ei_wake_word_engine_infer_4class(pcm, samples, &scores)) {
        return false;
    }
    // Return YES score as the primary score for backward compatibility
    if (score_out) *score_out = scores.yes;
    return true;
}
