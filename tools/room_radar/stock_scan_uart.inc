/* Stock M1 UART Wi-Fi scan compatibility. ESP-AT continues to use SPI. */
#include <stdio.h>
#include <string.h>
#include "driver/uart.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define STOCK_UART UART_NUM_0
#define STOCK_AP_LIMIT 24

static wifi_ap_record_t stock_aps[STOCK_AP_LIMIT];
static uint16_t stock_ap_count;

static void stock_write(const char *text)
{
    uart_write_bytes(STOCK_UART, text, strlen(text));
}

static const char *stock_auth(wifi_auth_mode_t mode)
{
    switch (mode) {
    case WIFI_AUTH_OPEN: return "OPEN";
    case WIFI_AUTH_WEP: return "WEP";
    case WIFI_AUTH_WPA_PSK: return "WPA";
    case WIFI_AUTH_WPA2_PSK: return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK: return "WPA/WPA2";
    case WIFI_AUTH_WPA3_PSK: return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK: return "WPA2/WPA3";
    default: return "UNKNOWN";
    }
}

static void stock_scan(void)
{
    wifi_mode_t mode;
    stock_ap_count = 0;
    if (esp_wifi_get_mode(&mode) != ESP_OK) return;
    if (mode == WIFI_MODE_NULL && esp_wifi_set_mode(WIFI_MODE_STA) != ESP_OK) return;
    if (mode == WIFI_MODE_AP && esp_wifi_set_mode(WIFI_MODE_APSTA) != ESP_OK) return;
    if (esp_wifi_start() != ESP_OK || esp_wifi_scan_start(NULL, true) != ESP_OK) return;
    uint16_t count = STOCK_AP_LIMIT;
    if (esp_wifi_scan_get_ap_records(&count, stock_aps) == ESP_OK) stock_ap_count = count;
    else esp_wifi_clear_ap_list();
}

static void stock_list(void)
{
    for (uint16_t i = 0; i < stock_ap_count; ++i) {
        const wifi_ap_record_t *ap = &stock_aps[i];
        char ssid[33] = {0};
        for (unsigned j = 0; j < sizeof(ssid) - 1 && ap->ssid[j]; ++j) {
            unsigned char c = ap->ssid[j];
            ssid[j] = (c < 32 || c == 127 || c == '"') ? '?' : (char)c;
        }
        char line[96];
        snprintf(line, sizeof(line), "[%u] \"%s\" %u %d %02x:%02x:%02x:%02x:%02x:%02x %s\r\n",
                 (unsigned)i, ssid, (unsigned)ap->primary, (int)ap->rssi,
                 ap->bssid[0], ap->bssid[1], ap->bssid[2],
                 ap->bssid[3], ap->bssid[4], ap->bssid[5], stock_auth(ap->authmode));
        stock_write(line);
    }
}

static void stock_uart_task(void *arg)
{
    (void)arg;
    char line[24];
    size_t size = 0;
    bool discard = false;
    for (;;) {
        unsigned char ch;
        if (uart_read_bytes(STOCK_UART, &ch, 1, portMAX_DELAY) != 1) continue;
        if (ch != '\n') {
            if (ch == '\r') continue;
            if (size + 1 < sizeof(line)) line[size++] = (char)ch;
            else discard = true;
            continue;
        }
        line[size] = '\0';
        if (!discard && !strcmp(line, "scan -a")) stock_scan();
        else if (!discard && !strcmp(line, "list -a")) stock_list();
        else if (discard || (size && strcmp(line, "mode -w"))) stock_write("Unsupported command\r\n");
        stock_write(">> ");
        size = 0;
        discard = false;
    }
}

static void room_radar_stock_uart_start(void)
{
    if (uart_driver_install(STOCK_UART, 256, 0, 0, NULL, 0) != ESP_OK) return;
    if (uart_set_baudrate(STOCK_UART, 115200) != ESP_OK ||
        xTaskCreate(stock_uart_task, "stock_wifi", 3072, NULL, 3, NULL) != pdPASS) {
        uart_driver_delete(STOCK_UART);
    }
}
