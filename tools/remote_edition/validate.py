#!/usr/bin/env python3
"""Native tests for Remote Edition; does not access or flash the M1."""
from pathlib import Path
import subprocess, os, re, argparse, shutil, tempfile, atexit
repo=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc',default=os.environ.get('CC') or shutil.which('cc') or shutil.which('gcc') or shutil.which('clang') or shutil.which('cl'))
args=parser.parse_args()
if not args.cc: parser.error('Install a native C compiler or use --cc with its path.')
compiler=args.cc
is_msvc=Path(compiler).name.lower() in ('cl','cl.exe')
temporary=tempfile.TemporaryDirectory(prefix='m1-remote-tests-')
atexit.register(temporary.cleanup)
out=Path(temporary.name)
env=os.environ.copy()
shim=repo/'tools/host_test'; src=repo/'m1_csrc'
log=[]
def run(cmd,cwd=out):
 r=subprocess.run(list(map(str,cmd)),env=env,cwd=cwd,capture_output=True,text=True)
 log.append(r.stdout+r.stderr)
 if r.returncode: print(r.stdout,r.stderr); raise RuntimeError(f'Failed {cmd[0]}: {r.returncode}')
 return r.stdout
def compile(name,files):
 exe=out/(name+'.exe')
 if is_msvc:
  command=[compiler,'/nologo','/W3','/std:c11','/D_CRT_SECURE_NO_WARNINGS','/I'+str(shim),'/I'+str(src),*files,'/Fe:'+str(exe),'/Fo:'+str(out)+'\\']
 else:
  command=[compiler,'-std=c11','-Wall','-Wextra','-O2','-I'+str(shim),'-I'+str(src),*files,'-o',exe]
 run(command)
 return exe
common=[shim/'ff_shim.c',src/'flipper_file.c',src/'flipper_ir.c']
exe=compile('roundtrip',[shim/'test_flipper_ir.c',*common]); print(run([exe]))
exe=compile('navigation',[shim/'test_ir_remote_view.c']); print(run([exe]))
exe=compile('validate',[shim/'validate_ir.c',*common]); print(run([exe,*repo.glob('ir_database/**/*.ir')]))

# Compile the exact three universal-parser function bodies from the firmware,
# without the hardware/UI layer, and exercise the real FatFs file reader.
code=(src/'m1_ir_universal.c').read_text()
functions=[]
for signature in ['static uint8_t map_flipper_protocol(const char *name)', 'static bool parse_ir_signal_block(flipper_file_t *ff, ir_universal_cmd_t *cmd)', 'static uint16_t parse_ir_file(const char *filepath)']:
 start=code.index(signature+'\n{'); end=code.index('\n} //',start)+2
 functions.append(code[start:end])
