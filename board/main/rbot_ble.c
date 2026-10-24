/**
 * @file rbot_ble.c
 * @brief R-bot BLE GATT Service Implementation (NimBLE stack).
 */

#include "rbot_ble.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "cJSON.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/ble_uuid.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

#include "board_status.h"
#include "board_face.h"
#include "board_network.h"

static const char *TAG = "RBOT_BLE";

/* ── Custom GATT Service & Characteristic 128-bit UUIDs ──── */
/* Primary Service: 7b4a0001-9d3f-4c2e-8a61-2f0c5b7e1000 */
static const ble_uuid128_t s_rbot_svc_uuid =
    BLE_UUID128_INIT(0x00, 0x10, 0x7e, 0x5b, 0x0c, 0x2f, 0x61, 0x8a,
                     0x2e, 0x4c, 0x3f, 0x9d, 0x01, 0x00, 0x4a, 0x7b);

/* Command Characteristic (Write): 7b4a0002-9d3f-4c2e-8a61-2f0c5b7e1000 */
static const ble_uuid128_t s_rbot_cmd_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x10, 0x7e, 0x5b, 0x0c, 0x2f, 0x61, 0x8a,
                     0x2e, 0x4c, 0x3f, 0x9d, 0x02, 0x00, 0x4a, 0x7b);

/* Status Characteristic (Read, Notify): 7b4a0003-9d3f-4c2e-8a61-2f0c5b7e1000 */
static const ble_uuid128_t s_rbot_status_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x10, 0x7e, 0x5b, 0x0c, 0x2f, 0x61, 0x8a,
                     0x2e, 0x4c, 0x3f, 0x9d, 0x03, 0x00, 0x4a, 0x7b);

/* Config Characteristic (Read, Write): 7b4a0004-9d3f-4c2e-8a61-2f0c5b7e1000 */
static const ble_uuid128_t s_rbot_config_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x10, 0x7e, 0x5b, 0x0c, 0x2f, 0x61, 0x8a,
                     0x2e, 0x4c, 0x3f, 0x9d, 0x04, 0x00, 0x4a, 0x7b);

/* Event Characteristic (Notify): 7b4a0005-9d3f-4c2e-8a61-2f0c5b7e1000 */
static const ble_uuid128_t s_rbot_event_chr_uuid =
    BLE_UUID128_INIT(0x00, 0x10, 0x7e, 0x5b, 0x0c, 0x2f, 0x61, 0x8a,
                     0x2e, 0x4c, 0x3f, 0x9d, 0x05, 0x00, 0x4a, 0x7b);

/* Value handles for characteristics */
static uint16_t s_cmd_val_handle;
static uint16_t s_status_val_handle;
static uint16_t s_config_val_handle;
static uint16_t s_event_val_handle;

/* State variables */
static uint16_t s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
static bool s_is_connected = false;
static uint8_t s_own_addr_type = 0;
static char s_status_buf[512] = "{\"v\":1,\"id\":0,\"ok\":true,\"state\":\"idle\"}";
static char s_config_name[32] = "Mochi";

/* Forward declarations */
static void rbot_ble_start_advertising(void);
static void rbot_ble_process_command(const char *raw_json);
static void rbot_ble_set_status_and_notify(const char *json_str);
static int rbot_gap_event(struct ble_gap_event *event, void *arg);

bool rbot_ble_is_connected(void)
{
    return s_is_connected;
}

void rbot_ble_notify_status(const char *json_str)
{
    if (!json_str) return;
    rbot_ble_set_status_and_notify(json_str);
}

void rbot_ble_notify_event(const char *json_str)
{
    if (!json_str || !s_is_connected || s_conn_handle == BLE_HS_CONN_HANDLE_NONE) return;
    struct os_mbuf *om = ble_hs_mbuf_from_flat(json_str, strlen(json_str));
    if (om) {
        ble_gatts_notify_custom(s_conn_handle, s_event_val_handle, om);
    }
}

