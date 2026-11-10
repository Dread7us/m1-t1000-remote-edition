/* See COPYING.txt for license details. */
#include "main.h"
#include "app_freertos.h"
#include "m1_display.h"
#include "m1_room_radar.h"
#include "m1_virtual_kb.h"
#include "esp_app_main.h"
#include "ff.h"

#define MAP_PATH "0:/System/room-radar.tsv"
#define MAP_ROWS 24U
#define RADAR_REPLY_SIZE 512U

static room_radar_reading_t readings[MAP_ROWS];
static char reply[RADAR_REPLY_SIZE];

static int radar_key(TickType_t timeout)
{
    S_M1_Main_Q_t event;
    S_M1_Buttons_Status keys;
    if (xQueueReceive(main_q_hdl, &event, timeout) != pdTRUE ||
        event.q_evt_type != Q_EVENT_KEYPAD ||
        xQueueReceive(button_events_q_hdl, &keys, 0) != pdTRUE) return -1;
    const int ids[] = {BUTTON_BACK_KP_ID, BUTTON_OK_KP_ID, BUTTON_UP_KP_ID,
                       BUTTON_DOWN_KP_ID, BUTTON_LEFT_KP_ID};
    for (unsigned i = 0; i < sizeof(ids) / sizeof(ids[0]); ++i)
        if (keys.event[ids[i]] == BUTTON_EVENT_CLICK) return ids[i];
    return -1;
}

static void radar_panel(const char *title, const char *a, const char *b,
                        const char *c, const char *action)
{
    m1_u8g2_firstpage();
    m1_draw_header_bar(&m1_u8g2, title, NULL);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 5, 23, 118, a, TEXT_ALIGN_LEFT);
    m1_draw_text(&m1_u8g2, 5, 34, 118, b, TEXT_ALIGN_LEFT);
    m1_draw_text(&m1_u8g2, 5, 45, 118, c, TEXT_ALIGN_LEFT);
    m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Back", action,
                       action ? button_ok_8x8 : NULL);
    m1_u8g2_nextpage();
}

static void wait_back(void)
{
    while (radar_key(portMAX_DELAY) != BUTTON_BACK_KP_ID) {}
}

static bool radar_at(const char *command)
{
    memset(reply, 0, sizeof(reply));
    uint8_t result = spi_AT_send_recv(command, reply, sizeof(reply) - 1U, 2);
    size_t len = strlen(reply);
    return result == SUCCESS && len >= 4 && !strcmp(reply + len - 4, "OK\r\n") &&
           (len == 4 || reply[len - 5] == '\n');
}

static void clean_label(char *label)
{
    for (; *label; ++label)
        if ((unsigned char)*label < 32U || (unsigned char)*label > 126U) *label = '_';
}

static bool map_save(room_radar_reading_t *reading)
{
    char line[104];
    clean_label(reading->room);
    clean_label(reading->network);
    int len = snprintf(line, sizeof(line), "%s\t%s\t%u\t%d\t%u\n",
                       reading->room, reading->network, reading->networks,
                       reading->rssi, reading->channel);
    if (len <= 0 || (size_t)len >= sizeof(line)) return false;
    FIL file;
    f_mkdir("0:/System");
    if (f_open(&file, MAP_PATH, FA_WRITE | FA_OPEN_APPEND) != FR_OK) return false;
    FSIZE_t start = f_tell(&file);
    UINT written = 0;
    bool ok = f_write(&file, line, (UINT)len, &written) == FR_OK && written == (UINT)len;
    if (!ok && f_lseek(&file, start) == FR_OK) f_truncate(&file);
    FRESULT closed = f_close(&file);
    return ok && closed == FR_OK;
}

static unsigned map_load(void)
{
    FIL file;
    char line[104];
    unsigned count = 0;
    if (f_open(&file, MAP_PATH, FA_READ) != FR_OK) return 0;
    while (f_gets(line, sizeof(line), &file)) {
        room_radar_reading_t row = {0};
        int end = 0;
        if (sscanf(line, "%24[^\t]\t%32[^\t]\t%u\t%d\t%u%n", row.room, row.network,
                   &row.networks, &row.rssi, &row.channel, &end) == 5 &&
            (line[end] == '\n' || line[end] == '\r') && row.channel >= 1 &&
            row.channel <= 14 && row.rssi >= -127 && row.rssi <= 0) {
            clean_label(row.room);
            clean_label(row.network);
            if (count == MAP_ROWS) {
                memmove(readings, readings + 1, sizeof(readings) - sizeof(readings[0]));
                --count;
            }
            readings[count++] = row;
        }
    }
    f_close(&file);
    return count;
}

