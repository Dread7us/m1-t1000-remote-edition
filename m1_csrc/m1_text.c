/* See COPYING.txt for license details. */
#include "main.h"
#include "m1_display.h"
#include "m1_text.h"
#include <stdio.h>
#include <string.h>
#include <ctype.h>

#define TEXT_LENGTH 512
static uint32_t scroll_start;
static char dialog_body[TEXT_LENGTH*2+2];

void m1_text_reset_scroll(void) {scroll_start=HAL_GetTick();}
void m1_draw_marquee(u8g2_t *display,int x,int y,int width,const char *text) {
    if(!text || width<=0)return;
    if(u8g2_GetStrWidth(display,text)<=width){m1_draw_text(display,x,y,width,text,TEXT_ALIGN_LEFT);return;}
    size_t len=strlen(text),tail=len;char window[TEXT_LENGTH+1];
    /* Determine the final character-aligned window that fits. */
    while(tail && u8g2_GetStrWidth(display,text+tail-1)<=width)--tail;
    unsigned tick=(HAL_GetTick()-scroll_start)/180,travel=(unsigned)tail;
    unsigned phase=tick%(travel*2+16),offset;
    if(phase<8)offset=0;
    else if(phase<8+travel)offset=phase-8;
    else if(phase<16+travel)offset=travel;
    else offset=travel*2+16-phase;
    size_t n=len-offset;if(n>TEXT_LENGTH)n=TEXT_LENGTH;
    memcpy(window,text+offset,n);window[n]=0;
    while(n && u8g2_GetStrWidth(display,window)>width)window[--n]=0;
    u8g2_DrawStr(display,x,y,window);
}
/* Wrap without discarding source characters. Returns the next unread offset. */
static size_t next_line(u8g2_t *display,const char *text,size_t offset,char *line,unsigned width) {
    size_t start=offset,n=0,last_space=0;
    while(text[offset] && text[offset]!='\n' && n<127) {
        line[n++]=text[offset++];line[n]=0;
        if(u8g2_GetStrWidth(display,line)>width){--n;--offset;line[n]=0;break;}
        if(isspace((unsigned char)line[n-1]))last_space=n;
    }
    if(text[offset] && text[offset]!='\n' && last_space){offset=start+last_space;line[last_space]=0;}
    if(offset==start && text[offset] && text[offset]!='\n'){line[0]=text[offset++];line[1]=0;}
    if(text[offset]=='\n')++offset;
    return offset;
}
static unsigned page_count(u8g2_t *display,const char *text) {
    unsigned lines=0;size_t offset=0;char line[128];
    do {offset=next_line(display,text,offset,line,118);++lines;}while(text[offset]);
    return (lines+2)/3;
}
static void draw_page(u8g2_t *display,const char *text,unsigned page) {
    size_t offset=0;char line[128];
    for(unsigned i=0;i<(page+1)*3;++i) {
        line[0]=0;offset=next_line(display,text,offset,line,118);
        if(i>=page*3)u8g2_DrawStr(display,5,22+(i%3)*10,line);
    }
}
uint8_t m1_text_dialog(u8g2_t *display,const char *title,const char *line1,const char *line2,const char *buttons) {
    unsigned page=0,cursor=0;
    snprintf(dialog_body,sizeof(dialog_body),"%s%s%s",line1?line1:"",line1&&line2&&line1[0]&&line2[0]?"\n":"",line2?line2:"");
    unsigned count=buttons?u8x8_GetStringLineCnt(buttons):0;
    m1_text_reset_scroll();
    for (;;) {
        u8g2_FirstPage(display);u8g2_SetFont(display,M1_DISP_FUNC_MENU_FONT_N);
        unsigned pages=page_count(display,dialog_body);if(page>=pages)page=pages-1;
        char badge[24];snprintf(badge,sizeof(badge),"%u/%u",page+1,pages);
        m1_draw_header_bar(display,title,badge);
        u8g2_SetDrawColor(display,1);u8g2_SetFont(display,M1_DISP_FUNC_MENU_FONT_N);
        draw_page(display,dialog_body,page);
        if(!count)m1_draw_bottom_bar(display,button_back_8x8,"Back",pages>1?"Up/Down":"OK",button_ok_8x8);
        else {
            unsigned first=cursor>=2?cursor-1:0,shown=count<3?count:3;if(first+shown>count)first=count-shown;
            for(unsigned i=first;i<first+shown;++i) {
                const char *p=u8x8_GetStringLineStart(i,buttons);char label[64];unsigned n=0;
                while(p[n]&&p[n]!='\n'&&n<sizeof(label)-1){label[n]=p[n];++n;}label[n]=0;
                int w=124/shown,x=2+(i-first)*w;m1_ui_selection(display,x,51,w-2,12,i==cursor);
                if(i==cursor)m1_draw_marquee(display,x+2,60,w-6,label);else m1_draw_text(display,x+2,60,w-6,label,TEXT_ALIGN_CENTER);
            }
        }
        u8g2_NextPage(display);
        S_M1_Main_Q_t event;S_M1_Buttons_Status key;
        if(xQueueReceive(main_q_hdl,&event,pdMS_TO_TICKS(120))!=pdTRUE || event.q_evt_type!=Q_EVENT_KEYPAD)continue;
        if(xQueueReceive(button_events_q_hdl,&key,0)!=pdTRUE)continue;
        if(key.event[BUTTON_BACK_KP_ID]==BUTTON_EVENT_CLICK){xQueueReset(main_q_hdl);xQueueReset(button_events_q_hdl);return 0;}
        if(key.event[BUTTON_OK_KP_ID]==BUTTON_EVENT_CLICK){xQueueReset(main_q_hdl);xQueueReset(button_events_q_hdl);return count?(uint8_t)(cursor+1):1;}
        if(key.event[BUTTON_UP_KP_ID]==BUTTON_EVENT_CLICK){if(pages>1)page=page?page-1:pages-1;else if(count)cursor=cursor?cursor-1:count-1;}
        if(key.event[BUTTON_DOWN_KP_ID]==BUTTON_EVENT_CLICK){if(pages>1)page=(page+1)%pages;else if(count)cursor=(cursor+1)%count;}
        if(key.event[BUTTON_LEFT_KP_ID]==BUTTON_EVENT_CLICK && count)cursor=cursor?cursor-1:count-1;
        if(key.event[BUTTON_RIGHT_KP_ID]==BUTTON_EVENT_CLICK && count)cursor=(cursor+1)%count;
        m1_text_reset_scroll();
    }
}
