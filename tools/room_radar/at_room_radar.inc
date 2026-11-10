/* See COPYING.txt for license details. */
#include "radar_metric.h"
#include "sdkconfig.h"
#include "ping/ping_sock.h"
#include "lwip/def.h"
#include "lwip/ip_addr.h"
#include "lwip/icmp.h"
#include "lwip/inet_chksum.h"
#include "lwip/netif.h"
#include "lwip/sockets.h"

#define RADAR_IDLE 0U
#define RADAR_RUNNING 1U
#define RADAR_DONE 2U
#define RADAR_ERROR 3U

static portMUX_TYPE radar_lock = portMUX_INITIALIZER_UNLOCKED;
static unsigned radar_state, radar_samples, radar_value;
static bool radar_cancel, radar_ping_done, radar_ping_reply;
static esp_ping_handle_t radar_ping;
#if CONFIG_ESP_WIFI_CSI_ENABLED
static radar_metric_t radar_metric;
static uint8_t radar_ap[6];
static bool radar_capture;
static unsigned radar_csi_length;

static void radar_csi(void *context, wifi_csi_info_t *info)
{
    (void)context;
    /* Drop the first word on all frames so the same bins are compared. */
    if (!info || !info->buf || info->len < 4U + 2U * RADAR_BINS) return;
    portENTER_CRITICAL(&radar_lock);
    if (radar_capture && !memcmp(info->mac, radar_ap, sizeof(radar_ap))) {
        if (!radar_csi_length) radar_csi_length = info->len;
        if (info->len == radar_csi_length) radar_metric_add(&radar_metric, info->buf + 4);
    }
    portEXIT_CRITICAL(&radar_lock);
}
#endif

static bool radar_cancelled(void)
{
    portENTER_CRITICAL(&radar_lock);
    bool value = radar_cancel;
    portEXIT_CRITICAL(&radar_lock);
    return value;
}

static void radar_ping_success(esp_ping_handle_t ping, void *args)
{
    (void)args;
    portENTER_CRITICAL(&radar_lock);
    if (radar_ping == ping) radar_ping_reply = true;
    portEXIT_CRITICAL(&radar_lock);
}

static void radar_ping_end(esp_ping_handle_t ping, void *args)
{
    (void)args;
    portENTER_CRITICAL(&radar_lock);
    if (radar_ping == ping) radar_ping_done = true;
    portEXIT_CRITICAL(&radar_lock);
}

/* Only one bounded ping session exists at a time. The callbacks keep no stack pointers. */
static bool radar_run_ping(uint32_t target, unsigned count, unsigned timeout_ms,
                           unsigned budget_ms, int netif_index, bool *reply_received)
{
    esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();
    ip_addr_set_ip4_u32(&config.target_addr, target);
    config.count = count;
    config.interval_ms = 100;
    config.timeout_ms = timeout_ms;
    config.data_size = 8;
    config.interface = netif_index;
    const esp_ping_callbacks_t callbacks = {
        .on_ping_success = radar_ping_success, .on_ping_end = radar_ping_end,
    };
    esp_ping_handle_t ping;
    if (esp_ping_new_session(&config, &callbacks, &ping) != ESP_OK) return false;
    portENTER_CRITICAL(&radar_lock);
    radar_ping_done = radar_ping_reply = false;
    radar_ping = ping;
    portEXIT_CRITICAL(&radar_lock);
    bool done = false;
    if (esp_ping_start(ping) == ESP_OK) {
        TickType_t start = xTaskGetTickCount();
        do {
            vTaskDelay(pdMS_TO_TICKS(10) ? pdMS_TO_TICKS(10) : 1);
            portENTER_CRITICAL(&radar_lock);
            done = radar_ping_done;
            portEXIT_CRITICAL(&radar_lock);
        } while (!done && !radar_cancelled() &&
                 xTaskGetTickCount() - start < pdMS_TO_TICKS(budget_ms));
    }
    portENTER_CRITICAL(&radar_lock);
    *reply_received = radar_ping_reply;
    radar_ping = NULL;
    portEXIT_CRITICAL(&radar_lock);
    if (!done) esp_ping_stop(ping);
    esp_ping_delete_session(ping);
    return done;
}

