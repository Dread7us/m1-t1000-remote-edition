/* See COPYING.txt for license details. */
#ifndef M1_DEVICE_LIBRARY_H
#define M1_DEVICE_LIBRARY_H
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

#define DEVICE_LIMIT 16U
typedef struct {
    char name[25];
    char address[18];
    uint8_t kind; /* 0: LAN IPv4, 1: Wi-Fi access point, 2: Bluetooth LE */
} saved_device_t;

static inline bool device_address_valid(unsigned kind, const char *address)
{
    if (kind == 0) {
        for (const char *p = address; *p; ++p)
            if (!isdigit((unsigned char)*p) && *p != '.') return false;
        unsigned a, b, c, d;
        int end = 0;
        if (sscanf(address, "%u.%u.%u.%u%n", &a, &b, &c, &d, &end) != 4 ||
            address[end] || a > 255 || b > 255 || c > 255 || d > 255) return false;
        return (a == 10 || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168)) &&
               d != 0 && d != 255;
    }
    if (kind > 2 || strlen(address) != 17) return false;
    for (unsigned i = 0; i < 17; ++i)
        if (i % 3 == 2 ? address[i] != ':' : !isxdigit((unsigned char)address[i])) return false;
    return true;
}
static inline bool device_valid(const saved_device_t *device)
{
    if (!memchr(device->name, 0, sizeof(device->name)) ||
        !memchr(device->address, 0, sizeof(device->address)) || !device->name[0]) return false;
    for (const char *p = device->name; *p; ++p)
        if ((unsigned char)*p < 32 || (unsigned char)*p == 127) return false;
    return device_address_valid(device->kind, device->address);
}
void device_library_run(void);
bool device_library_save_ap(const char *name, const char *bssid);
#endif