harness='''#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "m1_ir_universal.h"
#include "flipper_file.h"
#include "m1_ir_remote_view.h"
static ir_universal_cmd_t s_commands[IR_UNIVERSAL_MAX_CMDS];
'''+ '\n'.join(functions)+r'''
int main(int argc, char **argv) {
 assert(argc==4);
 assert(parse_ir_file(argv[1])==64);
 assert(!strcmp(s_commands[63].name,"Button63"));
 assert(s_commands[63].command==63);
 assert(parse_ir_file(argv[2])==3);
 assert(s_commands[0].is_raw && s_commands[0].raw_occurrence==0);
 assert(s_commands[1].is_raw && s_commands[1].raw_occurrence==1);
 assert(!s_commands[2].is_raw && s_commands[2].address==7 && s_commands[2].command==2);
 uint16_t count=parse_ir_file(argv[3]);const char *names[64];uint16_t slots[IR_REMOTE_GRID_MAX];
 assert(count==25);for(uint16_t i=0;i<count;i++)names[i]=s_commands[i].name;
 assert(ir_remote_grid_build(names,count,slots,IR_REMOTE_GRID_MAX)>0);
 assert(!strcmp(names[slots[0]],"Power") && !strcmp(names[slots[1]],"Mute"));
 assert(!strcmp(names[slots[2]],"Vol_Up") && !strcmp(names[slots[3]],"Ch_Up"));
 assert(!strcmp(names[slots[4]],"Vol_Down") && !strcmp(names[slots[5]],"Ch_Down"));
 puts("PASS: actual universal parser loads all 64 commands, rejects missing/invalid fields and empty raw data, disambiguates repeated raw names");
 return 0;
}
'''
(out/'parser.c').write_text(harness)
header='Filetype: IR signals file\nVersion: 1\n#\n'
(out/'many.ir').write_text(header+''.join(f'name: Button{i}\ntype: parsed\nprotocol: NEC\naddress: 07 00 00 00\ncommand: {i:02X} 00 00 00\n#\n' for i in range(64)))
(out/'malformed.ir').write_text(header+'''name: MissingAddress
type: parsed
protocol: NEC
command: 01 00 00 00
#
name: MissingCommand
type: parsed
protocol: NEC
address: 07 00 00 00
#
name: InvalidHex
type: parsed
protocol: NEC
address: ZZ
command: 01 00 00 00
#
name: EmptyRaw
type: raw
frequency: 38000
#
name: Power
type: raw
frequency: 38000
data: 9000 4500 560 560
#
name: Power
type: raw
frequency: 38000
data: 9000 4500 560 1690
#
name: Valid
type: parsed
protocol: NEC
address: 07 00 00 00
command: 02 00 00 00
#
''')
exe=compile('parser',[out/'parser.c',shim/'ff_shim.c',src/'flipper_file.c'])
print(run([exe,out/'many.ir',out/'malformed.ir',repo/'ir_database/TV/Samsung.ir']))
# Run the actual render functions with drawing-bound assertions. The mocked
# font widths are conservative (tiny=4 px, regular=5 px per character).
geometry=(repo/'tools/remote_edition/geometry_prelude.h').read_text()
for signature in ['static void remote_draw_text(', 'static void draw_commands(const char *title, uint16_t selection)', 'static const uint8_t *remote_button_icon(const char *name)', 'static void draw_remote_grid(const char *title, uint16_t selection)']:
 start=code.index(signature,code.index('static void remote_draw_text('))
 body=code.index('{',start); depth=1; end=body+1
 while depth:
  depth+=(code[end]=='{')-(code[end]=='}');end+=1
 geometry+='\n'+code[start:end]+'\n'
geometry+='''
int main(void) {
 const char *names[]={"Power","Mute","Vol_up","Vol_dn","Ch_next","Ch_prev","Play","Pause","Next","Prev","A_very_long_unknown_button_name"};
 for(unsigned i=0;i<64;i++) snprintf(s_commands[i].name,sizeof(s_commands[i].name),"%s",names[i%11]);
 for(s_cmd_count=1;s_cmd_count<=64;s_cmd_count++)for(uint16_t i=0;i<s_cmd_count;i++)for(unsigned tx=0;tx<2;tx++) {
  s_remote_tx_started=(bool)tx;
  m1_u8g2.width=128;m1_u8g2.height=64;
  draw_commands("A remote filename longer than the entire display could ever fit",i);
  m1_u8g2.width=64;m1_u8g2.height=128;
  const char *allnames[64];for(uint16_t j=0;j<s_cmd_count;j++)allnames[j]=s_commands[j].name;
  s_grid_count=ir_remote_grid_build(allnames,s_cmd_count,s_grid_order,IR_REMOTE_GRID_MAX);
  for(uint16_t j=0;j<s_grid_count;j++)if(s_grid_order[j]!=IR_REMOTE_EMPTY)
   draw_remote_grid("A remote filename longer than the entire display could ever fit",j);
 }
 puts("PASS: actual list and portrait renderers stay within 128x64 / 64x128 bounds for every selection, 1..64 buttons, long names, all icons and TX/idle states");
 return 0;
}
'''
(out/'geometry.c').write_text(geometry)
exe=compile('geometry',[out/'geometry.c']);print(run([exe]))
# Exercise real builder persistence against the real IR reader/writer. Only
# keyboard and directory discovery are mocked; saved file bytes are real.
custom=(src/'m1_ir_custom.c').read_text()
builder=(repo/'tools/remote_edition/builder_prelude.h').read_text()
for signature in ['static void ir_custom_sanitize_name(', 'static bool ir_custom_unique_path(', 'static bool ir_custom_create_empty(', 'static void ir_custom_next_button(', 'static void ir_custom_skip_suggestion(', 'static void ir_custom_suggest_identity(', 'static bool ir_custom_save_signal(', 'static bool ir_custom_rename_remote(']:
 matches=list(re.finditer(re.escape(signature),custom))
 # Definitions have an opening brace before the next semicolon; declarations do not.
 start=next(m.start() for m in matches if custom.find('{',m.start()) < custom.find(';',m.start()))
 body=custom.index('{',start);depth=1;end=body+1
 while depth:
  depth+=(custom[end]=='{')-(custom[end]=='}');end+=1
 builder+='\n'+custom[start:end]+'\n'
