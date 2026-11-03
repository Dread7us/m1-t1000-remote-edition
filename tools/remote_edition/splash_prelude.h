/* Minimal drawing spies for the actual firmware welcome-screen function. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include "m1_splash_art.h"
#include "m1_branding.h"
typedef struct {int font;} u8g2_t;
static u8g2_t m1_u8g2;
static struct {int op_mode;unsigned active_timestamp;} m1_device_stat;
#define T1000_VERSION_STRING "v0.4.0 M1 r3"
#define M1_LCD_DISPLAY_WIDTH 128
#define M1_LCD_DISPLAY_HEIGHT 64
#define M1_DISP_DRAW_COLOR_TXT 1
#define M1_POWERUP_LOGO_FONT 1
#define M1_DISP_MAIN_MENU_FONT_B 2
#define M1_DISP_MAIN_MENU_FONT_N 3
#define M1_OPERATION_MODE_DISPLAY_ON 1
#define M1_BACKLIGHT_BRIGHTNESS 1
static unsigned logo_seen,title_seen,footer_seen,line_seen;
static void u8g2_SetPowerSave(u8g2_t *d,bool value) {(void)d;(void)value;}
static void u8g2_FirstPage(u8g2_t *d) {(void)d;}
static int u8g2_NextPage(u8g2_t *d) {(void)d;return 0;}
static void u8g2_SetDrawColor(u8g2_t *d,int color) {(void)d;(void)color;}
static void u8g2_DrawFrame(u8g2_t *d,int x,int y,int w,int h) {(void)d;assert(x==0 && y==0 && w==128 && h==64);}
static void u8g2_DrawHLine(u8g2_t *d,int x,int y,int w) {(void)d;(void)x;(void)y;(void)w;line_seen++;}
static void u8g2_DrawXBMP(u8g2_t *d,int x,int y,int w,int h,const uint8_t *bits) {(void)d;assert(x==40 && y==3 && w==48 && h==38 && bits==m1_splash_avatar_48x38);logo_seen++;}
static void u8g2_SetFont(u8g2_t *d,int font) {d->font=font;}
static unsigned u8g2_GetStrWidth(u8g2_t *d,const char *text) {return (unsigned)strlen(text)*(d->font==2?6:5);}
static void u8g2_DrawStr(u8g2_t *d,int x,int y,const char *text) {
 assert(x>=0 && x+(int)u8g2_GetStrWidth(d,text)<=127 && y<=62);
 if(y==52) {assert(!strcmp(text,M1_PRODUCT_NAME));title_seen++;}
 else {assert(y==62);footer_seen++;}
}
static void m1_backlight_on(int level) {(void)level;}
static unsigned HAL_GetTick(void) {return 123;}
