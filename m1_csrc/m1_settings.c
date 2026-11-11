/* See COPYING.txt for license details. */

/*
*
*  m1_settings.c
*
*  M1 RFID functions
*
* M1 Project
*
*/

/*************************** I N C L U D E S **********************************/

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "stm32h5xx_hal.h"
#include "main.h"
#include "m1_settings.h"
#include "m1_branding.h"
#include "m1_buzzer.h"
#include "m1_lcd.h"
#include "m1_lp5814.h"
#include "m1_display.h"
#include "ff.h"
#include "m1_log_debug.h"
#include "m1_fw_update_bl.h"
#include "m1_t1000_version.h"
#include "m1_system.h"
#include "m1_file_util.h"
#include "m1_rgb_backlight.h"
#include "m1_builtin_apps.h"
#include "m1_kb_layout.h"
#include "m1_ui_theme.h"
m1_ui_preferences_t m1_ui = M1_UI_DEFAULTS;
static void appearance_load(void);
static void appearance_save(void);

/*************************** D E F I N E S ************************************/

#define SETTINGS_TAG              "SETT"
#define SETTINGS_FILE_PATH        "0:/System/settings.cfg"
#define SETTINGS_FILE_MAX_SIZE    512

#define SETTING_ABOUT_CHOICES_MAX		2 //5

#define ABOUT_BOX_Y_POS_ROW_1			10
#define ABOUT_BOX_Y_POS_ROW_2			20
#define ABOUT_BOX_Y_POS_ROW_3			30
#define ABOUT_BOX_Y_POS_ROW_4			40
#define ABOUT_BOX_Y_POS_ROW_5			50

/* LCD & Notifications menu items (backlight moved to System > Backlight) */
#define LCD_SETTINGS_ITEMS   5
#define LCD_SET_BUZZER       0
#define LCD_SET_LED          1
#define LCD_SET_ORIENT       2
#define LCD_SET_SLEEP        3
#define LCD_SET_TIMEZONE     4

//************************** S T R U C T U R E S *******************************

/***************************** V A R I A B L E S ******************************/

static const uint8_t s_brightness_values[] = { 0, 64, 128, 192, 255 };
static bool s_backlight_ready;
void settings_restore_backlight(void)
{
    if (s_backlight_ready)
        m1_backlight_on(s_brightness_values[m1_brightness_level < 5 ? m1_brightness_level : 4]);
}

static const char *s_brightness_text[] = { "Off", "Low", "Med", "High", "Max" };
static const char *s_orient_text[] = { "Normal", "Southpaw" };
static const char *s_sleep_text[] = { "30s", "1 min", "5 min", "10 min", "15 min", "Never" };
static const char *s_bl_type_text[] = { "Stock", "RGB" };

/* Backlight type: 0=Stock (LP5814), 1=RGB (SK6805 mod) — saved to settings.cfg */
uint8_t m1_backlight_type = 0;

/********************* F U N C T I O N   P R O T O T Y P E S ******************/

void settings_system(void);
void settings_about(void);
static void settings_about_display_choice(uint8_t choice);
static void settings_apply_orientation(uint8_t orient);
static const char *settings_lcd_item_label(uint8_t item);
static const char *settings_lcd_item_value(uint8_t item);
static void settings_lcd_draw(uint8_t sel);
bool settings_save_to_sd(void);

/*************** F U N C T I O N   I M P L E M E N T A T I O N ****************/


/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
/**
  * @brief  Apply screen orientation and sync m1_southpaw_mode
  */
/*============================================================================*/
static void settings_apply_orientation(uint8_t orient)
{
    /* Portrait belongs to the IR remote view; system screens are 128x64. */
    if (orient != M1_ORIENT_SOUTHPAW) orient = M1_ORIENT_NORMAL;
    m1_screen_orientation = orient;
    m1_southpaw_mode = (orient == M1_ORIENT_SOUTHPAW) ? 1 : 0;

    if (orient == M1_ORIENT_SOUTHPAW)
        u8g2_SetDisplayRotation(&m1_u8g2, U8G2_R0);
    else if (orient == M1_ORIENT_REMOTE)
        u8g2_SetDisplayRotation(&m1_u8g2, U8G2_R1);
    else
        u8g2_SetDisplayRotation(&m1_u8g2, U8G2_R2);
}

static const char *settings_lcd_item_label(uint8_t item)
{
    switch (item)
    {
        case LCD_SET_BUZZER:     return "Buzzer";
        case LCD_SET_LED:        return "LED Notify";
        case LCD_SET_ORIENT:     return "Orientation";
        case LCD_SET_SLEEP:      return "Sleep After";
        case LCD_SET_TIMEZONE:    return "UTC Offset";
        default:                 return "";
    }
}

static const char *settings_lcd_item_value(uint8_t item)
{
    switch (item)
    {
        case LCD_SET_BUZZER:     return m1_buzzer_on ? "On" : "Off";
        case LCD_SET_LED:        return m1_led_notify_on ? "On" : "Off";
        case LCD_SET_ORIENT:     return s_orient_text[m1_screen_orientation];
        case LCD_SET_SLEEP:      return s_sleep_text[m1_sleep_timeout_idx];
        case LCD_SET_TIMEZONE:
        {
            static char tz_buf[8];
            if (m1_tz_offset_hours >= 0)
                snprintf(tz_buf, sizeof(tz_buf), "UTC+%d", m1_tz_offset_hours);
            else
                snprintf(tz_buf, sizeof(tz_buf), "UTC%d", m1_tz_offset_hours);
            return tz_buf;
        }
        default:                 return "";
    }
}