builder+='\n'+(repo/'tools/remote_edition/builder_cases.c').read_text()
(out/'builder.c').write_text(builder)
(out/'IR/TV').mkdir(parents=True)
(out/'IR/Learned').mkdir()
library='Filetype: IR signals file\nVersion: 1\n#\n'
for name,command in [('Power',2),('Vol_up',7)]:
 library+=f'name: {name}\ntype: parsed\nprotocol: Samsung32\naddress: 07 00 00 00\ncommand: {command:02X} 00 00 00\n#\n'
(out/'IR/TV/Samsung.ir').write_text(library)
(out/'IR/TV/Other.ir').write_text(library)
exe=compile('builder',[out/'builder.c',*common]);print(run([exe]))
system=(src/'m1_system.c').read_text()
start=system.index('void startup_info_screen_display(const char *scr_text)\n{')
end=system.index('\n} //',start)+2
splash=(repo/'tools/remote_edition/splash_prelude.h').read_text()+'\n'+system[start:end]+'''
int main(void) {
 assert(sizeof(m1_splash_avatar_48x38)==228);
 startup_info_screen_display("");
 startup_info_screen_display("UPDATE COMPLETED!");
 startup_info_screen_display("ROLLBACK COMPLETED!");
 assert(logo_seen==3 && title_seen==3 && footer_seen==3 && line_seen==0);
 puts("PASS: actual splash shows the avatar, title and version/status in bounds without a crossing divider");
 return 0;
}
'''
(out/'splash.c').write_text(splash)
exe=compile('splash',[out/'splash.c']);print(run([exe]))

