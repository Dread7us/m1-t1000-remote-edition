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
} m1_ui_preferences_t;
#define M1_UI_DEFAULTS {1, 0, 1, 0, 1, 1, 1}
extern m1_ui_preferences_t m1_ui;

static inline uint8_t m1_ui_step(uint8_t value, uint8_t count, int direction, bool wrap)
{
    if (!count) return 0;
    if (value >= count) value = 0;
    if (direction < 0) return value ? value - 1 : (wrap ? count - 1 : 0);
    return value + 1 < count ? value + 1 : (wrap ? 0 : value);
}
#endif
