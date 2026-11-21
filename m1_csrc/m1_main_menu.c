/* See COPYING.txt for license details. */

/*
*
* m1_main_menu.c
*
* Top-level menu layout definition
*
* Defines the main menu with Storage, Power, and System as separate
* top-level categories.  The menu navigation engine lives in m1_menu.c.
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include "m1_branding.h"
#include "m1_compile_cfg.h"
#include "m1_display.h"
#include "m1_menu.h"
#include "m1_room_radar.h"
#include "m1_room_watch.h"
#include "m1_wifi_client_lab.h"
#include "m1_device_library.h"
#include "m1_settings.h"
#include "m1_system.h"

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

static void action_startup_splash(void);

extern S_M1_Menu_t menu_Sub_GHz;
extern S_M1_Menu_t menu_125KHz_RFID;
extern S_M1_Menu_t menu_NFC;
extern S_M1_Menu_t menu_Infrared;
extern S_M1_Menu_t menu_GPIO;
extern S_M1_Menu_t menu_Wifi;
extern S_M1_Menu_t menu_Bluetooth;
#ifdef M1_APP_ESPNOW_LINK_ENABLE
extern S_M1_Menu_t menu_ESPLink;
#endif
extern S_M1_Menu_t menu_Setting_Storage_About;
extern S_M1_Menu_t menu_Setting_Storage_Explore;
extern S_M1_Menu_t menu_Setting_Storage_Mount;
extern S_M1_Menu_t menu_Setting_Storage_Unmount;
extern S_M1_Menu_t menu_Setting_Storage_Format;
extern S_M1_Menu_t menu_Setting_Power_Info;
extern S_M1_Menu_t menu_Setting_Power_Reboot;
extern S_M1_Menu_t menu_Setting_Power_Off;
extern S_M1_Menu_t menu_Settings_LCD_and_Notifications;
extern S_M1_Menu_t menu_Settings_Backlight;
extern S_M1_Menu_t menu_Settings_System;
extern S_M1_Menu_t menu_Settings_About;
extern S_M1_Menu_t menu_Setting_Firmware_Update;
extern S_M1_Menu_t menu_Setting_Firmware_Swap_Banks;
extern S_M1_Menu_t menu_Setting_ESP32;
extern void menu_setting_storage_init(void);
extern void menu_setting_power_init(void);

#ifdef M1_APP_BADUSB_ENABLE
extern S_M1_Menu_t menu_BadUSB;
#endif
#ifdef M1_APP_GAMES_ENABLE
extern S_M1_Menu_t menu_Games;
#endif
#ifdef M1_APP_APPS_ENABLE
extern S_M1_Menu_t menu_Apps;
#endif

/***************************** V A R I A B L E S ******************************/

static S_M1_Menu_t menu_Startup_Splash =
{
    "Startup Splash", action_startup_splash, NULL, NULL, 0, 0, NULL, NULL, {NULL}
};

static S_M1_Menu_t menu_Appearance = {"Appearance", settings_appearance, NULL, NULL, 0, 0, NULL, NULL, {NULL}};

static S_M1_Menu_t menu_Date_Time = {"Date & Time", settings_set_time, NULL, NULL, 0, 0, NULL, NULL, {NULL}};

static S_M1_Menu_t menu_Storage =
{
    "Storage", menu_setting_storage_init, NULL, NULL, 5, 0, menu_m1_icon_apps, NULL,
    {&menu_Setting_Storage_About, &menu_Setting_Storage_Explore, &menu_Setting_Storage_Mount,
     &menu_Setting_Storage_Unmount, &menu_Setting_Storage_Format}
};

static S_M1_Menu_t menu_Power =
{
    "Power", menu_setting_power_init, NULL, NULL, 3, 0, menu_m1_icon_setting, NULL,
    {&menu_Setting_Power_Info, &menu_Setting_Power_Reboot, &menu_Setting_Power_Off}
};

static S_M1_Menu_t menu_Settings = {
    .title = "Settings", .num_submenu_items = 12, .icon_ptr = menu_m1_icon_setting,
    .submenu = {&menu_Setting_Firmware_Swap_Banks, &menu_Appearance, &menu_Date_Time,
                &menu_Settings_Backlight, &menu_Settings_LCD_and_Notifications,
                &menu_Settings_System, &menu_Setting_Firmware_Update, &menu_Setting_ESP32,
                &menu_Settings_About, &menu_Startup_Splash, &menu_Storage, &menu_Power}
};

/*
 * Main menu — base 10 items + optional BadUSB, Games, Apps.
 * Optional items are inserted between Bluetooth and Storage.
 */

#ifdef M1_APP_BADUSB_ENABLE
#define OPT_BADUSB  1
#define ENT_BADUSB  &menu_BadUSB,
#else
#define OPT_BADUSB  0
#define ENT_BADUSB
#endif

