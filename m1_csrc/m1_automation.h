/* See COPYING.txt for license details. */
#ifndef M1_AUTOMATION_H
#define M1_AUTOMATION_H
#include <stdint.h>
#include <stdbool.h>
#define AUTOMATION_STEPS 16
#define AUTOMATION_PATH 192
#define AUTOMATION_NAME 32
#define AUTOMATION_WAIT_MAX 60000
typedef enum { ACTION_OK, ACTION_CANCELLED, ACTION_FAILED } m1_action_result_t;
typedef bool (*m1_action_cancel_fn)(void);
typedef enum { AUTO_IR=1, AUTO_SUBGHZ=2, AUTO_WAIT=3 } automation_type_t;
typedef struct {
    uint8_t type;
    uint16_t button;
    uint32_t wait_ms, fingerprint;
    char label[AUTOMATION_NAME];
    char signal_name[AUTOMATION_NAME];
    char path[AUTOMATION_PATH];
} automation_step_t;
typedef struct {
    char name[AUTOMATION_NAME];
    uint8_t count;
    automation_step_t steps[AUTOMATION_STEPS];
} automation_t;
bool automation_load(const char *path, automation_t *chain);
bool automation_save(const char *path, const automation_t *chain);
bool automation_fingerprint(const char *path, uint32_t *result);
bool automation_move(automation_t *chain, unsigned index, int direction);
bool automation_remove(automation_t *chain, unsigned index);
bool automation_sgh_info(const char *path, uint32_t *frequency, uint8_t *modulation);
void automation_run(void);
#endif
