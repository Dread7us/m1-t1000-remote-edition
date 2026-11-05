/* See COPYING.txt for license details. */
#ifndef M1_TEXT_H
#define M1_TEXT_H
#include "u8g2.h"
void m1_draw_marquee(u8g2_t *display,int x,int y,int width,const char *text);
void m1_text_reset_scroll(void);
uint8_t m1_text_dialog(u8g2_t *display,const char *title,const char *line1,const char *line2,const char *buttons);
#endif
