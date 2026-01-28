#include "board_network.h"
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_sntp.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include <errno.h>

#define TAG "BD_NET"

#define WIFI_SSID "rara"
#define WIFI_PASS "123456788"

#define MAX_RECONNECT_BOOT 10
#define MAX_RECONNECT_RUNTIME 3
#define MIN_RECONNECT_INTERVAL_MS 10000 
#define WIFI_STABLE_DELAY_MS 5000
#define WIFI_STABLE_DELAY_MS 5000
#define NETWORK_LATCH_MS 60000

#define NET_CHECK_INTERVAL_MS 30000
#define HTTP_TIMEOUT_MS 5000
#define NET_CHECK_HOST "rbots.dev"
#define NET_CHECK_PORT 443

typedef enum {
    WIFI_PHASE_BOOT,
    WIFI_PHASE_RUNTIME
} wifi_phase_t;

struct board_network_s {
    volatile wifi_status_t wifi_status;
    volatile bool wifi_dirty;
    wifi_phase_t wifi_phase;
    uint32_t wifi_connected_time;
    uint8_t wifi_retry_count;
    int64_t wifi_last_retry_ms;
    esp_timer_handle_t wifi_reconnect_timer;
    bool wifi_reconnect_pending;
    
    volatile net_state_t net_state;
    int64_t net_last_success_ms;
    
    bool time_sync_started;
    bool time_synced;
    
    char ip_str[INET_ADDRSTRLEN];

    // Net Check State
    int net_sock;
    int64_t net_check_start_ms;
    int64_t net_last_check_ms;
    bool net_force_check;
    bool net_connected;
    
    ip_addr_t net_target_ip;
    bool net_ip_valid;
    bool net_dns_pending;
};

static struct board_network_s g_net_instance; // Singleton pattern for callbacks

// --- Private Helpers ---

static void wifi_schedule_reconnect(board_network_t *net, uint32_t delay_ms);

static void wifi_set_status(board_network_t *net, wifi_status_t status)
{
    if (net->wifi_status == status) return;
    net->wifi_status = status;
    net->wifi_dirty = true;

    switch (status) {
        case WIFI_OFF: ESP_LOGI(TAG, "WIFI OFF"); break;
        case WIFI_CONNECTING: ESP_LOGI(TAG, "WIFI CONNECTING"); break;
        case WIFI_CONNECTED: ESP_LOGI(TAG, "WIFI CONNECTED (wait stable)"); break;
        case WIFI_CONNECTED_STABLE: ESP_LOGI(TAG, "WIFI STABLE"); break;
        case WIFI_ERROR: ESP_LOGE(TAG, "WIFI ERROR"); break;
        default: break;
    }
}

static void wifi_reconnect_timer_cb(void *arg)
{
    board_network_t *net = (board_network_t *)arg;
    net->wifi_reconnect_pending = false;

    if (net->wifi_status == WIFI_CONNECTED || net->wifi_status == WIFI_CONNECTED_STABLE) return;

    int max_retry = (net->wifi_phase == WIFI_PHASE_RUNTIME) ? MAX_RECONNECT_RUNTIME : MAX_RECONNECT_BOOT;
    if (net->wifi_retry_count >= max_retry) {
        if (net->wifi_phase == WIFI_PHASE_RUNTIME) {
            wifi_set_status(net, WIFI_ERROR);
            ESP_LOGE(TAG, "Max Retries Reached (Giving Up)");
        } else {
            ESP_LOGW(TAG, "Boot Retries Exhausted (Waiting)");
        }
        return;
    }

    net->wifi_last_retry_ms = esp_timer_get_time() / 1000;
    net->wifi_retry_count++;
    ESP_LOGI(TAG, "Retry %d", net->wifi_retry_count);
    wifi_set_status(net, WIFI_CONNECTING);
    esp_wifi_connect();
}

static void wifi_schedule_reconnect(board_network_t *net, uint32_t delay_ms)
{
    if (net->wifi_reconnect_timer == NULL) {
        const esp_timer_create_args_t args = {
            .callback = &wifi_reconnect_timer_cb,
            .arg = net,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "wifi_reconn"
        };
        esp_timer_create(&args, &net->wifi_reconnect_timer);
    }
    if (net->wifi_reconnect_timer == NULL) {
        wifi_reconnect_timer_cb(net); // Fallback
        return;
    }
    if (net->wifi_reconnect_pending) {
        esp_timer_stop(net->wifi_reconnect_timer);
    }
    net->wifi_reconnect_pending = true;
    esp_timer_start_once(net->wifi_reconnect_timer, (uint64_t)delay_ms * 1000ULL);
}

