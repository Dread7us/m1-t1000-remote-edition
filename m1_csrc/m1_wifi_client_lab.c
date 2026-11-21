/* See COPYING.txt for license details. */
#include "main.h"
#include "m1_compile_cfg.h"
#include "app_freertos.h"
#include "m1_display.h"
#include "m1_virtual_kb.h"
#include "m1_room_radar.h"
#include "m1_room_watch.h"
#include "m1_device_library.h"
#include "m1_wifi_client_lab.h"
#include "esp_app_main.h"
#include "ff.h"
#include <stdlib.h>
#define CLIENT_PATH "0:/System/wifi-clients.dat"
_Static_assert(sizeof(wifi_client_profile_t)==CLIENT_LAB_ROW_SIZE,"Client profile layout changed");
static wifi_client_profile_t clients[CLIENT_LAB_LIMIT];
static unsigned client_count;
static bool client_recovered;
static bool client_lab_active,client_lab_supported,client_lab_save_ok;
static unsigned client_lab_view,client_lab_selected,client_lab_preview,client_lab_tx_calls,client_lab_save_calls;
unsigned wifi_client_lab_status_json(char *out,unsigned capacity)
{
    unsigned active,count,supported,view,selected,preview,tx_calls,saves,save_ok;
    taskENTER_CRITICAL();active=client_lab_active;count=client_count;supported=client_lab_supported;view=client_lab_view;
    selected=client_lab_selected;preview=client_lab_preview;tx_calls=client_lab_tx_calls;saves=client_lab_save_calls;save_ok=client_lab_save_ok;taskEXIT_CRITICAL();
    int n=snprintf(out,capacity,"{\"active\":%u,\"count\":%u,\"supported\":%u,\"view\":%u,\"selected\":%u,\"previews\":%u,\"transmit_calls\":%u,\"save_calls\":%u,\"save_ok\":%u}",
                   active,count,supported,view,selected,preview,tx_calls,saves,save_ok);
    return n>0 && (unsigned)n<capacity?(unsigned)n:0;
}