#ifdef M1_APP_GAMES_ENABLE
#define OPT_GAMES   1
#define ENT_GAMES   &menu_Games,
#else
#define OPT_GAMES   0
#define ENT_GAMES
#endif

#ifdef M1_APP_APPS_ENABLE
#define OPT_APPS    1
#define ENT_APPS    &menu_Apps,
#else
#define OPT_APPS    0
#define ENT_APPS
#endif

#ifdef M1_APP_ESPNOW_LINK_ENABLE
#define OPT_ESPLINK 1
#define ENT_ESPLINK &menu_ESPLink,
#else
#define OPT_ESPLINK 0
#define ENT_ESPLINK
#endif

#define MAIN_MENU_COUNT  (8 + OPT_BADUSB + OPT_GAMES + OPT_APPS + OPT_ESPLINK)

static S_M1_Menu_t menu_Test_Devices = {.title = "Saved Test Devices", .sub_func = device_library_run};

static S_M1_Menu_t menu_Tools =
{
    "Tools", NULL, NULL, NULL, MAIN_MENU_COUNT, 0, NULL, NULL,
    {&menu_Sub_GHz, &menu_125KHz_RFID, &menu_NFC, &menu_GPIO,
     &menu_Wifi, &menu_Bluetooth,
     ENT_ESPLINK
     ENT_BADUSB
     ENT_GAMES
     ENT_APPS
     &menu_Storage, &menu_Power}
};

static S_M1_Menu_t menu_WiFi_Clients = {.title = "Wi-Fi Clients", .sub_func = wifi_client_lab_open};
static S_M1_Menu_t menu_Room_Watch = {.title = "Room Watch", .sub_func = room_watch_open};
static S_M1_Menu_t menu_Signal_Map = {.title = "Wi-Fi Signal Map", .sub_func = room_radar_open_map};
static S_M1_Menu_t menu_Room_Motion = {.title = "Room Motion", .sub_func = room_radar_open_motion};
static S_M1_Menu_t menu_LAN_Devices = {.title = "LAN Devices", .sub_func = room_radar_open_lan};

static void roomscope_home_draw(const S_M1_Menu_t *menu, uint8_t selected)
{
    static const uint8_t icons[][8] = {
        {0x18,0x3c,0x7e,0xdb,0xdb,0x7e,0x3c,0x18},
        {0x00, 0x3c, 0x42, 0x18, 0x24, 0x00, 0x18, 0x00},
        {0x18, 0x24, 0x42, 0x99, 0x99, 0x42, 0x24, 0x18},
        {0x18, 0x18, 0x18, 0x7e, 0x42, 0x42, 0x66, 0x66},
        {0x3c, 0x42, 0x5a, 0x42, 0x5a, 0x42, 0x3c, 0x00},
        {0x18,0x3c,0x42,0x18,0x24,0x42,0x18,0x18},
        {0x3c, 0x42, 0x18, 0x24, 0x24, 0x18, 0x42, 0x3c},
        {0x18, 0x5a, 0x3c, 0x66, 0x66, 0x3c, 0x5a, 0x18},
        {0x00, 0x7e, 0x42, 0x5a, 0x42, 0x7e, 0x00, 0x00}
    };
    m1_u8g2_firstpage();
    m1_draw_header_bar(&m1_u8g2, "RoomScope", NULL);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    uint8_t first = selected < 3 ? 0 : selected - 2;
    for (uint8_t row = first; row < menu->num_submenu_items && row < first + 3; ++row) {
        int y = 14 + 12 * (row - first);
        m1_ui_selection(&m1_u8g2, 1, y, 126, 11, selected == row);
        u8g2_DrawXBMP(&m1_u8g2, 6, y + 1, 8, 8, icons[row]);
        if (selected == row)
            m1_draw_marquee(&m1_u8g2, 20, y + 8, 102, menu->submenu[row]->title);
        else m1_draw_text(&m1_u8g2, 20, y + 8, 102, menu->submenu[row]->title, TEXT_ALIGN_LEFT);
    }
    m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Splash", "Open", button_ok_8x8);
    m1_u8g2_nextpage();
}

S_M1_Menu_t menu_Main = {
    .title = "Room Radar",
    .num_submenu_items = 9,
    .gui_menu_update = roomscope_home_draw,
    .submenu = {&menu_Room_Watch, &menu_Signal_Map, &menu_Room_Motion, &menu_LAN_Devices, &menu_Infrared,
                &menu_WiFi_Clients, &menu_Test_Devices, &menu_Settings, &menu_Tools}
};

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/

static void action_startup_splash(void)
{
    startup_info_screen_display(M1_PRODUCT_HOME_SUBTITLE);
}