# Run the actual appearance load/save code over the host filesystem.
settings=(src/'m1_settings.c').read_text()
begin=settings.index('#define APPEARANCE_PATH')
end=settings.index('void settings_appearance(void)',begin)
appearance=r"""#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "ff.h"
#include "m1_ui_theme.h"
m1_ui_preferences_t m1_ui=M1_UI_DEFAULTS;
static int failures, fail_write, fail_rename;
static int m1_u8g2;
static void m1_message_box(void *d,const char *a,const char *b,const char *c,const char *e) {(void)d;(void)a;(void)b;(void)c;(void)e;++failures;}
static FRESULT f_mkdir(const char *p) {(void)p;return FR_OK;}
#define f_read appearance_read
static FRESULT appearance_read(FIL *f,void *b,UINT n,UINT *read) {*read=(UINT)fread(b,1,n,f->fp);return FR_OK;}
static FRESULT f_write(FIL *f,const void *b,UINT n,UINT *written) {*written=fail_write ? 0:(UINT)fwrite(b,1,n,f->fp);return fail_write ? FR_DISK_ERR:FR_OK;}
static FRESULT real_rename(const char *a,const char *b) {return f_rename(a,b);}
static FRESULT test_rename(const char *a,const char *b) {if(fail_rename && strstr(a,".tmp"))return FR_DISK_ERR;return real_rename(a,b);}
#define f_rename test_rename
"""+settings[begin:end]+r"""
int main(void) {
 for(int count=1;count<=24;++count)for(int v=0;v<count;++v) {
  assert(m1_ui_step(v,count,-1,0)==(v?v-1:0));
  assert(m1_ui_step(v,count,1,0)==(v+1<count?v+1:v));
  assert(m1_ui_step(v,count,-1,1)==(v?v-1:count-1));
  assert(m1_ui_step(v,count,1,1)==(v+1)%count);
 }
 appearance_save();assert(!failures);
 m1_ui.style=2;m1_ui.outline=1;m1_ui.clock=0;m1_ui.wrap=0;appearance_save();assert(!failures);
 m1_ui_preferences_t expected=m1_ui,defaults=M1_UI_DEFAULTS;
 m1_ui=defaults;appearance_load();assert(!memcmp(&m1_ui,&expected,sizeof(expected)));
 fail_write=1;m1_ui.style=0;appearance_save();assert(failures==1);
 m1_ui=defaults;appearance_load();assert(!memcmp(&m1_ui,&expected,sizeof(expected)));
 fail_write=0;fail_rename=1;m1_ui.style=0;appearance_save();assert(failures==2);
 m1_ui=defaults;appearance_load();assert(!memcmp(&m1_ui,&expected,sizeof(expected)));
 fail_rename=0;
 FILE *f=fopen("System/appearance.cfg","wb");assert(f);fputs("version=1\np0=9\np1=-1\np2=99\np6=",f);fclose(f);
 m1_ui=defaults;appearance_load();assert(!memcmp(&m1_ui,&defaults,sizeof(defaults)));
 f=fopen("System/appearance.cfg","wb");assert(f);fputs("version=2\np0=2\n",f);fclose(f);
 m1_ui=defaults;appearance_load();assert(!memcmp(&m1_ui,&defaults,sizeof(defaults)));
 puts("PASS: appearance persistence, malformed values, version rejection, short-write and replacement-failure rollback; wrap and bounded navigation");
 return 0;
}
"""
(out/'System').mkdir(exist_ok=True)
(out/'appearance.c').write_text(appearance)
exe=compile('appearance',[out/'appearance.c',shim/'ff_shim.c']);print(run([exe]))


# Check the actual shared drawing primitives against constrained font metrics.
display=(src/'m1_display.c').read_text(encoding='utf-8')
functions=[]
for name in ['m1_draw_text','m1_ui_selection','m1_draw_content_frame']:
 match=re.search(r'\nvoid '+name+r'\([^;]*?\)\n\{',display);assert match,name
 start=match.start()+1;end=match.end();depth=1
 while depth:
  if display[end]=='{':depth+=1
  if display[end]=='}':depth-=1
  end+=1
 functions.append(display[start:end])
