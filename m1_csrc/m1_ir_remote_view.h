/* See COPYING.txt for license details. */
#ifndef M1_IR_REMOTE_VIEW_H_
#define M1_IR_REMOTE_VIEW_H_
#include <stdint.h>
#include <ctype.h>
#include <string.h>
#include <stdbool.h>
#define IR_REMOTE_EMPTY UINT16_MAX
#define IR_REMOTE_GRID_MAX 70

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

/* Presentation order only: source signal indexes and file contents stay intact. */
static inline uint16_t ir_remote_grid_build(const char *const *names, uint16_t count,
                                           uint16_t *slots, uint16_t capacity)
{
    if (!count || count > 64 || capacity < count + 6) return 0;
    bool used[64] = {false};
    const ir_remote_icon_t roles[6] = {IR_ICON_POWER, IR_ICON_MUTE, IR_ICON_PLUS,
                                      IR_ICON_UP, IR_ICON_MINUS, IR_ICON_DOWN};
    bool television = false;
    for (uint16_t i = 0; i < count; i++) {
        ir_remote_icon_t role = ir_remote_icon(names[i]);
        if (role == IR_ICON_PLUS || role == IR_ICON_MINUS || role == IR_ICON_UP || role == IR_ICON_DOWN)
            television = true;
    }
    uint16_t n = television ? 6 : 0;
    if (television) {
        for (uint16_t slot = 0; slot < 6; slot++) {
            slots[slot] = IR_REMOTE_EMPTY;
            for (uint16_t i = 0; i < count; i++)
                if (!used[i] && ir_remote_icon(names[i]) == roles[slot]) {
                    slots[slot] = i; used[i] = true; break;
                }
        }
        /* Input is a useful alternative to Mute. Keep a vacant slot when
           neither exists so volume/channel columns never move upward. */
        if (slots[1] == IR_REMOTE_EMPTY) {
            for (uint16_t i = 0; i < count; i++) {
                char key[32]; unsigned k = 0;
                for (const char *p = names[i]; *p && k < 31; p++)
                    if (isalnum((unsigned char)*p)) key[k++] = (char)tolower((unsigned char)*p);
                key[k] = 0;
                if (!used[i] && (!strcmp(key,"input") || !strcmp(key,"source") || !strcmp(key,"inputselect"))) {
                    slots[1] = i; used[i] = true; break;
                }
            }
        }
    } else {
        for (unsigned role = 0; role < 2; role++)
            for (uint16_t i = 0; i < count; i++)
                if (!used[i] && ir_remote_icon(names[i]) == roles[role]) {
                    slots[n++] = i; used[i] = true; break;
                }
    }
    for (uint16_t i = 0; i < count; i++) if (!used[i]) slots[n++] = i;
    if (n & 1U) slots[n++] = IR_REMOTE_EMPTY;
    return n;
}

static inline uint16_t ir_remote_grid_move(uint16_t selected, const uint16_t *slots,
                                          uint16_t count, int dx, int dy)
{
    if (!count) return 0;
    int step = dx ? (dx > 0 ? 1 : -1) : (dy > 0 ? 2 : -2);
    uint16_t next = selected;
    for (uint16_t i = 0; i < count; i++) {
        next = (uint16_t)(((int)next + step + count) % count);
        if (slots[next] != IR_REMOTE_EMPTY) return next;
    }
    return selected;
}
#endif