static void show_reading(const room_radar_reading_t *row, const char *footer)
{
    char info[32];
    snprintf(info, sizeof(info), "%d dBm  Ch %u  APs %u", row->rssi, row->channel, row->networks);
    radar_panel("Wi-Fi map", row->room, row->network, info, footer);
}

static void radar_map(void)
{
    unsigned count = map_load();
    unsigned index = count ? count - 1U : 0U;
    for (;;) {
        if (count) show_reading(&readings[index], "Add");
        else radar_panel("Wi-Fi map", "No saved readings", "Choose a spot", "OK adds a reading", "Add");
        int key = radar_key(portMAX_DELAY);
        if (key == BUTTON_BACK_KP_ID) return;
        if (key == BUTTON_UP_KP_ID && index) --index;
        if (key == BUTTON_DOWN_KP_ID && index + 1U < count) ++index;
        if (key != BUTTON_OK_KP_ID) continue;

        char name[64] = {0};
        if (!m1_vkb_get_filename("Location name", "Room", name) || !name[0]) continue;
        room_radar_reading_t row = {0};
        snprintf(row.room, sizeof(row.room), "%.24s", name);
        radar_panel("Wi-Fi map", row.room, "Scanning 2.4 GHz", "Hold the M1 here", NULL);
        if (!wifi_room_radar_scan(&row)) {
            radar_panel("Wi-Fi map", "No valid reading", "Try another spot", NULL, NULL);
            wait_back();
            continue;
        }
        if (!map_save(&row)) {
            radar_panel("Wi-Fi map", "Reading not saved", "Check mounted SD", NULL, NULL);
            wait_back();
            continue;
        }
        count = map_load();
        index = count ? count - 1U : 0U;
    }
}

static void radar_hotspot(void)
{
    for (;;) {
        const char *mode = radar_at("AT+CWMODE?\r\n") ? strstr(reply, "+CWMODE:") : NULL;
        if (!mode || (mode[8] != '2' && mode[8] != '3')) {
            radar_panel("Hotspot clients", "M1 hotspot inactive", "Set AP mode in Wi-Fi", NULL, "Retry");
        } else if (radar_at("AT+CWLIF\r\n")) {
            unsigned count = 0;
            for (const char *p = reply; (p = strstr(p, "+CWLIF:")); p += 7) ++count;
            char line[26];
            snprintf(line, sizeof(line), "DHCP clients: %u", count);
            radar_panel("Hotspot clients", line, "Connected to M1 AP", "Static IPs may be absent", "Rescan");
        } else {
            radar_panel("Hotspot clients", "List unavailable", "Check M1 hotspot", NULL, "Retry");
        }
        int key;
        do { key = radar_key(portMAX_DELAY); } while (key != BUTTON_OK_KP_ID && key != BUTTON_BACK_KP_ID);
        if (key == BUTTON_BACK_KP_ID) return;
    }
}

/* The coprocessor runs bounded jobs. This screen polls and can cancel either job. */
static bool radar_job(unsigned operation, room_radar_result_t *result, bool *cancelled)
{
    const char *title = operation == 1U ? "Room motion" : "LAN devices";
    const char *command = operation == 1U ? "AT+M1RADAR=1\r\n" : "AT+M1RADAR=2\r\n";
    *cancelled = false;
    if (!radar_at(command)) return false;
    TickType_t start = xTaskGetTickCount();
    radar_panel(title, operation == 1U ? "Sampling Wi-Fi CSI" : "Checking local peers",
                operation == 1U ? "Keep the M1 still" : "Private subnet only", NULL, NULL);
    TickType_t last_poll = start;
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(operation == 1U ? 8500 : 38000)) {
        if (radar_key(pdMS_TO_TICKS(100)) == BUTTON_BACK_KP_ID) {
            *cancelled = true;
            break;
        }
        TickType_t now = xTaskGetTickCount();
        if (now - last_poll < pdMS_TO_TICKS(500)) continue;
        last_poll = now;
        if (!radar_at("AT+M1RADAR?\r\n") || !room_radar_parse_result(reply, result)) break;
        if (result->state == ROOM_RADAR_DONE) return true;
        if (result->state != ROOM_RADAR_RUNNING) break;
    }
    radar_at("AT+M1RADAR=0\r\n");
    return false;
}

