#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// [1.1.48] Simplified API - only Edge Impulse functions are used
bool ei_wake_word_engine_init(void);
bool ei_wake_word_engine_infer(const int16_t *pcm, size_t samples, float *score_out);
int16_t* wake_word_get_window_buffer(void);

#ifdef __cplusplus
}
#endif