static bool radar_peer_reply(int sock, uint32_t target, uint16_t id, uint16_t sequence)
{
    struct sockaddr_in destination = {0};
    destination.sin_family = AF_INET;
    destination.sin_addr.s_addr = htonl(target);
    struct icmp_echo_hdr request = {0};
    ICMPH_TYPE_SET(&request, ICMP_ECHO);
    request.id = htons(id);
    request.seqno = htons(sequence);
    request.chksum = inet_chksum(&request, sizeof(request));
    if (sendto(sock, &request, sizeof(request), 0, (struct sockaddr *)&destination,
               sizeof(destination)) != (int)sizeof(request)) return false;

    TickType_t start = xTaskGetTickCount();
    while (!radar_cancelled() && xTaskGetTickCount() - start < pdMS_TO_TICKS(90)) {
        uint8_t packet[96];
        struct sockaddr_in sender;
        socklen_t size = sizeof(sender);
        int received = recvfrom(sock, packet, sizeof(packet), MSG_DONTWAIT,
                                (struct sockaddr *)&sender, &size);
        if (received >= 0 && sender.sin_family == AF_INET &&
            sender.sin_addr.s_addr == destination.sin_addr.s_addr &&
            radar_is_echo_reply(packet, (unsigned)received, id, sequence)) return true;
        vTaskDelay(1);
    }
    return false;
}

static bool radar_measure_lan(const esp_netif_ip_info_t *ip, int netif_index,
                              unsigned *probed, unsigned *found)
{
    uint32_t address = ntohl(ip->ip.addr), first, last;
    if (!radar_subnet(address, ntohl(ip->netmask.addr), &first, &last)) return false;
    int sock = socket(AF_INET, SOCK_RAW, IPPROTO_ICMP);
    if (sock < 0) return false;
    struct ifreq iface = {0};
    bool ok = netif_index_to_name(netif_index, iface.ifr_name) != NULL &&
              setsockopt(sock, SOL_SOCKET, SO_BINDTODEVICE, &iface, sizeof(iface)) == 0;
    uint16_t id = (uint16_t)esp_random();
    TickType_t start = xTaskGetTickCount();
    for (uint32_t candidate = first; ok && candidate <= last; ++candidate) {
        if (radar_cancelled() || xTaskGetTickCount() - start >= pdMS_TO_TICKS(30000)) {
            ok = false;
            break;
        }
        if (candidate == address) continue;
        ++*probed;
        if (radar_peer_reply(sock, candidate, id, (uint16_t)*probed)) ++*found;
        portENTER_CRITICAL(&radar_lock);
        radar_samples = *probed;
        radar_value = *found;
        portEXIT_CRITICAL(&radar_lock);
        vTaskDelay(1);
    }
    close(sock);
    return ok;
}

#if CONFIG_ESP_WIFI_CSI_ENABLED
static bool radar_measure_csi(const esp_netif_ip_info_t *ip, const wifi_ap_record_t *ap,
                              int netif_index, unsigned *samples, unsigned *score)
{
    if (!ip->gw.addr) return false;
    /* ESP32-C6 IDF acquisition configuration for legacy and HT20 frames. */
    wifi_csi_config_t config = {
        .enable = true, .acquire_csi_legacy = true, .acquire_csi_ht20 = true,
        .acquire_csi_su = true,
    };
    portENTER_CRITICAL(&radar_lock);
    memset(&radar_metric, 0, sizeof(radar_metric));
    memcpy(radar_ap, ap->bssid, sizeof(radar_ap));
    radar_csi_length = 0;
    radar_capture = true;
    portEXIT_CRITICAL(&radar_lock);
    bool started = esp_wifi_set_csi_config(&config) == ESP_OK &&
                   esp_wifi_set_csi_rx_cb(radar_csi, NULL) == ESP_OK &&
                   esp_wifi_set_csi(true) == ESP_OK;
    bool heard = false;
    bool done = started && radar_run_ping(ip->gw.addr, 32, 100, 6500, netif_index, &heard);
    portENTER_CRITICAL(&radar_lock);
    radar_capture = false;
    *samples = radar_metric.frames;
    *score = radar_metric_score(&radar_metric);
    portEXIT_CRITICAL(&radar_lock);
    esp_wifi_set_csi(false);
    esp_wifi_set_csi_rx_cb(NULL, NULL);
    return done;
}
#endif