static void rbot_ble_set_status_and_notify(const char *json_str)
{
    strncpy(s_status_buf, json_str, sizeof(s_status_buf) - 1);
    s_status_buf[sizeof(s_status_buf) - 1] = '\0';
    ESP_LOGI(TAG, "BLE TX Status: %s", s_status_buf);

    if (s_is_connected && s_conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        struct os_mbuf *om = ble_hs_mbuf_from_flat(s_status_buf, strlen(s_status_buf));
        if (om) {
            ble_gatts_notify_custom(s_conn_handle, s_status_val_handle, om);
        }
    }
}

static void rbot_ble_process_command(const char *raw_json)
{
    ESP_LOGI(TAG, "BLE RX Cmd: %s", raw_json);
    cJSON *root = cJSON_Parse(raw_json);
    if (!root) {
        rbot_ble_set_status_and_notify("{\"v\":1,\"id\":0,\"ok\":false,\"error_code\":\"INVALID_ARGUMENT\",\"error_message\":\"Invalid JSON format\"}");
        return;
    }

    cJSON *v_item = cJSON_GetObjectItem(root, "v");
    int v = v_item ? v_item->valueint : 0;
    cJSON *id_item = cJSON_GetObjectItem(root, "id");
    int id = id_item ? id_item->valueint : 0;
    cJSON *type_item = cJSON_GetObjectItem(root, "type");
    const char *type = (type_item && type_item->valuestring) ? type_item->valuestring : "";
    cJSON *action_item = cJSON_GetObjectItem(root, "action");
    const char *action = (action_item && action_item->valuestring) ? action_item->valuestring : "";

    if (v != 1) {
        char err[160];
        snprintf(err, sizeof(err),
                 "{\"v\":1,\"id\":%d,\"ok\":false,\"error_code\":\"UNSUPPORTED_VERSION\",\"error_message\":\"Unsupported protocol version\"}",
                 id);
        rbot_ble_set_status_and_notify(err);
        cJSON_Delete(root);
        return;
    }

    /* 1. PING */
    if (strcasecmp(type, "ping") == 0 || strcasecmp(action, "ping") == 0) {
        char resp[128];
        snprintf(resp, sizeof(resp), "{\"v\":1,\"id\":%d,\"ok\":true,\"pong\":true}", id);
        rbot_ble_set_status_and_notify(resp);
    }
    /* 2. GET_INFO (Truthful firmware identity and build metadata) */
    else if (strcasecmp(action, "get_info") == 0) {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        char resp[384];
        snprintf(resp, sizeof(resp),
                 "{\"v\":1,\"id\":%d,\"ok\":true,\"robot_name\":\"Rbot-%s\",\"firmware_version\":\"%s\",\"hardware_id\":\"ESP32-S3-N16R8\",\"build_date\":\"%s %s\"}",
                 id, s_config_name, app_desc->version, app_desc->date, app_desc->time);
        rbot_ble_set_status_and_notify(resp);
    }
    /* 3. GET_STATUS (Truthful runtime telemetry without fabricated states) */
    else if (strcasecmp(action, "get_status") == 0) {
        const esp_app_desc_t *app_desc = esp_app_get_description();
        bool wifi_ok = board_wifi_is_stable();
        char resp[384];
        snprintf(resp, sizeof(resp),
                 "{\"v\":1,\"id\":%d,\"ok\":true,\"robot_name\":\"Rbot-%s\",\"firmware_version\":\"%s\",\"hardware_id\":\"ESP32-S3-N16R8\",\"active_face\":\"unavailable\",\"movement_state\":\"unavailable\",\"has_real_battery\":false,\"wifi_connected\":%s,\"server_connected\":\"unknown\",\"ota_supported\":false}",
                 id, s_config_name, app_desc->version, wifi_ok ? "true" : "false");
        rbot_ble_set_status_and_notify(resp);
    }
    /* 4. GET_CAPABILITIES (Truthful capability negotiation) */
    else if (strcasecmp(action, "get_capabilities") == 0) {
        char resp[384];
        snprintf(resp, sizeof(resp),
                 "{\"v\":1,\"id\":%d,\"ok\":true,\"capabilities\":{\"motor\":false,\"face\":false,\"voice\":false,\"ota\":false,\"battery_adc\":false,\"config_write\":false,\"watch_face_write\":false,\"wifi_provisioning\":false,\"wifi_ota\":false,\"rollback\":false}}",
                 id);
        rbot_ble_set_status_and_notify(resp);
    }
    /* 5. Any other command (MOTOR_MOVE, SET_CONFIG, SET_WATCH_FACE, REBOOT, OTA, etc.)
     * Returns NOT_SUPPORTED per read-only safety boundary.
     */
    else {
        char resp[192];
        snprintf(resp, sizeof(resp),
                 "{\"v\":1,\"id\":%d,\"ok\":false,\"error_code\":\"NOT_SUPPORTED\",\"error_message\":\"Command not supported in read-only slice\"}",
                 id);
        rbot_ble_set_status_and_notify(resp);
    }

    cJSON_Delete(root);
}