static void radar_measure(unsigned operation)
{
    const char *title = operation == 1U ? "Room motion" : "LAN devices";
    room_radar_result_t result;
    if (!radar_at("AT+M1RADAR?\r\n") || !room_radar_parse_result(reply, &result)) {
        radar_panel(title, "ESP32 update needed", "Room Radar companion", "See project setup guide", NULL);
        wait_back();
        return;
    }
    bool calibrated = false;
    unsigned baseline = 0;
    char summary[32] = {0};
    radar_panel(title, operation == 1U ? "Calibrate empty room" : "Your connected network",
                operation == 1U ? "Set M1 down first" : "Counts ping replies",
                operation == 1U ? "Press OK, stay still" : "Use on your own LAN", "Start");
    for (;;) {
        int key = radar_key(portMAX_DELAY);
        if (key == BUTTON_BACK_KP_ID) return;
        if (key == BUTTON_LEFT_KP_ID && operation == 1U) {
            calibrated = false;
            radar_panel(title, "Calibrate empty room", "Hold position still", NULL, "Start");
            continue;
        }
        if (key != BUTTON_OK_KP_ID) continue;
        bool cancelled;
        if (!radar_job(operation, &result, &cancelled)) {
            if (cancelled) return;
            radar_panel(title, "Measurement failed", "Check Wi-Fi connection", NULL, "Retry");
            continue;
        }
        if (operation == 2U) {
            snprintf(summary, sizeof(summary), "%u peers replied", result.value);
            char detail[28];
            snprintf(detail, sizeof(detail), "%u addresses checked", result.samples);
            radar_panel(title, summary, detail, "ICMP responders only", "Rescan");
        } else if (result.samples < ROOM_RADAR_MIN_SAMPLES) {
            radar_panel(title, "Too few CSI samples", "Try closer to router", NULL, "Retry");
        } else if (!calibrated) {
            baseline = result.value;
            calibrated = true;
            radar_panel(title, "Baseline recorded", "Keep M1 in same spot", "Left recalibrates", "Measure");
        } else {
            snprintf(summary, sizeof(summary), "Score %u / base %u", result.value, baseline);
            radar_panel(title, room_radar_motion(baseline, result.value) ? "Motion detected" : "Quiet window",
                        summary, "Left recalibrates", "Measure");
        }
    }
}

void room_radar_run(void)
{
    const char *items[] = {"Wi-Fi map", "LAN devices", "Hotspot clients", "Room motion"};
    unsigned selected = 0;
    for (;;) {
        unsigned first = selected >= 3 ? selected - 2 : 0;
        m1_u8g2_firstpage();
        m1_draw_header_bar(&m1_u8g2, "Room Radar", "2.4G");
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        for (unsigned i = first; i < first + 3; ++i) {
            int y = 15 + (int)(i - first) * 12;
            m1_ui_selection(&m1_u8g2, 2, y, 124, 11, i == selected);
            m1_draw_text(&m1_u8g2, 8, y + 8, 112, items[i], TEXT_ALIGN_LEFT);
        }
        m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Back", "Open", button_ok_8x8);
        m1_u8g2_nextpage();
        int key = radar_key(portMAX_DELAY);
        if (key == BUTTON_BACK_KP_ID) return;
        if (key == BUTTON_UP_KP_ID && selected) --selected;
        if (key == BUTTON_DOWN_KP_ID && selected < 3) ++selected;
        if (key == BUTTON_OK_KP_ID) {
            switch (selected) {
            case 0: radar_map(); break;
            case 1: radar_measure(2); break;
            case 2: radar_hotspot(); break;
            case 3: radar_measure(1); break;
            }
        }
    }
}
