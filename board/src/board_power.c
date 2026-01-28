#include "board_power.h"
#include <stdlib.h>
#include "esp_log.h"

#define TAG "BD_PWR"

struct board_power_s {
    // ADC handle or similar would go here
    int dummy;
};

static struct board_power_s g_power_instance;

board_power_t* board_power_init(void)
{
    ESP_LOGI(TAG, "Init");
    // Init ADC, GPIOs for battery reading
    return &g_power_instance;
}

float board_power_get_voltage(board_power_t *power)
{
    // TODO: Real implementation
    return 3.9f;
}

bool board_power_is_charging(board_power_t *power)
{
    // TODO: Real implementation
    return true;
}

battery_state_t board_power_get_state(board_power_t *power)
{
    if (board_power_is_charging(power)) return BAT_CHARGING;

    float v = board_power_get_voltage(power);
    if (v >= 4.0f) return BAT_FULL;
    if (v >= 3.7f) return BAT_MED;
    if (v >= 3.4f) return BAT_LOW;
    if (v < 3.3f) return BAT_CRIT;
    return BAT_LOW;
}
