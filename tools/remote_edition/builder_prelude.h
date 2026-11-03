/* Host shims for the actual remote builder's persistence and name suggestions. */
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include "flipper_ir.h"
#include "m1_ir_remote_view.h"
#define IR_CUSTOM_DIR "0:/IR"
#define IR_CUSTOM_NAME_MAX_LEN 32
#define IR_CUSTOM_PATH_MAX_LEN 256
#define IR_CUSTOM_MAX_BUTTONS 64
#define IR_CUSTOM_MAX_DEDUP 99
#define AM_DIR 0x10
#define pdMS_TO_TICKS(n) (n)
typedef struct {unsigned index;} DIR;
static unsigned ambiguous_library;
static flipper_ir_signal_t s_scan_sig;
static const char *answers[8]; static unsigned answer_index;
static uint8_t m1_vkb_get_filename(const char *prompt,char *initial,char *out) {
 (void)prompt;const char *answer=answers[answer_index++];
 if(answer && !strcmp(answer,"<cancel>"))return 0;
 snprintf(out,32,"%s",answer && answer[0] ? answer : initial);
 return (uint8_t)strlen(out);
}
static void ir_custom_draw_message(const char *a,const char *b) {(void)a;(void)b;}
static void ir_custom_wait_key(void) {}
static void m1_wdt_reset(void) {}
static void vTaskDelay(unsigned ticks) {(void)ticks;}
static FRESULT f_mkdir(const char *path) {(void)path;return FR_OK;}
static FRESULT f_opendir(DIR *dir,const char *path) {dir->index=0;return !strcmp(path,"0:/IR/TV") ? FR_OK : FR_NO_PATH;}
static FRESULT f_readdir(DIR *dir,FILINFO *info) {
 memset(info,0,sizeof(*info));
 if(dir->index==0)snprintf(info->fname,sizeof(info->fname),"Samsung.ir");
 if(dir->index==1 && ambiguous_library)snprintf(info->fname,sizeof(info->fname),"Other.ir");
 dir->index++;return FR_OK;
}
static FRESULT f_closedir(DIR *dir) {(void)dir;return FR_OK;}