harness=r"""#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <assert.h>
#include "m1_ui_theme.h"
typedef int u8g2_t;typedef int u8g2_uint_t;
typedef enum {TEXT_ALIGN_LEFT,TEXT_ALIGN_CENTER,TEXT_ALIGN_RIGHT} S_M1_text_align_t;
m1_ui_preferences_t m1_ui=M1_UI_DEFAULTS;
static int color=1,left,right,calls;
static unsigned u8g2_GetStrWidth(u8g2_t *d,const char *s){(void)d;return (unsigned)strlen(s)*5;}
static void u8g2_DrawStr(u8g2_t *d,int x,int y,const char *s){(void)d;(void)y;assert(x>=left && x+(int)strlen(s)*5<=right);++calls;}
static void u8g2_SetDrawColor(u8g2_t *d,int c){(void)d;color=c;}
static void bounds(int x,int y,int w,int h){assert(x>=0&&y>=0&&x+w<=128&&y+h<=64);}
static void u8g2_DrawBox(u8g2_t *d,int x,int y,int w,int h){(void)d;bounds(x,y,w,h);}
static void u8g2_DrawFrame(u8g2_t *d,int x,int y,int w,int h){(void)d;bounds(x,y,w,h);}
static void u8g2_DrawRBox(u8g2_t *d,int x,int y,int w,int h,int r){(void)r;u8g2_DrawBox(d,x,y,w,h);}
static void u8g2_DrawRFrame(u8g2_t *d,int x,int y,int w,int h,int r){(void)r;u8g2_DrawFrame(d,x,y,w,h);}
static void u8g2_DrawVLine(u8g2_t *d,int x,int y,int h){(void)d;bounds(x,y,1,h);}
"""+'\n'.join(functions)+r"""
int main(void){
 u8g2_t screen=0;char long_label[300];memset(long_label,'W',sizeof(long_label)-1);long_label[sizeof(long_label)-1]=0;
 for(int width=0;width<=124;++width)for(int align=0;align<3;++align){
  left=2;right=2+width;m1_draw_text(&screen,left,30,width,long_label,align);
  m1_draw_text(&screen,left,30,width,"OK",align);m1_draw_text(&screen,left,30,width,NULL,align);
 }
 for(int style=0;style<3;++style)for(int outline=0;outline<2;++outline){
  m1_ui.style=style;m1_ui.outline=outline;
  m1_ui_selection(&screen,2,15,124,11,true);assert(color==(outline?1:0));
  m1_ui_selection(&screen,2,27,124,11,false);assert(color==1);
  m1_draw_content_frame(&screen,2,14,124,35);
 }
 assert(calls>0);puts("PASS: actual theme primitives and long-label ellipsis stay in bounds at every width/alignment and all six styles");return 0;
}
"""
(out/'ui-render.c').write_text(harness)
exe=compile('ui-render',[out/'ui-render.c']);print(run([exe]))


exe=compile('automation-files',[shim/'test_automation.c',shim/'ff_shim.c',src/'flipper_file.c']);print(run([exe]))

automation_source=(src/'m1_automation.c').read_text(encoding='utf-8')
start=automation_source.index('static void execute(void) {');end=automation_source.index('\nvoid automation_run(void)',start)
executor=r"""#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m1_automation.h"
typedef unsigned TickType_t;
#define pdMS_TO_TICKS(x) (x)
#define portTICK_PERIOD_MS 1
static automation_t chain;
static bool cancelled;
static int main_q_hdl,button_events_q_hdl,m1_u8g2;
static int bad_source=-1,accept=1,stop_gap,ir_result,rf_result;
static unsigned ticks,waits,checks;
static char sent[32],message[64];
static void xQueueReset(int q){(void)q;}
static TickType_t xTaskGetTickCount(void){return ticks;}
static bool cancel(void){return cancelled;}
static bool wait_ms(unsigned ms){ticks+=ms;++waits;if(stop_gap){cancelled=true;return false;}return true;}
static void progress(unsigned i,const char *s,unsigned p){(void)i;(void)s;assert(p<=100);}
static bool preflight(unsigned i){++checks;return (int)i!=bad_source;}
static void error(const char *a,const char *b){(void)b;snprintf(message,sizeof(message),"%s",a);}
static int m1_message_box_choice(int *d,const char *a,const char *b,const char *c,const char *e){(void)d;(void)a;(void)b;(void)c;(void)e;return accept?2:0;}
static m1_action_result_t ir_automation_send(const char *p,uint16_t b,m1_action_cancel_fn c){(void)p;(void)b;(void)c;strcat(sent,"I");return ir_result;}
static m1_action_result_t sub_ghz_automation_send(const char *p,m1_action_cancel_fn c){(void)p;(void)c;strcat(sent,"S");return rf_result;}
"""+automation_source[start:end]+r"""
static void reset(void){memset(&chain,0,sizeof(chain));chain.count=3;chain.steps[0].type=AUTO_IR;chain.steps[1].type=AUTO_WAIT;chain.steps[1].wait_ms=100;chain.steps[2].type=AUTO_SUBGHZ;cancelled=false;ticks=waits=checks=0;sent[0]=message[0]=0;bad_source=-1;accept=1;stop_gap=0;ir_result=rf_result=ACTION_OK;}
int main(void){
 reset();execute();assert(!strcmp(sent,"IS")&&checks==6&&waits>=3&&!strcmp(message,"Sequence sent"));
 reset();bad_source=2;execute();assert(!sent[0]&&!strcmp(message,"Source unavailable"));
 reset();accept=0;execute();assert(!sent[0]);
 reset();stop_gap=1;execute();assert(!strcmp(sent,"I")&&!strcmp(message,"Automation stopped"));
 reset();ir_result=ACTION_FAILED;execute();assert(!strcmp(sent,"I")&&!strcmp(message,"Automation stopped"));
 reset();ir_result=ACTION_CANCELLED;execute();assert(!strcmp(sent,"I")&&cancelled);
 reset();chain.count=0;execute();assert(!sent[0]&&!strcmp(message,"No actions yet"));
 puts("PASS: actual chain executor preserves IR/Wait/RF order, preflights all sources before any output, cancels at confirmation/during actions/between actions, and stops after a failed action");return 0;
}
"""
(out/'automation-executor.c').write_text(executor)
exe=compile('automation-executor',[out/'automation-executor.c']);print(run([exe]))