static void settings_lcd_draw(uint8_t sel)
{
    char badge[12];
    uint8_t visible_start = 0;

    if (sel >= 2U && LCD_SETTINGS_ITEMS > 2U)
        visible_start = (uint8_t)(sel - 1U);

    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1), (unsigned)LCD_SETTINGS_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Settings", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    for (uint8_t vi = 0; vi < 2U && (visible_start + vi) < LCD_SETTINGS_ITEMS; vi++)
    {
        uint8_t item = visible_start + vi;
        uint8_t y = 30 + vi * 12;
        const char *label = settings_lcd_item_label(item);
        const char *value = settings_lcd_item_value(item);

        if (item == sel)
        {
            m1_ui_selection(&m1_u8g2, 6, y - 7, 114, 11, true);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        }
        else
        {
            m1_ui_selection(&m1_u8g2, 6, y - 7, 114, 11, false);
            m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
        }
    }

    m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Back", "Change", button_ok_8x8);
    m1_u8g2_nextpage();
}


/*============================================================================*/
/**
  * @brief  LCD & Notifications settings — scrollable 5-item menu
  *         Brightness, Buzzer, LED Notify, Orientation, Sleep After
  */
/*============================================================================*/
void settings_lcd_and_notifications(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    uint8_t sel = 0;
    uint8_t needs_redraw = 1;

    while (1)
    {
        if (needs_redraw)
        {
            needs_redraw = 0;
            settings_lcd_draw(sel);
        }

        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (ret != pdTRUE) continue;

        /* Back — save and exit */
        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            settings_save_to_sd();
            xQueueReset(main_q_hdl);
            break;
        }

        /* Up/Down — navigate */
        if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel == 0) ? (LCD_SETTINGS_ITEMS - 1) : (sel - 1);
            needs_redraw = 1;
        }
        if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel + 1) % LCD_SETTINGS_ITEMS;
            needs_redraw = 1;
        }

        /* Left — decrement selected setting */
        if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == LCD_SET_BUZZER)
                m1_buzzer_on = !m1_buzzer_on;
            else if (sel == LCD_SET_LED)
                m1_led_notify_on = !m1_led_notify_on;
            else if (sel == LCD_SET_ORIENT)
                settings_apply_orientation(m1_screen_orientation == M1_ORIENT_NORMAL ? M1_ORIENT_SOUTHPAW : M1_ORIENT_NORMAL);
            else if (sel == LCD_SET_SLEEP)
                m1_sleep_timeout_idx = (m1_sleep_timeout_idx == 0) ? 5 : (m1_sleep_timeout_idx - 1);
            else if (sel == LCD_SET_TIMEZONE)
                m1_tz_offset_hours = (m1_tz_offset_hours <= -12) ? 14 : (m1_tz_offset_hours - 1);
            needs_redraw = 1;
        }

        /* Right — increment selected setting */
        if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == LCD_SET_BUZZER)
            {
                m1_buzzer_on = !m1_buzzer_on;
                if (m1_buzzer_on) m1_buzzer_notification();
            }
            else if (sel == LCD_SET_LED)
                m1_led_notify_on = !m1_led_notify_on;
            else if (sel == LCD_SET_ORIENT)
                settings_apply_orientation(m1_screen_orientation == M1_ORIENT_NORMAL ? M1_ORIENT_SOUTHPAW : M1_ORIENT_NORMAL);
            else if (sel == LCD_SET_SLEEP)
                m1_sleep_timeout_idx = (m1_sleep_timeout_idx >= 5) ? 0 : (m1_sleep_timeout_idx + 1);
            else if (sel == LCD_SET_TIMEZONE)
                m1_tz_offset_hours = (m1_tz_offset_hours >= 14) ? -12 : (m1_tz_offset_hours + 1);
            needs_redraw = 1;
        }

        /* Center OK changes the selected setting, matching the footer. */
        if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK) {
            if (sel == LCD_SET_BUZZER) m1_buzzer_on = !m1_buzzer_on;
            else if (sel == LCD_SET_LED) m1_led_notify_on = !m1_led_notify_on;
            else if (sel == LCD_SET_ORIENT) settings_apply_orientation(!m1_screen_orientation);
            else if (sel == LCD_SET_SLEEP) m1_sleep_timeout_idx = (m1_sleep_timeout_idx + 1) % 6;
            else if (sel == LCD_SET_TIMEZONE) m1_tz_offset_hours = m1_tz_offset_hours >= 14 ? -12 : m1_tz_offset_hours + 1;
            needs_redraw = 1;
        }
    } /* while (1) */
} /* settings_lcd_and_notifications */


/*============================================================================*/
/**
/*============================================================================*/
void settings_buzzer(void)
{
	//buzzer_demo_play();
} // void settings_sound(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void settings_power(void)
{
	;
} // void settings_power(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/

/*============================================================================*/
/**
  * @brief  Manual date/time set screen.
  *         Fields: Year Month Day Hour Min Sec
  *         Left/Right = move field, Up/Down = increment/decrement, OK = save, BACK = cancel
  */