static bool wifi_scan_for_ssid(void)
{
    // Simplified scan
    wifi_scan_config_t scan_config = { 0 };
    esp_wifi_scan_start(&scan_config, true);
    
    uint16_t ap_count = 0;
    esp_wifi_scan_get_ap_num(&ap_count);
    if (ap_count == 0) return false;
    
    wifi_ap_record_t *ap_list = (wifi_ap_record_t *)malloc(ap_count * sizeof(wifi_ap_record_t));
    if (!ap_list) return false;
    
    esp_wifi_scan_get_ap_records(&ap_count, ap_list);
    bool found = false;
    for (int i = 0; i < ap_count; i++) {
        if (strcmp((char *)ap_list[i].ssid, WIFI_SSID) == 0) {
            found = true;
            break;
        }
    }
    free(ap_list);
    return found;
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base, int32_t event_id, void *event_data)
{
    board_network_t *net = (board_network_t *)arg;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        wifi_set_status(net, WIFI_CONNECTING);
        net->wifi_last_retry_ms = esp_timer_get_time() / 1000;
        if (wifi_scan_for_ssid()) {
            esp_wifi_connect();
        } else {
            ESP_LOGW(TAG, "Network not found, delaying");
            wifi_schedule_reconnect(net, 30000);
        }
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "Disconnected");
        net->wifi_connected_time = 0;
        
        int64_t now_ms = esp_timer_get_time() / 1000;
        int max_retry = (net->wifi_phase == WIFI_PHASE_RUNTIME) ? MAX_RECONNECT_RUNTIME : MAX_RECONNECT_BOOT;
        
        if (net->wifi_retry_count >= max_retry) {
             if (net->wifi_phase == WIFI_PHASE_RUNTIME) {
                 wifi_set_status(net, WIFI_ERROR);
             }
             return;
        }
        
        uint32_t delay = 0;
        if (net->wifi_last_retry_ms != 0) {
            int64_t elapsed = now_ms - net->wifi_last_retry_ms;
            if (elapsed < MIN_RECONNECT_INTERVAL_MS) delay = MIN_RECONNECT_INTERVAL_MS - elapsed;
        }
        
        wifi_set_status(net, WIFI_CONNECTING);
        if (delay > 0) {
            wifi_schedule_reconnect(net, delay);
        } else {
            net->wifi_last_retry_ms = now_ms;
            net->wifi_retry_count++;
            esp_wifi_connect();
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        esp_ip4_addr_t *ip = &event->ip_info.ip;
        snprintf(net->ip_str, sizeof(net->ip_str), IPSTR, IP2STR(ip));
        
        net->wifi_retry_count = 0;
        if (net->wifi_reconnect_pending) {
            esp_timer_stop(net->wifi_reconnect_timer);
            net->wifi_reconnect_pending = false;
        }
        
        wifi_set_status(net, WIFI_CONNECTED);
        if (net->wifi_phase == WIFI_PHASE_BOOT) net->wifi_phase = WIFI_PHASE_RUNTIME;
        net->wifi_connected_time = xTaskGetTickCount();
    }
}

static void net_dns_found_cb(const char *name, const ip_addr_t *ipaddr, void *callback_arg)
{
    board_network_t *net = (board_network_t *)callback_arg;
    if (!net) return;
    
    if (ipaddr == NULL) {
        net->net_ip_valid = false;
        net->net_dns_pending = false;
        // Fail silently here, tick will handle timeout/retry
        return;
    }
    if (IP_IS_V4(ipaddr)) {
        net->net_target_ip = *ipaddr;
        net->net_ip_valid = true;
        net->net_dns_pending = false;
        ESP_LOGI(TAG, "DNS OK");
    } else {
        net->net_ip_valid = false;
        net->net_dns_pending = false;
    }
}

