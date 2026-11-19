#ifndef FPLFLIP_YUYV_H
#define FPLFLIP_YUYV_H
#include "profile.h"

/* Four indexed pixels or two ARGB4444 pixels per word. Reverse the pixel
 * sequence without changing a palette index, color nibble, or alpha. */
static inline void turn_osd(const uint32_t *restrict src, uint32_t *restrict dst,
                            unsigned words, unsigned indexed) {
    for (unsigned i=0; i<words; ++i) {
        uint32_t v=src[words-1-i];
        dst[i]=indexed ? __builtin_bswap32(v) : (v<<16)|(v>>16);
    }
}

/* Little-endian [Y0 U Y1 V] -> [Y1 U Y0 V]. ARMv7 has REV and ROR. */
static inline uint32_t reverse_pair(uint32_t pair) {
    uint32_t r = __builtin_bswap32(pair);
    return (r >> 8) | (r << 24);
}

/* The source and the private display buffer never overlap. Stage a group
 * in registers before storing so both buses can transfer adjacent words.
 * 810 packed pairs per line leaves a two-word tail. */
static inline void xform_word_rev(const uint32_t *restrict s, uint32_t *restrict d) {
    unsigned n = FPLFLIP_WORDS;
    while (n >= 4) {
        n -= 4;
        uint32_t a = s[n], b = s[n+1], c = s[n+2], e = s[n+3];
        d[0] = reverse_pair(e); d[1] = reverse_pair(c);
        d[2] = reverse_pair(b); d[3] = reverse_pair(a);
        d += 4;
    }
    while (n) *d++ = reverse_pair(s[--n]);
}

static inline void xform_copy(const uint32_t *restrict s, uint32_t *restrict d) {
    unsigned i = 0;
    for (; i + 4 <= FPLFLIP_WORDS; i += 4) {
        uint32_t a = s[i], b = s[i+1], c = s[i+2], e = s[i+3];
        d[i] = a; d[i+1] = b; d[i+2] = c; d[i+3] = e;
    }
    for (; i < FPLFLIP_WORDS; ++i) d[i] = s[i];
}
#endif
