#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef uint32_t TickType_t;
typedef struct {int q_evt_type;} S_M1_Main_Q_t;
typedef struct {int event[2];} S_M1_Buttons_Status;
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define Q_EVENT_KEYPAD 1
#define BUTTON_BACK_KP_ID 0
#define BUTTON_LEFT_KP_ID 1
#define BUTTON_EVENT_CLICK 1
#define IR_UNIVERSAL_PATH_MAX_LEN 128
#define IRMP_UNKNOWN_PROTOCOL 0
#define main_q_hdl 1
#define button_events_q_hdl 2
#define u8g2_FirstPage(...) ((void)0)
#define u8g2_SetDrawColor(...) ((void)0)
#define u8g2_SetFont(...) ((void)0)
#define u8g2_DrawStr(...) ((void)0)
#define m1_u8g2_nextpage(...) ((void)0)
#define m1_led_fast_blink(...) ((void)0)
#define m1_buzzer_notification(...) ((void)0)
#define xQueueReset(...) ((void)0)
static char s_raw_tx_filepath[128];
static struct {bool is_raw;uint8_t protocol,address,command,flags;char name[16];} s_commands[2]={{false,1,2,3,0,"Parsed"},{true,0,0,0,0,"Raw"}};
static struct {uint8_t protocol,address,command,flags;} s_tx_irmp_data;
static bool s_remote_tx_started,s_remote_tx_raw;
static TickType_t ticks,s_remote_tx_tick;
static unsigned starts,completed,stops,phase;
static int mode;
static uint16_t parse_ir_file(const char *p){(void)p;return 2;}
static TickType_t xTaskGetTickCount(void){return ticks;}
static void vTaskDelay(unsigned t){ticks+=t;}
static void remote_tx_stop(void){if(s_remote_tx_started)++stops;s_remote_tx_started=false;}
static bool remote_tx_finished(void){if(!s_remote_tx_started)return true;if(mode==1)return false;if(++phase<4)return false;++completed;return true;}
static void infrared_encode_sys_init(void){assert(s_remote_tx_started);++starts;phase=0;}
static void irsnd_generate_tx_data(void *unused){(void)unused;}
#define irsnd_generate_tx_data(...) ((void)0)
static void infrared_transmit(int init){(void)init;}
static void transmit_raw_command(const void *cmd){(void)cmd;s_remote_tx_started=true;infrared_encode_sys_init();}
static int xQueueReceive(int q,void *p,unsigned timeout){ticks+=timeout;if(mode==2&&ticks>=2){if(q==1)((S_M1_Main_Q_t *)p)->q_evt_type=Q_EVENT_KEYPAD;else ((S_M1_Buttons_Status *)p)->event[0]=1;return 1;}return 0;}