static int rbot_gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                               struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    /* Handle Command Characteristic writes */
    if (attr_handle == s_cmd_val_handle) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            char rx_buf[384];
            uint16_t len = OS_MBUF_PKTLEN(ctxt->om);
            if (len >= sizeof(rx_buf)) len = sizeof(rx_buf) - 1;
            os_mbuf_copydata(ctxt->om, 0, len, rx_buf);
            rx_buf[len] = '\0';
            rbot_ble_process_command(rx_buf);
            return 0;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Handle Status Characteristic reads */
    if (attr_handle == s_status_val_handle) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            int rc = os_mbuf_append(ctxt->om, s_status_buf, strlen(s_status_buf));
            return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    /* Handle Config Characteristic (Read-only in this vertical slice) */
    if (attr_handle == s_config_val_handle) {
        if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
            char cfg_json[128];
            snprintf(cfg_json, sizeof(cfg_json), "{\"v\":1,\"device_name\":\"%s\",\"writable\":false}", s_config_name);
            int rc = os_mbuf_append(ctxt->om, cfg_json, strlen(cfg_json));
            return (rc == 0) ? 0 : BLE_ATT_ERR_INSUFFICIENT_RES;
        }
        if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
            ESP_LOGW(TAG, "Config write rejected: configuration is read-only in this slice");
            return BLE_ATT_ERR_WRITE_NOT_PERMITTED;
        }
        return BLE_ATT_ERR_UNLIKELY;
    }

    return BLE_ATT_ERR_UNLIKELY;
}

static const struct ble_gatt_svc_def s_gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = &s_rbot_svc_uuid.u,
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                /* 1. Command Characteristic (Write-with-response only) */
                .uuid = &s_rbot_cmd_chr_uuid.u,
                .access_cb = rbot_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_WRITE,
                .val_handle = &s_cmd_val_handle,
            },
            {
                /* 2. Status Characteristic (Read, Notify) */
                .uuid = &s_rbot_status_chr_uuid.u,
                .access_cb = rbot_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_status_val_handle,
            },
            {
                /* 3. Configuration Characteristic (Read-only) */
                .uuid = &s_rbot_config_chr_uuid.u,
                .access_cb = rbot_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_READ,
                .val_handle = &s_config_val_handle,
            },
            {
                /* 4. Event Characteristic (Notify) */
                .uuid = &s_rbot_event_chr_uuid.u,
                .access_cb = rbot_gatt_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &s_event_val_handle,
            },
            { 0 } /* Terminating characteristic */
        },
    },
    { 0 } /* Terminating service */
};

static void rbot_ble_start_advertising(void)
{
    struct ble_gap_adv_params adv_params;
    struct ble_hs_adv_fields adv_fields;
    struct ble_hs_adv_fields rsp_fields;
    int rc;

    /* 1. Main advertisement payload (Flags + 128-bit Primary Service UUID) */
    memset(&adv_fields, 0, sizeof(adv_fields));
    adv_fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    adv_fields.uuids128 = (ble_uuid128_t *)&s_rbot_svc_uuid;
    adv_fields.num_uuids128 = 1;
    adv_fields.uuids128_is_complete = 1;

    rc = ble_gap_adv_set_fields(&adv_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv fields: rc=%d", rc);
        return;
    }

    /* 2. Scan response payload (Complete Local Name: e.g. "Rbot-Mochi") */
    char full_name[64];
    snprintf(full_name, sizeof(full_name), "Rbot-%s", s_config_name);

    memset(&rsp_fields, 0, sizeof(rsp_fields));
    rsp_fields.name = (uint8_t *)full_name;
    rsp_fields.name_len = strlen(full_name);
    rsp_fields.name_is_complete = 1;

    rc = ble_gap_adv_rsp_set_fields(&rsp_fields);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to set adv rsp fields: rc=%d", rc);
        return;
    }

    /* 3. Start advertising indefinitely */
    memset(&adv_params, 0, sizeof(adv_params));
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    rc = ble_gap_adv_start(s_own_addr_type, NULL, BLE_HS_FOREVER,
                           &adv_params, rbot_gap_event, NULL);
    if (rc != 0) {
        ESP_LOGE(TAG, "Failed to start advertising: rc=%d", rc);
    } else {
        ESP_LOGI(TAG, "✅ BLE advertising active as '%s' (Service UUID: 7b4a0001-...)", full_name);
    }
}

