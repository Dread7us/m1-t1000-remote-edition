/* See COPYING.txt for license details. */
#include "m1_automation.h"
#include "flipper_file.h"
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <limits.h>

static bool clean(const char *value, size_t capacity) {
    size_t n=0;
    while (n<capacity && value[n]) {
        if ((unsigned char)value[n]<32 || (unsigned char)value[n]>126) return false;
        ++n;
    }
    return n>0 && n<capacity;
}
static bool source_path(const char *path) {
    return clean(path,AUTOMATION_PATH) && !strncmp(path,"0:/",3) && !strstr(path,"..") && !strchr(path+3,':') && !strchr(path,'\\');
}
static bool valid(const automation_t *chain) {
    if (!clean(chain->name,sizeof(chain->name)) || chain->count>AUTOMATION_STEPS) return false;
    for (unsigned i=0;i<chain->count;++i) {
        const automation_step_t *step=&chain->steps[i];
        if (!clean(step->label,sizeof(step->label))) return false;
        if (step->type==AUTO_WAIT) {if (step->wait_ms<100 || step->wait_ms>AUTOMATION_WAIT_MAX) return false;}
        else if (step->type==AUTO_IR || step->type==AUTO_SUBGHZ) {
            if (!source_path(step->path) || step->button>=64) return false;
            if (step->type==AUTO_IR && !clean(step->signal_name,sizeof(step->signal_name))) return false;
        } else return false;
    }
    return true;
}
static bool field(flipper_file_t *file,const char *key,char *out,size_t cap) {
    if (!ff_read_line(file) || !ff_parse_kv(file) || strcmp(file->key,key) || strlen(file->value)>=cap) return false;
    strcpy(out,file->value);return true;
}
static bool number(flipper_file_t *file,const char *key,uint32_t max,uint32_t *out) {
    char text[16],*end;
    if (!field(file,key,text,sizeof(text)) || !isdigit((unsigned char)text[0])) return false;
    uint32_t value=0;
    for(end=text;*end;++end) {
        if (!isdigit((unsigned char)*end) || value>max/10 || (value==max/10 && (uint32_t)(*end-'0')>max%10)) return false;
        value=value*10+(uint32_t)(*end-'0');
    }
    *out=(uint32_t)value;return true;
}
bool automation_load(const char *path,automation_t *chain) {
    flipper_file_t file;uint32_t n;bool ok=false;
    memset(chain,0,sizeof(*chain));
    if (!ff_open(&file,path)) return false;
    if (!field(&file,"Filetype",file.line,sizeof(file.line)) || strcmp(file.line,"M1 Automation") ||
        !number(&file,"Version",1,&n) || n!=1 || !field(&file,"Name",chain->name,sizeof(chain->name)) ||
        !number(&file,"Steps",AUTOMATION_STEPS,&n)) goto done;
    chain->count=(uint8_t)n;
    for (unsigned i=0;i<chain->count;++i) {
        automation_step_t *s=&chain->steps[i];
        if (!number(&file,"Type",AUTO_WAIT,&n)) goto done;
        s->type=(uint8_t)n;
        if (!field(&file,"Label",s->label,sizeof(s->label))) goto done;
        if (s->type==AUTO_WAIT) {if (!number(&file,"Wait",AUTOMATION_WAIT_MAX,&s->wait_ms)) goto done;}
        else {
            if (!field(&file,"Path",s->path,sizeof(s->path)) || !number(&file,"Button",63,&n)) goto done;
            s->button=(uint16_t)n;
            if (s->type==AUTO_IR && !field(&file,"Signal",s->signal_name,sizeof(s->signal_name))) goto done;
            if (!number(&file,"Fingerprint",UINT32_MAX,&s->fingerprint)) goto done;
        }
    }
    if (ff_read_line(&file)) goto done; /* Reject trailing/unknown steps. */
    ok=valid(chain);
done:
    ff_close(&file);if (!ok) memset(chain,0,sizeof(*chain));return ok;
}
bool automation_save(const char *path,const automation_t *chain) {
    FIL file;char temp[AUTOMATION_PATH+8],backup[AUTOMATION_PATH+8];bool ok;
    if (!source_path(path) || !valid(chain)) return false;
    snprintf(temp,sizeof(temp),"%s.tmp",path);snprintf(backup,sizeof(backup),"%s.bak",path);
    if (f_open(&file,temp,FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK) return false;
    ok=f_printf(&file,"Filetype: M1 Automation\nVersion: 1\nName: %s\nSteps: %u\n",chain->name,chain->count)>0;
    for (unsigned i=0;i<chain->count && ok;++i) {
        const automation_step_t *s=&chain->steps[i];
        ok=f_printf(&file,"Type: %u\nLabel: %s\n",s->type,s->label)>0;
        if (ok && s->type==AUTO_WAIT) ok=f_printf(&file,"Wait: %lu\n",(unsigned long)s->wait_ms)>0;
        else if (ok) {
            ok=f_printf(&file,"Path: %s\nButton: %u\n",s->path,s->button)>0;
            if(ok && s->type==AUTO_IR)ok=f_printf(&file,"Signal: %s\n",s->signal_name)>0;
            if(ok)ok=f_printf(&file,"Fingerprint: %lu\n",(unsigned long)s->fingerprint)>0;
        }
    }
    FRESULT closed=f_close(&file);
    if (!ok || closed!=FR_OK) {f_unlink(temp);return false;}
    FRESULT removed=f_unlink(backup);
    if (removed!=FR_OK && removed!=FR_NO_FILE) return false;
    FRESULT moved=f_rename(path,backup);
    if (moved!=FR_OK && moved!=FR_NO_FILE) return false;
    if (f_rename(temp,path)==FR_OK) return true;
    if (moved==FR_OK) f_rename(backup,path);
    return false;
}
bool automation_move(automation_t *chain,unsigned index,int direction) {
    if (index>=chain->count || (direction!=1 && direction!=-1) || (direction<0 && !index) || (direction>0 && index+1>=chain->count)) return false;
    unsigned other=direction<0 ? index-1:index+1;
    automation_step_t temp=chain->steps[index];chain->steps[index]=chain->steps[other];chain->steps[other]=temp;return true;
}
bool automation_duplicate(automation_t *chain,unsigned index) {
    if(index>=chain->count || chain->count>=AUTOMATION_STEPS) return false;
    memmove(&chain->steps[index+2],&chain->steps[index+1],(chain->count-index-1)*sizeof(chain->steps[0]));
    chain->steps[index+1]=chain->steps[index];
    ++chain->count;
    return true;
}
bool automation_remove(automation_t *chain,unsigned index) {
    if (index>=chain->count) return false;
    memmove(&chain->steps[index],&chain->steps[index+1],(chain->count-index-1)*sizeof(chain->steps[0]));
    memset(&chain->steps[--chain->count],0,sizeof(chain->steps[0]));return true;
}
bool automation_fingerprint(const char *path,uint32_t *result) {
    FIL file;UINT count;uint8_t bytes[128];uint32_t hash=2166136261u,total=0;FRESULT status;
    if (f_open(&file,path,FA_READ)!=FR_OK) return false;
    do {
        status=f_read(&file,bytes,sizeof(bytes),&count);
        if (status!=FR_OK || total+count>262144) {f_close(&file);return false;}
        for (unsigned i=0;i<count;++i) hash=(hash^bytes[i])*16777619u;
        total+=count;
    } while(count);
    status=f_close(&file);if (status!=FR_OK || !total) return false;*result=hash;return true;
}

/* Validate all timings before any radio output; accept native signed durations. */
bool automation_sgh_info(const char *path,uint32_t *frequency,uint8_t *modulation) {
    FIL file;char line[128],*end;bool ok=false;unsigned stage=0;uint32_t count=0,duration=0;
    if (f_open(&file,path,FA_READ)!=FR_OK) return false;
    while (stage<4 && f_gets(line,sizeof(line),&file)) {
        size_t len=strlen(line);if (len<2 || line[len-2]!='\r' || line[len-1]!='\n') goto done;
        while (len && (line[len-1]=='\n'||line[len-1]=='\r')) line[--len]=0;
        if (stage==0 && strcmp(line,"Filetype: M1 SubGHz NOISE")) goto done;
        if (stage==1 && strncmp(line,"Version: ",9)) goto done;
        if (stage==2) {
            if (strncmp(line,"Frequency: ",11) || !isdigit((unsigned char)line[11])) goto done;
            unsigned long value=strtoul(line+11,&end,10);
            if (*end || value<142000000 || value>1050000000) goto done;
            *frequency=(uint32_t)value;
        }
        if (stage==3) {
            if (!strcmp(line,"Modulation: OOK")) *modulation=0;
            else if (!strcmp(line,"Modulation: ASK")) *modulation=1;
            else if (!strcmp(line,"Modulation: FSK")) *modulation=2;
            else goto done;
        }
        ++stage;
    }
    if (stage!=4) goto done;
    /* Tokenize independently of data-line length, using fixed-size reads. */
    char token[16];unsigned used=0;uint8_t bytes[128];UINT got;FRESULT status;int previous='\n';
    do {
        status=f_read(&file,bytes,sizeof(bytes),&got);if (status!=FR_OK) goto done;
        for (unsigned i=0;i<=got;++i) {
            if (i==got && got) break;
            int c=i<got ? bytes[i]:' ';
            if (i<got) {if ((c=='\n' && previous!='\r') || (previous=='\r' && c!='\n')) goto done;previous=c;}
            if (isspace(c)) {
                if (!used) continue;
                token[used]=0;used=0;
                if (!strcmp(token,"Data:")) continue;
                char *p=token;if (*p=='+'||*p=='-') ++p;
                if (!isdigit((unsigned char)*p)) goto done;
                unsigned long value=strtoul(p,&end,10);
                if (*end || !value || value>65535 || ++count>65536 || duration+value>5000000) goto done;
                duration+=(uint32_t)value;
            } else {if (used>=sizeof(token)-1) goto done;token[used++]=(char)c;}
        }
    } while(got);
    ok=count>1 && count%2==0 && previous=='\n';
done:
    f_close(&file);return ok;
}