ir=(src/'m1_ir_universal.c').read_text(encoding='utf-8');ir=ir[ir.index('m1_action_result_t ir_automation_send('):]
rf=(src/'m1_sub_ghz.c').read_text(encoding='utf-8');rf=rf[rf.index('m1_action_result_t sub_ghz_automation_send('):]
adapters=r"""#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m1_automation.h"
typedef unsigned TickType_t;
typedef struct {int q_evt_type;} S_M1_Main_Q_t;
typedef struct {int modulation;} S_M1_SubGHz_Scan_Config;
#define pdTRUE 1
#define pdMS_TO_TICKS(x) (x)
#define Q_EVENT_IRRED_TX 1
#define Q_EVENT_SUBGHZ_TX 2
#define IR_UNIVERSAL_NAME_MAX_LEN 32
#define SUB_GHZ_BAND_CUSTOM 128
#define SUB_GHZ_BAND_EOL 12
#define SUB_GHZ_OPMODE_ISOLATED 0
#define SUB_GHZ_RAW_DATA_PARSER_ERROR_MASK 128
#define SUB_GHZ_RAW_DATA_PARSER_IDLE 10
#define LED_BLINK_ON_RGB 1
#define LED_FASTBLINK_PWM_OFF 0
#define LED_FASTBLINK_ONTIME_OFF 0
static int main_q_hdl,mode,radio,start_fail,buffer_fail,blocked,cancel_after,cancel_calls,starts,stops,releases;
static unsigned ticks,subghz_custom_freq_hz=315000000;
static S_M1_SubGHz_Scan_Config subghz_scan_config={2};
static uint8_t subghz_replay_ret_code,subghz_replay_band,subghz_replay_channel,subghz_tx_power_idx;
static uint8_t tx_power_values[]={12};static int double_buffer_ptr_id;
static struct {char dat_filename[256];} datfile_info;
static bool s_remote_tx_started,s_automation_tx;
static char s_raw_tx_filepath[256];static int s_commands[]={100,200,300},last_command;
static TickType_t xTaskGetTickCount(void){return ticks;}
static int xQueueReceive(int q,S_M1_Main_Q_t *e,unsigned timeout){(void)q;ticks+=timeout;if(mode==1)return 0;e->q_evt_type=radio?Q_EVENT_SUBGHZ_TX:Q_EVENT_IRRED_TX;return 1;}
static bool cancelled_test(void){return cancel_after && ++cancel_calls>=cancel_after;}
static unsigned ir_automation_buttons(const char *p){(void)p;return 3;}
static bool ir_automation_button(unsigned i,char *n,size_t c){snprintf(n,c,"button");return i<3;}
static void transmit_command(const int *p){last_command=*p;++starts;s_remote_tx_started=!start_fail;}
static void remote_tx_stop(void){s_remote_tx_started=false;++stops;}
static void m1_led_fast_blink(int a,int b,int c){(void)a;(void)b;(void)c;}
static bool sub_ghz_automation_check(const char *p){(void)p;return !blocked;}
static bool automation_sgh_info_mock(const char *p,uint32_t *f,uint8_t *m){(void)p;*f=433920000;*m=0;return true;}
#define automation_sgh_info automation_sgh_info_mock
static void menu_sub_ghz_init(void){}
static int sub_ghz_ring_buffers_init(void){return buffer_fail;}
static int sub_ghz_raw_samples_init(void){return 0;}
static uint8_t sub_ghz_replay_start(bool r,int b,int c,int p){(void)r;(void)b;(void)c;(void)p;++starts;return start_fail==2?129:start_fail?0:64;}
static uint8_t sub_ghz_replay_continue(uint8_t state){(void)state;return mode==2?129:10;}
static void sub_ghz_raw_tx_stop(void){++stops;}
static void sub_ghz_tx_raw_deinit(void){}
static void sub_ghz_raw_samples_deinit(bool discard){assert(!discard);++releases;}
static void sub_ghz_ring_buffers_deinit(void){}
static void sub_ghz_set_opmode(int a,int b,int c,int d){(void)a;(void)b;(void)c;(void)d;}
static void menu_sub_ghz_exit(void){}
"""+ir+'\n'+rf+r"""
static void reset(void){mode=radio=start_fail=buffer_fail=blocked=cancel_after=cancel_calls=starts=stops=releases=0;ticks=0;last_command=0;}
int main(void){
 reset();assert(ir_automation_send("remote.ir",1,cancelled_test)==ACTION_OK);assert(last_command==200&&starts==1&&stops==1&&!s_remote_tx_started);
 reset();assert(ir_automation_send("remote.ir",99,cancelled_test)==ACTION_FAILED);assert(!starts);
 reset();mode=1;assert(ir_automation_send("remote.ir",0,cancelled_test)==ACTION_FAILED);assert(stops==1);
 reset();cancel_after=2;assert(ir_automation_send("remote.ir",0,cancelled_test)==ACTION_CANCELLED);assert(stops==1);
 reset();start_fail=1;assert(ir_automation_send("remote.ir",0,cancelled_test)==ACTION_FAILED);
 reset();radio=1;assert(sub_ghz_automation_send("lamp.sgh",cancelled_test)==ACTION_OK);assert(starts==1&&stops==1&&releases==1&&subghz_custom_freq_hz==315000000&&subghz_scan_config.modulation==2);
 reset();radio=1;blocked=1;assert(sub_ghz_automation_send("lamp.sgh",cancelled_test)==ACTION_FAILED);assert(!starts&&!releases);
 reset();radio=1;buffer_fail=1;assert(sub_ghz_automation_send("lamp.sgh",cancelled_test)==ACTION_FAILED);assert(!starts&&!stops&&releases==1);
 reset();radio=1;start_fail=1;assert(sub_ghz_automation_send("lamp.sgh",cancelled_test)==ACTION_FAILED);assert(!stops&&releases==1);
 reset();radio=1;start_fail=2;assert(sub_ghz_automation_send("lamp.sgh",cancelled_test)==ACTION_FAILED);assert(stops==1&&releases==1);
 reset();radio=1;mode=1;assert(sub_ghz_automation_send("lamp.sgh",cancelled_test)==ACTION_FAILED);assert(stops==1&&releases==1);
 reset();radio=1;mode=2;assert(sub_ghz_automation_send("lamp.sgh",cancelled_test)==ACTION_FAILED);assert(stops==1);
 reset();radio=1;cancel_after=2;assert(sub_ghz_automation_send("lamp.sgh",cancelled_test)==ACTION_CANCELLED);assert(stops==1&&releases==1);
 puts("PASS: actual IR adapter sends only the selected button; actual RF adapter performs one bounded burst; both clean up on completion, cancellation, start failure, allocation failure, parser failure and timeout; RF configuration is restored");return 0;
}
"""
(out/'automation-adapters.c').write_text(adapters)
exe=compile('automation-adapters',[out/'automation-adapters.c']);print(run([exe]))