static uint8_t client_wire[5+CLIENT_LAB_LIMIT*CLIENT_LAB_ROW_SIZE+4];
static int client_key(TickType_t timeout)
{
    S_M1_Main_Q_t event;S_M1_Buttons_Status buttons;
    if(xQueueReceive(main_q_hdl,&event,timeout)!=pdTRUE || event.q_evt_type!=Q_EVENT_KEYPAD ||
       xQueueReceive(button_events_q_hdl,&buttons,0)!=pdTRUE)return -1;
    const int keys[]={BUTTON_BACK_KP_ID,BUTTON_OK_KP_ID,BUTTON_UP_KP_ID,BUTTON_DOWN_KP_ID};
    for(unsigned i=0;i<4;i++)if(buttons.event[keys[i]]==BUTTON_EVENT_CLICK)return keys[i];
    return -1;
}
static void client_panel(const char *title,const char *a,const char *b,const char *c,const char *action)
{
    m1_u8g2_firstpage();m1_draw_header_bar(&m1_u8g2,title,NULL);
    u8g2_SetFont(&m1_u8g2,M1_DISP_FUNC_MENU_FONT_N);
    m1_draw_marquee(&m1_u8g2,5,24,118,a?a:"");
    m1_draw_text(&m1_u8g2,5,36,118,b,TEXT_ALIGN_LEFT);
    m1_draw_text(&m1_u8g2,5,47,118,c,TEXT_ALIGN_LEFT);
    m1_draw_bottom_bar(&m1_u8g2,button_back_8x8,"Back",action,action?button_ok_8x8:NULL);m1_u8g2_nextpage();
}
static void client_message(const char *title,const char *a,const char *b)
{
    for(;;){client_lab_view=7;client_panel(title,a,b,NULL,"OK");int key=client_key(pdMS_TO_TICKS(120));
        if(key==BUTTON_BACK_KP_ID || key==BUTTON_OK_KP_ID)return;}
}
static int client_choose(const char *title,const char *const *items,unsigned count)
{
    unsigned selected=0;
    for(;;){
        client_lab_view=!strcmp(title,"Client Lab")?1:!strcmp(title,"Manage")?2:!strcmp(title,"Test modes")?3:4;client_lab_selected=selected;
        m1_u8g2_firstpage();m1_draw_header_bar(&m1_u8g2,title,NULL);
        u8g2_SetFont(&m1_u8g2,M1_DISP_FUNC_MENU_FONT_N);
        unsigned first=selected<3?0:selected-2;
        for(unsigned i=first;i<count && i<first+3;i++){
            int y=14+(int)(i-first)*12;m1_ui_selection(&m1_u8g2,2,y,124,11,selected==i);
            if(i==selected)m1_draw_marquee(&m1_u8g2,6,y+8,116,items[i]);
            else m1_draw_text(&m1_u8g2,6,y+8,116,items[i],TEXT_ALIGN_LEFT);
        }
        m1_draw_bottom_bar(&m1_u8g2,button_back_8x8,"Back","Select",button_ok_8x8);m1_u8g2_nextpage();
        int key=client_key(pdMS_TO_TICKS(120));if(key==BUTTON_BACK_KP_ID)return -1;
        if(key==BUTTON_OK_KP_ID)return (int)selected;
        if(key==BUTTON_UP_KP_ID)selected=selected?selected-1:count-1;
        if(key==BUTTON_DOWN_KP_ID)selected=(selected+1)%count;
    }
}
static bool client_load_file(const char *path)
{
    FIL file;UINT n;if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    unsigned size=(unsigned)f_size(&file);
    bool ok=size>=9 && size<=sizeof(client_wire) && f_read(&file,client_wire,size,&n)==FR_OK && n==size;
    if(f_close(&file)!=FR_OK)ok=false;
    if(!ok || memcmp(client_wire,"RSC1",4) || client_wire[4]>CLIENT_LAB_LIMIT)return false;
    unsigned count=client_wire[4];
    if(size!=9+count*CLIENT_LAB_ROW_SIZE || room_watch_get(client_wire+size-4,4)!=room_watch_hash(client_wire,size-4))return false;
    for(unsigned i=0;i<count;i++){
        wifi_client_profile_t profile;memcpy(&profile,client_wire+5+i*CLIENT_LAB_ROW_SIZE,sizeof(profile));
        if(!client_profile_valid(&profile))return false;
    }
    memcpy(clients,client_wire+5,count*CLIENT_LAB_ROW_SIZE);client_count=count;return true;
}
static bool client_load(void)
{
    client_count=0;client_recovered=false;
    if(client_load_file(CLIENT_PATH))return true;
    if(client_load_file(CLIENT_PATH ".bak")){client_recovered=true;return true;}
    FILINFO info;return f_stat(CLIENT_PATH,&info)==FR_NO_FILE && f_stat(CLIENT_PATH ".bak",&info)==FR_NO_FILE;
}
static bool client_save(void)
{
    ++client_lab_save_calls;client_lab_save_ok=false;
    if(client_count>CLIENT_LAB_LIMIT)return false;
    for(unsigned i=0;i<client_count;i++)if(!client_profile_valid(&clients[i]))return false;
    unsigned size=9+client_count*CLIENT_LAB_ROW_SIZE;memcpy(client_wire,"RSC1",4);client_wire[4]=(uint8_t)client_count;
    memcpy(client_wire+5,clients,client_count*CLIENT_LAB_ROW_SIZE);room_watch_put(client_wire+size-4,room_watch_hash(client_wire,size-4),4);
    FIL file;UINT n;FILINFO info;f_mkdir("0:/System");
    if(f_open(&file,CLIENT_PATH ".tmp",FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return false;
    bool ok=f_write(&file,client_wire,size,&n)==FR_OK && n==size && f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;if(!ok)return false;
    bool prior=f_stat(CLIENT_PATH,&info)==FR_OK;
    if(prior && client_recovered){if(f_unlink(CLIENT_PATH)!=FR_OK)return false;prior=false;}
    if(prior){FRESULT removed=f_unlink(CLIENT_PATH ".bak");if(removed!=FR_OK && removed!=FR_NO_FILE)return false;
        if(f_rename(CLIENT_PATH,CLIENT_PATH ".bak")!=FR_OK)return false;}
    if(f_rename(CLIENT_PATH ".tmp",CLIENT_PATH)!=FR_OK){if(prior)f_rename(CLIENT_PATH ".bak",CLIENT_PATH);return false;}
    client_recovered=false;client_lab_save_ok=true;return true;
}
static int client_duplicate(const wifi_client_profile_t *profile,int except)
{
    for(unsigned i=0;i<client_count;i++)if((int)i!=except && client_mac_equal(profile->station,clients[i].station) &&
        client_mac_equal(profile->ap,clients[i].ap))return (int)i;
    return -1;
}
static bool client_edit(wifi_client_profile_t *profile)
{
    char text[40];
    if(!m1_vkb_get_filename("Name",profile->name,text))return false;
    snprintf(profile->name,sizeof(profile->name),"%.24s",text);
    if(!m1_vkb_get_filename("Client MAC",profile->station,text))return false;
    if(strlen(text)!=17){client_message("Bad MAC","Use the client Wi-Fi MAC","From your router list");return false;}
    snprintf(profile->station,18,"%s",text);
    const char *routers[]={"Use current Wi-Fi","Enter router MAC"};int option=client_choose("Router",routers,2);if(option<0)return false;
    if(option==0){
        if(!room_radar_connect_network())return false;
        char response[256]={0};room_watch_snapshot_t current={0};
        if(spi_AT_send_recv("AT+CWJAP?\r\n",response,sizeof(response),3)!=SUCCESS || !room_watch_connected(response,&current)){
            client_message("No router","Join your 2.4 GHz Wi-Fi","Then retry");return false;}
        snprintf(profile->ap,18,"%s",current.bssid);profile->channel=(uint8_t)current.channel;
    }else{
        if(!m1_vkb_get_filename("Router MAC",profile->ap,text))return false;
        if(strlen(text)!=17)return false;snprintf(profile->ap,18,"%s",text);
        char previous[4];snprintf(previous,sizeof(previous),"%u",profile->channel?profile->channel:1);
        if(!m1_vkb_get_filename("Ch 1-14",previous,text))return false;
        char *end;unsigned long value=strtoul(text,&end,10);if(!text[0] || *end || value<1 || value>14)return false;
        profile->channel=(uint8_t)value;
    }
    const char *ip_options[]={"Skip IP response check","Set private IP address"};option=client_choose("IP check",ip_options,2);if(option<0)return false;
    if(option==0)profile->ip[0]=0;
    else{if(!m1_vkb_get_filename("Private IP",profile->ip,text) || strlen(text)>15)return false;snprintf(profile->ip,16,"%s",text);}
    for(char *p=profile->station;*p;p++)*p=(char)tolower((unsigned char)*p);
    for(char *p=profile->ap;*p;p++)*p=(char)tolower((unsigned char)*p);
    if(!client_profile_valid(profile)){client_message("Bad target","Use distinct unicast MACs","Check the router pairing");return false;}
    return true;
}
static bool client_at(const char *command,char *reply,unsigned capacity)
{
    memset(reply,0,capacity);
    return spi_AT_send_recv(command,reply,(int)capacity,5)==SUCCESS && room_radar_reply_status(reply)==ROOM_RADAR_OK_REPLY;
}
static bool client_support(void)
{
#ifndef M1_APP_WIFI_OFFENSIVE_ENABLE
    client_lab_supported=false;return false;
#else
    char response[192];
    /* Stop/query is non-transmitting and confirms this command family exists. */
    client_lab_supported=client_at("AT+M1DEAUTHSTOP\r\n",response,sizeof(response)) && strstr(response,"+M1DEAUTHSTOP:");
    return client_lab_supported;
#endif
}
static bool client_preflight(const wifi_client_profile_t *saved,wifi_client_profile_t *target,bool support)
{
    if(!client_profile_valid(saved) || !room_radar_connect_network())return false;
    client_lab_view=10;
    client_panel("Client Lab","Finding saved router","No test frames sent",NULL,NULL);
    device_observation_t observation={0};
    if(!wifi_test_saved_ap(saved->ap,&observation) || !observation.found || observation.channel<1 || observation.channel>14){
        client_message("No router","Saved router not visible","Check BSSID and range");return false;}
    *target=*saved;target->channel=(uint8_t)observation.channel;
    if(support && !client_support()){client_message("Not ready","Radio mode unavailable","No reconnect test run");return false;}
    return true;
}
static bool client_review(const wifi_client_profile_t *target,unsigned rounds,bool preview)
{
    char station[32],ap[32],plan[32];snprintf(station,32,"STA %s",target->station);snprintf(ap,32,"AP %s",target->ap);
    for(;;){client_lab_view=5;client_panel("Target",station,ap,"Verify in router list","Next");int key=client_key(pdMS_TO_TICKS(120));
        if(key==BUTTON_BACK_KP_ID)return false;if(key==BUTTON_OK_KP_ID)break;}
    snprintf(plan,32,"5 attempts x %u round%s",rounds,rounds==1?"":"s");
    for(;;){client_lab_view=6;client_panel(preview?"Preview":"Run test?",target->name,plan,preview?"No test frames sent":"Client may disconnect",preview?"Done":"Run");
        int key=client_key(pdMS_TO_TICKS(120));if(key==BUTTON_BACK_KP_ID)return false;if(key==BUTTON_OK_KP_ID)return true;}
}
static bool client_cleanup(void)
{
    char response[192];bool stopped=client_at("AT+M1DEAUTHSTOP\r\n",response,sizeof(response));
    /* Always attempt STA restoration, even if stop returned an error. */
    bool restored=client_at("AT+M1MONITOR=0\r\n",response,sizeof(response));
    if(!stopped || !restored){if(!wifi_room_radar_recover())return false;}
    return room_radar_connect_network();
}
static bool client_record(const wifi_client_profile_t *p,unsigned rounds,unsigned completed,const char *outcome,bool restored)
{
    FIL file;UINT n;char line[220];int size=snprintf(line,sizeof(line),"%lu\t%s\t%s\t%s\t%u\t%u\t%u\t%s\t%s\r\n",
        (unsigned long)HAL_GetTick(),p->name,p->station,p->ap,p->channel,rounds,completed,outcome,restored?"wifi-ready":"wifi-needs-check");
    if(size<0 || (unsigned)size>=sizeof(line) || f_open(&file,"0:/System/wifi-client-tests.tsv",FA_WRITE|FA_OPEN_APPEND)!=FR_OK)return false;
    bool ok=f_write(&file,line,(UINT)size,&n)==FR_OK && n==(UINT)size && f_sync(&file)==FR_OK;
    if(f_close(&file)!=FR_OK)ok=false;return ok;
}
static void client_run(const wifi_client_profile_t *profile,unsigned rounds)
{
#ifndef M1_APP_WIFI_OFFENSIVE_ENABLE
    (void)profile;(void)rounds;client_message("Disabled","This build omits test TX",NULL);return;
#else
    wifi_client_profile_t target;char command[112],response[256];
    if((rounds!=1 && rounds!=3) || !client_preflight(profile,&target,true) || !client_test_command(&target,command,sizeof(command)) ||
       !client_review(&target,rounds,false))return;
    unsigned completed=0;bool failed=false,cancelled=false;uint32_t started=HAL_GetTick();
    for(unsigned round=0;round<rounds;round++){
        if(client_key(0)==BUTTON_BACK_KP_ID){cancelled=true;break;}
        if(HAL_GetTick()-started>=20000U){failed=true;break;}
        char detail[32];snprintf(detail,32,"Round %u/%u: 5 attempts",round+1,rounds);
        client_lab_view=8;client_panel("Testing",target.name,detail,"Back stops next round",NULL);
        /* Never retry this mutation: a timeout may hide an accepted request. */
        ++client_lab_tx_calls;
        if(!client_at(command,response,sizeof(response)) || !client_attempts_reported(response,5)){failed=true;break;}
        ++completed;
        if(round+1<rounds){uint32_t pause=HAL_GetTick();
            while(HAL_GetTick()-pause<5000U)if(client_key(pdMS_TO_TICKS(100))==BUTTON_BACK_KP_ID){cancelled=true;break;}
            if(cancelled)break;}
    }
    client_lab_view=9;client_panel("Restoring","Stopping test mode","Reconnecting Wi-Fi",NULL,NULL);
    bool restored=client_cleanup();
    bool logged=client_record(&target,rounds,completed,cancelled?"cancelled":failed?"radio-error":"requests-accepted-effect-unverified",restored);
    client_message(cancelled?"Cancelled":failed?"Test error":"Requested",restored?"Wi-Fi restored":"Wi-Fi needs recovery","Effect not verified");
    if(!logged)client_message("Log error","Result could not be saved","Check mounted SD card");
#endif
}
static void client_response(const wifi_client_profile_t *profile)
{
    if(!client_profile_valid(profile))return;
    if(!profile->ip[0]){client_message("Need IP","Edit client to set IP","Use a private IPv4");return;}
    if(!room_radar_connect_network())return;
    char command[48],response[192];snprintf(command,48,"AT+PING=\"%s\"\r\n",profile->ip);
    bool replied=client_at(command,response,sizeof(response)) && strstr(response,"+PING:");
    client_record(profile,0,0,replied?"ip-reply":"ip-no-reply",true);
    client_message("IP check",replied?"IP responded":"No IP reply","Not an association check");
}
void wifi_client_lab_open(void)
{
    client_lab_active=true;client_lab_supported=false;
    if(!client_load()){client_message("Load error","Check SD card or backup",NULL);client_lab_active=false;return;}
    for(;;){
        const char *items[CLIENT_LAB_LIMIT+1]={"+ Add Wi-Fi client"};
        for(unsigned i=0;i<client_count;i++)items[i+1]=clients[i].name;
        int index=client_choose("Client Lab",items,client_count+1);if(index<0){client_lab_active=false;return;}
        if(index==0){
            if(client_count==CLIENT_LAB_LIMIT){client_message("List full","Delete a client first",NULL);continue;}
            wifi_client_profile_t p={0};if(!client_edit(&p))continue;
            if(client_duplicate(&p,-1)>=0){client_message("Duplicate","Client and router saved",NULL);continue;}
            clients[client_count++]=p;if(!client_save()){client_message("Save error","Previous list retained",NULL);if(!client_load()){client_lab_active=false;return;}}continue;
        }
        unsigned selected=(unsigned)index-1;
        for(;;){
            const char *modes[]={"Check radio support","Preview (no frames)","Disconnect test (once)","Disconnect test (x3)","Check IP response","Manage client"};
            int mode=client_choose("Test modes",modes,6);if(mode<0)break;
            if(mode==0){wifi_client_profile_t target;if(client_preflight(&clients[selected],&target,true))client_message("Ready beta","Radio command available","Live effect unverified");}
            if(mode==1){wifi_client_profile_t target;if(client_preflight(&clients[selected],&target,false)){
                if(client_review(&target,1,true)){++client_lab_preview;if(!client_record(&target,0,0,"preview-no-test-frames",true))client_message("Log error","Preview was not saved",NULL);}}}
            if(mode==2 || mode==3)client_run(&clients[selected],mode==2?1:3);
            if(mode==4)client_response(&clients[selected]);
            if(mode!=5)continue;
            const char *actions[]={"View target","Rename client","Edit client","Delete client"};
            int action=client_choose("Manage",actions,4);if(action<0)continue;
            if(action==0){client_review(&clients[selected],1,true);continue;}
            wifi_client_profile_t profile=clients[selected];bool removed=false;
            if(action==1){char name[40];if(!m1_vkb_get_filename("Name",profile.name,name))continue;
                snprintf(profile.name,25,"%.24s",name);if(!client_profile_valid(&profile))continue;clients[selected]=profile;}
            else if(action==2){if(!client_edit(&profile))continue;
                if(client_duplicate(&profile,(int)selected)>=0){client_message("Duplicate","Client and router saved",NULL);continue;}clients[selected]=profile;}
            else{if(m1_message_box_choice(&m1_u8g2,"Delete?",profile.name,NULL,"Cancel\nDelete")!=2)continue;
                memmove(&clients[selected],&clients[selected+1],(client_count-selected-1)*sizeof(profile));--client_count;removed=true;}
            if(!client_save()){client_message("Save error","Previous list retained",NULL);if(!client_load()){client_lab_active=false;return;}}
            if(removed || selected>=client_count)break;
        }
    }
}
