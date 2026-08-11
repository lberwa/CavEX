/* Fuer beide Builds benoetigt: */

/* __isIPL: weder libogc-eigenes noch 3.1.0 definieren das.
 * ogc_crt0.o prueft damit ob vom IPL gebootet; im HBC immer 0. */
int __isIPL(void) { return 0; }

/* Nur fuer libogc 3.1.0 (MY=0): */
#ifdef BUILD_LIBOGC31

#include <stdint.h>

/*
 * DCStoreRange: in libogc 3.1.0 static inline, wiitoolsmodule.o
 * (gegen altes libogc gebaut) erwartet eine echte Funktion.
 */
void DCStoreRange(void *addr, uint32_t len) {
    uint8_t *a = (uint8_t *)(((uint32_t)addr) & ~31U);
    uint8_t *end = (uint8_t *)addr + len;
    while (a < end) {
        __asm__ volatile("dcbst 0, %0" : : "r"(a));
        a += 32;
    }
    __asm__ volatile("sync" : : : "memory");
}

#endif /* BUILD_LIBOGC31 */