/*============================================================================*/
static void settings_set_time_draw(const m1_time_t *dt, uint8_t field)
{
    char line[32];

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Local time", "Save");

    u8g2_SetFont(&m1_u8g2,M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2,8,19,114,"L/R field  U/D edit",TEXT_ALIGN_LEFT);

    /* Date line: YYYY-MM-DD */
    snprintf(line, sizeof(line), "%04u-%02u-%02u",
             (unsigned)dt->year, (unsigned)dt->month, (unsigned)dt->day);
    u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_text(&m1_u8g2, 8, 27, 114, line, TEXT_ALIGN_LEFT);

    /* Time line: HH:MM:SS */
    if(m1_ui.clock_12h)
        snprintf(line,sizeof(line),"%02u:%02u:%02u %s",(dt->hour+11)%12+1,dt->minute,dt->second,dt->hour<12?"AM":"PM");
    else snprintf(line,sizeof(line),"%02u:%02u:%02u",dt->hour,dt->minute,dt->second);
    u8g2_SetFont(&m1_u8g2, M1_DISP_LARGE_FONT_1B);
    m1_draw_text(&m1_u8g2, 8, 46, 114, line, TEXT_ALIGN_LEFT);

    /* Measure the prefix in the actual font so every field stays aligned. */
    char prefix[32];unsigned position;
    if(field<3) {
        static const uint8_t positions[]={0,5,8};position=positions[field];
        snprintf(prefix,sizeof(prefix),"%04u-%02u-%02u",dt->year,dt->month,dt->day);
        u8g2_SetFont(&m1_u8g2,M1_DISP_FUNC_MENU_FONT_N);
    } else {
        static const uint8_t positions[]={0,3,6};position=positions[field-3];
        snprintf(prefix,sizeof(prefix),"%s",line);
        u8g2_SetFont(&m1_u8g2,M1_DISP_LARGE_FONT_1B);
    }
    prefix[position]=0;
    u8g2_SetDrawColor(&m1_u8g2,M1_DISP_DRAW_COLOR_TXT);
    u8g2_DrawHLine(&m1_u8g2,8+u8g2_GetStrWidth(&m1_u8g2,prefix),field<3?29:48,u8g2_GetStrWidth(&m1_u8g2,field==0?"0000":"00"));

    m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Back", "Save", button_ok_8x8);
    m1_u8g2_nextpage();
}

static uint8_t settings_dim(uint16_t year, uint8_t month)
{
    static const uint8_t mdays[] = {31,28,31,30,31,30,31,31,30,31,30,31};
    if (month < 1U || month > 12U) return 30U;
    uint8_t d = mdays[month - 1U];
    if (month == 2U && year % 4U == 0U && (year % 100U != 0U || year % 400U == 0U)) d = 29U;
    return d;
}

void settings_set_time(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;
    m1_time_t dt;
    bool dirty=false;
    uint8_t field = 0;   /* 0=Year 1=Month 2=Day 3=Hour 4=Min 5=Sec */

    m1_get_localtime(&dt);
    /* Clamp to sane defaults if RTC is uninitialised */
    if (dt.year < 2020U || dt.year > 2099U) dt.year  = 2025U;
    if (dt.month < 1U  || dt.month > 12U)  dt.month  = 1U;
    if (dt.day   < 1U  || dt.day   > 31U)  dt.day    = 1U;
    if (dt.hour  > 23U)                     dt.hour   = 0U;
    if (dt.minute > 59U)                    dt.minute = 0U;
    if (dt.second > 59U)                    dt.second = 0U;
    /* The save API derives weekday from the selected local date. */

    settings_set_time_draw(&dt, field);

    while (1)
    {
        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (ret != pdTRUE) continue;

        bool commit=this_button_status.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK;
        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if(!dirty){xQueueReset(main_q_hdl);return;}
            uint8_t choice=m1_message_box_choice(&m1_u8g2,"Save time changes?","Save the edited time?","Back keeps editing","Save\nDiscard");
            if(choice==1)commit=true;
            else if(choice==2){xQueueReset(main_q_hdl);return;}
            else {settings_set_time_draw(&dt,field);continue;}
        }

        /* Left/Right — move field */
        if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK)
            field = (field == 0U) ? 5U : (uint8_t)(field - 1U);
        if (this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
            field = (field + 1U) % 6U;

        /* Up — increment */
        if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            switch (field)
            {
                case 0: dt.year   = (dt.year   >= 2099U) ? 2020U : (uint16_t)(dt.year + 1U);  break;
                case 1: dt.month  = (dt.month  >= 12U)   ? 1U    : (uint8_t)(dt.month + 1U);  break;
                case 2: dt.day    = (dt.day    >= settings_dim(dt.year, dt.month)) ? 1U : (uint8_t)(dt.day + 1U); break;
                case 3: dt.hour   = (dt.hour   >= 23U)   ? 0U    : (uint8_t)(dt.hour + 1U);   break;
                case 4: dt.minute = (dt.minute >= 59U)   ? 0U    : (uint8_t)(dt.minute + 1U); break;
                case 5: dt.second = (dt.second >= 59U)   ? 0U    : (uint8_t)(dt.second + 1U); break;
                default: break;
            }
        }

        /* Down — decrement */
        if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            switch (field)
            {
                case 0: dt.year   = (dt.year   <= 2020U) ? 2099U : (uint16_t)(dt.year - 1U);  break;
                case 1: dt.month  = (dt.month  <= 1U)    ? 12U   : (uint8_t)(dt.month - 1U);  break;
                case 2: dt.day    = (dt.day    <= 1U)    ? settings_dim(dt.year, dt.month) : (uint8_t)(dt.day - 1U); break;
                case 3: dt.hour   = (dt.hour   == 0U)    ? 23U   : (uint8_t)(dt.hour - 1U);   break;
                case 4: dt.minute = (dt.minute == 0U)    ? 59U   : (uint8_t)(dt.minute - 1U); break;
                case 5: dt.second = (dt.second == 0U)    ? 59U   : (uint8_t)(dt.second - 1U); break;
                default: break;
            }
        }

        if(this_button_status.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK || this_button_status.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK)dirty=true;

        /* Clamp day after month/year change */
        {
            uint8_t max_day = settings_dim(dt.year, dt.month);
            if (dt.day > max_day) dt.day = max_day;
        }

        /* OK — commit and save */
        if (commit)
        {
            if(!m1_set_localtime(&dt)) {m1_message_box(&m1_u8g2,"Clock not saved","RTC write failed or","date is out of range","OK");settings_set_time_draw(&dt,field);continue;}
            m1_buzzer_notification();
            char summary[32];
            if(m1_ui.clock_12h)snprintf(summary,sizeof(summary),"%02u:%02u %s",(dt.hour+11)%12+1,dt.minute,dt.hour<12?"AM":"PM");
            else snprintf(summary,sizeof(summary),"%02u:%02u",dt.hour,dt.minute);
            m1_message_box(&m1_u8g2,"Time saved",summary,"OK to return","OK");
            xQueueReset(main_q_hdl);
            return;
        }

        settings_set_time_draw(&dt, field);
    }
}


