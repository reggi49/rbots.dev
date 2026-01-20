#include "command_dispatcher.h"

#include <string.h>

#include "esp_log.h"
#include "esp_system.h"

#include "board_status.h"

static const char *TAG = "CLOUD";

static face_state_t mood_from_value(const char *value)
{
    if (!value) return FACE_NEUTRAL;
    if (strcmp(value, "HAPPY") == 0) return FACE_HAPPY;
    if (strcmp(value, "CONFUSED") == 0) return FACE_CONFUSED;
    if (strcmp(value, "ERROR") == 0) return FACE_ERROR;
    return FACE_NEUTRAL;
}

void command_dispatcher_handle(const cloud_command_t *cmd)
{
    if (!cmd || cmd->type == CLOUD_CMD_NONE) return;

    switch (cmd->type) {
        case CLOUD_CMD_SET_MOOD:
        {
            face_state_t mood = mood_from_value(cmd->value);
            board_request_face_state(mood);
            ESP_LOGI(TAG, "CMD SET_MOOD %s", cmd->value[0] ? cmd->value : "IDLE");
            break;
        }
        case CLOUD_CMD_SHOW_ICON:
            ESP_LOGI(TAG, "CMD SHOW_ICON %s", cmd->value[0] ? cmd->value : "ICON");
            break;
        case CLOUD_CMD_RESET:
            ESP_LOGI(TAG, "CMD RESET");
            esp_restart();
            break;
        default:
            break;
    }
}
