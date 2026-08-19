/* Exhaustive-ish zigzag round-trip tests. */
#include "../src/zigzag.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void check(int32_t x)
{
    uint32_t u = nsz_zigzag_encode(x);
    int32_t back = nsz_zigzag_decode(u);
    if (back != x) {
        fprintf(stderr, "FAIL zigzag: %d -> %u -> %d\n", x, u, back);
        failures++;
    }
}

static void check_pair(int32_t x, uint32_t expect)
{
    uint32_t u = nsz_zigzag_encode(x);
    if (u != expect) {
        fprintf(stderr, "FAIL zigzag map: %d -> %u (expected %u)\n",
                x, u, expect);
        failures++;
    }
}

int main(void)
{
    /* the defining small-value mapping */
    check_pair(0, 0u);
    check_pair(-1, 1u);
    check_pair(1, 2u);
    check_pair(-2, 3u);
    check_pair(2, 4u);

    /* full 18-bit window around zero (covers every int16 residual and
     * every possible delta of two int16 values) */
    for (int32_t x = -131072; x <= 131072; x++)
        check(x);

    /* powers of two and their neighbours across the int32 range */
    for (int shift = 0; shift < 31; shift++) {
        int32_t p = (int32_t)(1u << shift);
        check(p - 1); check(p); check(p + 1);
        check(-p - 1); check(-p); check(-p + 1);
    }

    /* extremes */
    check(INT32_MAX);
    check(INT32_MIN);
    check(INT32_MAX - 1);
    check(INT32_MIN + 1);

    /* random 32-bit values, deterministic seed */
    srand(12345);
    for (int i = 0; i < 10000000; i++) {
        uint32_t r = ((uint32_t)rand() << 17) ^ ((uint32_t)rand() << 3) ^
                     (uint32_t)rand();
        check((int32_t)r);
    }

    if (failures) {
        fprintf(stderr, "test_zigzag: %d FAILURES\n", failures);
        return 1;
    }
    printf("test_zigzag: all passed\n");
    return 0;
}