static void net_abort_check(board_network_t *net)
{
    if (net->net_sock >= 0) {
        close(net->net_sock);
        net->net_sock = -1;
    }
    net->net_check_start_ms = 0;
    net->net_force_check = false;
    net->net_connected = false;
    net->net_dns_pending = false;
}

// --- Public ---

board_network_t* board_network_init(void)
{
    board_network_t *net = &g_net_instance;
    memset(net, 0, sizeof(struct board_network_s));
    net->net_sock = -1;
    net->wifi_dirty = true;
    net->wifi_phase = WIFI_PHASE_BOOT;
    
    nvs_flash_init(); // Should handle retries/erase if needed
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_t *netif = esp_netif_create_default_wifi_sta();
    esp_netif_set_hostname(netif, "rbot");
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    
    esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, net);
    esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, net);
    
    wifi_config_t wifi_config = {0};
    strncpy((char *)wifi_config.sta.ssid, WIFI_SSID, sizeof(wifi_config.sta.ssid));
    strncpy((char *)wifi_config.sta.password, WIFI_PASS, sizeof(wifi_config.sta.password));
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    esp_wifi_start();
    
    return net;
}

bool board_network_is_stable(board_network_t *net)
{
    if (!net) return false;
    if (net->wifi_status == WIFI_CONNECTED_STABLE) return true;
    if (net->wifi_status != WIFI_CONNECTED) return false;
    
    if (net->wifi_connected_time > 0 && 
       (xTaskGetTickCount() - net->wifi_connected_time) >= pdMS_TO_TICKS(WIFI_STABLE_DELAY_MS)) {
        wifi_set_status(net, WIFI_CONNECTED_STABLE);
        return true;
    }
    return false;
}

bool board_network_in_runtime_phase(board_network_t *net)
{
    return net && (net->wifi_phase == WIFI_PHASE_RUNTIME);
}

wifi_status_t board_network_get_status(board_network_t *net)
{
    return net ? net->wifi_status : WIFI_OFF;
}

bool board_network_is_connected(board_network_t *net)
{
    if (!net) return false;
    wifi_status_t ws = net->wifi_status;
    bool wifi_ok = (ws == WIFI_CONNECTED || ws == WIFI_CONNECTED_STABLE);
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool net_ok = (net->net_state == NET_ONLINE) ||
                  (net->net_last_success_ms != 0 && (now_ms - net->net_last_success_ms) < NETWORK_LATCH_MS);
    return wifi_ok && net_ok;
}

net_state_t board_network_get_net_state(board_network_t *net)
{
    return net ? net->net_state : NET_UNKNOWN;
}

void board_network_set_net_state(board_network_t *net, net_state_t state)
{
    if (!net) return;
    if (net->net_state == state) return;
    net->net_state = state;
    net->wifi_dirty = true; // Trigger redraw

    switch (state) {
        case NET_CHECKING:
            ESP_LOGI(TAG, "CHECKING");
            break;
        case NET_WIFI_ONLY:
            ESP_LOGI(TAG, "WIFI ONLY");
            break;
        case NET_ONLINE:
            ESP_LOGI(TAG, "ONLINE");
            net->net_last_success_ms = esp_timer_get_time() / 1000;
            break;
        case NET_OFFLINE:
            ESP_LOGE(TAG, "OFFLINE");
            break;
        default:
            break;
    }
}

bool board_network_wait_for_time_sync(board_network_t *net, uint32_t timeout_ms)
{
    if (!net) return false;
    if (!net->time_sync_started) {
        sntp_setoperatingmode(SNTP_OPMODE_POLL);
        sntp_setservername(0, "pool.ntp.org");
        sntp_init();
        net->time_sync_started = true;
    }
    if (net->time_synced) return true;
    
    // Simple check
    time_t now = 0;
    time(&now);
    if (now > 1609459200) { net->time_synced = true; return true; }
    
    // Wait loop
    uint32_t start = esp_timer_get_time() / 1000;
    while((esp_timer_get_time() / 1000 - start) < timeout_ms) {
        vTaskDelay(pdMS_TO_TICKS(100));
        time(&now);
        if (now > 1609459200) { net->time_synced = true; return true; }
    }
    return false;
}

const char* board_network_get_ip_str(board_network_t *net)
{
    return net ? net->ip_str : "";
}

