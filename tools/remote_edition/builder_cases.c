/* Included after the exact production builder helpers in the native harness. */
static void reset_answers(const char *first,const char *second) {
 memset(answers,0,sizeof(answers));answers[0]=first;answers[1]=second;answer_index=0;
}
int main(void) {
 char path[256]="";
 flipper_ir_signal_t sig={0};sig.valid=true;sig.type=FLIPPER_IR_SIGNAL_PARSED;
 sig.parsed.protocol=IRMP_SAMSUNG32_PROTOCOL;sig.parsed.address=7;sig.parsed.command=2;
 reset_answers("","");assert(ir_custom_save_signal(path,&sig));
 assert(!strcmp(path,"0:/IR/Samsung.ir"));assert(flipper_ir_count_signals(path)==1);
 sig.parsed.command=7;reset_answers("",NULL);assert(ir_custom_save_signal(path,&sig));
 sig.parsed.command=9;reset_answers("",NULL);assert(ir_custom_save_signal(path,&sig));
 assert(flipper_ir_count_signals(path)==3);
 char second[256]="";sig.parsed.command=2;reset_answers("","");assert(ir_custom_save_signal(second,&sig));
 assert(!strcmp(second,"0:/IR/Samsung_1.ir") && flipper_ir_count_signals(path)==3);
 flipper_file_t ff;flipper_ir_signal_t read;
 assert(flipper_ir_open(&ff,path));
 assert(flipper_ir_read_signal(&ff,&read) && !strcmp(read.name,"Power") && read.parsed.command==2);
 assert(flipper_ir_read_signal(&ff,&read) && !strcmp(read.name,"Vol_up") && read.parsed.command==7);
 assert(flipper_ir_read_signal(&ff,&read) && !strcmp(read.name,"Mute") && read.parsed.command==9);
 ff_close(&ff);
 sig.parsed.command=2;reset_answers("",NULL);assert(!ir_custom_save_signal(path,&sig));
 assert(flipper_ir_count_signals(path)==3);
 reset_answers("Living Room",NULL);assert(ir_custom_rename_remote(path,"Samsung"));
 assert(flipper_ir_count_signals("0:/IR/Living Room.ir")==3);
 assert(f_stat(path,NULL)==FR_NO_FILE);
 char collision[256]="";sig.parsed.command=2;reset_answers("","");assert(ir_custom_save_signal(collision,&sig));
 reset_answers("Living Room",NULL);assert(!ir_custom_rename_remote(collision,"Samsung"));
 assert(flipper_ir_count_signals(collision)==1 && flipper_ir_count_signals("0:/IR/Living Room.ir")==3);
 char cancelled[256]="";reset_answers("<cancel>",NULL);assert(!ir_custom_save_signal(cancelled,&sig));assert(!cancelled[0]);
 reset_answers("Cancelled Remote","<cancel>");assert(!ir_custom_save_signal(cancelled,&sig));
 assert(f_stat("0:/IR/Cancelled Remote.ir",NULL)==FR_NO_FILE);
 char full[256]="many.ir";reset_answers("",NULL);assert(!ir_custom_save_signal(full,&sig));assert(flipper_ir_count_signals(full)==64);
 ambiguous_library=1;char title[32],button[32]="Power";
 ir_custom_suggest_identity(&sig,title,button);assert(!strcmp(title,"Samsung32 Remote"));
 flipper_ir_signal_t raw={0};raw.valid=true;raw.type=FLIPPER_IR_SIGNAL_RAW;
 raw.raw.frequency=38000;raw.raw.duty_cycle=0.33f;raw.raw.sample_count=8;
 for(unsigned i=0;i<8;i++)raw.raw.samples[i]=560;
 char rawpath[256]="";reset_answers("","");assert(ir_custom_save_signal(rawpath,&raw));
 assert(!strcmp(rawpath,"0:/IR/Raw Remote.ir"));
 reset_answers("",NULL);assert(ir_custom_save_signal(rawpath,&raw));assert(flipper_ir_count_signals(rawpath)==2);
 puts("PASS: actual builder groups learned buttons in one file, suggests matched titles/functions, retains existing signals, rejects duplicate names, renames without overwriting, cancels without files, and falls back on ambiguous identity");
 return 0;
}
