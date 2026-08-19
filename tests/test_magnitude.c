/*
 * Magnitude class + mantissa mapping tests.
 *
 * Exhaustive round-trip over the full supported zigzag range [0, 2^19-1]
 * in one adaptive stream, plus random sequences and specific corner values.
 */
#include "../src/magnitude.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void fail(const char *name, const char *why)
{
    fprintf(stderr, "FAIL %-24s %s\n", name, why);
    failures++;
}

static void roundtrip(const char *name, const uint32_t *vals, size_t n)
{
    /* worst case ~3 bytes/value */
    size_t cap = n * 4 + 64;
    uint8_t *buf = (uint8_t *)malloc(cap);
    NszMagModels *mm = (NszMagModels *)malloc(sizeof(NszMagModels));

    /* deterministic pseudo-random context walk, replayed at decode */
    nsz_mag_models_init(mm);
    NszRcEnc enc;
    nsz_rc_enc_init(&enc, buf, cap);
    for (size_t i = 0; i < n; i++) {
        uint32_t ctx = (uint32_t)(i * 2654435761u) % NSZ_CTX_COUNT;
        uint32_t grp = (uint32_t)(i * 40503u) % NSZ_CTX_GROUPS;
        if (nsz_mag_encode(&enc, mm, ctx, grp, vals[i]) != 0) {
            fail(name, "encode rejected in-range value");
            goto done;
        }
    }
    nsz_rc_enc_flush(&enc);
    if (enc.overflow) {
        fail(name, "encoder overflow");
        goto done;
    }

    nsz_mag_models_init(mm); /* decoder starts from the same fresh state */
    NszRcDec dec;
    nsz_rc_dec_init(&dec, buf, enc.pos);
    for (size_t i = 0; i < n; i++) {
        uint32_t ctx = (uint32_t)(i * 2654435761u) % NSZ_CTX_COUNT;
        uint32_t grp = (uint32_t)(i * 40503u) % NSZ_CTX_GROUPS;
        uint32_t u;
        if (nsz_mag_decode(&dec, mm, ctx, grp, &u) != 0 || u != vals[i]) {
            char msg[80];
            snprintf(msg, sizeof(msg),
                     "value %zu: expected %u got %u", i, vals[i], u);
            fail(name, msg);
            goto done;
        }
    }
    if (dec.error) {
        fail(name, "decoder read past end");
        goto done;
    }
    printf("PASS %-24s n=%-8zu encoded=%zu bytes\n", name, n, enc.pos);
done:
    free(buf);
    free(mm);
}

int main(void)
{
    enum { RANGE = 1u << 19, NRAND = 1000000 };
    uint32_t *vals =
        (uint32_t *)malloc((RANGE > NRAND ? RANGE : NRAND) * sizeof(uint32_t));
    size_t i;

    /* corner values */
    {
        uint32_t corners[] = { 0, 1, 2, 3, 4, 7, 8, 15, 16, 255, 256,
                               1023, 1024, 65535, 65536,
                               RANGE - 2, RANGE - 1, RANGE,
                               (1u << 20) - 2 /* true upper bound */ };
        roundtrip("corners", corners, sizeof(corners) / sizeof(corners[0]));
    }

    /* exhaustive: every value in the supported range, ascending */
    for (i = 0; i < RANGE; i++)
        vals[i] = (uint32_t)i;
    roundtrip("exhaustive-ascending", vals, RANGE);

    /* exhaustive descending (different adaptation path) */
    for (i = 0; i < RANGE; i++)
        vals[i] = (uint32_t)(RANGE - 1 - i);
    roundtrip("exhaustive-descending", vals, RANGE);

    /* small values only (the realistic case) */
    srand(11);
    for (i = 0; i < NRAND; i++)
        vals[i] = (uint32_t)(rand() % 64);
    roundtrip("small-values", vals, NRAND);

    /* random full-range */
    srand(22);
    for (i = 0; i < NRAND; i++)
        vals[i] = ((uint32_t)rand() * 2654435761u) % RANGE;
    roundtrip("random-full-range", vals, NRAND);

    /* out-of-range value must be rejected by the encoder */
    {
        uint8_t buf[64];
        NszMagModels *mm = (NszMagModels *)malloc(sizeof(NszMagModels));
        nsz_mag_models_init(mm);
        NszRcEnc enc;
        nsz_rc_enc_init(&enc, buf, sizeof(buf));
        if (nsz_mag_encode(&enc, mm, 0, 0, (1u << 20) - 1) == 0)
            fail("out-of-range", "encoder accepted 2^20-1");
        else
            printf("PASS %-24s encoder rejects 2^20-1\n", "out-of-range");
        free(mm);
    }

    free(vals);
    if (failures) {
        fprintf(stderr, "test_magnitude: %d FAILURES\n", failures);
        return 1;
    }
    printf("test_magnitude: all passed\n");
    return 0;
}