/*============================================================================*/
/**
  * @brief  Device options: ESP32 auto-init
  */
/*============================================================================*/

#define SYS_MENU_ITEMS   1
#define SYS_ITEM_ESP32   0


static void settings_system_draw(uint8_t sel)
{
    const char *label0 = "ESP32 at boot";
    const char *val0   = m1_esp32_auto_init ? "ON" : "OFF";

    char badge[8];
    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1U), (unsigned)SYS_MENU_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Settings", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    struct { const char *lbl; const char *val; } items[SYS_MENU_ITEMS] = {
        { label0, val0 }
    };

    uint8_t visible_start = (sel >= 2U) ? (uint8_t)(sel - 1U) : 0U;
    for (uint8_t vi = 0; vi < 2U && (visible_start + vi) < SYS_MENU_ITEMS; vi++)
    {
        uint8_t item = visible_start + vi;
        uint8_t y    = 30U + vi * 12U;
        if (item == sel)
        {
            m1_ui_selection(&m1_u8g2, 6, y - 7, 114, 11, true);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
            m1_draw_text(&m1_u8g2, 10, y, 70, items[item].lbl, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 82, y, 34, items[item].val, TEXT_ALIGN_RIGHT);
            u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
            u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
        }
        else
        {
            m1_ui_selection(&m1_u8g2, 6, y - 7, 114, 11, false);
            m1_draw_text(&m1_u8g2, 10, y, 70, items[item].lbl, TEXT_ALIGN_LEFT);
            m1_draw_text(&m1_u8g2, 82, y, 34, items[item].val, TEXT_ALIGN_RIGHT);
        }
    }

    m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Back", "Select", button_ok_8x8);
    m1_u8g2_nextpage();
}

void settings_system(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    uint8_t sel = 0;
    uint8_t needs_redraw = 1;

    while (1)
    {
        if (needs_redraw)
        {
            needs_redraw = 0;
            settings_system_draw(sel);
        }

        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (ret != pdTRUE) continue;

        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            settings_save_to_sd();
            xQueueReset(main_q_hdl);
            break;
        }

        if (this_button_status.event[BUTTON_UP_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (sel == 0U) ? (SYS_MENU_ITEMS - 1U) : (uint8_t)(sel - 1U);
            needs_redraw = 1;
        }
        if (this_button_status.event[BUTTON_DOWN_KP_ID] == BUTTON_EVENT_CLICK)
        {
            sel = (uint8_t)((sel + 1U) % SYS_MENU_ITEMS);
            needs_redraw = 1;
        }

        if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK ||
            this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK ||
            this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == SYS_ITEM_ESP32)
            {
                m1_esp32_auto_init = m1_esp32_auto_init ? 0 : 1;
                settings_save_to_sd();
                needs_redraw = 1;
            }

        }
    }
}





/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
void settings_about(void)
{
	S_M1_Buttons_Status this_button_status;
	S_M1_Main_Q_t q_item;
	BaseType_t ret;
	uint8_t choice;

	choice = 0;
	settings_about_display_choice(choice);

	while (1 ) // Main loop of this task
	{
		;
		; // Do other parts of this task here
		;

		ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
		if (ret==pdTRUE)
		{
			if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			{
				// Notification is only sent to this task when there's any button activity,
				// so it doesn't need to wait when reading the event from the queue
				ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
				if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK ) // user wants to exit?
				{
					; // Do extra tasks here if needed
					xQueueReset(main_q_hdl); // Reset main q before return
					break; // Exit and return to the calling task (subfunc_handler_task)
				} // if ( this_button_status.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ) // Previous?
				{
					choice--;
					if ( choice > SETTING_ABOUT_CHOICES_MAX )
						choice = SETTING_ABOUT_CHOICES_MAX;
					settings_about_display_choice(choice);
				} // else if ( this_button_status.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK )
				else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK ) // Next?
				{
					choice++;
					if ( choice > SETTING_ABOUT_CHOICES_MAX )
						choice = 0;
					settings_about_display_choice(choice);
				} // else if ( this_button_status.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK )
			} // if ( q_item.q_evt_type==Q_EVENT_KEYPAD )
			else
			{
				; // Do other things for this task
			}
		} // if (ret==pdTRUE)
	} // while (1 ) // Main loop of this task

} // void settings_about(void)



/*============================================================================*/
/**
  * @brief
  * @param
  * @retval
  */
