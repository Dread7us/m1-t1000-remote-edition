/* See COPYING.txt for license details. */
#ifndef M1_IR_REMOTE_VIEW_H_
#define M1_IR_REMOTE_VIEW_H_
#include <stdint.h>
#include <ctype.h>
#include <string.h>

/* Six buttons per portrait page, arranged in two columns. */
static inline uint16_t ir_remote_move(uint16_t selected, uint16_t count, int dx, int dy)
{
    if (!count) return 0;
    if (selected >= count) selected = 0;
    if (dx) {
        uint16_t other = selected ^ 1U;
        return other < count ? other : selected;
    }
    if (dy > 0) return selected + 2 < count ? (uint16_t)(selected + 2) : (uint16_t)(selected & 1U);
    if (dy < 0) {
        if (selected >= 2) return selected - 2;
        uint16_t last = count - 1;
        if ((last & 1U) != (selected & 1U) && last) last--;
        return last;
    }
    return selected;
}

typedef enum {
    IR_ICON_TEXT, IR_ICON_POWER, IR_ICON_MUTE, IR_ICON_PLUS, IR_ICON_MINUS,
    IR_ICON_UP, IR_ICON_DOWN, IR_ICON_PLAY, IR_ICON_PAUSE, IR_ICON_NEXT, IR_ICON_PREVIOUS
} ir_remote_icon_t;

static inline ir_remote_icon_t ir_remote_icon(const char *name)
{
    char key[32]; unsigned n = 0;
    while (*name && n < sizeof(key) - 1) {
        unsigned char c = (unsigned char)*name++;
        if (isalnum(c) || c == '+' || c == '-') key[n++] = (char)tolower(c);
    }
    key[n] = 0;
    if (!strcmp(key,"power") || !strcmp(key,"poweroff") || !strcmp(key,"poweron") || !strcmp(key,"off") || !strcmp(key,"on")) return IR_ICON_POWER;
    if (!strcmp(key,"mute")) return IR_ICON_MUTE;
    if (!strcmp(key,"volup") || !strcmp(key,"volumeup") || !strcmp(key,"vol+") || !strcmp(key,"v+") || !strcmp(key,"volume+")) return IR_ICON_PLUS;
    if (!strcmp(key,"voldn") || !strcmp(key,"voldown") || !strcmp(key,"volumedown") || !strcmp(key,"vol-") || !strcmp(key,"v-") || !strcmp(key,"volume-")) return IR_ICON_MINUS;
    if (!strcmp(key,"chup") || !strcmp(key,"chnext") || !strcmp(key,"channelup") || !strcmp(key,"ch+")) return IR_ICON_UP;
    if (!strcmp(key,"chdn") || !strcmp(key,"chprev") || !strcmp(key,"chdown") || !strcmp(key,"channeldown") || !strcmp(key,"ch-")) return IR_ICON_DOWN;
    if (!strcmp(key,"play")) return IR_ICON_PLAY;
    if (!strcmp(key,"pause")) return IR_ICON_PAUSE;
    if (!strcmp(key,"next")) return IR_ICON_NEXT;
    if (!strcmp(key,"prev") || !strcmp(key,"previous")) return IR_ICON_PREVIOUS;
    return IR_ICON_TEXT;
}
#endif
