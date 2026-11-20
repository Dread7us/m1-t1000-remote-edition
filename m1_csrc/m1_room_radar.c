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
                       BUTTON_DOWN_KP_ID, BUTTON_LEFT_KP_ID, BUTTON_RIGHT_KP_ID};
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

static room_radar_reply_t radar_exchange(const char *command)
{
    memset(reply, 0, sizeof(reply));
    if (spi_AT_send_recv(command, reply, sizeof(reply) - 1U, 3) != SUCCESS)
        return ROOM_RADAR_NO_REPLY;
    return room_radar_reply_status(reply);
}

static bool radar_at(const char *command)
{
    return radar_exchange(command) == ROOM_RADAR_OK_REPLY;
}

static bool radar_status(room_radar_result_t *result, room_radar_reply_t *status)
{
    *status = radar_exchange("AT+M1RADAR?\r\n");
    return *status != ROOM_RADAR_NO_REPLY && room_radar_status_data(reply, result);
}

static void radar_save_status(const char *stage)
{
    FIL file;
    UINT written;
    f_mkdir("0:/System");
    if (f_open(&file, "0:/System/room-radar-status.txt", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK)
        return;
    f_write(&file, stage, (UINT)strlen(stage), &written);
    f_write(&file, "\r\n", 2, &written);
    f_write(&file, reply, (UINT)strlen(reply), &written);
    f_close(&file);
}

static bool radar_log_window(bool measuring, unsigned index, unsigned score, unsigned samples)
{
    FIL file;
    UINT written;
    char line[80];
    int length = snprintf(line, sizeof(line), "%lu,%s,%u,%u,%u\r\n",
                          (unsigned long)HAL_GetTick(), measuring ? "measure" : "quiet",
                          index, score, samples);
    if (length <= 0 || length >= (int)sizeof(line)) return false;
    f_mkdir("0:/System");
    if (f_open(&file, "0:/System/room-motion.csv", FA_WRITE | FA_OPEN_APPEND) != FR_OK) return false;
    bool saved = f_write(&file, line, (UINT)length, &written) == FR_OK &&
                 written == (UINT)length && f_sync(&file) == FR_OK;
    if (f_close(&file) != FR_OK) saved = false;
    return saved;
}

static bool radar_radio_ready(const char *title)
{
    radar_panel(title, "Checking Wi-Fi", "Please wait...", NULL, NULL);
    wifi_room_radar_prepare();
    if (radar_at("AT\r\n")) return true;
    radar_panel(title, "Recovering Wi-Fi", "Starting radio...", NULL, NULL);
    if (wifi_room_radar_recover() && radar_at("AT\r\n")) return true;
    radar_save_status("Radio readiness failed");
    radar_panel(title, "Wi-Fi did not start", "Reconnect USB or reboot", "Then try again", NULL);
    wait_back();
    return false;
}

static bool radar_connected(void)
{
    if (!radar_at("AT+CWJAP?\r\n") || !strstr(reply, "+CWJAP:")) return false;
    if (!radar_at("AT+CIPSTA?\r\n")) return false;
    const char *ip = strstr(reply, "+CIPSTA:ip:\"");
    return ip && strncmp(ip + 12, "0.0.0.0\"", 8) != 0;
}

static bool radar_try_network(const room_radar_network_t *network, bool use_saved)
{
    char password[65] = {0};
    if (!use_saved && network->secured &&
        !m1_vkb_get_password("Wi-Fi password", password, sizeof(password))) return false;
    radar_panel("Wi-Fi setup", "Joining network", network->ssid, "Please wait...", NULL);
    bool saved = false;
    bool connected = wifi_room_radar_join(network->ssid, password, use_saved, &saved);
    memset(password, 0, sizeof(password));
    if (connected && !saved) {
        radar_panel("Wi-Fi setup", "Connected for this use", "Could not save password", "Check SD card later", "Continue");
        int key;
        do { key = radar_key(portMAX_DELAY); }
        while (key != BUTTON_OK_KP_ID && key != BUTTON_BACK_KP_ID);
        if (key == BUTTON_BACK_KP_ID) return false;
    }
    return connected && radar_connected();
}

static bool radar_connect(void)
{
    if (radar_connected()) return true;
    room_radar_network_t networks[ROOM_RADAR_NETWORKS];
    radar_panel("Wi-Fi setup", "Finding networks", "Using saved passwords", NULL, NULL);
    unsigned count = wifi_room_radar_networks(networks, ROOM_RADAR_NETWORKS);
    /* Reconnect a visible saved network, strongest first. */
    for (unsigned i = 0; i < count; ++i) {
        if (radar_key(0) == BUTTON_BACK_KP_ID) return false;
        if (networks[i].saved) {
            if (radar_try_network(&networks[i], true)) return true;
            break;
        }
    }
    unsigned selected = 0;
    for (;;) {
        if (!count) {
            radar_panel("Wi-Fi setup", "No 2.4 GHz networks", "Move closer to router", NULL, "Rescan");
        } else {
            char detail[32];
            snprintf(detail, sizeof(detail), "%d dBm  %u/%u", networks[selected].rssi, selected + 1U, count);
            radar_panel("Choose Wi-Fi", networks[selected].ssid, detail, "Up/Down choose network", "Join");
        }
        int key = radar_key(portMAX_DELAY);
        if (key == BUTTON_BACK_KP_ID) return false;
        if (key == BUTTON_UP_KP_ID && selected) --selected;
        if (key == BUTTON_DOWN_KP_ID && selected + 1U < count) ++selected;
        if (key != BUTTON_OK_KP_ID) continue;
        if (!count) {
            count = wifi_room_radar_networks(networks, ROOM_RADAR_NETWORKS);
            continue;
        }
        if (radar_try_network(&networks[selected], false)) return true;
        radar_panel("Wi-Fi setup", "Could not join", "Check password or range", NULL, "Try again");
        do { key = radar_key(portMAX_DELAY); }
        while (key != BUTTON_OK_KP_ID && key != BUTTON_BACK_KP_ID);
        if (key == BUTTON_BACK_KP_ID) return false;
    }
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
    radar_panel("Wi-Fi Map", row->room, row->network, info, footer);
}

static void radar_map(void)
{
    room_radar_reading_t row = {0};
    unsigned count = map_load();
    unsigned index = count ? count - 1U : 0U;
    bool live = true;
    bool refresh = true;
    for (;;) {
        if (refresh) {
            snprintf(row.room, sizeof(row.room), "Spot %u", count + 1U);
            radar_panel("Wi-Fi Map", "Reading this spot", "Scanning 2.4 GHz", NULL, NULL);
            if (!wifi_room_radar_scan(&row)) {
                radar_panel("Wi-Fi Map", "No valid reading", "Move closer to Wi-Fi", NULL, "Retry");
                int key = radar_key(portMAX_DELAY);
                if (key == BUTTON_BACK_KP_ID) return;
                continue;
            }
            refresh = false;
        }
        if (live) show_reading(&row, "Save");
        else show_reading(&readings[index], "New");
        int key = radar_key(portMAX_DELAY);
        if (key == BUTTON_BACK_KP_ID) return;
        if (key == BUTTON_LEFT_KP_ID) {live = true; refresh = true;}
        if (key == BUTTON_RIGHT_KP_ID && live) {
            char name[64] = {0};
            if (m1_vkb_get_filename("Name this spot", row.room, name) && name[0])
                snprintf(row.room, sizeof(row.room), "%.24s", name);
        }
        if (key == BUTTON_UP_KP_ID && count) {
            if (live) index = count - 1U;
            else if (index) --index;
            live = false;
        }
        if (key == BUTTON_DOWN_KP_ID && !live && index + 1U < count) ++index;
        if (key != BUTTON_OK_KP_ID) continue;
        if (!live) {live = true; refresh = true; continue;}
        if (!map_save(&row)) {
            radar_panel("Wi-Fi Map", "Reading not saved", "Check mounted SD", NULL, NULL);
            wait_back();
        } else {
            count = map_load();
            index = count ? count - 1U : 0U;
            live = false;
        }
    }
}

static bool radar_start_hotspot(char previous_mode)
{
    char password[65] = {0}, quoted[129], command[200];
    if (!m1_vkb_get_password("Hotspot password", password, sizeof(password))) return false;
    size_t length = strlen(password);
    if (length < 8 || length > 63 || !room_radar_quote(password, quoted, sizeof(quoted))) {
        memset(password, 0, sizeof(password));
        radar_panel("M1 hotspot", "Use 8 to 63 characters", "Choose a password", NULL, NULL);
        wait_back();
        return false;
    }
    snprintf(command, sizeof(command), "AT+CWSAP=\"M1-Radar\",\"%s\",6,3\r\n", quoted);
    memset(password, 0, sizeof(password));
    memset(quoted, 0, sizeof(quoted));
    bool ok = radar_at("AT+CWMODE=3\r\n") && radar_at(command);
    if (!ok) {
        char restore[24];
        snprintf(restore, sizeof(restore), "AT+CWMODE=%c\r\n", previous_mode);
        radar_at(restore);
    }
    memset(command, 0, sizeof(command));
    memset(reply, 0, sizeof(reply));
    return ok;
}

static void radar_hotspot(void)
{
    for (;;) {
        const char *mode = radar_at("AT+CWMODE?\r\n") ? strstr(reply, "+CWMODE:") : NULL;
        char previous_mode = mode && mode[8] >= '0' && mode[8] <= '3' ? mode[8] : '1';
        bool active = previous_mode == '2' || previous_mode == '3';
        if (!active) {
            radar_panel("Hotspot Clients", "Start an M1 hotspot", "Join it from your phone", "Name: M1-Radar", "Start");
        } else if (radar_at("AT+CWLIF\r\n")) {
            unsigned count = 0;
            for (const char *p = reply; (p = strstr(p, "+CWLIF:")); p += 7) ++count;
            char line[26];
            snprintf(line, sizeof(line), "DHCP clients: %u", count);
            radar_panel("Hotspot Clients", line, "Connected to M1 AP", "Static IPs may be absent", "Rescan");
        } else {
            radar_panel("Hotspot Clients", "List unavailable", "Check M1 hotspot", NULL, "Retry");
        }
        int key;
        do { key = radar_key(portMAX_DELAY); } while (key != BUTTON_OK_KP_ID && key != BUTTON_BACK_KP_ID);
        if (key == BUTTON_BACK_KP_ID) return;
        if (!active && !radar_start_hotspot(previous_mode)) {
            radar_panel("M1 hotspot", "Hotspot did not start", "Try another password", NULL, NULL);
            wait_back();
        }
    }
}

/* The coprocessor runs bounded jobs. This screen polls and can cancel either job. */
static bool radar_job(unsigned operation, room_radar_result_t *result, bool *cancelled)
{
    const char *title = operation == 1U ? "Room Motion" : "LAN Devices";
    const char *command = operation == 1U ? "AT+M1RADAR=1\r\n" : "AT+M1RADAR=2\r\n";
    *cancelled = false;
    if (!radar_at(command)) return false;
    TickType_t start = xTaskGetTickCount();
    if (operation == 2U)
        radar_panel(title, "Checking local peers", "Private subnet only", NULL, NULL);
    TickType_t last_poll = start;
    while (xTaskGetTickCount() - start < pdMS_TO_TICKS(operation == 1U ? 8500 : 38000)) {
        if (radar_key(pdMS_TO_TICKS(100)) == BUTTON_BACK_KP_ID) {
            *cancelled = true;
            break;
        }
        TickType_t now = xTaskGetTickCount();
        if (now - last_poll < pdMS_TO_TICKS(500)) continue;
        last_poll = now;
        room_radar_reply_t status;
        if (!radar_status(result, &status)) {
            if (status == ROOM_RADAR_NO_REPLY) continue;
            break;
        }
        if (result->state == ROOM_RADAR_DONE) return true;
        if (result->state != ROOM_RADAR_RUNNING) break;
    }
    radar_at("AT+M1RADAR=0\r\n");
    return false;
}

static void radar_measure(unsigned operation)
{
    const char *title = operation == 1U ? "Room Motion" : "LAN Devices";
    room_radar_result_t result;
    room_radar_reply_t status;
    if (operation == 1U && !radar_log_window(false, 0, 0, 0))
        m1_message_box(&m1_u8g2, "Motion Log Not Saved", "Mount SD card", "Then retry", "OK");
    bool recovered = false;
    while (!radar_status(&result, &status)) {
        if (status == ROOM_RADAR_ERROR_REPLY) {
            radar_panel(title, "Needs Radar firmware", "Basic Wi-Fi is ready", "See Advanced", NULL);
            wait_back();
            return;
        }
        if (!recovered && status == ROOM_RADAR_NO_REPLY) {
            recovered = true;
            radar_panel(title, "Checking radio link", "Please wait...", NULL, NULL);
            wifi_room_radar_recover();
            continue;
        }
        radar_save_status("Room Radar capability query failed");
        radar_panel(title, status == ROOM_RADAR_NO_REPLY ? "Radio did not reply" : "Incomplete reply",
                    "See Advanced", NULL, "Retry");
        int key;
        do { key = radar_key(portMAX_DELAY); }
        while (key != BUTTON_OK_KP_ID && key != BUTTON_BACK_KP_ID);
        if (key == BUTTON_BACK_KP_ID) return;
        recovered = false;
    }
    if (!radar_connect()) return;
    bool calibrated = false;
    unsigned baseline = 0, quiet_low = 0, quiet_high = 0;
    unsigned measurement_windows = 1;
    char summary[32];
    bool first_run = true;
    for (;;) {
        if (first_run && operation == 1U) {
            radar_panel(title, "Set M1 down", "Keep the room quiet", "Starts in 5 seconds", NULL);
            TickType_t start = xTaskGetTickCount();
            while (xTaskGetTickCount() - start < pdMS_TO_TICKS(5000)) {
                if (radar_key(pdMS_TO_TICKS(100)) == BUTTON_BACK_KP_ID) return;
            }
        }
        int key = first_run ? BUTTON_OK_KP_ID : radar_key(portMAX_DELAY);
        first_run = false;
        if (key == BUTTON_BACK_KP_ID) return;
        if (operation == 1U && key == BUTTON_LEFT_KP_ID) {
            calibrated = false;
            first_run = true;
            continue;
        }
        if (operation == 1U && calibrated && key == BUTTON_RIGHT_KP_ID) {
            measurement_windows = measurement_windows == 1 ? 3 : 1;
            radar_panel(title, measurement_windows == 1 ? "Quick: 1 window" : "Extended: 3 windows",
                        "Keep the M1 still", "Left: Calibrate", "Measure");
            continue;
        }
        if (key != BUTTON_OK_KP_ID) continue;

        unsigned windows = operation != 1U ? 1U :
                           calibrated ? measurement_windows : ROOM_RADAR_QUIET_WINDOWS;
        unsigned low = ROOM_RADAR_MAX_SCORE, high = 0, total = 0;
        room_radar_motion_result_t verdict = ROOM_RADAR_QUIET;
        bool failed = false;
        bool log_saved = true;
        for (unsigned i = 0; i < windows; ++i) {
            if (operation == 1U) {
                snprintf(summary, sizeof(summary), "Window %u of %u", i + 1U, windows);
                radar_panel(title, calibrated ? "Measuring motion" : "Recording quiet room",
                            summary, "Keep the M1 still", NULL);
            }
            if (operation == 1U) memset(&result, 0, sizeof(result));
            bool cancelled;
            if (!radar_job(operation, &result, &cancelled)) {
                if (operation == 1U &&
                    !radar_log_window(calibrated, i + 1U, result.value, result.samples))
                    m1_message_box(&m1_u8g2, "Motion Log Not Saved", "Mount SD card", "Then retry", "OK");
                if (cancelled) return;
                if (!radar_connected()) {
                    calibrated = false;
                    if (!radar_connect()) return;
                }
                radar_panel(title, "Could not measure", "Check Wi-Fi and retry", NULL, "Retry");
                failed = true;
                break;
            }
            if (operation != 1U) break;
            log_saved = radar_log_window(calibrated, i + 1U, result.value, result.samples) && log_saved;
            if (result.samples < ROOM_RADAR_MIN_SAMPLES) {
                radar_panel(title, "Inconclusive", "Too few Wi-Fi samples", "Try closer to router", "Retry");
                failed = true;
                break;
            }
            if (result.value < low) low = result.value;
            if (result.value > high) high = result.value;
            total += result.value;
            if (calibrated) {
                room_radar_motion_result_t window = room_radar_classify(
                    quiet_low, quiet_high, baseline, result.value);
                if (window > verdict) verdict = window;
            }
        }
        if (operation == 1U && !log_saved)
            m1_message_box(&m1_u8g2, "Motion Log Not Saved", "Check mounted SD card",
                           "Result not recorded", "OK");
        if (failed) continue;

        if (operation == 2U) {
            snprintf(summary, sizeof(summary), "%u peers replied", result.value);
            char detail[28];
            snprintf(detail, sizeof(detail), "%u addresses checked", result.samples);
            radar_panel(title, summary, detail, "ICMP responders only", "Rescan");
        } else if (!calibrated) {
            baseline = total / windows;
            quiet_low = low;
            quiet_high = high;
            calibrated = room_radar_baseline_stable(low, high, baseline);
            snprintf(summary, sizeof(summary), "Quiet scores %u-%u", low, high);
            radar_panel(title, calibrated ? "Baseline recorded" : "Inconclusive",
                        summary, calibrated ? "Right: Change length" : "Try another spot",
                        calibrated ? "Measure" : "Retry");
        } else {
            snprintf(summary, sizeof(summary), "Avg %u / base %u", total / windows, baseline);
            const char *label = verdict == ROOM_RADAR_MOTION ? "Motion detected" :
                                verdict == ROOM_RADAR_INCONCLUSIVE ? "Inconclusive" : "No motion detected";
            radar_panel(title, label, summary, verdict == ROOM_RADAR_INCONCLUSIVE ?
                        "Try another spot" : "Right: Change length", "Measure");
        }
    }
}

bool room_radar_connect_network(void)
{
    return radar_radio_ready("Wi-Fi Setup") && radar_connect();
}

void room_radar_open_map(void)
{
    if (radar_radio_ready("Wi-Fi Signal Map")) radar_map();
}

void room_radar_open_motion(void)
{
    if (radar_radio_ready("Room Motion")) radar_measure(1);
}

void room_radar_open_lan(void)
{
    if (radar_radio_ready("LAN Devices")) radar_measure(2);
}

static void radar_diagnostics(void)
{
    radar_panel("Radio Status", "Checking Wi-Fi", NULL, NULL, NULL);
    wifi_room_radar_prepare();
    bool online = radar_at("AT\r\n");
    room_radar_result_t result;
    room_radar_reply_t status = ROOM_RADAR_NO_REPLY;
    bool radar = online && radar_status(&result, &status);
    const char *detail = radar ? "Room sensing ready" :
                         status == ROOM_RADAR_ERROR_REPLY ? "Standard radio firmware" :
                         online ? "Radar reply unavailable" : "Radio did not reply";
    radar_save_status(detail);
    radar_panel("Radio Status", online ? "Wi-Fi responded" : "Wi-Fi unavailable", detail, NULL, NULL);
    wait_back();
}

static unsigned radar_menu(const char *title, const char *const items[3], unsigned selected)
{
    for (;;) {
        m1_u8g2_firstpage();
        m1_draw_header_bar(&m1_u8g2, title, "2.4G");
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        for (unsigned i = 0; i < 3; ++i) {
            int y = 15 + (int)i * 12;
            m1_ui_selection(&m1_u8g2, 2, y, 124, 11, i == selected);
            m1_draw_text(&m1_u8g2, 8, y + 8, 112, items[i], TEXT_ALIGN_LEFT);
        }
        m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Back", "Open", button_ok_8x8);
        m1_u8g2_nextpage();
        int key = radar_key(portMAX_DELAY);
        if (key == BUTTON_BACK_KP_ID) return 3;
        if (key == BUTTON_UP_KP_ID) selected = (selected + 2) % 3;
        if (key == BUTTON_DOWN_KP_ID) selected = (selected + 1) % 3;
        if (key == BUTTON_OK_KP_ID) return selected;
    }
}

void room_radar_run(void)
{
    static const char *const home[] = {"Wi-Fi Map", "Room Motion", "Advanced"};
    static const char *const advanced[] = {"Radio Status", "LAN Devices", "Hotspot Clients"};
    unsigned selected = 0;
    unsigned advanced_selected = 0;
    for (;;) {
        selected = radar_menu("Room Radar", home, selected);
        if (selected == 3) return;
        if (selected == 2) {
            while ((advanced_selected = radar_menu("Advanced", advanced, advanced_selected)) < 3) {
                if (advanced_selected == 0) radar_diagnostics();
                else if (radar_radio_ready(advanced[advanced_selected])) {
                    if (advanced_selected == 1) radar_measure(2);
                    else radar_hotspot();
                }
            }
            advanced_selected = 0;
        } else if (radar_radio_ready(home[selected])) {
            if (selected == 0) radar_map();
            else radar_measure(1);
        }
    }
}
