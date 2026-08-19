/* Context model unit tests: determinism, bounds, monotonicity, causality. */
#include "../src/context.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static int failures = 0;

static void fail(const char *name, const char *why)
{
    fprintf(stderr, "FAIL %-24s %s\n", name, why);
    failures++;
}

int main(void)
{
    /* determinism: two instances fed identically must agree at every step */
    {
        NszContext a, b;
        nsz_context_init(&a);
        nsz_context_init(&b);
        srand(3);
        int ok = 1;
        for (int i = 0; i < 1000000; i++) {
            if (nsz_context_id(&a) != nsz_context_id(&b) ||
                nsz_context_group(&a) != nsz_context_group(&b)) {
                fail("determinism", "instances diverged");
                ok = 0;
                break;
            }
            uint32_t r = (uint32_t)(rand() % 300);
            nsz_context_update(&a, r);
            nsz_context_update(&b, r);
        }
        if (ok)
            printf("PASS %-24s 1M steps identical\n", "determinism");
    }

    /* bounds: id < 256, group < 4 under adversarial inputs */
    {
        NszContext c;
        nsz_context_init(&c);
        srand(17);
        int ok = 1;
        for (int i = 0; i < 1000000; i++) {
            uint32_t id = nsz_context_id(&c);
            uint32_t g = nsz_context_group(&c);
            if (id >= NSZ_CTX_COUNT || g >= NSZ_CTX_GROUPS) {
                fail("bounds", "id or group out of range");
                ok = 0;
                break;
            }
            /* adversarial: mostly extremes, up to the residual bound 2^18 */
            uint32_t r;
            switch (rand() % 4) {
            case 0: r = 0; break;
            case 1: r = (uint32_t)(1u << 18); break;
            case 2: r = (uint32_t)(rand() % 5); break;
            default: r = (uint32_t)(rand() % (1 << 18)); break;
            }
            nsz_context_update(&c, r);
        }
        if (ok)
            printf("PASS %-24s 1M adversarial steps in range\n", "bounds");
    }

    /* monotonicity: sustained low noise must land in a lower volatility
     * level than sustained high noise */
    {
        NszContext lo, hi;
        nsz_context_init(&lo);
        nsz_context_init(&hi);
        for (int i = 0; i < 1000; i++) {
            nsz_context_update(&lo, 1);
            nsz_context_update(&hi, 1000);
        }
        uint32_t llo = nsz_ctx_vol_level(lo.vol);
        uint32_t lhi = nsz_ctx_vol_level(hi.vol);
        if (llo >= lhi)
            fail("monotonicity", "low-noise level not below high-noise");
        else
            printf("PASS %-24s quiet level %u < noisy level %u\n",
                   "monotonicity", llo, lhi);
    }

    /* surprise: after steady noise, a huge residual must raise surprise */
    {
        NszContext c;
        nsz_context_init(&c);
        for (int i = 0; i < 1000; i++)
            nsz_context_update(&c, 20);
        uint32_t steady = nsz_context_id(&c) % NSZ_CTX_SURPRISE;
        nsz_context_update(&c, 5000);
        uint32_t shocked = nsz_context_id(&c) % NSZ_CTX_SURPRISE;
        if (shocked <= steady)
            fail("surprise", "large residual did not raise surprise");
        else
            printf("PASS %-24s steady s=%u -> shock s=%u\n",
                   "surprise", steady, shocked);
    }

    /* vol level quantization: 4 levels per octave, monotone non-decreasing */
    {
        uint32_t prev = 0;
        int ok = 1;
        for (uint32_t v = 0; v < (1u << 22); v++) {
            uint32_t l = nsz_ctx_vol_level(v);
            if (l < prev || l > 63) {
                fail("quantization", "level not monotone or out of range");
                ok = 0;
                break;
            }
            prev = l;
        }
        if (ok)
            printf("PASS %-24s exhaustive over V in [0, 2^22)\n",
                   "quantization");
    }

    if (failures) {
        fprintf(stderr, "test_context: %d FAILURES\n", failures);
        return 1;
    }
    printf("test_context: all passed\n");
    return 0;
}
