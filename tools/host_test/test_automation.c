/* See COPYING.txt for license details. */
#include "m1_automation.h"
#include "ff.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static int fail_replace;
static FRESULT tested_rename(const char *a,const char *b) {
    if(fail_replace && strstr(a,".tmp"))return FR_DISK_ERR;
    return f_rename(a,b);
}
#define f_rename tested_rename
#include "m1_automation_file.c"
#undef f_rename
static void write_file(const char *path,const char *text) {FILE *f=fopen(path,"wb");assert(f);assert(fwrite(text,1,strlen(text),f)==strlen(text));assert(!fclose(f));}
static void bad_chain(const char *text) {automation_t out;write_file("bad.chain",text);assert(!automation_load("bad.chain",&out));assert(!out.count);}
int main(void) {
    automation_t a={0},b={0};strcpy(a.name,"Movie Night");
    assert(automation_save("0:/test.chain",&a));assert(automation_load("test.chain",&b));assert(!b.count);
    for(unsigned i=0;i<AUTOMATION_STEPS;++i) {
        automation_step_t *s=&a.steps[a.count++];s->type=i%3+1;s->button=i;s->fingerprint=0xffffffff;
        s->wait_ms=60000;snprintf(s->label,sizeof(s->label),"Device %u",i);strcpy(s->path,"0:/IR/Samsung.ir");
        strcpy(s->signal_name,"Power");
    }
    assert(automation_save("0:/test.chain",&a));assert(automation_load("test.chain",&b));assert(b.count==a.count);
    for(unsigned i=0;i<a.count;++i){assert(a.steps[i].type==b.steps[i].type);assert(!strcmp(a.steps[i].label,b.steps[i].label));if(a.steps[i].type!=AUTO_WAIT){assert(a.steps[i].fingerprint==b.steps[i].fingerprint);assert(a.steps[i].button==b.steps[i].button);assert(!strcmp(a.steps[i].path,b.steps[i].path));}else assert(a.steps[i].wait_ms==b.steps[i].wait_ms);}
    assert(!automation_move(&b,0,-1));assert(!automation_move(&b,15,1));assert(automation_move(&b,1,-1));assert(!strcmp(b.steps[0].label,"Device 1"));
    assert(automation_remove(&b,0));assert(b.count==15 && !strcmp(b.steps[0].label,"Device 0"));assert(!automation_remove(&b,99));
    fail_replace=1;assert(!automation_save("0:/test.chain",&b));fail_replace=0;
    assert(automation_load("test.chain",&b));assert(b.count==16 && !strcmp(b.steps[0].label,"Device 0"));
    strcpy(b.name,"injected\nType: 2");assert(!automation_save("0:/test.chain",&b));
    bad_chain("Filetype: M1 Automation\nVersion: 2\nName: Test\nSteps: 0\n");
    bad_chain("Filetype: M1 Automation\nVersion: 1\nName: Test\nSteps: 17\n");
    bad_chain("Filetype: M1 Automation\nVersion: 1\nName: Test\nSteps: 999999999999999999999999\n");
    bad_chain("Filetype: M1 Automation\nVersion: 1\nName: Test\nSteps: 1\nType: 3\nLabel: Wait\nWait: 0\n");
    bad_chain("Filetype: M1 Automation\nVersion: 1\nName: Test\nSteps: 1\nType: 3\nLabel: Wait\nWait: 60001\n");
    bad_chain("Filetype: M1 Automation\nVersion: 1\nName: Test\nSteps: 0\nType: 1\n");
    bad_chain("Filetype: M1 Automation\nVersion: 1\nName: Test\nSteps: 1\nType: 0\nLabel: Bad\nPath: 0:/IR/a.ir\nButton: 0\nFingerprint: 1\n");
    bad_chain("Filetype: M1 Automation\nVersion: 1\nName: Test\nSteps: 1\nType: 1\nLabel: Bad\nPath: 0:/../private\nButton: 0\nFingerprint: 1\n");
    uint32_t first,second;write_file("remote.ir","original");assert(automation_fingerprint("remote.ir",&first));write_file("remote.ir","changed");assert(automation_fingerprint("remote.ir",&second));assert(first!=second);assert(!automation_fingerprint("absent.ir",&first));
    const char *header="Filetype: M1 SubGHz NOISE\r\nVersion: 0.8\r\nFrequency: 433920000\r\nModulation: OOK\r\n";
    FILE *f=fopen("valid.sgh","wb");assert(f);fputs(header,f);fputs("Data:",f);for(int i=0;i<2000;++i)fputs(i%2?" -100":" +100",f);fputs("\r\n",f);fclose(f);
    uint32_t frequency;uint8_t modulation;
    assert(automation_sgh_info("valid.sgh",&frequency,&modulation));assert(frequency==433920000 && !modulation);
    f=fopen("invalid.sgh","wb");fputs(header,f);fputs("Data: +100 -0\r\n",f);fclose(f);assert(!automation_sgh_info("invalid.sgh",&frequency,&modulation));
    f=fopen("invalid.sgh","wb");fputs(header,f);fputs("Data: +100 -999999999999999\r\n",f);fclose(f);assert(!automation_sgh_info("invalid.sgh",&frequency,&modulation));
    f=fopen("invalid.sgh","wb");fputs(header,f);fputs("Data: +100 -100 +100\r\n",f);fclose(f);assert(!automation_sgh_info("invalid.sgh",&frequency,&modulation));
    f=fopen("invalid.sgh","wb");fputs(header,f);for(int i=0;i<100;++i)fputs("Data: 60000 60000\r\n",f);fclose(f);assert(!automation_sgh_info("invalid.sgh",&frequency,&modulation));
    f=fopen("invalid.sgh","wb");fputs(header,f);fputs("Data: 100 -100\n",f);fclose(f);assert(!automation_sgh_info("invalid.sgh",&frequency,&modulation));
    puts("PASS: automation roundtrips all 16 IR/RF/Wait steps; edit ordering; replacement rollback; invalid versions/counts/delays/paths; changed-file fingerprints; long native RF lines and malformed/oversized timings");return 0;
}
