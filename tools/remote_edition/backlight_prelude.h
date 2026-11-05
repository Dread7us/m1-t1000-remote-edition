#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
typedef unsigned UINT;
typedef int FRESULT;
typedef struct {int index;} FIL;
typedef int FILINFO;
#define FR_OK 0
#define FR_NO_FILE 1
#define FA_WRITE 2
#define FA_CREATE_ALWAYS 8
#define SETTINGS_FILE_PATH "settings"
#define SETTINGS_TAG "test"
#define KB_LAYOUT_PATH_MAX 128
#define M1_LOG_W(...) ((void)0)
static char files[3][1024]; static bool exists[3];
static int fail_write,fail_sync,fail_close,fail_install;
static int index_of(const char *p){return strstr(p,".tmp")?1:strstr(p,".bak")?2:0;}
static int f_mkdir(const char *p){(void)p;return 0;}
static int f_open(FIL *f,const char *p,int flags){(void)flags;f->index=index_of(p);exists[f->index]=true;files[f->index][0]=0;return 0;}
static int f_write(FIL *f,const void *p,UINT n,UINT *bw){*bw=fail_write?n-1:n;if(!fail_write){strncat(files[f->index],p,n);}return 0;}
static int f_sync(FIL *f){(void)f;return fail_sync;}
static int f_close(FIL *f){(void)f;return fail_close;}
static int f_stat(const char *p,FILINFO *i){(void)i;return exists[index_of(p)]?0:1;}
static int f_unlink(const char *p){int i=index_of(p);if(!exists[i])return 1;exists[i]=false;return 0;}
static int f_rename(const char *a,const char *b){int i=index_of(a),j=index_of(b);if(!exists[i])return 1;if(fail_install&&i==1&&j==0)return 2;assert(!exists[j]);strcpy(files[j],files[i]);exists[j]=true;exists[i]=false;return 0;}
static uint8_t m1_brightness_level=4,m1_buzzer_on,m1_led_notify_on,m1_screen_orientation,m1_sleep_timeout_idx,m1_esp32_auto_init,m1_backlight_type;
static int m1_tz_offset_hours;
static struct {struct {int ism_band_region;} config;} m1_device_stat;
static int rgb_bl_get_mode(void){return 0;} static int rgb_bl_get_effect(void){return 0;}
static int rgb_bl_get_brightness(void){return 128;} static int rgb_bl_reactive_is_on(void){return 0;}
static void rgb_bl_get_custom(uint8_t *r,uint8_t *g,uint8_t *b){*r=*g=*b=0;}
static const char *kb_layout_path(void){return "";}
static int applied=-1;
static void m1_backlight_on(int b){applied=b;}
static const uint8_t s_brightness_values[]={0,64,128,192,255};
static bool s_backlight_ready;