ui=(src/'m1_automation.c').read_text(encoding='utf-8')
pieces=[]
for signature in ['static const uint8_t *step_icon(', 'static void list_draw(', 'static void progress(']:
 start=ui.index(signature);end=ui.index('{',start)+1;depth=1
 while depth:
  if ui[end]=='{':depth+=1
  if ui[end]=='}':depth-=1
  end+=1
 pieces.append(ui[start:end])
icons='\n'.join(m[0] for m in re.finditer(r'static const uint8_t icon_.*?;',ui))
screen=r"""#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m1_automation.h"
typedef int u8g2_t;
static u8g2_t m1_u8g2;
static automation_t chain;
static char rows[66][32];
static const uint8_t button_back_8x8[8]={0},button_ok_8x8[8]={0};
#define M1_DISP_FUNC_MENU_FONT_N 0
#define TEXT_ALIGN_LEFT 0
static void m1_u8g2_firstpage(void){}
static void m1_u8g2_nextpage(void){}
static void bounds(int x,int y,int w,int h){assert(x>=0&&y>=0&&w>=0&&h>=0&&x+w<=128&&y+h<=64);}
static void m1_draw_header_bar(u8g2_t *d,const char *t,const char *b){(void)d;assert(t&&b);}
static void u8g2_SetFont(u8g2_t *d,int f){(void)d;(void)f;}
static void u8g2_SetDrawColor(u8g2_t *d,int c){(void)d;(void)c;}
static void m1_ui_selection(u8g2_t *d,int x,int y,int w,int h,bool s){(void)d;(void)s;bounds(x,y,w,h);}
static void u8g2_DrawXBMP(u8g2_t *d,int x,int y,int w,int h,const uint8_t *b){(void)d;assert(b);bounds(x,y,w,h);}
static void m1_draw_text(u8g2_t *d,int x,int y,int w,const char *t,int a){(void)d;(void)a;assert(t);bounds(x,y-7,w,8);}
static void u8g2_DrawRFrame(u8g2_t *d,int x,int y,int w,int h,int r){(void)d;(void)r;bounds(x,y,w,h);}
static void u8g2_DrawBox(u8g2_t *d,int x,int y,int w,int h){(void)d;bounds(x,y,w,h);}
static void m1_draw_bottom_bar(u8g2_t *d,const uint8_t *a,const char *b,const char *c,const uint8_t *e){(void)d;(void)a;(void)b;(void)c;(void)e;}
"""+icons+'\n'+'\n'.join(pieces)+r"""
int main(void){
 strcpy(chain.name,"Movie Night");
 for(unsigned n=0;n<=16;++n){chain.count=n;for(unsigned i=0;i<n;++i){chain.steps[i].type=i%3+1;chain.steps[i].wait_ms=60000;strcpy(chain.steps[i].label,"A deliberately long action label");}
  unsigned choices=n+(n<16);for(unsigned i=0;i<choices;++i)memset(rows[i],'W',31);
  for(unsigned selected=0;selected<choices;++selected)list_draw(chain.name,selected,choices,true);
  for(unsigned i=0;i<n;++i){progress(i,"Running",0);progress(i,"Waiting",100);progress(i,"Checking",999);}
 }
 for(unsigned n=1;n<=65;++n)for(unsigned i=0;i<n;++i)list_draw("Files",i,n,false);
 puts("PASS: actual automation lists and progress screens stay in 128x64 bounds for all 0..16-step chains, 1..65-choice lists, long labels and progress extremes");return 0;
}
"""
(out/'automation-ui.c').write_text(screen)
exe=compile('automation-ui',[out/'automation-ui.c']);print(run([exe]))

(out/'test-results.txt').write_text('\n'.join(log))
print('ALL CONTROL/IR HOST TESTS PASSED')
