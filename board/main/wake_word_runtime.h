#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool wake_word_engine_init(void);
bool wake_word_engine_ready(void);

#ifdef __cplusplus
}
#endif
