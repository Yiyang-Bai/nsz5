/*
 * zigzag.h — signed <-> unsigned zigzag mapping.
 *
 *   0 -> 0, -1 -> 1, +1 -> 2, -2 -> 3, +2 -> 4, ...
 *
 * All operations are done in unsigned arithmetic to avoid signed-shift
 * undefined/implementation-defined behavior. Round-trip is exact for the
 * complete int32 range, including INT32_MIN and INT32_MAX.
 */
#ifndef NSZ_ZIGZAG_H
#define NSZ_ZIGZAG_H

#include <stdint.h>

static inline uint32_t nsz_zigzag_encode(int32_t x)
{
    /* (x << 1) ^ (x >> 31), expressed without UB:
     * the arithmetic right shift by 31 produces all-ones for negatives,
     * which equals (0 - (x < 0)) in unsigned arithmetic. */
    uint32_t ux = (uint32_t)x;
    uint32_t sign = 0u - (ux >> 31);
    return (ux << 1) ^ sign;
}

static inline int32_t nsz_zigzag_decode(uint32_t u)
{
    uint32_t sign = 0u - (u & 1u);
    return (int32_t)((u >> 1) ^ sign);
}

#endif /* NSZ_ZIGZAG_H */
