/* See COPYING.txt for license details. */
#ifndef M1_KASEIKYO_FIELDS_H
#define M1_KASEIKYO_FIELDS_H
#include <stdint.h>
/* Flipper packs id/vendor/genres into its 26-bit address. IRMP keeps the
 * vendor in address, genre1/id/command in command and genre2 in flags. */
static inline void m1_kaseikyo_decode(uint32_t address,uint32_t command,uint16_t *vendor,uint16_t *data,uint8_t *flags) {
    *vendor=(uint16_t)(address>>8);
    *data=(uint16_t)((command&0x3ff)|(((address>>24)&3)<<10)|(((address>>4)&15)<<12));
    *flags=(uint8_t)((address&15)<<4);
}
static inline uint32_t m1_kaseikyo_address(uint16_t vendor,uint16_t command,uint8_t flags) {
    return ((uint32_t)((command>>10)&3)<<24)|((uint32_t)vendor<<8)|((uint32_t)(command>>12)<<4)|(flags>>4);
}
#endif