/*============================================================================*/
static void settings_about_display_choice(uint8_t choice)
{
	char badge[8];
	char prn_name[24];

	snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(choice + 1), (unsigned)(SETTING_ABOUT_CHOICES_MAX + 1));

	m1_u8g2_firstpage();
	m1_draw_header_bar(&m1_u8g2, "Settings", badge);
	m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);

	switch (choice)
	{
		case 0: // FW info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
			m1_draw_text(&m1_u8g2, 8, 24, 114, M1_PRODUCT_NAME, TEXT_ALIGN_LEFT);
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 34, 114, T1000_VERSION_STRING, TEXT_ALIGN_LEFT);
			sprintf(prn_name, "Active bank: %d", (m1_device_stat.active_bank==BANK1_ACTIVE)?1:2);
			m1_draw_text(&m1_u8g2, 8, 44, 114, prn_name, TEXT_ALIGN_LEFT);
			break;

		case 1: // Company info
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
			m1_draw_text(&m1_u8g2, 8, 24, 114, "MonstaTek Inc.", TEXT_ALIGN_LEFT);
			u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
			m1_draw_text(&m1_u8g2, 8, 34, 114, "San Jose, CA, USA", TEXT_ALIGN_LEFT);
			break;

		default:
			u8g2_DrawXBMP(&m1_u8g2, 37, 20, 55, 24, m1_device_55x24);
			break;
	} // switch (choice)

	m1_draw_bottom_bar(&m1_u8g2, arrowleft_8x8, "Prev", "Next", arrowright_8x8);
	m1_u8g2_nextpage(); // Update display RAM
} // static void settings_about_display_choice(uint8_t choice)


/*============================================================================*/
/**
  * @brief  Save user settings to SD card (0:/System/settings.cfg)
  */
/*============================================================================*/
bool settings_save_to_sd(void)
{
    FIL fp;
    FRESULT fres;
    UINT bw;
    char buf[64];

    bool ok = true;
    /* Ensure System directory exists */
    f_mkdir("0:/System");

    fres = f_open(&fp, SETTINGS_FILE_PATH ".tmp", FA_WRITE | FA_CREATE_ALWAYS);
    if (fres != FR_OK)
    {
        M1_LOG_W(SETTINGS_TAG, "Save failed (err=%d)\r\n", fres);
        return false;
    }

    snprintf(buf, sizeof(buf), "brightness=%d\n", m1_brightness_level);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "buzzer=%d\n", m1_buzzer_on);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "led_notify=%d\n", m1_led_notify_on);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "orientation=%d\n", m1_screen_orientation);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "sleep_timeout=%d\n", m1_sleep_timeout_idx);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "esp32_auto_init=%d\n", m1_esp32_auto_init);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "tz_offset=%d\n", m1_tz_offset_hours);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "ism_region=%d\n", m1_device_stat.config.ism_band_region);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "backlight_type=%d\n", m1_backlight_type);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

    snprintf(buf, sizeof(buf), "rgb_mode=%d\n", (int)rgb_bl_get_mode());
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);
    snprintf(buf, sizeof(buf), "rgb_effect=%d\n", (int)rgb_bl_get_effect());
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);
    snprintf(buf, sizeof(buf), "rgb_brightness=%d\n", (int)rgb_bl_get_brightness());
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);
    {
        uint8_t cr, cg, cb;
        rgb_bl_get_custom(&cr, &cg, &cb);
        snprintf(buf, sizeof(buf), "rgb_custom=%d,%d,%d\n", (int)cr, (int)cg, (int)cb);
        if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);
    }
    snprintf(buf, sizeof(buf), "rgb_reactive=%d\n", (int)rgb_bl_reactive_is_on());
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);

#ifdef M1_APP_BADBT_ENABLE
    snprintf(buf, sizeof(buf), "badbt_name=%s\n", m1_badbt_name);
    if (ok) ok = f_write(&fp, buf, strlen(buf), &bw) == FR_OK && bw == strlen(buf);
#endif

    /* Empty when the built-in US layout is active */
    {
        char kb_buf[KB_LAYOUT_PATH_MAX + 16];
        snprintf(kb_buf, sizeof(kb_buf), "kb_layout=%s\n", kb_layout_path());
        if (ok) ok = f_write(&fp, kb_buf, strlen(kb_buf), &bw) == FR_OK && bw == strlen(kb_buf);
    }

    if (ok) ok = f_sync(&fp) == FR_OK;
    FRESULT closed = f_close(&fp);
    if (!ok || closed != FR_OK) return false;
    FILINFO info;
    fres = f_stat(SETTINGS_FILE_PATH, &info);
    if (fres == FR_OK) {
        fres = f_unlink(SETTINGS_FILE_PATH ".bak");
        if (fres != FR_OK && fres != FR_NO_FILE) return false;
    } else if (fres != FR_NO_FILE) return false;
    fres = f_rename(SETTINGS_FILE_PATH, SETTINGS_FILE_PATH ".bak");
    if (fres != FR_OK && fres != FR_NO_FILE) return false;
    if (f_rename(SETTINGS_FILE_PATH ".tmp", SETTINGS_FILE_PATH) != FR_OK) {
        if (fres == FR_OK) f_rename(SETTINGS_FILE_PATH ".bak", SETTINGS_FILE_PATH);
        return false;
    }
    return true;
}


/*============================================================================*/
/**
  * @brief  Load user settings from SD card (0:/System/settings.cfg)
  *         Sets m1_southpaw_mode and applies display rotation.
  */
