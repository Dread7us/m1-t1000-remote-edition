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

typedef enum {
    ROOM_RADAR_NO_REPLY,
    ROOM_RADAR_DATA_REPLY,
    ROOM_RADAR_OK_REPLY,
    ROOM_RADAR_ERROR_REPLY
} room_radar_reply_t;

/* The transport can return a complete data line without the final OK, or
 * bundle the final acknowledgement with a later Wi-Fi notification. */
static inline room_radar_reply_t room_radar_reply_status(const char *reply)
{
    if (!reply[0] || !strncmp(reply, "TIMEOUT(", 8) || !strncmp(reply, "SEND_ERR=", 9))
        return ROOM_RADAR_NO_REPLY;
    const char *line = reply;
    while (*line) {
        size_t len = strcspn(line, "\r\n");
        if (line[len] && len == 2 && !memcmp(line, "OK", len)) return ROOM_RADAR_OK_REPLY;
        if (line[len] && len == 5 && !memcmp(line, "ERROR", len)) return ROOM_RADAR_ERROR_REPLY;
        line += len;
        line += strspn(line, "\r\n");
    }
    return ROOM_RADAR_DATA_REPLY;
}

static inline bool room_radar_status_data(const char *reply, room_radar_result_t *result)
{
    const char *line = strstr(reply, "+M1RADAR:");
    return line && strpbrk(line, "\r\n") &&
           room_radar_reply_status(reply) != ROOM_RADAR_ERROR_REPLY &&
           room_radar_parse_result(line, result);
}

static inline bool room_radar_motion(unsigned baseline, unsigned score)
{
    return score > baseline + 12U + baseline / 2U;
}

/* ESP-AT quoted arguments escape separators and reject line/control bytes. */
static inline bool room_radar_quote(const char *input, char *output, unsigned capacity)
{
    unsigned used = 0;
    if (!capacity) return false;
    output[0] = 0;
    while (*input) {
        unsigned char value = (unsigned char)*input++;
        if (value < 32U || value == 127U) return false;
        bool escaped = value == '"' || value == '\\' || value == ',';
        if (used + (escaped ? 2U : 1U) >= capacity) return false;
        if (escaped) output[used++] = '\\';
        output[used++] = (char)value;
    }
    output[used] = 0;
    return true;
}

typedef struct {
    char ssid[33];
    int rssi;
    bool secured;
    bool saved;
} room_radar_network_t;

#define ROOM_RADAR_NETWORKS 16U
bool wifi_room_radar_scan(room_radar_reading_t *reading);
unsigned wifi_room_radar_networks(room_radar_network_t *networks, unsigned capacity);
bool wifi_room_radar_join(const char *ssid, const char *password, bool use_saved,
                          bool *credentials_saved);
void wifi_room_radar_prepare(void);
bool wifi_room_radar_recover(void);
void wifi_room_radar(void);
void room_radar_run(void);

#endif
