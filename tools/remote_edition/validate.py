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
static FRESULT f_read(FIL *f,void *b,UINT n,UINT *read) {*read=(UINT)fread(b,1,n,f->fp);return FR_OK;}
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

(out/'test-results.txt').write_text('\n'.join(log))
print('ALL CONTROL/IR HOST TESTS PASSED')