/*============================================================================*/
void settings_load_from_sd(void)
{
    appearance_load();
    FIL fp;
    FRESULT fres;
    UINT br;
    char buf[SETTINGS_FILE_MAX_SIZE];
    char *p;
    int val;

    fres = f_open(&fp, SETTINGS_FILE_PATH, FA_READ);
    if (fres == FR_NO_FILE) fres = f_open(&fp, SETTINGS_FILE_PATH ".bak", FA_READ);
    if (fres != FR_OK)
        goto apply;  /* No settings file yet — apply defaults */

    fres = f_read(&fp, buf, sizeof(buf) - 1, &br);
    f_close(&fp);

    if (fres != FR_OK || br == 0)
        goto apply;

    buf[br] = '\0';

    /* Parse "brightness=X" */
    p = strstr(buf, "brightness=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val >= 0 && val <= 4)
            m1_brightness_level = (uint8_t)val;
    }

    /* Parse "buzzer=X" */
    p = strstr(buf, "buzzer=");
    if (p != NULL)
    {
        val = (int)(*(p + 7) - '0');
        if (val == 0 || val == 1)
            m1_buzzer_on = (uint8_t)val;
    }

    /* Parse "led_notify=X" */
    p = strstr(buf, "led_notify=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val == 0 || val == 1)
            m1_led_notify_on = (uint8_t)val;
    }

    /* Parse "orientation=X" */
    p = strstr(buf, "orientation=");
    if (p != NULL)
    {
        val = (int)(*(p + 12) - '0');
        if (val >= 0 && val <= 2)
            m1_screen_orientation = (uint8_t)val;
    }

    /* Parse "sleep_timeout=X" */
    p = strstr(buf, "sleep_timeout=");
    if (p != NULL)
    {
        val = (int)(*(p + 14) - '0');
        if (val >= 0 && val <= 5)
            m1_sleep_timeout_idx = (uint8_t)val;
    }

    /* Parse "esp32_auto_init=X" */
    p = strstr(buf, "esp32_auto_init=");
    if (p != NULL)
    {
        val = (int)(*(p + 16) - '0');
        if (val == 0 || val == 1)
            m1_esp32_auto_init = (uint8_t)val;
    }

    /* Parse "tz_offset=X" */
    p = strstr(buf, "tz_offset=");
    if (p != NULL)
    {
        p += 10;
        int tz = atoi(p);
        if (tz >= -12 && tz <= 14)
            m1_tz_offset_hours = (int8_t)tz;
    }

    /* Parse "ism_region=X" */
    p = strstr(buf, "ism_region=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val >= 0 && val <= 3)
            m1_device_stat.config.ism_band_region = (uint8_t)val;
    }

    /* Parse "backlight_type=X" */
    p = strstr(buf, "backlight_type=");
    if (p != NULL)
    {
        val = (int)(*(p + 15) - '0');
        if (val == 0 || val == 1)
            m1_backlight_type = (uint8_t)val;
    }

    /* Parse "rgb_mode=X" */
    p = strstr(buf, "rgb_mode=");
    if (p != NULL)
    {
        val = atoi(p + 9);
        if (val >= 0 && val < RGB_MODE_COUNT)
            rgb_bl_set_mode((rgb_bl_mode_t)val);
    }

    /* Parse "rgb_effect=X" */
    p = strstr(buf, "rgb_effect=");
    if (p != NULL)
    {
        val = (int)(*(p + 11) - '0');
        if (val >= 0 && val < RGB_EFFECT_COUNT)
            rgb_bl_set_effect((rgb_bl_effect_t)val);
    }

    /* Parse "rgb_brightness=X" */
    p = strstr(buf, "rgb_brightness=");
    if (p != NULL)
    {
        val = atoi(p + 15);
        if (val >= 0 && val <= 255)
            rgb_bl_set_brightness((uint8_t)val);
    }

    /* Parse "rgb_custom=R,G,B" */
    p = strstr(buf, "rgb_custom=");
    if (p != NULL)
    {
        int cr = 0, cg = 0, cb = 0;
        if (sscanf(p + 11, "%d,%d,%d", &cr, &cg, &cb) == 3)
        {
            rgb_bl_mode_t saved_mode = rgb_bl_get_mode();
            rgb_bl_set_custom((uint8_t)(cr & 0xFF), (uint8_t)(cg & 0xFF), (uint8_t)(cb & 0xFF));
            /* set_custom forces CUSTOM mode — restore the saved color mode so
             * the persisted rgb_mode wins unless the user actually chose Custom. */
            rgb_bl_set_mode(saved_mode);
        }
    }

    /* Parse "rgb_reactive=X" — start the reactive timer if enabled for the RGB
     * mod. The timer no-ops until the RGB hardware is brought up below. */
    p = strstr(buf, "rgb_reactive=");
    if (p != NULL && *(p + 13) == '1' && m1_backlight_type == 1)
    {
        rgb_bl_reactive_set(1);
    }

    /* Legacy: migrate "southpaw=1" if no orientation key found */
    if (strstr(buf, "orientation=") == NULL)
    {
        p = strstr(buf, "southpaw=");
        if (p != NULL && *(p + 9) == '1')
            m1_screen_orientation = M1_ORIENT_SOUTHPAW;
    }

#ifdef M1_APP_BADBT_ENABLE
    /* Parse "badbt_name=XYZ" */
    p = strstr(buf, "badbt_name=");
    if (p != NULL)
    {
        p += 11;  /* skip "badbt_name=" */
        char *end = strchr(p, '\n');
        if (!end) end = p + strlen(p);
        uint8_t len = end - p;
        if (len > BADBT_NAME_MAX_LEN) len = BADBT_NAME_MAX_LEN;
        if (len > 0)
        {
            memcpy(m1_badbt_name, p, len);
            m1_badbt_name[len] = '\0';
        }
    }
#endif

    /* Parse "kb_layout=<path>" — BadUSB/Bad-BT keyboard layout. An empty or
     * missing value keeps the built-in US layout. */
    p = strstr(buf, "kb_layout=");
    if (p != NULL)
    {
        char kb_path[KB_LAYOUT_PATH_MAX];
        char *end;
        size_t len;

        p += 10;  /* skip "kb_layout=" */
        end = strchr(p, '\n');
        if (!end) end = p + strlen(p);

        len = (size_t)(end - p);
        if (len > 0 && len < sizeof(kb_path))
        {
            memcpy(kb_path, p, len);
            kb_path[len] = '\0';
            if (!kb_layout_load(kb_path))
                M1_LOG_W(SETTINGS_TAG, "Layout load failed: %s\r\n", kb_path);
        }
    }

apply:
    /* Settings are ready before any splash or wake restores the light. */
    s_backlight_ready = true;
    settings_restore_backlight();

    /* If RGB type selected, init the RGB hardware */
    if (m1_backlight_type == 1)
    {
        rgb_bl_init();
        rgb_bl_on();
    }

    /* Apply orientation */
    settings_apply_orientation(m1_screen_orientation);
}


