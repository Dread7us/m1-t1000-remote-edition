#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "m1_ir_universal.h"
#include "m1_ir_remote_view.h"
typedef struct {int width,height,font;} u8g2_t;
typedef enum {TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER,TEXT_ALIGN_RIGHT} S_M1_text_align_t;
#define BROWSE_NAME_MAX_LEN 64
#define M1_DISP_DRAW_COLOR_TXT 1
#define M1_DISP_DRAW_COLOR_BG 0
#define M1_DISP_FUNC_MENU_FONT_N ((const uint8_t *)5)
#define u8g2_font_tiny5_tf ((const uint8_t *)4)
static u8g2_t m1_u8g2;
static ir_universal_cmd_t s_commands[64];
static uint16_t s_cmd_count;
static bool s_remote_tx_started;
static const uint8_t button_ok_8x8[8],arrowleft_8x8[8];
static const uint8_t button_power_18x18[54],button_mute_18x18[54],button_plus_18x18[54],button_minus_18x18[54],button_up_18x18[54],button_down_18x18[54],button_play_18x18[54],button_pause_18x18[54],button_next_18x18[54],button_previous_18x18[54];
static void bounds(u8g2_t *d,int x,int y,int w,int h) {assert(x>=0 && y>=0 && x+w<=d->width && y+h<=d->height);}
static void u8g2_FirstPage(u8g2_t *d) {(void)d;}
static void u8g2_SetDrawColor(u8g2_t *d,int c) {(void)d;(void)c;}
static void u8g2_SetFont(u8g2_t *d,const uint8_t *f) {d->font=(int)(uintptr_t)f;}
static int u8g2_GetStrWidth(u8g2_t *d,const char *s) {return (int)strlen(s)*d->font;}
static void u8g2_DrawHLine(u8g2_t *d,int x,int y,int w) {bounds(d,x,y,w,1);}
static void u8g2_DrawBox(u8g2_t *d,int x,int y,int w,int h) {bounds(d,x,y,w,h);}
static void u8g2_DrawFrame(u8g2_t *d,int x,int y,int w,int h) {bounds(d,x,y,w,h);}
static void u8g2_DrawXBMP(u8g2_t *d,int x,int y,int w,int h,const uint8_t *p) {(void)p;bounds(d,x,y,w,h);}
static void m1_draw_text(u8g2_t *d,int x,int y,int w,const char *s,S_M1_text_align_t a) {
 int textw=u8g2_GetStrWidth(d,s); assert(textw<=w);
 if(a==TEXT_ALIGN_CENTER)x+=(w-textw)/2;else if(a==TEXT_ALIGN_RIGHT)x+=w-textw;
 bounds(d,x,y-5,textw,5);
}
static void m1_draw_bottom_bar(u8g2_t *d,const uint8_t *l,const char *lt,const char *rt,const uint8_t *r) {(void)l;(void)lt;(void)rt;(void)r;bounds(d,0,51,128,13);}
static void m1_u8g2_nextpage(void) {}