void board_network_tick(board_network_t *net)
{
    if (!net) return;
    int64_t now_ms = esp_timer_get_time() / 1000;
    bool wifi_connected = (net->wifi_status == WIFI_CONNECTED || net->wifi_status == WIFI_CONNECTED_STABLE);

    if (!wifi_connected) {
        if (net->net_state != NET_UNKNOWN) {
            net_abort_check(net);
            board_network_set_net_state(net, NET_UNKNOWN);
        }
        return;
    }

    // Determine if check is due
    bool due = (net->net_last_check_ms == 0) || ((now_ms - net->net_last_check_ms) >= NET_CHECK_INTERVAL_MS);
    if (net->net_force_check) due = true;

    // If we're not checking and not due, just return
    if (net->net_state != NET_CHECKING && !due) {
        return;
    }

    // Start check if not started
    if (net->net_state != NET_CHECKING) {
        net->net_last_check_ms = now_ms;
        net->net_force_check = false;
        net->net_check_start_ms = now_ms;
        net->net_connected = false;
        board_network_set_net_state(net, NET_CHECKING);
    }

    // Timeout
    if (net->net_check_start_ms != 0 && (now_ms - net->net_check_start_ms) > HTTP_TIMEOUT_MS) {
        net->net_ip_valid = false;
        net->net_dns_pending = false;
        net_abort_check(net);
        board_network_set_net_state(net, NET_OFFLINE); 
        // Force retry sooner? For now stick to interval
        return;
    }

    // DNS Step
    if (!net->net_ip_valid && !net->net_dns_pending) {
        ESP_LOGI(TAG, "DNS resolve %s", NET_CHECK_HOST);
        err_t derr = dns_gethostbyname(NET_CHECK_HOST, &net->net_target_ip, net_dns_found_cb, net);
        if (derr == ERR_OK) {
             net->net_ip_valid = IP_IS_V4(&net->net_target_ip);
             net->net_dns_pending = false;
        } else if (derr == ERR_INPROGRESS) {
             net->net_dns_pending = true;
        } else {
             net->net_ip_valid = false;
             net->net_dns_pending = false;
             net_abort_check(net);
             board_network_set_net_state(net, NET_OFFLINE); 
             return;
        }
    }

    if (net->net_dns_pending || !net->net_ip_valid) {
        return; // Waiting for DNS
    }

    // TCP Connect Step
    if (net->net_sock < 0 && !net->net_connected) {
        net->net_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
        if (net->net_sock < 0) {
            net_abort_check(net);
            board_network_set_net_state(net, NET_OFFLINE);
            return;
        }

        // Non-blocking
        int flags = fcntl(net->net_sock, F_GETFL, 0);
        fcntl(net->net_sock, F_SETFL, flags | O_NONBLOCK);

        struct sockaddr_in sa = {0};
        sa.sin_family = AF_INET;
        sa.sin_port = htons(NET_CHECK_PORT);
        sa.sin_addr.s_addr = ip_2_ip4(&net->net_target_ip)->addr;

        int r = connect(net->net_sock, (struct sockaddr *)&sa, sizeof(sa));
        if (r == 0) {
            // Immediate success
            net->net_connected = true;
            board_network_set_net_state(net, NET_ONLINE);
            net_abort_check(net);
        } else if (r < 0 && errno != EINPROGRESS) {
            // Fail
            net->net_ip_valid = false; // refresh DNS next time
            net_abort_check(net);
            board_network_set_net_state(net, NET_OFFLINE);
        }
    } else if (net->net_sock >= 0) {
        // Poll for completion
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(net->net_sock, &wfds);
        struct timeval tv = {0, 0};
        int sr = select(net->net_sock + 1, NULL, &wfds, NULL, &tv);

        if (sr > 0 && FD_ISSET(net->net_sock, &wfds)) {
            int so_error = 0;
            socklen_t len = sizeof(so_error);
            getsockopt(net->net_sock, SOL_SOCKET, SO_ERROR, &so_error, &len);
            if (so_error == 0) {
                net->net_connected = true;
                board_network_set_net_state(net, NET_ONLINE);
            } else {
                 net->net_ip_valid = false;
                 board_network_set_net_state(net, NET_OFFLINE);
            }
            net_abort_check(net);
        }
    }
}
