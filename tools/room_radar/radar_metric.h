/* See COPYING.txt for license details. */
#ifndef ROOM_RADAR_METRIC_H
#define ROOM_RADAR_METRIC_H

#include <stdint.h>
#include <stdbool.h>

#define RADAR_BINS 24U
#define RADAR_MAX_FRAMES 200U

typedef struct {
    uint16_t previous[RADAR_BINS];
    unsigned frames;
    unsigned total_change;
} radar_metric_t;

/* Compare normalized magnitudes from successive frames from the same AP.
 * Values describe variation in the RF path, not a head count. */
static inline void radar_metric_add(radar_metric_t *metric, const int8_t *iq)
{
    if (metric->frames >= RADAR_MAX_FRAMES) return;
    unsigned bins[RADAR_BINS];
    unsigned total = 0;
    for (unsigned i = 0; i < RADAR_BINS; ++i) {
        int a = iq[2U * i], b = iq[2U * i + 1U];
        bins[i] = (unsigned)(a < 0 ? -a : a) + (unsigned)(b < 0 ? -b : b);
        total += bins[i];
    }
    if (!total) return;
    unsigned delta = 0;
    for (unsigned i = 0; i < RADAR_BINS; ++i) {
        unsigned normalized = bins[i] * 1000U / total;
        unsigned previous = metric->previous[i];
        delta += normalized > previous ? normalized - previous : previous - normalized;
        metric->previous[i] = (uint16_t)normalized;
    }
    if (metric->frames) metric->total_change += delta / 2U;
    ++metric->frames;
}

static inline unsigned radar_metric_score(const radar_metric_t *metric)
{
    return metric->frames > 1 ? metric->total_change / (metric->frames - 1U) : 0;
}

/* Bound the probe to a directly attached private subnet with at most 254 hosts.
 * Inputs use host byte order. Reject non-contiguous or host-only masks. */
static inline bool radar_subnet(uint32_t ip, uint32_t mask, uint32_t *first, uint32_t *last)
{
    uint32_t host_bits = ~mask;
    bool private_ip = (ip >> 24) == 10U || (ip >> 20) == 0xac1U || (ip >> 16) == 0xc0a8U;
    if (!private_ip || host_bits < 3U || host_bits > 255U || (host_bits & (host_bits + 1U)))
        return false;
    uint32_t network = ip & mask;
    if (ip == network || ip == (network | host_bits)) return false;
    *first = network + 1U;
    *last = (network | host_bits) - 1U;
    return true;
}

static inline bool radar_is_echo_reply(const uint8_t *packet, unsigned length,
                                      uint16_t id, uint16_t sequence)
{
    if (length < 28U || (packet[0] >> 4) != 4U || packet[9] != 1U) return false;
    unsigned offset = (packet[0] & 15U) * 4U;
    if (offset < 20U || length < offset + 8U) return false;
    const uint8_t *icmp = packet + offset;
    return icmp[0] == 0U && icmp[1] == 0U &&
           ((unsigned)icmp[4] << 8 | icmp[5]) == id &&
           ((unsigned)icmp[6] << 8 | icmp[7]) == sequence;
}

#endif
