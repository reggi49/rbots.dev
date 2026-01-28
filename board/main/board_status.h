#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include "board_types.h"

face_state_t board_get_face_state(void);
void board_set_face_state(face_state_t face);
void board_request_face_state(face_state_t face);
bool board_consume_requested_face_state(face_state_t *face);
bool board_wait_for_time_sync(uint32_t timeout_ms);
bool board_wifi_is_stable(void);
bool board_wifi_in_runtime_phase(void);
bool board_can_network(void);
wifi_status_t board_get_wifi_status(void);
battery_state_t board_get_battery_state(void);
void board_set_net_state(net_state_t state);
void board_submit_chat_prompt(const char *text);
bool board_consume_chat_prompt(char *out, size_t max_len);
void board_wakeword_notify(float score);
void board_set_ai_state(ai_state_t s);
void board_set_ai_text(const char *text, int ttl_ms);
void board_clear_ai_text(void);
ai_state_t board_get_ai_state(void);
