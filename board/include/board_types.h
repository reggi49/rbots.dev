#pragma once

#include <stdint.h>
#include <stdbool.h>

// Face Expressions
typedef enum {
    FACE_BOOT = 0,
    FACE_IDLE,          // NEUTRAL / Standby
    FACE_LISTENING,     // Listening with audio wave rings
    FACE_THINKING,      // Thinking with pupils up & thought sparkles
    FACE_SPEAKING,      // Speaking with mouth modulated by audio
    FACE_HAPPY,         // 9. Happy (Crescent eyes & floating hearts)
    FACE_ERROR,         // 20. Error / Concerned
    FACE_DISCONNECTED,  // Offline / Disconnected
    // Kawaii Robot Cat Expressions (Reference Panels & Extensions)
    FACE_EXCITED,       // 1. Excited (Sunburst rays, paws up, sparkling eyes)
    FACE_SURPRISED,     // 2. Surprised ('!!!' marks, open 'O' mouth, paws to cheeks)
    FACE_SQUINT,        // 3. Squint (Candy, left wink, ':3' smile, paw up)
    FACE_WINK_LEFT,     // 4. Wink Left (Mint/sky, left eye closed, smiling mouth)
    FACE_WINK_RIGHT,    // 5. Wink Right (Peach/pink, right eye closed)
    FACE_CONFUSED,      // 6. Confused ('???' marks, asymmetric gaze, paw at chin)
    FACE_SLEEPY,        // 7. Sleepy (Moon, stars, 'zzz', sleeping on pillow)
    FACE_SAD,           // 8. Sad / Error Sad (Broken heart, tears, downward pout)
    FACE_RECOVERED,     // 10. Recovered (Green leaves, sunny smile, relaxed)
    FACE_NEUTRAL,       // 11. Neutral (Relaxed open eyes, soft smile, breathing)
    FACE_BLINK,         // 15. Blink
    FACE_CURIOUS,       // 16. Curious (Asymmetric eyes, curious tilt)
    FACE_ANGRY,         // 17. Angry / Frustrated (Slanted brows, frown, anger mark)
    FACE_LOVE,          // 18. Love (Heart pupils, floating hearts, glowing blush)
    FACE_PLAYFUL,       // 19. Playful (Wink + tongue blep, paw wave)
    FACE_COUNT,
    // Aliases for compatibility with legacy tests & commands
    FACE_OFFLINE = FACE_DISCONNECTED
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
