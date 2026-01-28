#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wake_word_engine_init(void);
bool wake_word_engine_ready(void);
bool wake_word_engine_infer_from_pcm(const int16_t *pcm, size_t samples, float *score_out);
bool wake_word_engine_extract_features(const int16_t *pcm, size_t samples, float *feat_out, size_t feat_len);
bool wake_word_engine_infer_from_features(const float *feat_49x10, float *score_out);

#ifdef __cplusplus
}
#endif
