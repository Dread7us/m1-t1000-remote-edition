/* See COPYING.txt for license details. */
#ifndef M1_UI_THEME_H
#define M1_UI_THEME_H
#include <stdint.h>
#include <stdbool.h>

/* Kept independent of hardware and upstream's fixed firmware config struct. */
typedef struct {
    uint8_t style;       /* Classic, Soft, Minimal */
    uint8_t outline;     /* 0=Filled, 1=Outline, 2=Underline */
    uint8_t clock;
    uint8_t clock_12h;
    uint8_t battery;
    uint8_t animations;
    uint8_t wrap;
    uint8_t sort;
} m1_ui_preferences_t;
#define M1_UI_DEFAULTS {1, 0, 1, 0, 1, 1, 1, M1_SORT_ALPHA}
enum { M1_SORT_ALPHA, M1_SORT_NEWEST, M1_SORT_OLDEST, M1_SORT_DEFAULT };

static inline int m1_name_compare(const char *a, const char *b)
{
    for (;;) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y || !x) return (int)x - (int)y;
    }
}

static inline bool m1_sort_before(const char *a, uint32_t a_date, const char *b,
                                  uint32_t b_date, uint8_t mode)
{
    if (mode == M1_SORT_DEFAULT) return false;
    if (a_date != b_date) {
        if (mode == M1_SORT_NEWEST) return a_date > b_date;
        if (mode == M1_SORT_OLDEST) return a_date < b_date;
    }
    return m1_name_compare(a, b) < 0;
}

extern m1_ui_preferences_t m1_ui;

static inline uint8_t m1_ui_step(uint8_t value, uint8_t count, int direction, bool wrap)
{
    if (!count) return 0;
    if (value >= count) value = 0;
    if (direction < 0) return value ? value - 1 : (wrap ? count - 1 : 0);
    return value + 1 < count ? value + 1 : (wrap ? 0 : value);
}
#endif