/*============================================================================*/
/**
  * @brief  Ensure all module SD card directories exist
  *         Call once after SD card is mounted (e.g. after settings_load_from_sd)
  */
/*============================================================================*/
void settings_ensure_sd_folders(void)
{
    static const char * const dirs[] = {
        "0:/NFC",
        "0:/NFC/assets",
        "0:/RFID",
        "0:/SUBGHZ",
        "0:/IR",
        "0:/BadUSB",
        KB_LAYOUT_DIR,
        "0:/BT",
        "0:/System",
        "0:/apps"
    };

    for (uint8_t i = 0; i < sizeof(dirs) / sizeof(dirs[0]); i++)
    {
        fs_directory_ensure(dirs[i]);
    }
}

/*============================================================================*/
/**
  * @brief  Standalone Backlight menu — Type toggle + OK launches Stock/RGB app
  */
/*============================================================================*/
#define BL_MENU_ITEMS   1
#define BL_MENU_TYPE    0
#define BL_POLL_MS      120U

static void backlight_menu_draw(uint8_t sel)
{
    char badge[8];
    snprintf(badge, sizeof(badge), "%u/%u", (unsigned)(sel + 1U), (unsigned)BL_MENU_ITEMS);

    m1_u8g2_firstpage();
    u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
    m1_draw_header_bar(&m1_u8g2, "Backlight", badge);
    m1_draw_content_frame(&m1_u8g2, 2, 14, 124, 35);
    u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);

    const char *label = "Type";
    const char *value = s_bl_type_text[m1_backlight_type];
    uint8_t y = 30;

    if (sel == BL_MENU_TYPE)
    {
        u8g2_DrawBox(&m1_u8g2, 6, y - 7, 114, 11);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_BG);
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_B);
        m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
        m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
        u8g2_SetDrawColor(&m1_u8g2, M1_DISP_DRAW_COLOR_TXT);
        u8g2_SetFont(&m1_u8g2, M1_DISP_SUB_MENU_FONT_N);
    }
    else
    {
        m1_ui_selection(&m1_u8g2, 6, y - 7, 114, 11, false);
        m1_draw_text(&m1_u8g2, 10, y, 64, label, TEXT_ALIGN_LEFT);
        m1_draw_text(&m1_u8g2, 78, y, 38, value, TEXT_ALIGN_RIGHT);
    }

    m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Back", "OK=Edit", button_ok_8x8);
    m1_u8g2_nextpage();
}

