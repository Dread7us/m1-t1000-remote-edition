/* See COPYING.txt for license details. */
#include "main.h"
#include "m1_automation.h"
#include "m1_display.h"
#include "m1_tasks.h"
#include "m1_ir_universal.h"
#include "m1_sub_ghz.h"
#include "m1_virtual_kb.h"
#include "ff.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define AUTO_DIR "0:/IR/Automations"
#define AUTO_FILES 32
static automation_t chain;
static struct {char name[64],path[AUTOMATION_PATH];bool directory;} files[AUTO_FILES];
static char rows[66][AUTOMATION_NAME];
static char current[AUTOMATION_PATH];
static bool cancelled;
static const uint8_t icon_ir[]={0x10,0x00,0x44,0x00,0x00,0x00,0x7c,0x00,0x44,0x00,0x54,0x00,0x44,0x00,0x6c,0x00,0x44,0x00,0x7c,0x00};
static const uint8_t icon_rf[]={0x82,0x00,0x44,0x00,0x28,0x00,0x10,0x00,0x10,0x00,0x38,0x00,0x54,0x00,0x10,0x00,0x10,0x00,0x38,0x00};
static const uint8_t icon_wait[]={0x7c,0x00,0x82,0x00,0x11,0x01,0x11,0x01,0x71,0x01,0x01,0x01,0x01,0x01,0x82,0x00,0x7c,0x00,0x00,0x00};
static const uint8_t icon_chain[]={0x1c,0x00,0x22,0x00,0x22,0x00,0x5c,0x00,0xa0,0x00,0x40,0x01,0x40,0x01,0x80,0x00,0x00,0x00,0x00,0x00};
static const uint8_t *step_icon(unsigned type) {return type==AUTO_IR ? icon_ir:type==AUTO_SUBGHZ ? icon_rf:icon_wait;}
static void error(const char *title,const char *detail) {m1_message_box(&m1_u8g2,title,detail,"", "OK");}
static int key(void) {
    S_M1_Main_Q_t event;S_M1_Buttons_Status buttons;
    for (;;) {
        if (xQueueReceive(main_q_hdl,&event,pdMS_TO_TICKS(120))!=pdTRUE) return -1;
        if (event.q_evt_type!=Q_EVENT_KEYPAD) continue;
        if (xQueueReceive(button_events_q_hdl,&buttons,0)!=pdTRUE) continue;
        for (unsigned i=0;i<NUM_BUTTONS_MAX;++i) if (buttons.event[i]==BUTTON_EVENT_CLICK) return (int)i;
    }
}
static bool cancel(void) {
    S_M1_Buttons_Status buttons;
    while (xQueueReceive(button_events_q_hdl,&buttons,0)==pdTRUE)
        if (buttons.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK) cancelled=true;
    return cancelled;
}
static bool wait_ms(uint32_t ms) {
    TickType_t start=xTaskGetTickCount();
    do {if(cancel()) return false;vTaskDelay(pdMS_TO_TICKS(20));} while(xTaskGetTickCount()-start<pdMS_TO_TICKS(ms));
    return true;
}
static void list_draw(const char *title,unsigned selected,unsigned count,bool steps) {
    char badge[12];snprintf(badge,sizeof(badge),"%u/%u",selected+1,count);
    m1_u8g2_firstpage();m1_draw_header_bar(&m1_u8g2,title,badge);
    u8g2_SetFont(&m1_u8g2,M1_DISP_FUNC_MENU_FONT_N);
    unsigned start=selected<3?0:selected-2;
    for(unsigned r=0;r<3 && start+r<count;++r) {
        unsigned i=start+r;int y=14+r*12;
        m1_ui_selection(&m1_u8g2,2,y,124,11,i==selected);
        const uint8_t *icon=steps && i<chain.count ? step_icon(chain.steps[i].type):icon_chain;
        u8g2_DrawXBMP(&m1_u8g2,5,y+1,10,10,icon);
        if(i==selected)m1_draw_marquee(&m1_u8g2,19,y+8,103,rows[i]);else m1_draw_text(&m1_u8g2,19,y+8,103,rows[i],TEXT_ALIGN_LEFT);
    }
    m1_draw_bottom_bar(&m1_u8g2,button_back_8x8,"Back","Select",button_ok_8x8);m1_u8g2_nextpage();
}
static int pick_at(const char *title,unsigned count,unsigned selected,bool steps) {
    m1_text_reset_scroll();
    if (!count) return -1;
    if(selected>=count) selected=count-1;
    for (;;) {
        list_draw(title,selected,count,steps);int k=key();
        if(k>=0)m1_text_reset_scroll();
        if(k==BUTTON_BACK_KP_ID) return -1;
        if(k==BUTTON_OK_KP_ID) return (int)selected;
        if(k==BUTTON_UP_KP_ID) selected=m1_ui_step(selected,count,-1,m1_ui.wrap);
        if(k==BUTTON_DOWN_KP_ID) selected=m1_ui_step(selected,count,1,m1_ui.wrap);
    }
}
static int actions(const char *title,const char *const *labels,unsigned count) {
    for(unsigned i=0;i<count;++i) snprintf(rows[i],sizeof(rows[i]),"%s",labels[i]);
    return pick_at(title,count,0,false);
}
static bool extension(const char *name,const char *suffix) {
    size_t n=strlen(name),s=strlen(suffix);if(n<s)return false;
    for(unsigned i=0;i<s;++i)if(tolower((unsigned char)name[n-s+i])!=tolower((unsigned char)suffix[i]))return false;
    return true;
}
static bool choose_file(bool rf,char *output) {
    char folder[AUTOMATION_PATH];const char *root=rf?"0:/SUBGHZ":"0:/IR";unsigned page=0;
    snprintf(folder,sizeof(folder),"%s",root);
    for (;;) {
        DIR directory;FILINFO entry;unsigned count=0,skipped=0;bool more=false;
        if(f_opendir(&directory,folder)!=FR_OK){error("Folder unavailable",folder);return false;}
        while(f_readdir(&directory,&entry)==FR_OK && entry.fname[0]) {
            if(entry.fname[0]=='.' || (!(entry.fattrib&AM_DIR) && !extension(entry.fname,rf?".sgh":".ir")))continue;
            if(skipped++<page*AUTO_FILES)continue;
            if(count==AUTO_FILES){more=true;break;}
            if(snprintf(files[count].path,sizeof(files[count].path),"%s/%s",folder,entry.fname)>=(int)sizeof(files[count].path))continue;
            snprintf(files[count].name,sizeof(files[count].name),"%s",entry.fname);files[count].directory=(entry.fattrib&AM_DIR)!=0;++count;
        }
        f_closedir(&directory);
        unsigned n=0;snprintf(rows[n++],sizeof(rows[0]),".. Parent / Back");
        for(unsigned i=0;i<count;++i)snprintf(rows[n++],sizeof(rows[0]),"%s%.*s",files[i].directory?"> ":"",29,files[i].name);
        int prev=-1,next=-1;
        if(page){prev=n;snprintf(rows[n++],sizeof(rows[0]),"< Previous page");}
        if(more){next=n;snprintf(rows[n++],sizeof(rows[0]),"> Next page");}
        int selected=pick_at(rf?"Sub-GHz files":"IR remotes",n,count?1:0,false);
        if(selected<0)return false;
        if(selected==prev){--page;continue;}if(selected==next){++page;continue;}
        if(!selected){if(!strcmp(folder,root))return false;*strrchr(folder,'/')=0;page=0;continue;}
        --selected;
        if(files[selected].directory){snprintf(folder,sizeof(folder),"%s",files[selected].path);page=0;continue;}
        snprintf(output,AUTOMATION_PATH,"%s",files[selected].path);return true;
    }
}
static bool wait_step(automation_step_t *step) {
    static const uint32_t values[]={100,250,500,1000,2000,3000,5000,10000,15000,30000,60000};
    unsigned selection=3;
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);++i){snprintf(rows[i],sizeof(rows[i]),"Wait %lu.%02lu seconds",(unsigned long)values[i]/1000,(unsigned long)(values[i]%1000)/10);if(values[i]==step->wait_ms)selection=i;}
    int result=pick_at("Wait between actions",11,selection,false);if(result<0)return false;
    memset(step,0,sizeof(*step));step->type=AUTO_WAIT;step->wait_ms=values[result];snprintf(step->label,sizeof(step->label),"%s",rows[result]);return true;
}
static bool build_step(automation_step_t *step) {
    static const char *const types[]={"IR remote button","Sub-GHz recording","Wait / delay"};
    int type=actions("Add action",types,3);if(type<0)return false;
    memset(step,0,sizeof(*step));step->type=type+1;
    if(step->type==AUTO_WAIT)return wait_step(step);
    if(!choose_file(step->type==AUTO_SUBGHZ,step->path))return false;
    if(step->type==AUTO_IR) {
        unsigned n=ir_automation_buttons(step->path);if(n>64)n=64;
        if(!n){error("No supported buttons","Choose another remote");return false;}
        for(unsigned i=0;i<n;++i) if(!ir_automation_button(i,rows[i],sizeof(rows[i])))snprintf(rows[i],sizeof(rows[i]),"[Unsupported]");
        int selected=pick_at("Choose IR button",n,0,false);if(selected<0)return false;
        step->button=(uint16_t)selected;
        if(!ir_automation_button(selected,step->label,sizeof(step->label))){error("Unsupported signal","Choose another button");return false;}
        snprintf(step->signal_name,sizeof(step->signal_name),"%s",step->label);
        char button[AUTOMATION_NAME];snprintf(button,sizeof(button),"%s",step->label);
        const char *remote=strrchr(step->path,'/');remote=remote?remote+1:step->path;
        size_t stem=strlen(remote);if(stem>3)stem-=3;if(stem>14)stem=14;
        snprintf(step->label,sizeof(step->label),"%.*s / %.14s",(int)stem,remote,button);
    } else {
        if(!sub_ghz_automation_check(step->path)){error("Cannot use recording","Check file and region");return false;}
        const char *name=strrchr(step->path,'/');snprintf(step->label,sizeof(step->label),"%.31s",name?name+1:step->path);
    }
    if(!automation_fingerprint(step->path,&step->fingerprint)){error("Cannot read source","Check SD card");return false;}
    return true;
}
static bool fresh_path(char *path) {
    FILINFO info;f_mkdir("0:/IR");f_mkdir(AUTO_DIR);
    for(unsigned i=1;i<=999;++i){
        char backup[AUTOMATION_PATH+8];snprintf(path,AUTOMATION_PATH,AUTO_DIR "/chain_%03u.chain",i);
        FRESULT r=f_stat(path,&info);if(r==FR_OK)continue;if(r!=FR_NO_FILE)return false;
        snprintf(backup,sizeof(backup),"%s.bak",path);r=f_stat(backup,&info);
        if(r==FR_NO_FILE)return true;
        if(r!=FR_OK)return false;
    }
    return false;
}
static bool save(void) {if(automation_save(current,&chain))return true;error("Automation not saved","Check mounted SD card");return false;}
static void editor(void) {
    unsigned cursor=0;bool dirty=false;
    for (;;) {
        for(unsigned i=0;i<chain.count;++i)snprintf(rows[i],sizeof(rows[i]),"%u. %s",i+1,chain.steps[i].label);
        if(chain.count<AUTOMATION_STEPS)snprintf(rows[chain.count],sizeof(rows[0]),"+ Add action");
        unsigned count=chain.count+(chain.count<AUTOMATION_STEPS);
        int selected=pick_at(chain.name,count,cursor,true);
        if(selected<0){if(!dirty || save())return;continue;}cursor=selected;
        if((unsigned)selected==chain.count){automation_step_t step;if(build_step(&step)){chain.steps[chain.count++]=step;dirty=true;}continue;}
        static const char *const menu[]={"Move earlier","Move later","Replace action","Rename action","Change wait","Duplicate action","Delete action"};
        int action=actions(chain.steps[selected].label,menu,7);
        if(action==0){if(automation_move(&chain,selected,-1)){cursor--;dirty=true;}}
        else if(action==1){if(automation_move(&chain,selected,1)){cursor++;dirty=true;}}
        else if(action==2){automation_step_t step;if(build_step(&step)){chain.steps[selected]=step;dirty=true;}}
        else if(action==3){char name[32];if(m1_vkb_get_filename("Action label",chain.steps[selected].label,name)){snprintf(chain.steps[selected].label,sizeof(chain.steps[selected].label),"%s",name);dirty=true;}}
        else if(action==4){if(chain.steps[selected].type==AUTO_WAIT){if(wait_step(&chain.steps[selected]))dirty=true;}else error("Select a Wait step","Add Wait to set delays");}
        else if(action==5){if(automation_duplicate(&chain,selected)){cursor=selected+1;dirty=true;}else error("16 steps already","Remove a step to copy");}
        else if(action==6 && m1_message_box_choice(&m1_u8g2,"Delete this action?",chain.steps[selected].label,"Other steps stay","Cancel\nDelete")==2){automation_remove(&chain,selected);dirty=true;}
    }
}
static void progress(unsigned index,const char *state,unsigned percent) {
    char badge[12],detail[36];snprintf(badge,sizeof(badge),"%u/%u",index+1,chain.count);
    const automation_step_t *step=&chain.steps[index];
    m1_u8g2_firstpage();m1_draw_header_bar(&m1_u8g2,chain.name,badge);
    u8g2_SetFont(&m1_u8g2,M1_DISP_FUNC_MENU_FONT_N);
    u8g2_DrawXBMP(&m1_u8g2,5,18,10,10,step_icon(step->type));
    m1_draw_text(&m1_u8g2,20,25,104,step->label,TEXT_ALIGN_LEFT);
    if(step->type==AUTO_WAIT)snprintf(detail,sizeof(detail),"%s %lu.%02lu s",state,(unsigned long)step->wait_ms/1000,(unsigned long)(step->wait_ms%1000)/10);
    else snprintf(detail,sizeof(detail),"%s  %s",state,step->type==AUTO_IR?"Infrared":"Sub-GHz");
    m1_draw_text(&m1_u8g2,5,37,118,detail,TEXT_ALIGN_LEFT);
    u8g2_SetDrawColor(&m1_u8g2,1);u8g2_DrawRFrame(&m1_u8g2,4,42,120,6,2);
    if(percent)u8g2_DrawBox(&m1_u8g2,6,44,116*(percent>100?100:percent)/100,2);
    m1_draw_bottom_bar(&m1_u8g2,button_back_8x8,"Stop","",NULL);m1_u8g2_nextpage();
}
static bool preflight(unsigned index) {
    const automation_step_t *step=&chain.steps[index];if(step->type==AUTO_WAIT)return true;
    uint32_t stamp;
    if(!automation_fingerprint(step->path,&stamp)||stamp!=step->fingerprint)return false;
    if(step->type==AUTO_SUBGHZ)return sub_ghz_automation_check(step->path);
    char name[AUTOMATION_NAME];return step->button<ir_automation_buttons(step->path) && ir_automation_button(step->button,name,sizeof(name)) && !strcmp(name,step->signal_name);
}
static void execute(void) {
    if(!chain.count){error("No actions yet","Edit steps to add one");return;}
    cancelled=false;xQueueReset(main_q_hdl);xQueueReset(button_events_q_hdl);
    for(unsigned i=0;i<chain.count;++i){progress(i,"Checking",0);if(cancel())return;if(!preflight(i)){error("Source unavailable", "Edit / replace this step");return;}}
    if(m1_message_box_choice(&m1_u8g2,"Run automation?",chain.name,"Aim M1 at your devices","Cancel\nRun")!=2)return;
    xQueueReset(main_q_hdl);xQueueReset(button_events_q_hdl);
    for(unsigned i=0;i<chain.count;++i) {
        if(cancel())break;
        if(!preflight(i)){error("Stopped at source","File changed or missing");return;}
        automation_step_t *step=&chain.steps[i];progress(i,"Running",i*100/chain.count);
        m1_action_result_t result=ACTION_OK;
        if(step->type==AUTO_WAIT) {
            TickType_t start=xTaskGetTickCount();
            while(xTaskGetTickCount()-start<pdMS_TO_TICKS(step->wait_ms)){
                unsigned elapsed=(unsigned)((xTaskGetTickCount()-start)*portTICK_PERIOD_MS);
                progress(i,"Waiting",elapsed*100/step->wait_ms);if(!wait_ms(100)){result=ACTION_CANCELLED;break;}
            }
        } else if(step->type==AUTO_IR)result=ir_automation_send(step->path,step->button,cancel);
        else result=sub_ghz_automation_send(step->path,cancel);
        if(result==ACTION_CANCELLED){cancelled=true;break;}
        if(result!=ACTION_OK){error("Automation stopped","Action failed / timed out");return;}
        if(step->type!=AUTO_WAIT && !wait_ms(250))break;
    }
    char summary[32];snprintf(summary,sizeof(summary),"%u steps processed",chain.count);
    error(cancelled?"Automation stopped":"Sequence sent",cancelled?"Remaining steps skipped":summary);
}
void automation_run(void) {
    f_mkdir("0:/IR");f_mkdir(AUTO_DIR);
    for (;;) {
        DIR directory;FILINFO entry;unsigned count=0;
        if(f_opendir(&directory,AUTO_DIR)!=FR_OK){error("SD card unavailable","Mount card in Storage");return;}
        snprintf(rows[0],sizeof(rows[0]),"+ New automation");
        while(count<AUTO_FILES && f_readdir(&directory,&entry)==FR_OK && entry.fname[0]) {
            if((entry.fattrib&AM_DIR)||!extension(entry.fname,".chain"))continue;
            if(snprintf(files[count].path,sizeof(files[count].path),AUTO_DIR "/%s",entry.fname)>=(int)sizeof(files[count].path))continue;
            if(automation_load(files[count].path,&chain))snprintf(rows[count+1],sizeof(rows[0]),"%s",chain.name);
            else snprintf(rows[count+1],sizeof(rows[0]),"! %.*s",28,entry.fname);
            ++count;
        }
        f_closedir(&directory);
        int selected=pick_at("Automations",count+1,0,false);if(selected<0)return;
        if(!selected) {
            if(count==AUTO_FILES){error("32 automations saved","Delete one to add more");continue;}
            char name[32];if(!m1_vkb_get_filename("Automation name","Movie Night",name))continue;
            memset(&chain,0,sizeof(chain));snprintf(chain.name,sizeof(chain.name),"%s",name);
            if(!fresh_path(current)||!save())continue;
            editor();continue;
        }
        snprintf(current,sizeof(current),"%s",files[selected-1].path);
        if(!automation_load(current,&chain)){error("Invalid automation","Restore its .bak on PC");continue;}
        for (;;) {
            static const char *const menu[]={"Run sequence","Edit steps","Rename","Duplicate","Delete"};
            int action=actions(chain.name,menu,5);if(action<0)break;
            if(action==0)execute();else if(action==1)editor();
            else if(action==2){char name[32];if(m1_vkb_get_filename("Automation name",chain.name,name)){snprintf(chain.name,sizeof(chain.name),"%s",name);save();}}
            else if(action==3){char path[AUTOMATION_PATH];if(fresh_path(path)&&automation_save(path,&chain))error("Automation copied","Copy appears in list");else error("Copy failed","Check SD card");}
            else if(action==4 && m1_message_box_choice(&m1_u8g2,"Delete automation?",chain.name,"Remotes stay unchanged","Cancel\nDelete")==2){if(f_unlink(current)!=FR_OK)error("Delete failed","Check SD card");else break;}
        }
    }
}
