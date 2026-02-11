#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// [1.1.50] 4-Class Wake Word Result Structure (no heap allocation)
// Class indices from model_variables.h: { "no", "noise", "unknown", "yes" }
typedef struct {
    float yes;      // Index 3 - Wake word detected
    float no;       // Index 0 - User rejection
    float noise;    // Index 1 - Background noise
    float unknown;  // Index 2 - Unknown speech
} ei_ww_scores_t;

// Wake Word Engine API
bool ei_wake_word_engine_init(void);

// [DEPRECATED] Single-score inference (kept for compatibility)
bool ei_wake_word_engine_infer(const int16_t *pcm, size_t samples, float *score_out);

// [1.1.50] 4-Class inference - returns all class scores
bool ei_wake_word_engine_infer_4class(const int16_t *pcm, size_t samples, ei_ww_scores_t *scores_out);

// Static buffer accessor (no heap allocation)
int16_t* wake_word_get_window_buffer(void);

// Constants
#define EI_WW_WINDOW_SAMPLES 15488  // ~968ms @ 16kHz (matches model)

#ifdef __cplusplus
}
#endif
