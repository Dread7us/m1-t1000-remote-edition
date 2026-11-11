#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "m1_room_radar.h"

typedef uint32_t TickType_t;
#define pdMS_TO_TICKS(x) (x)
#define portMAX_DELAY UINT32_MAX
#define BUTTON_BACK_KP_ID 0
#define BUTTON_OK_KP_ID 1
#define BUTTON_UP_KP_ID 2
#define BUTTON_DOWN_KP_ID 3
#define BUTTON_LEFT_KP_ID 4
static bool online, radio_online, recovery_ok, saved_network, fail_join;
static unsigned scans, joins, password_prompts, recoveries, jobs, saved_panels;
static unsigned key_count, key_index, ticks;
static int keys[8];
static char reply[512];
static int radar_key(TickType_t timeout)
{
    if (timeout == portMAX_DELAY) return key_index < key_count ? keys[key_index++] : BUTTON_BACK_KP_ID;
    ticks += timeout;
    return -1;
}
static TickType_t xTaskGetTickCount(void) { return ticks; }
static void radar_panel(const char *title, const char *a, const char *b, const char *c, const char *action)
{
    (void)title; (void)b; (void)c; (void)action;
    if (a && !strcmp(a, "Baseline recorded")) ++saved_panels;
}
static void wait_back(void) {}
static void radar_save_status(const char *stage) {(void)stage;}
static bool radar_at(const char *cmd)
{
    if (!strcmp(cmd,"AT\r\n")) return radio_online;
    if (!strcmp(cmd,"AT+CWJAP?\r\n")) {
        strcpy(reply, online ? "+CWJAP:\"Home\",\"aa:bb:cc:dd:ee:ff\",1,-30\r\nOK\r\n" : "No AP\r\nOK\r\n");
        return true;
    }
    if (!strcmp(cmd,"AT+CIPSTA?\r\n")) {
        strcpy(reply, online ? "+CIPSTA:ip:\"192.168.1.5\"\r\nOK\r\n" : "+CIPSTA:ip:\"0.0.0.0\"\r\nOK\r\n");
        return true;
    }
    assert(0); return false;
}
static bool wifi_room_radar_recover(void) { ++recoveries; radio_online=recovery_ok; return recovery_ok; }
static unsigned wifi_room_radar_networks(room_radar_network_t *networks, unsigned capacity)
{
    assert(capacity >= 1); ++scans;
    networks[0]=(room_radar_network_t){"Home",-30,true,saved_network};
    return 1;
}
static uint8_t m1_vkb_get_password(char *description, char *output, unsigned capacity)
{
    (void)description; assert(capacity==65); strcpy(output,"test-secret"); ++password_prompts; return 11;
}
static bool wifi_room_radar_join(const char *ssid,const char *password,bool use_saved,bool *saved)
{
    assert(!strcmp(ssid,"Home"));assert(use_saved || !strcmp(password,"test-secret"));
    ++joins;*saved=true;online=!fail_join;return online;
}
static room_radar_reply_t simulated_status = ROOM_RADAR_OK_REPLY;
static bool radar_status(room_radar_result_t *result, room_radar_reply_t *status)
{
    *result=(room_radar_result_t){ROOM_RADAR_IDLE,0,0}; *status=simulated_status;return simulated_status==ROOM_RADAR_OK_REPLY;
}
static bool radar_job(unsigned operation, room_radar_result_t *result, bool *cancelled)
{
    assert(online && (operation==1 || operation==2));++jobs;
    *cancelled=false;*result=(room_radar_result_t){ROOM_RADAR_DONE,32,10};return true;
}

static void mock_prepare(void) {}
