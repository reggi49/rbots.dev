#pragma once

#include <stdint.h>
#include <stdbool.h>

// Face Expressions
typedef enum {
    FACE_NEUTRAL,
    FACE_HAPPY,
    FACE_CONFUSED,
    FACE_ERROR
} face_state_t;

// Eye Blink State
typedef enum {
    BLINK_OPEN,
    BLINK_HALF,
    BLINK_CLOSED
} blink_state_t;

// WiFi Status
typedef enum {
    WIFI_OFF,
    WIFI_CONNECTING,
    WIFI_CONNECTED,
    WIFI_CONNECTED_STABLE,
    WIFI_ERROR
} wifi_status_t;

// Network Logic State
typedef enum {
    NET_UNKNOWN,
    NET_WIFI_ONLY,
    NET_CHECKING,
    NET_ONLINE,
    NET_OFFLINE
} net_state_t;

// Battery Levels
typedef enum {
    BAT_FULL,
    BAT_MED,
    BAT_LOW,
    BAT_CRIT,
    BAT_CHARGING
} battery_state_t;

// Chat UI State
typedef enum {
    CHAT_IDLE,
    CHAT_BLOCKED,
    CHAT_SENDING,
    CHAT_WAITING,
    CHAT_RENDER,
    CHAT_ERROR
} chat_state_t;

// AI Processing State
typedef enum {
    AI_IDLE,
    AI_LISTENING,
    AI_THINKING,
    AI_ANSWERING,
    AI_ERROR
} ai_state_t;
