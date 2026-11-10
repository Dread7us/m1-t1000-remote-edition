/* See COPYING.txt for license details. */
#ifndef M1_ROOM_RADAR_H
#define M1_ROOM_RADAR_H

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ROOM_RADAR_MIN_SAMPLES 12U
#define ROOM_RADAR_MAX_SCORE 1000U

typedef struct {
    char room[25];
    char network[33];
    unsigned networks;
    int rssi;
    unsigned channel;
} room_radar_reading_t;

typedef enum {
    ROOM_RADAR_IDLE, ROOM_RADAR_RUNNING, ROOM_RADAR_DONE, ROOM_RADAR_ERROR
} room_radar_state_t;

typedef struct {
    unsigned state;
    unsigned samples;
    unsigned value;
} room_radar_result_t;

static inline bool room_radar_parse_result(const char *reply, room_radar_result_t *result)
{
    const char *line = strstr(reply, "+M1RADAR:");
    unsigned version;
    int end = 0;
    room_radar_result_t parsed;
    if (!line || sscanf(line, "+M1RADAR:%u,%u,%u,%u%n", &version, &parsed.state,
                        &parsed.samples, &parsed.value, &end) != 4 ||
        version != 1 || parsed.state > ROOM_RADAR_ERROR ||
        parsed.samples > 254 || parsed.value > ROOM_RADAR_MAX_SCORE ||
        (line[end] != '\r' && line[end] != '\n' && line[end] != '\0')) return false;
    *result = parsed;
    return true;
}

static inline bool room_radar_motion(unsigned baseline, unsigned score)
{
    return score > baseline + 12U + baseline / 2U;
}

bool wifi_room_radar_scan(room_radar_reading_t *reading);
void wifi_room_radar(void);
void room_radar_run(void);

#endif