void settings_backlight(void)
{
    S_M1_Buttons_Status this_button_status;
    S_M1_Main_Q_t q_item;
    BaseType_t ret;

    uint8_t sel = 0;
    uint8_t needs_redraw = 1;

    while (1)
    {
        if (needs_redraw)
        {
            needs_redraw = 0;
            backlight_menu_draw(sel);
        }

        ret = xQueueReceive(main_q_hdl, &q_item, portMAX_DELAY);
        if (ret != pdTRUE) continue;
        if (q_item.q_evt_type != Q_EVENT_KEYPAD) continue;

        ret = xQueueReceive(button_events_q_hdl, &this_button_status, 0);
        if (ret != pdTRUE) continue;

        /* Back — save and exit */
        if (this_button_status.event[BUTTON_BACK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            settings_save_to_sd();
            xQueueReset(main_q_hdl);
            break;
        }

        /* Left/Right — toggle type */
        if (this_button_status.event[BUTTON_LEFT_KP_ID] == BUTTON_EVENT_CLICK ||
            this_button_status.event[BUTTON_RIGHT_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (sel == BL_MENU_TYPE)
            {
                m1_backlight_type = m1_backlight_type ? 0 : 1;
                needs_redraw = 1;
            }
        }

        /* OK — launch the appropriate backlight app */
        if (this_button_status.event[BUTTON_OK_KP_ID] == BUTTON_EVENT_CLICK)
        {
            if (m1_backlight_type == 1)
                app_rgb_backlight_run();
            else
                app_stock_backlight_run();
            needs_redraw = 1;
        }
    }
}

/* Appearance preferences intentionally use a separate, versioned file. */
#define APPEARANCE_PATH "0:/System/appearance.cfg"
static const char *appearance_labels[] = {"Theme", "Selection", "Clock", "Time Format", "Battery", "Animation", "Menu Wrap", "Sort By", "Reset Theme"};
static const uint8_t appearance_counts[] = {3, 3, 2, 2, 2, 2, 2, 4};
static uint8_t *appearance_field(unsigned i) {
    switch (i) {
    case 0: return &m1_ui.style; case 1: return &m1_ui.outline;
    case 2: return &m1_ui.clock; case 3: return &m1_ui.clock_12h;
    case 4: return &m1_ui.battery; case 5: return &m1_ui.animations;
    case 6: return &m1_ui.wrap;
    default: return &m1_ui.sort;
    }
}
static void appearance_load(void) {
    FIL file; UINT br; char text[160];
    if (f_open(&file, APPEARANCE_PATH, FA_READ) != FR_OK &&
        f_open(&file, APPEARANCE_PATH ".bak", FA_READ) != FR_OK) return;
    FRESULT result = f_read(&file, text, sizeof(text)-1, &br); f_close(&file);
    if (result != FR_OK) return;
    text[br] = 0;
    if (strncmp(text, "version=1\n", 10)) return;
    for (unsigned i=0; i<8; ++i) {
        char key[8]; snprintf(key, sizeof(key), "\np%u=", i);
        char *value = strstr(text, key);
        if (value && strlen(value) >= 5 && value[4] >= '0' && value[4] < '0'+appearance_counts[i] &&
            (value[5] == '\n' || value[5] == '\0')) *appearance_field(i) = value[4]-'0';
    }
}
static void appearance_save(void) {
    FIL file; UINT bw; char text[160];
    int n=snprintf(text, sizeof(text), "version=1\n");
    for (unsigned i=0; i<8; ++i) n+=snprintf(text+n, sizeof(text)-n, "p%u=%u\n", i, *appearance_field(i));
    f_mkdir("0:/System");
    if (f_open(&file, APPEARANCE_PATH ".tmp", FA_WRITE|FA_CREATE_ALWAYS) != FR_OK) goto failed;
    FRESULT result=f_write(&file, text, n, &bw);
    FRESULT closed=f_close(&file);
    if (result != FR_OK || bw != (UINT)n || closed != FR_OK) goto failed;
    /* Keep the prior settings until the replacement is fully written. */
    f_unlink(APPEARANCE_PATH ".bak");
    FRESULT moved=f_rename(APPEARANCE_PATH, APPEARANCE_PATH ".bak");
    if (moved != FR_OK && moved != FR_NO_FILE) goto failed;
    if (f_rename(APPEARANCE_PATH ".tmp", APPEARANCE_PATH) != FR_OK) {
        if (moved == FR_OK) f_rename(APPEARANCE_PATH ".bak", APPEARANCE_PATH);
        goto failed;
    }
    return;
failed:
    m1_message_box(&m1_u8g2, "Settings not saved", "Mount the SD card", "and try again", "OK");
}
void settings_appearance(void) {
    uint8_t sel=0;
    for (;;) {
        static const char *sort_modes[]={"A-Z", "Newest", "Oldest", "Default"};
        static const char *styles[]={"Classic", "Soft", "Minimal"};
        static const char *selections[]={"Filled", "Outline", "Underline"};
        m1_u8g2_firstpage();
        m1_draw_header_bar(&m1_u8g2, "Appearance", NULL);
        u8g2_SetFont(&m1_u8g2, M1_DISP_FUNC_MENU_FONT_N);
        if (sel == 0) {
            static const char *const theme_preview[] = {"Square Frames", "Rounded Frames", "Clean Rows"};
            m1_draw_text(&m1_u8g2, 6, 49, 116, theme_preview[m1_ui.style], TEXT_ALIGN_LEFT);
        }
        uint8_t start=sel < 3 ? 0 : sel-2;
        for (uint8_t row=0; row<3 && start+row<9; ++row) {
            uint8_t i=start+row; int y=15+row*12;
            m1_ui_selection(&m1_u8g2, 2, y, 124, 11, i==sel);
            m1_draw_text(&m1_u8g2, 6, y+8, 66, appearance_labels[i], TEXT_ALIGN_LEFT);
            const char *value = i==0 ? styles[m1_ui.style] : i==1 ? selections[m1_ui.outline] :
                i==3 ? (m1_ui.clock_12h ? "12h":"24h") : i==7 ? sort_modes[m1_ui.sort] : i==8 ? "OK" : (*appearance_field(i) ? "On":"Off");
            m1_draw_text(&m1_u8g2, 72, y+8, 50, value, TEXT_ALIGN_RIGHT);
        }
        m1_draw_bottom_bar(&m1_u8g2, button_back_8x8, "Save", "Change", button_ok_8x8);
        m1_u8g2_nextpage();
        S_M1_Main_Q_t event; S_M1_Buttons_Status key;
        if (xQueueReceive(main_q_hdl, &event, portMAX_DELAY)!=pdTRUE || event.q_evt_type!=Q_EVENT_KEYPAD) continue;
        if (xQueueReceive(button_events_q_hdl, &key, 0)!=pdTRUE) continue;
        if (key.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) { appearance_save(); xQueueReset(main_q_hdl); return; }
        if (key.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK) sel=m1_ui_step(sel,9,-1,m1_ui.wrap);
        else if (key.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK) sel=m1_ui_step(sel,9,1,m1_ui.wrap);
        else if (key.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK || key.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK || key.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK) {
            if (sel==8) {
                if (key.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK && m1_message_box_choice(&m1_u8g2,"Reset appearance?","Your remotes stay", "unchanged", "Cancel\nReset")==2) {
                    m1_ui_preferences_t defaults=M1_UI_DEFAULTS; m1_ui=defaults;
                }
            } else *appearance_field(sel)=m1_ui_step(*appearance_field(sel),appearance_counts[sel],key.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK ? -1:1,true);
        }
    }
}
