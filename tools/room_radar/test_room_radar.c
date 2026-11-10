#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "m1_room_radar.h"
#include "radar_metric.h"

int main(void)
{
    room_radar_result_t result;
    assert(room_radar_parse_result("\r\n+M1RADAR:1,2,32,40\r\nOK\r\n", &result));
    assert(result.state == ROOM_RADAR_DONE && result.samples == 32 && result.value == 40);
    assert(!room_radar_parse_result("+M1RADAR:2,2,32,40\r\n", &result));
    assert(!room_radar_parse_result("+M1RADAR:1,2,255,40\r\n", &result));
    assert(!room_radar_parse_result("+M1RADAR:1,2,32,40x\r\n", &result));
    assert(!room_radar_parse_result("ERROR\r\n", &result));
    assert(!room_radar_motion(10, 27) && room_radar_motion(10, 28));

    assert(room_radar_status_data("+M1RADAR:1,0,0,0\r\n", &result));
    assert(room_radar_status_data("+M1RADAR:1,2,32,40\r\nOK\r\nWIFI CONNECTED\r\n", &result));
    assert(!room_radar_status_data("+M1RADAR:1,2,32,4", &result));
    assert(!room_radar_status_data("+M1RADAR:1,2,32,40\r\nERROR\r\n", &result));
    assert(room_radar_reply_status("SEND_ERR=4") == ROOM_RADAR_NO_REPLY);
    assert(room_radar_reply_status("TIMEOUT(3s)") == ROOM_RADAR_NO_REPLY);
    assert(room_radar_reply_status("\r\nERROR\r\n") == ROOM_RADAR_ERROR_REPLY);
    assert(room_radar_reply_status("BOOK\r\n") == ROOM_RADAR_DATA_REPLY);
    assert(room_radar_reply_status("OK\r\nWIFI GOT IP\r\n") == ROOM_RADAR_OK_REPLY);

    uint32_t first, last;
    assert(radar_subnet(0xc0a80105U, 0xffffff00U, &first, &last));
    assert(first == 0xc0a80101U && last == 0xc0a801feU);
    assert(radar_subnet(0xac1f0005U, 0xfffffff8U, &first, &last));
    assert(first == 0xac1f0001U && last == 0xac1f0006U);
    assert(!radar_subnet(0xac200005U, 0xffffff00U, &first, &last));
    assert(!radar_subnet(0xc0a80105U, 0xfffffe00U, &first, &last));
    assert(!radar_subnet(0xc0a80105U, 0xfffffffaU, &first, &last));
    assert(!radar_subnet(0xc0a80100U, 0xffffff00U, &first, &last));

    radar_metric_t metric = {0};
    int8_t a[RADAR_BINS * 2], b[RADAR_BINS * 2] = {0};
    memset(a, 20, sizeof(a));
    radar_metric_add(&metric, b);
    assert(metric.frames == 0);
    radar_metric_add(&metric, a);
    memset(a, 40, sizeof(a));
    radar_metric_add(&metric, a);
    assert(metric.frames == 2 && radar_metric_score(&metric) == 0);
    b[0] = -128;
    radar_metric_add(&metric, b);
    assert(radar_metric_score(&metric) > 100);
    for (unsigned i = 0; i < 500; ++i) radar_metric_add(&metric, a);
    assert(metric.frames == RADAR_MAX_FRAMES);

    uint8_t packet[40] = {0};
    packet[0] = 0x46;
    packet[9] = 1;
    packet[28] = 0x12;
    packet[29] = 0x34;
    packet[31] = 0x06;
    assert(radar_is_echo_reply(packet, 32, 0x1234, 6));
    assert(!radar_is_echo_reply(packet, 31, 0x1234, 6));
    assert(!radar_is_echo_reply(packet, 32, 0x1234, 7));
    packet[24] = 8;
    assert(!radar_is_echo_reply(packet, 32, 0x1234, 6));
    puts("PASS: Room Radar CSI normalization, threshold, reply decoding, and private subnet bounds");
    return 0;
}
