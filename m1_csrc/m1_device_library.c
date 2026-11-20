/* See COPYING.txt for license details. */
#include "main.h"
#include "app_freertos.h"
#include "m1_display.h"
#include "m1_virtual_kb.h"
#include "m1_room_radar.h"
#include "m1_device_library.h"
#include "esp_app_main.h"
#include "ff.h"

#define DEVICE_PATH "0:/System/devices.dat"
static saved_device_t devices[DEVICE_LIMIT];
static unsigned device_count;
static bool recovered_backup;

static void message(const char *title, const char *text)
{
    m1_message_box(&m1_u8g2, title, text, "", "OK");
}
static int choose(const char *title, const char *const *items, unsigned count)
{
    unsigned selected = 0;
    for (;;) {
        m1_u8g2_firstpage();
        m1_draw_header_bar(&m1_u8g2, title, NULL);
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        unsigned first = selected < 3 ? 0 : selected - 2;
        for (unsigned i = first; i < count && i < first + 3; ++i) {
            int y = 14 + 12 * (int)(i - first);
            m1_ui_selection(&m1_u8g2, 2, y, 124, 11, i == selected);
            if (i == selected) m1_draw_marquee(&m1_u8g2, 7, y + 8, 114, items[i]);
            else m1_draw_text(&m1_u8g2, 7, y + 8, 114, items[i], TEXT_ALIGN_LEFT);
        }
        m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Back", "Select", button_ok_8x8);
        m1_u8g2_nextpage();
        S_M1_Main_Q_t event;
        S_M1_Buttons_Status buttons;
        if (xQueueReceive(main_q_hdl, &event, pdMS_TO_TICKS(120)) != pdTRUE ||
            event.q_evt_type != Q_EVENT_KEYPAD ||
            xQueueReceive(button_events_q_hdl, &buttons, 0) != pdTRUE) continue;
        if (buttons.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK) return -1;
        if (buttons.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) return (int)selected;
        if (buttons.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
            selected = selected ? selected - 1 : count - 1;
        if (buttons.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
            selected = (selected + 1) % count;
    }
}
static bool load_file(const char *path)
{
    FIL file;
    UINT read;
    uint8_t header[5];
    if (f_open(&file, path, FA_READ) != FR_OK) return false;
    bool ok = f_read(&file, header, sizeof(header), &read) == FR_OK &&
              read == sizeof(header) && !memcmp(header, "RSD1", 4) &&
              header[4] <= DEVICE_LIMIT;
    if (ok) {
        unsigned size = header[4] * sizeof(devices[0]);
        ok = f_size(&file) == sizeof(header) + size &&
             f_read(&file, devices, size, &read) == FR_OK && read == size;
        for (unsigned i = 0; ok && i < header[4]; ++i) ok = device_valid(&devices[i]);
        if (ok) device_count = header[4];
    }
    f_close(&file);
    return ok;
}
static bool load(void)
{
    device_count = 0;
    recovered_backup = false;
    if (load_file(DEVICE_PATH)) return true;
    if (load_file(DEVICE_PATH ".bak")) { recovered_backup = true; return true; }
    FILINFO info;
    return f_stat(DEVICE_PATH, &info) == FR_NO_FILE &&
           f_stat(DEVICE_PATH ".bak", &info) == FR_NO_FILE;
}
static bool save(void)
{
    FIL file;
    UINT written;
    const uint8_t header[] = {'R', 'S', 'D', '1', (uint8_t)device_count};
    unsigned size = device_count * sizeof(devices[0]);
    f_mkdir("0:/System");
    if (f_open(&file, DEVICE_PATH ".tmp", FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) return false;
    bool ok = f_write(&file, header, sizeof(header), &written) == FR_OK &&
              written == sizeof(header) &&
              f_write(&file, devices, size, &written) == FR_OK && written == size &&
              f_sync(&file) == FR_OK;
    if (f_close(&file) != FR_OK) ok = false;
    if (!ok) return false;
    /* Preserve recovery data if an earlier replacement was interrupted. */
    FILINFO info;
    bool prior = f_stat(DEVICE_PATH, &info) == FR_OK;
    if (prior && recovered_backup) {
        if (f_unlink(DEVICE_PATH) != FR_OK) return false;
        prior = false;
    }
    if (prior) {
        FRESULT removed = f_unlink(DEVICE_PATH ".bak");
        if (removed != FR_OK && removed != FR_NO_FILE) return false;
        if (f_rename(DEVICE_PATH, DEVICE_PATH ".bak") != FR_OK) return false;
    }
    if (f_rename(DEVICE_PATH ".tmp", DEVICE_PATH) != FR_OK) {
        if (prior) f_rename(DEVICE_PATH ".bak", DEVICE_PATH);
        return false;
    }
    recovered_backup = false;
    return true;
}
static int duplicate(const saved_device_t *device, int except)
{
    for (unsigned i = 0; i < device_count; ++i)
        if ((int)i != except && devices[i].kind == device->kind &&
            !strcmp(devices[i].address, device->address)) return (int)i;
    return -1;
}
static void normalize(saved_device_t *device)
{
    for (char *p = device->address; *p; ++p) *p = (char)tolower((unsigned char)*p);
}
bool device_library_save_ap(const char *name, const char *bssid)
{
    if (!load()) return false;
    saved_device_t device = {0};
    device.kind = 1;
    snprintf(device.name, sizeof(device.name), "%.24s", name && name[0] ? name : "Wi-Fi access point");
    snprintf(device.address, sizeof(device.address), "%s", bssid);
    normalize(&device);
    if (!device_valid(&device)) return false;
    if (duplicate(&device, -1) >= 0) return true;
    if (device_count == DEVICE_LIMIT) return false;
    devices[device_count++] = device;
    return save();
}
static bool edit(saved_device_t *device, bool new_device)
{
    static const char *const kinds[] = {"LAN Device (IP)", "Wi-Fi Access Point", "Bluetooth LE Device"};
    if (new_device) {
        int kind = choose("Device Type", kinds, 3);
        if (kind < 0) return false;
        device->kind = (uint8_t)kind;
    }
    char address[32];
    if (!m1_vkb_get_filename(device->kind == 0 ? "Private IPv4 address" : "Device MAC address",
                             device->address, address)) return false;
    if (strlen(address) >= sizeof(device->address)) {
        message("Invalid Address", "Address is too long");
        return false;
    }
    snprintf(device->address, sizeof(device->address), "%s", address);
    normalize(device);
    if (!device_address_valid(device->kind, device->address)) {
        message("Invalid Address", device->kind == 0 ? "Use a private IPv4" : "Use aa:bb:cc:dd:ee:ff");
        return false;
    }
    if (!device->name[0]) snprintf(device->name, sizeof(device->name), "%s", device->address);
    char name[32];
    if (!m1_vkb_get_filename("Device name", device->name, name)) return false;
    snprintf(device->name, sizeof(device->name), "%.24s", name);
    return device_valid(device);
}
static void reachability(const saved_device_t *device)
{
    if (device->kind != 0) {
        message("IP Address Needed", "Add a LAN Device first");
        return;
    }
    if (!room_radar_connect_network()) return;
    char command[48], response[256] = {0};
    snprintf(command, sizeof(command), "AT+PING=\"%s\"\r\n", device->address);
    m1_u8g2_firstpage();
    m1_draw_status_panel(&m1_u8g2, "Connection Check", "LAN", NULL, 0, 0,
                        device->name, "Checking response", "Please wait");
    m1_u8g2_nextpage();
    bool replied = spi_AT_send_recv(command, response, sizeof(response), 5) == SUCCESS &&
                   strstr(response, "+PING:") && room_radar_reply_status(response) == ROOM_RADAR_OK_REPLY;
    FIL report;
    UINT written;
    char line[96];
    int size = snprintf(line, sizeof(line), "%lu\t%s\t%s\t%s\r\n", (unsigned long)HAL_GetTick(),
                        device->name, device->address, replied ? "reply" : "no reply");
    bool saved = false;
    if (f_open(&report, "0:/System/device-checks.tsv", FA_WRITE | FA_OPEN_APPEND) == FR_OK) {
        saved = size > 0 && size < (int)sizeof(line) &&
                f_write(&report, line, (UINT)size, &written) == FR_OK && written == (UINT)size;
        if (f_close(&report) != FR_OK) saved = false;
    }
    m1_message_box(&m1_u8g2, replied ? "Device Responded" : "No Reply", device->address,
                   saved ? "Report saved to SD" : "Report could not save", "OK");
}
void device_library_run(void)
{
    if (!load()) {
        message("Device List Unavailable", "Check SD card or backup");
        return;
    }
    for (;;) {
        const char *labels[DEVICE_LIMIT + 1] = {"+ Add Device"};
        for (unsigned i = 0; i < device_count; ++i) labels[i + 1] = devices[i].name;
        int selected = choose("Saved Test Devices", labels, device_count + 1);
        if (selected < 0) return;
        if (selected == 0) {
            if (device_count == DEVICE_LIMIT) { message("Device List Full", "Delete a device first"); continue; }
            saved_device_t device = {0};
            if (!edit(&device, true)) continue;
            if (duplicate(&device, -1) >= 0) { message("Already Saved", "Address already exists"); continue; }
            devices[device_count++] = device;
            if (!save()) { message("Save Failed", "Check SD card"); if (!load()) return; }
            continue;
        }
        unsigned index = (unsigned)selected - 1;
        static const char *const actions[] = {"Check Connection", "View Address", "Rename Device", "Edit Address", "Delete Device"};
        int action = choose(devices[index].name, actions, 5);
        if (action < 0) continue;
        if (action == 0) reachability(&devices[index]);
        else if (action == 1) message("Saved Address", devices[index].address);
        else {
            saved_device_t device = devices[index];
            if (action == 2) {
                char name[32];
                if (!m1_vkb_get_filename("Device name", device.name, name)) continue;
                snprintf(device.name, sizeof(device.name), "%.24s", name);
            } else if (action == 3) {
                if (!edit(&device, false)) continue;
                if (duplicate(&device, (int)index) >= 0) { message("Already Saved", "Address already exists"); continue; }
            } else {
                if (m1_message_box_choice(&m1_u8g2, "Delete Device?", device.name, "", "Cancel\nDelete") != 2) continue;
                memmove(&devices[index], &devices[index + 1], (device_count - index - 1) * sizeof(device));
                --device_count;
            }
            if (action != 4) {
                if (!device_valid(&device)) { message("Invalid Name", "Enter a readable name"); continue; }
                devices[index] = device;
            }
            if (!save()) { message("Save Failed", "Check SD card"); if (!load()) return; }
        }
    }
}
