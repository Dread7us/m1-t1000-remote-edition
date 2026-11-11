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
automation_api=code[code.index('uint16_t ir_automation_buttons('):code.index('m1_action_result_t ir_automation_send(')]
harness='''#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include "m1_ir_universal.h"
#include "flipper_file.h"
#include "m1_ir_remote_view.h"
#include "m1_kaseikyo_fields.h"
#include "flipper_ir.h"
static ir_universal_cmd_t s_commands[IR_UNIVERSAL_MAX_CMDS];
static uint16_t s_cmd_count;
'''+ '\n'.join(functions)+'\n'+automation_api+r'''
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
 char button[32];
 s_cmd_count=0;assert(ir_automation_buttons(argv[1])==64);
 assert(ir_automation_button(0,button,sizeof(button)) && !strcmp(button,"Button0"));
 assert(ir_automation_button(63,button,sizeof(button)) && !strcmp(button,"Button63"));
 assert(ir_automation_buttons(argv[3])==25);
 assert(!ir_automation_button(25,button,sizeof(button))); /* no stale larger count */
 assert(!ir_automation_buttons("missing.ir"));assert(!ir_automation_button(0,button,sizeof(button)));
 assert(ir_automation_buttons("Epson.ir")==2);
 assert(ir_automation_button(0,button,sizeof(button)) && !strcmp(button,"Power"));
 assert(ir_automation_button(0,button,sizeof(button))); /* repeated choice is legal */
 assert(ir_automation_buttons("Denon.ir")==1);
 assert(s_commands[0].address==0x3254 && s_commands[0].command==0x4005 && s_commands[0].flags==0x10);
 flipper_file_t denon;flipper_ir_signal_t signal;
 assert(flipper_ir_open(&denon,"Denon.ir"));assert(flipper_ir_read_signal(&denon,&signal));ff_close(&denon);
 assert(signal.parsed.address==0x3254 && signal.parsed.command==0x4005 && signal.parsed.flags==0x10);
 puts("PASS: actual universal and streaming parsers preserve Denon Kaseikyo manufacturer, genre, id and command fields");
 puts("PASS: actual automation picker API works cold, changes remotes, clears missing files and permits repeated Epson Power selection");
 puts("PASS: actual universal parser loads all 64 commands, rejects missing/invalid fields and empty raw data, disambiguates repeated raw names");
 return 0;
}
'''
(out/'parser.c').write_text(harness)
header='Filetype: IR signals file\nVersion: 1\n#\n'
(out/'Denon.ir').write_text(header+'name: Power\ntype: parsed\nprotocol: Kaseikyo\naddress: 41 54 32 00\ncommand: 05 00 00 00\n#\n')
(out/'Epson.ir').write_text(header+'name: Power\ntype: parsed\nprotocol: NECext\naddress: 83 55 00 00\ncommand: 90 6F 00 00\n#\nname: Menu\ntype: parsed\nprotocol: NECext\naddress: 83 55 00 00\ncommand: 9A 65 00 00\n#\n')
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
exe=compile('parser',[out/'parser.c',*common])
print(run([exe,out/'many.ir',out/'malformed.ir',repo/'ir_database/TV/Samsung.ir']))
# Run the actual render functions with drawing-bound assertions. The mocked
# font widths are conservative (tiny=4 px, regular=5 px per character).
geometry=(repo/'tools/remote_edition/geometry_prelude.h').read_text()+'\nstatic bool ir_ota_tx_fault;\n'
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
  s_remote_tx_started=(bool)tx;ir_ota_tx_fault=false;
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
 m1_ui.style=2;m1_ui.outline=2;m1_ui.clock=0;m1_ui.wrap=0;appearance_save();assert(!failures);
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
#define m1_ui_capture_text(...) ((void)0)
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
static void u8g2_DrawHLine(u8g2_t *d,int x,int y,int w){(void)d;bounds(x,y,w,1);}
"""+'\n'.join(functions)+r"""
int main(void){
 u8g2_t screen=0;char long_label[300];memset(long_label,'W',sizeof(long_label)-1);long_label[sizeof(long_label)-1]=0;
 for(int width=0;width<=124;++width)for(int align=0;align<3;++align){
  left=2;right=2+width;m1_draw_text(&screen,left,30,width,long_label,align);
  m1_draw_text(&screen,left,30,width,"OK",align);m1_draw_text(&screen,left,30,width,NULL,align);
 }
 for(int style=0;style<3;++style)for(int outline=0;outline<3;++outline){
  m1_ui.style=style;m1_ui.outline=outline;
  m1_ui_selection(&screen,2,15,124,11,true);assert(color==(outline?1:0));
  m1_ui_selection(&screen,2,27,124,11,false);assert(color==1);
  m1_draw_content_frame(&screen,2,14,124,35);
 }
 assert(calls>0);puts("PASS: actual theme primitives and long-label ellipsis stay in bounds at every width/alignment and all nine style/selection combinations");return 0;
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
 reset();chain.steps[1]=chain.steps[0];execute();assert(!strcmp(sent,"IIS")); /* repeated commands must not be deduplicated */
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
static bool s_remote_tx_started,s_automation_tx,ir_ota_tx_fault;
static char s_raw_tx_filepath[256];static int s_commands[]={100,200,300},last_command;
static TickType_t xTaskGetTickCount(void){return ticks;}
static int xQueueReceive(int q,S_M1_Main_Q_t *e,unsigned timeout){(void)q;ticks+=timeout;if(mode==1)return 0;e->q_evt_type=radio?Q_EVENT_SUBGHZ_TX:Q_EVENT_IRRED_TX;return 1;}
static bool cancelled_test(void){return cancel_after && ++cancel_calls>=cancel_after;}
static unsigned ir_automation_buttons(const char *p){(void)p;return 3;}
static bool ir_automation_button(unsigned i,char *n,size_t c){snprintf(n,c,"button");return i<3;}
static void transmit_command(const int *p){last_command=*p;++starts;s_remote_tx_started=start_fail!=1;ir_ota_tx_fault=start_fail==2;}
static void remote_tx_stop(void){s_remote_tx_started=false;++stops;}
static bool remote_tx_finished(void){return mode!=1;}
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
static void reset(void){ir_ota_tx_fault=false;mode=radio=start_fail=buffer_fail=blocked=cancel_after=cancel_calls=starts=stops=releases=0;ticks=0;last_command=0;}
int main(void){
 reset();assert(ir_automation_send("remote.ir",1,cancelled_test)==ACTION_OK);assert(last_command==200&&starts==1&&stops==1&&!s_remote_tx_started);
 reset();assert(ir_automation_send("remote.ir",99,cancelled_test)==ACTION_FAILED);assert(!starts);
 reset();mode=1;assert(ir_automation_send("remote.ir",0,cancelled_test)==ACTION_FAILED);assert(stops==1);
 reset();cancel_after=2;assert(ir_automation_send("remote.ir",0,cancelled_test)==ACTION_CANCELLED);assert(stops==1);
 reset();start_fail=1;assert(ir_automation_send("remote.ir",0,cancelled_test)==ACTION_FAILED);
 reset();start_fail=2;assert(ir_automation_send("remote.ir",0,cancelled_test)==ACTION_FAILED&&stops==1);
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
static void m1_draw_marquee(u8g2_t *d,int x,int y,int w,const char *t){m1_draw_text(d,x,y,w,t,0);}
static void u8g2_DrawRFrame(u8g2_t *d,int x,int y,int w,int h,int r){(void)d;(void)r;bounds(x,y,w,h);}
static void u8g2_DrawBox(u8g2_t *d,int x,int y,int w,int h){(void)d;bounds(x,y,w,h);}
static void m1_draw_bottom_bar(u8g2_t *d,const uint8_t *a,const char *b,const char *c,const uint8_t *e){(void)d;(void)a;(void)b;(void)c;(void)e;}
"""+icons+'\n'+'\n'.join(pieces)+r"""
int main(void){
 strcpy(chain.name,"Movie Night");
 for(unsigned n=0;n<=16;++n){chain.count=n;for(unsigned i=0;i<n;++i){chain.steps[i].type=i%3+1;chain.steps[i].wait_ms=60000;memset(chain.steps[i].label,'W',sizeof(chain.steps[i].label)-1);chain.steps[i].label[sizeof(chain.steps[i].label)-1]=0;}
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


def extract_body(source,signature):
 start=source.index(signature);end=source.index('{',start)+1;depth=1
 while depth:
  if source[end]=='{':depth+=1
  if source[end]=='}':depth-=1
  end+=1
 return source[start:end]

# Exercise the exact interrupt TX branch with guard values and every buffer size.
interrupt=(src/'m1_int_hdl.c').read_text()
start=interrupt.index('if (htim == &Timerhdl_IrTx )');end=interrupt.index('\n\telse if (htim == &Timerhdl_IrRx )',start)
irq=r"""#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include <stdio.h>
typedef struct {unsigned ARR;} REG;
typedef struct {REG *Instance;} TIM_HandleTypeDef;
typedef struct {struct {unsigned ir_tx_data;} q_data;unsigned q_evt_type;} S_M1_Main_Q_t;
typedef int portBASE_TYPE;
#define pdFALSE 0
#define FALSE 0
#define IR_OTA_PULSE_BIT_MASK 1
#define Q_EVENT_IRRED_TX 1
#define portYIELD_FROM_ISR(x) ((void)(x))
static REG reg;static TIM_HandleTypeDef Timerhdl_IrTx={&reg};
static bool ir_ota_data_tx_active;static unsigned ir_ota_data_tx_counter,ir_ota_data_tx_len;
static uint16_t *pir_ota_data_tx_buffer;static unsigned complete,stopped;
static int main_q_hdl;
static bool ir_ota_tx_fault, fail_carrier;
static bool irsnd_on(void){return !fail_carrier;}
static void irsnd_off(void){}
static void disable(TIM_HandleTypeDef *h){(void)h;++stopped;}
#define __HAL_TIM_DISABLE(h) disable(h)
static int xQueueSendFromISR(int q,S_M1_Main_Q_t *e,int *w){(void)q;(void)w;assert(e->q_evt_type==1);++complete;return 1;}
static void transmit_irq(TIM_HandleTypeDef *htim){uint32_t cap_val;S_M1_Main_Q_t q_item;portBASE_TYPE xHigherPriorityTaskWoken=0;
"""+interrupt[start:end]+r"""
}
int main(void){
 uint16_t buffer[513];
 for(unsigned n=1;n<=512;++n){for(unsigned i=0;i<n;++i)buffer[i]=(uint16_t)(100+i*2+(i%2));buffer[n]=65531;
  ir_ota_data_tx_len=n;ir_ota_data_tx_counter=1;ir_ota_data_tx_active=true;pir_ota_data_tx_buffer=buffer;
  reg.ARR=buffer[n>1?1:0];complete=stopped=0;
  unsigned callbacks=0;while(ir_ota_data_tx_active){assert(++callbacks<=n);transmit_irq(&Timerhdl_IrTx);assert(reg.ARR!=65531);}
  assert(callbacks==n&&complete==1&&stopped==1);
 }
 ir_ota_data_tx_len=2;ir_ota_data_tx_counter=1;ir_ota_data_tx_active=true;
 buffer[0]=100;reg.ARR=101;fail_carrier=true;transmit_irq(&Timerhdl_IrTx);
 assert(ir_ota_tx_fault&&!ir_ota_data_tx_active);
 puts("PASS: actual IR interrupt handles 1..512 samples with no one-past-end prefetch, exactly one completion, and timer stop");return 0;
}
"""
(out/'ir-irq.c').write_text(irq);exe=compile('ir-irq',[out/'ir-irq.c']);print(run([exe]))

text_source=(src/'m1_text.c').read_text()
text_functions='\n'.join(extract_body(text_source,name) for name in ['void m1_text_reset_scroll(', 'void m1_draw_marquee(', 'static size_t next_line(', 'static unsigned page_count('])
text_test=r"""#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include <assert.h>
typedef int u8g2_t;
#define TEXT_SLOTS 8
#define TEXT_LENGTH 512
#define TEXT_ALIGN_LEFT 0
static uint32_t scroll_start,ticks;static int left,right,seen_start,seen_end;
static uint32_t HAL_GetTick(void){return ticks;}
static unsigned u8g2_GetStrWidth(u8g2_t *d,const char *s){(void)d;return (unsigned)strlen(s)*5;}
static void u8g2_DrawStr(u8g2_t *d,int x,int y,const char *s){(void)d;(void)y;assert(x>=left&&x+(int)strlen(s)*5<=right);if(!strncmp(s,"START",5))seen_start=1;if(strstr(s,"END"))seen_end=1;}
static void m1_draw_text(u8g2_t *d,int x,int y,int w,const char *s,int a){(void)w;(void)a;u8g2_DrawStr(d,x,y,s);}
"""+text_functions+r"""
int main(void){
 u8g2_t screen=0;char text[500];strcpy(text,"START ");for(int i=6;i<480;++i)text[i]='A'+i%26;strcpy(text+480," END");
 left=3;right=63;m1_text_reset_scroll();
 for(ticks=0;ticks<180000;ticks+=120)m1_draw_marquee(&screen,3,22,60,text);
 assert(seen_start&&seen_end);
 char line[128],joined[600];joined[0]=0;size_t position=0;
 do {line[0]=0;position=next_line(&screen,text,position,line,60);assert(strlen(line)<=12);strcat(joined,line);}while(text[position]);
 assert(!strcmp(joined,text));assert(page_count(&screen,text)>1);
 assert(page_count(&screen,"")==1);position=next_line(&screen,"first\nsecond",0,line,118);assert(position==6&&!strcmp(line,"first"));
 puts("PASS: actual scrolling exposes both ends of long text in bounds; wrapped pages preserve all source characters");return 0;
}
"""
(out/'readable-text.c').write_text(text_test);exe=compile('readable-text',[out/'readable-text.c']);print(run([exe]))


infrared=(src/'m1_infrared.c').read_text()
machine=extract_body(infrared,'S_M1_IR_Tx_States infrared_transmit(uint8_t init)\n{')
machine_test=r"""#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <assert.h>
typedef enum {IR_TX_INIT,IR_TX_ACTIVE,IR_TX_POST_PROCESS,IR_TX_DELAY,IR_TX_COMPLETED} S_M1_IR_Tx_States;
typedef int BaseType_t;typedef int TimerHandle_t;
typedef struct {unsigned ARR,SR;} Registers;
typedef struct {Registers *Instance;} Timer;
static Registers regs;static Timer Timerhdl_IrTx={&regs};
typedef uint32_t TickType_t;
static unsigned stage,post;static TickType_t ticks,ir_tx_delay_start,ir_tx_delay_ticks;
static bool ir_ota_data_tx_active,ir_ota_tx_fault,critical,fail_carrier;
static unsigned active_arr;
static TickType_t xTaskGetTickCount(void){return ticks;}
static uint16_t ir_ota_data_tx_counter,ir_ota_data_tx_len,*pir_ota_data_tx_buffer;
static uint16_t samples[]={101,200,101};
#define TRUE 1
#define pdFAIL 0
#define pdFALSE 0
#define pdPASS 1
#define NULL 0
#define pdMS_TO_TICKS(x) (x)
#define TIM_EVENTSOURCE_UPDATE 1
#define TIM_FLAG_UPDATE 1
#define __HAL_TIM_URS_ENABLE(h) ((void)(h))
#define __HAL_TIM_URS_DISABLE(h) ((void)(h))
#define IR_OTA_PULSE_BIT_MASK 1
#define taskENTER_CRITICAL() (critical=true)
#define taskEXIT_CRITICAL() (critical=false)
#define __HAL_TIM_CLEAR_FLAG(h,f) ((h)->Instance->SR &= ~(f))
#define __HAL_TIM_ENABLE(h) assert(critical && ir_ota_data_tx_active && ir_ota_data_tx_counter==1 && active_arr==pir_ota_data_tx_buffer[0] && (h)->Instance->ARR==pir_ota_data_tx_buffer[ir_ota_data_tx_len>1?1:0])
#define __HAL_TIM_DISABLE(h) ((void)(h))
#define HAL_IS_BIT_SET(a,b) ((a)&(b))
#define CLEAR_BIT(a,b) ((a)&=~(b))
static unsigned m1_make_ir_ota_multiframes(void){unsigned values[]={1,2000,1,0};assert(stage<4);return values[stage++];}
static unsigned m1_check_ir_ota_frame_status(void){return 1;}
static unsigned m1_get_ir_ota_frame_len(void){return 3;}
static uint16_t *m1_get_ir_ota_buffer_ptr(void){return samples;}
static void HAL_TIM_GenerateEvent(Timer *h,int e){(void)e;assert(critical);active_arr=h->Instance->ARR;}
static bool irsnd_on(void){assert(critical&&!ir_ota_data_tx_active);return !fail_carrier;}
static void irsnd_off(void){}
static uint8_t irsnd_tx_protocol(void){return 0x44;}
static void m1_ir_ota_frame_post_process(unsigned protocol){assert(protocol==0x44);++post;}
"""+extract_body(infrared,'bool infrared_start_frame(uint16_t *samples, uint16_t count)')+'\n'+machine+r"""
int main(void){for(unsigned command=0;command<1000;++command){stage=post=0;ticks=command%2?UINT32_MAX-1:100;ir_ota_data_tx_active=false;
 infrared_transmit(1);assert(infrared_transmit(0)==IR_TX_ACTIVE&&ir_ota_data_tx_active);
 ir_ota_data_tx_active=false;assert(infrared_transmit(0)==IR_TX_DELAY&&post==1);
 for(int i=0;i<20;++i)assert(infrared_transmit(0)==IR_TX_DELAY&&stage==2);
 ticks++;assert(infrared_transmit(0)==IR_TX_DELAY);
 ticks++;assert(infrared_transmit(0)==IR_TX_ACTIVE);
 ir_ota_data_tx_active=false;assert(infrared_transmit(0)==IR_TX_COMPLETED&&post==2&&stage==4);
 }fail_carrier=true;assert(!infrared_start_frame(samples,1)&&ir_ota_tx_fault&&!ir_ota_data_tx_active&&!critical);
 ir_ota_tx_fault=false;assert(!infrared_start_frame(samples,0)&&ir_ota_tx_fault);
 puts("PASS: actual parsed IR state machine completes both frames and gap without timer allocation or queue notifications across 1000 commands, including tick rollover");return 0;}

"""
(out/'ir-frames.c').write_text(machine_test);exe=compile('ir-frames',[out/'ir-frames.c']);print(run([exe]))

encoder=(repo/'Infrared/irsnd.c').read_text()
carrier=extract_body(encoder,'static void irsnd_set_freq(IRSND_FREQ_TYPE freq)\n{')
carrier_test=r"""#include <stdint.h>
#include <assert.h>
#include <stdio.h>
typedef uint32_t IRSND_FREQ_TYPE;
typedef struct {uint32_t Prescaler,CounterMode,Period,ClockDivision,AutoReloadPreload;} Init;
typedef struct {Init Init;} Timer;
typedef struct {uint32_t MasterOutputTrigger,MasterSlaveMode,MasterOutputTrigger2;} TIM_MasterConfigTypeDef;
typedef struct {uint32_t OCMode,Pulse,OCPolarity,OCNPolarity,OCFastMode,OCIdleState,OCNIdleState;} TIM_OC_InitTypeDef;
static Timer timer;static Timer *pir_timhdl_carrier=&timer;static uint32_t ir_carrier_pwm_channel=4,configs;
#define IR_ENCODE_CARRIER_PRESCALE_FACTOR 10
#define TIM_COUNTERMODE_UP 0
#define TIM_CLOCKDIVISION_DIV1 0
#define TIM_AUTORELOAD_PRELOAD_DISABLE 0
#define TIM_TRGO_RESET 0
#define TIM_MASTERSLAVEMODE_DISABLE 0
#define TIM_OCMODE_PWM1 1
#define TIM_OCPOLARITY_HIGH 0
#define TIM_OCNPOLARITY_HIGH 0
#define TIM_OCFAST_DISABLE 0
#define TIM_OCIDLESTATE_RESET 0
#define TIM_OCNIDLESTATE_RESET 0
#define HAL_OK 0
static unsigned HAL_RCC_GetPCLK2Freq(void){return 200000000;}
static void Error_Handler(void){assert(0);}
static int HAL_TIM_PWM_Init(Timer *t){assert(t->Init.Period==9);return 0;}
static int HAL_TIMEx_MasterConfigSynchronization(Timer *t,TIM_MasterConfigTypeDef *c){(void)t;assert(c->MasterOutputTrigger==0&&c->MasterSlaveMode==0&&c->MasterOutputTrigger2==0);return 0;}
static int HAL_TIM_PWM_ConfigChannel(Timer *t,TIM_OC_InitTypeDef *c,unsigned channel){(void)t;assert(channel==4&&c->OCPolarity==0&&c->OCIdleState==0&&c->OCNPolarity==0&&c->OCNIdleState==0&&c->Pulse==4);++configs;return 0;}
"""+carrier+r"""
int main(void){for(unsigned i=0;i<1000;++i){irsnd_set_freq(i%2?38000:56000);}assert(configs==1000);puts("PASS: actual carrier setup fully specifies polarity, idle states and master settings across 1000 alternating frequencies");return 0;}
"""
(out/'ir-carrier.c').write_text(carrier_test);exe=compile('ir-carrier',[out/'ir-carrier.c']);print(run([exe]))

# Compile the complete encoder with only the STM32 peripheral calls shimmed.
# In particular, validate the Denon continuation frame at the offset where the
# old length calculation sent unwritten timing entries.
full_encoder=(repo/'tools/remote_edition/encoder_prelude.h').read_text()
full_encoder+='\n'+f'#include "{(repo/"Infrared/irsndconfig.h").as_posix()}"\n#include "{(repo/"Infrared/irmpprotocols.h").as_posix()}"\n'
full_encoder+=encoder.replace('#include "irsnd.h"','',1)
full_encoder+=r"""
int main(void)
{
    TIM_HandleTypeDef timer = {0};
    TIM_TypeDef registers = {0};
    timer.Instance = &registers;
    irsnd_init(&timer, 12);
    assert(!irsnd_set_carrier_freq(0) && !irsnd_set_carrier_freq(UINT32_MAX));
    assert(irsnd_set_carrier_freq(38000));
    assert(irsnd_on() && (registers.CCER & (TIM_CCER_CC1NE << 12)));
    irsnd_off();
    carrier_failure=1; assert(!irsnd_on());
    carrier_failure=2; assert(!irsnd_on());
    carrier_failure=0;

    const uint8_t protocols[] = { IRMP_NEC_PROTOCOL, IRMP_NEC42_PROTOCOL,
        IRMP_SAMSUNG32_PROTOCOL, IRMP_SIRCS_PROTOCOL, IRMP_KASEIKYO_PROTOCOL,
        IRMP_DENON_PROTOCOL, IRMP_RC5_PROTOCOL, IRMP_RC6_PROTOCOL,
        IRMP_JVC_PROTOCOL, IRMP_LGAIR_PROTOCOL, IRMP_BOSE_PROTOCOL,
        IRMP_MATSUSHITA_PROTOCOL };
    for (unsigned rep=0; rep<50; ++rep) {
        for (unsigned p=0; p<sizeof(protocols); ++p) {
            irsnd_init(&timer, 12);
            IRMP_DATA data = {protocols[p], 0x1234, 0x25, 0};
            assert(irsnd_generate_tx_data(data));
            unsigned frames=0, steps=0;
            for (uint16_t next; (next=m1_make_ir_ota_multiframes()) != 0;) {
                assert(++steps<40);
                if (next>1) continue;
                assert(m1_check_ir_ota_frame_status());
                unsigned count=m1_get_ir_ota_frame_len();
                assert(count && count<=IR_TX_OTA_BITS_FRAME_LEN_MAX);
                bool has_mark=false;
                for (unsigned i=0;i<count;++i) {
                    uint16_t sample=m1_get_ir_ota_buffer_ptr()[i];
                    if (sample<=1) fprintf(stderr,"protocol=%u frame=%u entry=%u count=%u sample=%u\n",protocols[p],frames,i,count,sample);
                    assert(sample>1);
                    has_mark |= (sample & IR_OTA_PULSE_BIT_MASK) != 0;
                }
                assert(has_mark);
                if (protocols[p]==IRMP_DENON_PROTOCOL) assert(count==32);
                ++frames;
                m1_ir_ota_frame_post_process(irsnd_tx_protocol());
                memset(m1_get_ir_ota_buffer_ptr(),0,sizeof(ir_ota_pkt_buffer));
            }
            assert(frames>0);
            if (protocols[p]==IRMP_DENON_PROTOCOL) assert(frames==2);
        }
    }
    puts("PASS: full encoder generates initialized timing entries for 600 sends across 12 protocols, both Denon frames, and detects carrier-enable failures");
    return 0;
}
"""
(out/'ir-encoder.c').write_text(full_encoder);exe=compile('ir-encoder',[out/'ir-encoder.c']);print(run([exe]))



encoder=(repo/'Infrared/irsnd.c').read_text()
case=extract_body(encoder,'case IRMP_KASEIKYO_PROTOCOL:')
golden=r"""#include <stdint.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m1_kaseikyo_fields.h"
#define IRMP_KASEIKYO_PROTOCOL 1
#define KASEIKYO_ADDRESS_LEN 16
#define KASEIKYO_COMMAND_LEN 12
#define IRSND_REPETITION_MASK 15
#define TRUE 1
typedef struct {uint16_t address,command;uint8_t flags,protocol;} IRMP_DATA;
static uint8_t ir_tx_buffer[6],ir_tx_active;
static uint16_t bitsreverse(uint16_t x,unsigned n){uint16_t r=0;while(n--){r=(uint16_t)((r<<1)|(x&1));x>>=1;}return r;}
static void encode(IRMP_DATA irmp_data){uint16_t address,command;switch(irmp_data.protocol){
"""+case+r"""
}}
int main(void){IRMP_DATA data={0};data.protocol=1;m1_kaseikyo_decode(0x00325441,5,&data.address,&data.command,&data.flags);encode(data);
 const uint8_t expected[]={0x2a,0x4c,0x02,0x8a,0x00,0x88};assert(!memcmp(ir_tx_buffer,expected,6)&&ir_tx_active);
 for(unsigned id=0;id<4;++id)for(unsigned g1=0;g1<16;++g1)for(unsigned g2=0;g2<16;++g2){uint32_t address=(id<<24)|(0x3254u<<8)|(g1<<4)|g2;m1_kaseikyo_decode(address,0x315,&data.address,&data.command,&data.flags);assert(m1_kaseikyo_address(data.address,data.command,data.flags)==address);assert((data.command&0x3ff)==0x315);}
 puts("PASS: actual Kaseikyo encoder matches the Denon wire-byte golden vector; all id/genre combinations roundtrip without truncation");return 0;}
"""
(out/'kaseikyo.c').write_text(golden);exe=compile('kaseikyo',[out/'kaseikyo.c']);print(run([exe]))

exe=compile('long-ir',[shim/'test_long_ir.c',*common]);print(run([exe]))

header_source=(src/'m1_display.c').read_text(encoding='utf-8')
home_header=extract_body(header_source,'static void m1_gui_draw_menu_header(const char *title, uint8_t sel_item, uint8_t num_items)\n{')
header_test=r"""#include <assert.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "m1_ui_theme.h"
typedef int u8g2_t;
typedef struct {unsigned battery_level;} S_M1_Power_Status_t;
typedef struct {unsigned hour,minute;} m1_time_t;
m1_ui_preferences_t m1_ui=M1_UI_DEFAULTS;
static u8g2_t m1_u8g2;static unsigned menu_level_id,battery=100,hour=23,minute=59;static int signal=-42,sd=0,brand_seen;static bool rtc_ok=true;static char shown_clock[16];
static struct {int left,right;} spans[8];static unsigned span_count;
static uint8_t sd_card_10x10[20];
#define M1_LCD_DISPLAY_WIDTH 128
#define MENU_HEADER_HEIGHT 12
#define M1_DISP_DRAW_COLOR_BG 0
#define M1_DISP_DRAW_COLOR_TXT 1
#define M1_DISP_FUNC_MENU_FONT_N 0
#define TEXT_ALIGN_LEFT 0
#define TEXT_ALIGN_CENTER 1
#define TEXT_ALIGN_RIGHT 2
#define SD_access_OK 0
static void battery_power_status_get(S_M1_Power_Status_t *p){p->battery_level=battery;}
static bool m1_get_localtime_checked(m1_time_t *p){p->hour=hour;p->minute=minute;return rtc_ok;}
static int wifi_get_rssi(void){return signal;}
static int m1_sdcard_get_status(void){return sd;}
static void u8g2_SetDrawColor(u8g2_t *d,int c){(void)d;(void)c;}
static void u8g2_SetFont(u8g2_t *d,int f){(void)d;(void)f;}
static void bounds(int x,int y,int w,int h){assert(x>=0&&y>=0&&x+w<=128&&y+h<=64);}
static void u8g2_DrawBox(u8g2_t *d,int x,int y,int w,int h){(void)d;bounds(x,y,w,h);}
static void u8g2_DrawFrame(u8g2_t *d,int x,int y,int w,int h){(void)d;bounds(x,y,w,h);}
static void u8g2_DrawHLine(u8g2_t *d,int x,int y,int w){(void)d;bounds(x,y,w,1);}
static void u8g2_DrawXBMP(u8g2_t *d,int x,int y,int w,int h,const uint8_t *b){(void)d;(void)b;bounds(x,y,w,h);}
static void text_span(int x,int y,int width,const char *text,int align){int w=(int)strlen(text)*5;assert(w<=width);if(align==1)x+=(width-w)/2;else if(align==2)x+=width-w;bounds(x,y-7,w,8);for(unsigned i=0;i<span_count;++i)assert(x>=spans[i].right||x+w<=spans[i].left);spans[span_count].left=x;spans[span_count++].right=x+w;}
static void m1_draw_text(u8g2_t *d,int x,int y,int width,const char *text,int align){(void)d;if(strchr(text,':'))snprintf(shown_clock,sizeof(shown_clock),"%s",text);text_span(x,y,width,text,align);}
static void m1_draw_marquee(u8g2_t *d,int x,int y,int width,const char *text){(void)d;assert(!strcmp(text,"T-1000"));text_span(x,y,width,text,0);++brand_seen;}
"""+home_header+r"""
int main(void){unsigned cases=0;
 for(unsigned clock=0;clock<2;++clock)for(unsigned bat=0;bat<2;++bat)for(unsigned twelve=0;twelve<2;++twelve)for(unsigned level=0;level<=100;level+=10)for(unsigned h=0;h<24;h+=11)for(unsigned radio=0;radio<2;++radio)for(unsigned card=0;card<2;++card){
  m1_ui.clock=clock;m1_ui.battery=bat;m1_ui.clock_12h=twelve;battery=level;hour=h;signal=radio?-42:0;sd=card;
  span_count=0;brand_seen=0;m1_gui_draw_menu_header("T-1000",0,17);assert(brand_seen==1);++cases;
 }
 m1_ui.clock=1;m1_ui.clock_12h=1;hour=13;minute=1;span_count=0;m1_gui_draw_menu_header("T-1000",0,17);assert(!strcmp(shown_clock,"01:01p"));
 hour=0;minute=0;span_count=0;m1_gui_draw_menu_header("T-1000",0,17);assert(!strcmp(shown_clock,"12:00a"));
 hour=12;span_count=0;m1_gui_draw_menu_header("T-1000",0,17);assert(!strcmp(shown_clock,"12:00p"));
 rtc_ok=false;span_count=0;m1_gui_draw_menu_header("T-1000",0,17);assert(!strcmp(shown_clock,"--:--"));
 printf("PASS: actual home header shows complete T-1000 with non-overlapping clock/battery/status text in %u setting/status combinations; exact 13:01, noon, midnight and RTC failure display\n",cases);return 0;}
"""
(out/'home-header.c').write_text(header_test);exe=compile('home-header',[out/'home-header.c']);print(run([exe]))


clock=(src/'m1_system.c').read_text(encoding='utf-8')
clock_functions='\n'.join(extract_body(clock,signature) for signature in [
 'static uint8_t clock_month_days(', 'static bool clock_valid(', 'static uint8_t clock_weekday(',
 'bool m1_get_datetime_checked(', 'bool m1_shift_datetime(', 'bool m1_get_localtime_checked(',
 'bool m1_set_localtime(', 'bool m1_set_datetime_checked('])
rtc=r"""#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
typedef struct {uint16_t year;uint8_t month,day,hour,minute,second,weekday;} m1_time_t;
typedef struct {uint8_t Hours,Minutes,Seconds,DayLightSaving,StoreOperation;} RTC_TimeTypeDef;
typedef struct {uint8_t WeekDay,Month,Date,Year;} RTC_DateTypeDef;
typedef int HAL_StatusTypeDef;
static int hrtc,read_time_error,read_date_error,write_time_error,write_date_error,date_reads;
static int8_t m1_tz_offset_hours;
static RTC_TimeTypeDef saved_time;static RTC_DateTypeDef saved_date;
#define HAL_OK 0
#define RTC_FORMAT_BIN 0
#define RTC_DAYLIGHTSAVING_NONE 0
#define RTC_STOREOPERATION_RESET 0
static int HAL_RTC_GetTime(int *h,RTC_TimeTypeDef *t,int format){(void)h;(void)format;*t=saved_time;return read_time_error;}
static int HAL_RTC_GetDate(int *h,RTC_DateTypeDef *d,int format){(void)h;(void)format;*d=saved_date;++date_reads;return read_date_error;}
static int HAL_RTC_SetTime(int *h,RTC_TimeTypeDef *t,int format){(void)h;(void)format;if(!write_time_error)saved_time=*t;return write_time_error;}
static int HAL_RTC_SetDate(int *h,RTC_DateTypeDef *d,int format){(void)h;(void)format;if(!write_date_error)saved_date=*d;return write_date_error;}
static void HAL_PWR_EnableBkUpAccess(void){}
bool m1_set_datetime_checked(const m1_time_t *dt);
"""+clock_functions+r"""
static bool same(const m1_time_t *a,const m1_time_t *b){return a->year==b->year&&a->month==b->month&&a->day==b->day&&a->hour==b->hour&&a->minute==b->minute&&a->second==b->second;}
int main(void){
 m1_time_t local={2026,11,5,13,1,0,4},shown;
 for(int offset=-12;offset<=14;++offset){m1_tz_offset_hours=offset;assert(m1_set_localtime(&local));assert(m1_get_localtime_checked(&shown));assert(same(&local,&shown));assert(shown.weekday==4);}
 m1_tz_offset_hours=-8;assert(m1_set_localtime(&local));assert(saved_time.Hours==21&&saved_time.Minutes==1);assert(m1_get_localtime_checked(&shown)&&shown.hour==13&&shown.minute==1);
 m1_time_t dates[]={{2024,3,1,0,5,2,5},{2026,1,1,0,0,0,4},{2026,12,31,23,59,59,4},{2000,3,1,0,0,0,3},{2099,12,30,23,59,59,3}};
 unsigned cases=0;for(unsigned i=0;i<sizeof(dates)/sizeof(dates[0]);++i)for(int offset=-12;offset<=14;++offset){local=dates[i];m1_tz_offset_hours=offset;assert(m1_set_localtime(&local));assert(m1_get_localtime_checked(&shown));assert(same(&local,&shown));++cases;}
 local=(m1_time_t){2024,3,1,0,0,0,5};assert(m1_shift_datetime(&local,-1)&&local.month==2&&local.day==29&&local.hour==23);
 local=(m1_time_t){2026,1,1,0,0,0,4};assert(m1_shift_datetime(&local,-1)&&local.year==2025&&local.month==12&&local.day==31&&local.weekday==3);
 local=(m1_time_t){2026,11,5,13,1,0,4};assert(m1_set_datetime_checked(&local));
 read_time_error=1;date_reads=0;memset(&shown,0xa5,sizeof(shown));assert(!m1_get_localtime_checked(&shown)&&date_reads==1&&shown.year==2000);read_time_error=0;
 read_date_error=1;assert(!m1_get_localtime_checked(&shown));read_date_error=0;
 saved_date.Month=0;assert(!m1_get_localtime_checked(&shown));assert(!m1_get_localtime_checked(NULL));
 write_time_error=1;assert(!m1_set_localtime(&local));write_time_error=0;write_date_error=1;assert(!m1_set_localtime(&local));write_date_error=0;
 local.month=13;assert(!m1_set_localtime(&local));assert(!m1_set_datetime_checked(NULL));
 printf("PASS: actual local/UTC RTC APIs roundtrip 13:01 across all offsets and %u boundary cases; leap days, weekdays, failed reads/writes and invalid data are checked\n",cases);
 return 0;
}
"""
(out/'local-clock.c').write_text(rtc);exe=compile('local-clock',[out/'local-clock.c']);print(run([exe]))

# Exercise the exact Set Time workflow with mocked keys, not only conversion helpers.
settings=(src/'m1_settings.c').read_text()
workflow=extract_body(settings,'void settings_set_time(void)\n{')
workflow_test=r"""#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include "m1_ui_theme.h"
m1_ui_preferences_t m1_ui=M1_UI_DEFAULTS;
typedef struct {uint16_t year;uint8_t month,day,hour,minute,second,weekday;} m1_time_t;
typedef int BaseType_t;
typedef struct {int q_evt_type;} S_M1_Main_Q_t;
typedef struct {int event[7];} S_M1_Buttons_Status;
#define pdTRUE 1
#define portMAX_DELAY 999
#define Q_EVENT_KEYPAD 1
#define BUTTON_EVENT_CLICK 1
#define BUTTON_BACK_KP_ID 0
#define BUTTON_OK_KP_ID 1
#define BUTTON_UP_KP_ID 2
#define BUTTON_DOWN_KP_ID 3
#define BUTTON_LEFT_KP_ID 4
#define BUTTON_RIGHT_KP_ID 5
static int main_q_hdl,button_events_q_hdl=1,m1_u8g2,read_local,wrote_local,fail_write,keys;
static int script[16],script_count,choice_result=1,confirms,saved_hour;static char message[40];
static m1_time_t local={2026,11,5,13,1,0,4};
static void m1_get_localtime(m1_time_t *d){*d=local;++read_local;}
static bool m1_set_localtime(const m1_time_t *d){assert(d->hour<24&&d->minute==1);saved_hour=d->hour;++wrote_local;return !fail_write;}
static void settings_set_time_draw(const m1_time_t *d,unsigned f){(void)f;assert(d->hour==13 || d->hour==14);}
static unsigned settings_dim(unsigned y,unsigned m){(void)y;(void)m;return 30;}
static void m1_message_box(int *d,const char *a,const char *b,const char *c,const char *e){(void)d;(void)b;(void)c;(void)e;snprintf(message,sizeof(message),"%s",a);fail_write=0;}
static int m1_message_box_choice(int *d,const char *a,const char *b,const char *c,const char *e){(void)d;(void)a;(void)b;(void)c;(void)e;++confirms;return choice_result;}
static void m1_buzzer_notification(void){}
static void xQueueReset(int q){(void)q;}
static int xQueueReceive(int q,void *p,unsigned timeout){(void)timeout;if(q){S_M1_Buttons_Status *b=p;memset(b,0,sizeof(*b));int k=script_count?script[keys++]:keys++?BUTTON_BACK_KP_ID:BUTTON_OK_KP_ID;b->event[k]=1;}else ((S_M1_Main_Q_t *)p)->q_evt_type=1;return 1;}
"""+workflow+r"""
int main(void){settings_set_time();assert(read_local==1&&wrote_local==1&&!strcmp(message,"Time saved"));keys=0;fail_write=1;settings_set_time();assert(read_local==2&&wrote_local==2&&!strcmp(message,"Clock not saved"));
 script_count=5;script[0]=script[1]=script[2]=BUTTON_RIGHT_KP_ID;script[3]=BUTTON_UP_KP_ID;script[4]=BUTTON_BACK_KP_ID;
 keys=0;settings_set_time();assert(wrote_local==3&&saved_hour==14&&confirms==1&&!strcmp(message,"Time saved"));
 keys=0;choice_result=2;settings_set_time();assert(wrote_local==3&&confirms==2);
 keys=0;choice_result=0;script_count=6;script[5]=BUTTON_OK_KP_ID;settings_set_time();assert(wrote_local==4&&saved_hour==14&&confirms==3);
 keys=0;script_count=1;script[0]=BUTTON_BACK_KP_ID;settings_set_time();assert(wrote_local==4&&confirms==3);
 puts("PASS: actual Date & Time editor reads/saves local time, confirms success, reports failed saves, and supports Save/Discard/keep-editing on unsaved Back");return 0;}

"""
(out/'clock-editor.c').write_text(workflow_test);exe=compile('clock-editor',[out/'clock-editor.c']);print(run([exe]))


# Exercise the actual persistence routine with short writes, sync/close errors,
# interrupted rename recovery, all brightness levels and pre-load boot gating.
settings=(src/'m1_settings.c').read_text()
def function_body(text, signature):
 start=text.index(signature+'\n{');body=text.index('{',start);end=body+1;depth=1
 while depth:
  depth+=(text[end]=='{')-(text[end]=='}');end+=1
 return text[start:end]
light=(repo/'tools/remote_edition/backlight_prelude.h').read_text()
light+='\n'+function_body(settings,'bool settings_save_to_sd(void)')
light+='\n'+function_body(settings,'void settings_restore_backlight(void)')
light+=r"""
int main(void){
 settings_restore_backlight();assert(applied==-1);s_backlight_ready=true;
 for(unsigned i=0;i<5;i++){m1_brightness_level=i;settings_restore_backlight();assert(applied==s_brightness_values[i]);assert(settings_save_to_sd());char expected[32];snprintf(expected,sizeof(expected),"brightness=%u\n",i);assert(strstr(files[0],expected)==files[0]);}
 char old[1024];strcpy(old,files[0]);m1_brightness_level=1;
 fail_write=1;assert(!settings_save_to_sd()&&!strcmp(files[0],old));fail_write=0;
 fail_sync=1;assert(!settings_save_to_sd()&&!strcmp(files[0],old));fail_sync=0;
 fail_close=1;assert(!settings_save_to_sd()&&!strcmp(files[0],old));fail_close=0;
 fail_install=1;assert(!settings_save_to_sd()&&exists[0]&&!strcmp(files[0],old));fail_install=0;
 assert(!f_rename("settings","settings.bak"));fail_install=1;assert(!settings_save_to_sd()&&exists[2]&&!strcmp(files[2],old));fail_install=0;
 assert(settings_save_to_sd()&&strstr(files[0],"brightness=1\n")==files[0]);
 m1_brightness_level=255;settings_restore_backlight();assert(applied==255);
 puts("PASS: actual brightness restoration respects boot readiness and every level; settings commit checks short writes/sync/close, preserves prior data and recovers interrupted rename");return 0;}
"""
(out/'backlight.c').write_text(light);exe=compile('backlight',[out/'backlight.c']);print(run([exe]))
assert 'm1_backlight_on(M1_BACKLIGHT_BRIGHTNESS)' not in system
assert 'm1_backlight_on(192)' not in (src/'m1_sys_init.c').read_text()
assert 'SETTINGS_FILE_PATH ".bak", FA_READ' in settings
stock=(src/'app_stock_backlight.c').read_text()
assert stock.count('if (!settings_save_to_sd())')==4


blast=(repo/'tools/remote_edition/power_blast_prelude.h').read_text()+'\n'+function_body(code,'static void ir_power_blast(const char *title, const char *const *paths, uint8_t n_paths)')+r"""
int main(void){const char *paths[]={"test.ir"};
 ir_power_blast("Test",paths,1);assert(starts==2&&completed==2&&stops==2&&!s_remote_tx_started);
 starts=completed=stops=phase=ticks=0;mode=1;ir_power_blast("Timeout",paths,1);assert(starts==1&&completed==0&&stops==1&&!s_remote_tx_started&&ticks>=5000);
 starts=completed=stops=phase=ticks=0;mode=2;ir_power_blast("Cancel",paths,1);assert(starts==1&&stops==1&&!s_remote_tx_started&&ticks<5000);
 puts("PASS: actual power-blast workflow waits for full parsed/raw completion, cleans up each action, and handles timeout/cancellation without starting another signal");return 0;}
"""
(out/'power-blast.c').write_text(blast);exe=compile('power-blast',[out/'power-blast.c']);print(run([exe]))
assert 'xTimer' not in infrared
assert 'free(pir_ota_data_tx_buffer)' not in (src/'m1_cli.c').read_text(encoding='utf8')
rxinit=function_body(infrared,'void infrared_decode_sys_init(void)')
rxstop=function_body(infrared,'void infrared_decode_sys_deinit(void)')
assert rxinit.index('EDGE_DET_IDLE')<rxinit.index('HAL_TIM_IC_Start_IT')
assert rxstop.index('HAL_NVIC_DisableIRQ')<rxstop.index('HAL_TIM_IC_DeInit')
assert 'HAL_NVIC_ClearPendingIRQ' in rxstop and '__HAL_RCC_TIM2_FORCE_RESET' in rxinit

exe=compile('room-radar',[repo/'tools/room_radar/test_room_radar.c']);print(run([exe]))


setup=(repo/'tools/room_radar/setup_prelude.h').read_text()
# The real functions use the public declarations in the header. Rename the
# hardware adapters in this harness to avoid a static/non-static mismatch.
setup=setup.replace('wifi_room_radar_recover','mock_recover').replace('wifi_room_radar_networks','mock_networks').replace('wifi_room_radar_join','mock_join')
radar_source=(src/'m1_room_radar.c').read_text()
for signature in ['static bool radar_radio_ready(const char *title)', 'static bool radar_connected(void)', 'static bool radar_try_network(const room_radar_network_t *network, bool use_saved)', 'static bool radar_connect(void)', 'static void radar_measure(unsigned operation)']:
 piece=function_body(radar_source,signature)
 piece=piece.replace('wifi_room_radar_recover','mock_recover').replace('wifi_room_radar_networks','mock_networks').replace('wifi_room_radar_join','mock_join')
 setup+='\n'+piece
setup+=r"""
static void reset(void)
{
 online=radio_online=recovery_ok=saved_network=fail_join=false;
 scans=joins=password_prompts=recoveries=jobs=saved_panels=key_count=key_index=ticks=0;
}
int main(void)
{
 reset();online=true;assert(radar_connect() && scans==0 && joins==0);
 reset();saved_network=true;assert(radar_connect() && scans==1 && joins==1 && password_prompts==0);
 reset();keys[0]=BUTTON_OK_KP_ID;key_count=1;assert(radar_connect() && joins==1 && password_prompts==1);
 reset();saved_network=fail_join=true;assert(!radar_connect() && joins==1 && password_prompts==0);
 reset();assert(!radar_connect() && joins==0);
 reset();online=true;radar_measure(2);assert(jobs==1 && key_index==0);
 reset();online=true;radar_measure(1);assert(jobs==1 && ticks>=5000 && saved_panels==1);
 reset();recovery_ok=true;assert(radar_radio_ready("Test") && recoveries==1);
 puts("PASS: actual Room Radar setup reuses an existing link, reconnects saved Wi-Fi, prompts/saves once, recovers radio and automatically starts LAN/calibration with Back cancellation");
 return 0;
}
"""
(out/'room-radar-setup.c').write_text(setup);exe=compile('room-radar-setup',[out/'room-radar-setup.c']);print(run([exe]))

spi_source=(repo/'Esp_spi_at/examples/at_spi_master/spi/stm32/main/esp_app_main.c').read_text(encoding='utf-8')
spi_recovery=r"""
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#define SPI_TRANS_MAX_LEN 4096U
static int locked, esp_spi_msg_queue=1, spi_master_tx_ring_buf=2, esp_ctrl_req_sem=3;
static unsigned pending, queued, replies, requests, sends;
static uint8_t initiative_send_flag, current_send_seq, current_recv_seq;
static uint32_t plan_send_len;
static int32_t current_uid;
static void spi_mutex_lock(void){assert(!locked);locked=1;}
static void spi_mutex_unlock(void){assert(locked);locked=0;}
static void xQueueReset(int h){assert(locked && h==1);queued=0;}
static void xStreamBufferReset(int h){assert(locked && h==2);pending=0;}
static void xSemaphoreGive(int h){assert(locked && h==3);requests=1;}
static void spi_AT_reset_response_channel(void){assert(locked);replies=0;}
static uint32_t xStreamBufferBytesAvailable(int h){assert(locked && h==2);return pending;}
static void spi_master_request_to_write(uint8_t seq, uint32_t len){assert(locked && seq==1 && len==4);sends++;}
"""
for sig in ['static void notify_slave_to_recv(void)', 'static void spi_reset_session(void)']:
 spi_recovery+='\n'+function_body(spi_source,sig)
spi_recovery+=r"""
int main(void){
 initiative_send_flag=1;plan_send_len=80;current_send_seq=42;current_recv_seq=27;current_uid=31;
 pending=queued=replies=9;
 notify_slave_to_recv();assert(sends==0);
 spi_reset_session();assert(!pending && !queued && !replies && requests==1);
 assert(!initiative_send_flag && !plan_send_len && !current_send_seq && !current_recv_seq && !current_uid);
 pending=4;notify_slave_to_recv();notify_slave_to_recv();assert(sends==1 && plan_send_len==4);
 puts("PASS: a stalled SPI session cannot block the first command after radio reset");return 0;
}
"""
(out/'spi-recovery.c').write_text(spi_recovery)
exe=compile('spi-recovery',[out/'spi-recovery.c']);print(run([exe]))

(out/'test-results.txt').write_text('\n'.join(log))
print('ALL CONTROL/IR HOST TESTS PASSED')