static void radar_worker(void *arg)
{
    unsigned operation = (unsigned)(uintptr_t)arg;
    unsigned samples = 0, value = 0;
    bool ok = false;
    esp_netif_t *sta = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    esp_netif_ip_info_t ip;
    wifi_ap_record_t ap;
    if (sta && esp_wifi_sta_get_ap_info(&ap) == ESP_OK &&
        esp_netif_get_ip_info(sta, &ip) == ESP_OK && ip.ip.addr) {
        int index = esp_netif_get_netif_impl_index(sta);
        if (index > 0) {
            if (operation == 2) ok = radar_measure_lan(&ip, index, &samples, &value);
#if CONFIG_ESP_WIFI_CSI_ENABLED
            else ok = radar_measure_csi(&ip, &ap, index, &samples, &value);
#endif
        }
    }
    portENTER_CRITICAL(&radar_lock);
    radar_samples = samples;
    radar_value = value;
    radar_state = radar_cancel ? RADAR_IDLE : (ok ? RADAR_DONE : RADAR_ERROR);
    portEXIT_CRITICAL(&radar_lock);
    vTaskDelete(NULL);
}

static uint8_t at_query_cmd_m1radar(uint8_t *name)
{
    (void)name;
    portENTER_CRITICAL(&radar_lock);
    unsigned state = radar_state, samples = radar_samples, value = radar_value;
    portEXIT_CRITICAL(&radar_lock);
    at_send_line("\r\n+M1RADAR:1,%u,%u,%u\r\n", state, samples, value);
    return ESP_AT_RESULT_CODE_OK;
}

static uint8_t at_setup_cmd_m1radar(uint8_t params)
{
    int32_t operation;
    if (params != 1 || esp_at_get_para_as_digit(0, &operation) != ESP_AT_PARA_PARSE_RESULT_OK ||
        operation < 0 || operation > 2) return ESP_AT_RESULT_CODE_ERROR;
#if !CONFIG_ESP_WIFI_CSI_ENABLED
    if (operation == 1) return ESP_AT_RESULT_CODE_ERROR;
#endif
    portENTER_CRITICAL(&radar_lock);
    if (!operation) {
        radar_cancel = true;
        portEXIT_CRITICAL(&radar_lock);
        return ESP_AT_RESULT_CODE_OK;
    }
    if (radar_state == RADAR_RUNNING) {
        portEXIT_CRITICAL(&radar_lock);
        return ESP_AT_RESULT_CODE_ERROR;
    }
    radar_cancel = false;
    radar_samples = radar_value = 0;
    radar_state = RADAR_RUNNING;
    portEXIT_CRITICAL(&radar_lock);
    if (xTaskCreate(radar_worker, "room_radar", 4096, (void *)(uintptr_t)operation,
                    tskIDLE_PRIORITY + 2, NULL) == pdPASS) return ESP_AT_RESULT_CODE_OK;
    portENTER_CRITICAL(&radar_lock);
    radar_state = RADAR_ERROR;
    portEXIT_CRITICAL(&radar_lock);
    return ESP_AT_RESULT_CODE_ERROR;
}
