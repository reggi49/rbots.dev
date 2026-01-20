#pragma once

#include <stdbool.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>

typedef enum {
    FACE_NEUTRAL,
    FACE_HAPPY,
    FACE_CONFUSED,
    FACE_ERROR
} face_state_t;

typedef enum {
    BLINK_OPEN,
    BLINK_HALF,
    BLINK_CLOSED
} blink_state_t;

typedef enum {
    WIFI_OFF,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_CONNECTED_STABLE,
    WIFI_ERROR
} wifi_status_t;

typedef enum {
    NET_UNKNOWN,
    NET_WIFI_ONLY,
    NET_CHECKING,
    NET_ONLINE,
    NET_OFFLINE
} net_state_t;

typedef enum {
    BAT_FULL,
    BAT_MED,
    BAT_LOW,
    BAT_CRIT,
    BAT_CHARGING
} battery_state_t;

typedef enum {
    CHAT_IDLE,
    CHAT_BLOCKED,
    CHAT_SENDING,
    CHAT_WAITING,
    CHAT_RENDER,
    CHAT_ERROR
} chat_state_t;

typedef enum {
    AI_IDLE,
    AI_LISTENING,
    AI_THINKING,
    AI_ANSWERING,
    AI_ERROR
} ai_state_t;

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
void board_set_ai_state(ai_state_t s);
void board_set_ai_text(const char *text, int ttl_ms);
void board_clear_ai_text(void);
ai_state_t board_get_ai_state(void);