static int rbot_gap_event(struct ble_gap_event *event, void *arg)
{
    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        ESP_LOGI(TAG, "BLE connection %s; status=%d",
                 event->connect.status == 0 ? "ESTABLISHED" : "FAILED",
                 event->connect.status);
        if (event->connect.status == 0) {
            s_conn_handle = event->connect.conn_handle;
            s_is_connected = true;
        } else {
            s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
            s_is_connected = false;
            rbot_ble_start_advertising();
        }
        break;

    case BLE_GAP_EVENT_DISCONNECT:
        ESP_LOGI(TAG, "BLE DISCONNECTED (reason=%d); resuming advertising without altering robot state",
                 event->disconnect.reason);
        s_conn_handle = BLE_HS_CONN_HANDLE_NONE;
        s_is_connected = false;
        rbot_ble_start_advertising();
        break;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        ESP_LOGI(TAG, "BLE adv complete; resuming advertising");
        rbot_ble_start_advertising();
        break;

    case BLE_GAP_EVENT_SUBSCRIBE:
        ESP_LOGI(TAG, "BLE subscribe event; attr_handle=%d, cur_notify=%d",
                 event->subscribe.attr_handle, event->subscribe.cur_notify);
        break;

    case BLE_GAP_EVENT_MTU:
        ESP_LOGI(TAG, "BLE MTU updated: channel=%d, mtu=%d",
                 event->mtu.channel_id, event->mtu.value);
        break;

    default:
        break;
    }
    return 0;
}

static void rbot_ble_on_sync(void)
{
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error ensuring MAC address: rc=%d", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_own_addr_type);
    if (rc != 0) {
        ESP_LOGE(TAG, "Error inferring address type: rc=%d", rc);
        return;
    }
    rbot_ble_start_advertising();
}

static void rbot_ble_host_task(void *param)
{
    ESP_LOGI(TAG, "NimBLE host task started on Core 0");
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t rbot_ble_init(void)
{
    ESP_LOGI(TAG, "Initializing R-bot BLE GATT subsystem (NimBLE)...");

    int rc = nimble_port_init();
    if (rc != 0) {
        ESP_LOGE(TAG, "nimble_port_init failed: %d", rc);
        return ESP_FAIL;
    }

    ble_svc_gap_init();
    ble_svc_gatt_init();

    char dev_name[64];
    snprintf(dev_name, sizeof(dev_name), "Rbot-%s", s_config_name);
    rc = ble_svc_gap_device_name_set(dev_name);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_svc_gap_device_name_set failed: %d", rc);
    }

    rc = ble_gatts_count_cfg(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_count_cfg failed: %d", rc);
        return ESP_FAIL;
    }

    rc = ble_gatts_add_svcs(s_gatt_svcs);
    if (rc != 0) {
        ESP_LOGE(TAG, "ble_gatts_add_svcs failed: %d", rc);
        return ESP_FAIL;
    }

    ble_hs_cfg.sync_cb = rbot_ble_on_sync;

    /* Set default gap event handler */
    ble_gap_event_listener_register(NULL, rbot_gap_event, NULL);

    nimble_port_freertos_init(rbot_ble_host_task);
    ESP_LOGI(TAG, "✅ R-bot BLE GATT subsystem initialized successfully");
    return ESP_OK;
}

