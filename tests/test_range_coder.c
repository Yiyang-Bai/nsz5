/*
 * Standalone range coder tests: for many (probability, bit) sequences,
 * decode(encode(bits)) must reproduce the bits exactly. Also sanity-checks
 * that coding cost approaches entropy for skewed distributions.
 */
#include "../src/range_coder.h"

#include <math.h>
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

/*
 * Round-trips `n` bits with the given per-bit probabilities.
 * probs[i] = P(bit==0)*2048, must be in [1,2047].
 * Returns encoded size in bytes (0 on failure).
 */
static size_t roundtrip(const char *name, const uint8_t *bits,
                        const uint16_t *probs, size_t n)
{
    size_t cap = n + 64; /* worst case ~1 byte/bit for adversarial input */
    uint8_t *buf = (uint8_t *)malloc(cap);

    NszRcEnc enc;
    nsz_rc_enc_init(&enc, buf, cap);
    for (size_t i = 0; i < n; i++)
        nsz_rc_encode_bit(&enc, probs[i], bits[i]);
    nsz_rc_enc_flush(&enc);
    if (enc.overflow) {
        fail(name, "encoder overflow");
        free(buf);
        return 0;
    }

    NszRcDec dec;
    nsz_rc_dec_init(&dec, buf, enc.pos);
    for (size_t i = 0; i < n; i++) {
        int b = nsz_rc_decode_bit(&dec, probs[i]);
        if (b != bits[i]) {
            char msg[64];
            snprintf(msg, sizeof(msg), "bit %zu mismatch", i);
            fail(name, msg);
            free(buf);
            return 0;
        }
    }
    if (dec.error) {
        fail(name, "decoder read past end");
        free(buf);
        return 0;
    }
    printf("PASS %-24s n=%-8zu encoded=%zu bytes\n", name, n, enc.pos);
    free(buf);
    return enc.pos;
}

static void test_direct_bits(void)
{
    enum { N = 100000 };
    uint8_t *bits = (uint8_t *)malloc(N);
    srand(5);
    for (size_t i = 0; i < N; i++)
        bits[i] = (uint8_t)(rand() & 1);

    size_t cap = N / 8 + 64;
    uint8_t *buf = (uint8_t *)malloc(cap);
    NszRcEnc enc;
    nsz_rc_enc_init(&enc, buf, cap);
    for (size_t i = 0; i < N; i++)
        nsz_rc_encode_direct(&enc, bits[i]);
    nsz_rc_enc_flush(&enc);
    if (enc.overflow) {
        fail("direct-bits", "overflow");
        goto done;
    }

    NszRcDec dec;
    nsz_rc_dec_init(&dec, buf, enc.pos);
    for (size_t i = 0; i < N; i++) {
        if (nsz_rc_decode_direct(&dec) != bits[i]) {
            fail("direct-bits", "mismatch");
            goto done;
        }
    }
    /* direct bits must cost exactly 1 bit each (+ ~5 bytes flush) */
    if (enc.pos > N / 8 + 8) {
        fail("direct-bits", "cost exceeds 1 bit/bit");
        goto done;
    }
    printf("PASS %-24s n=%d encoded=%zu bytes (1.000 bit/bit)\n",
           "direct-bits", N, enc.pos);
done:
    free(bits);
    free(buf);
}

int main(void)
{
    enum { N = 1000000 };
    uint8_t *bits = (uint8_t *)malloc(N);
    uint16_t *probs = (uint16_t *)malloc(N * sizeof(uint16_t));
    size_t i;

    /* fixed p, all-zero bits: cost must be near -log2(p0) */
    for (i = 0; i < N; i++) { bits[i] = 0; probs[i] = 2000; }
    {
        size_t sz = roundtrip("all-zeros-p0.977", bits, probs, N);
        double ideal = -log2(2000.0 / 2048.0) * N / 8.0;
        if (sz && (double)sz > ideal * 1.05 + 16)
            fail("all-zeros-p0.977", "cost far from entropy");
    }

    /* all ones against a strong zero prediction: expensive but exact */
    for (i = 0; i < N / 10; i++) { bits[i] = 1; probs[i] = 2000; }
    roundtrip("all-ones-wrong-model", bits, probs, N / 10);

    /* alternating bits, p=1/2 */
    for (i = 0; i < N; i++) { bits[i] = (uint8_t)(i & 1); probs[i] = 1024; }
    roundtrip("alternating-p0.5", bits, probs, N);

    /* random bits with random probabilities (probability-consistent) */
    srand(2024);
    for (i = 0; i < N; i++) {
        probs[i] = (uint16_t)(1 + rand() % 2047);
        bits[i] = (uint8_t)(((uint32_t)rand() % 2048) >= probs[i]);
    }
    roundtrip("random-consistent", bits, probs, N);

    /* random bits with adversarial (uncorrelated) probabilities */
    srand(777);
    for (i = 0; i < N; i++) {
        probs[i] = (uint16_t)(1 + rand() % 2047);
        bits[i] = (uint8_t)(rand() & 1);
    }
    roundtrip("random-adversarial", bits, probs, N);

    /* extreme probabilities at both ends */
    srand(31);
    for (i = 0; i < N; i++) {
        probs[i] = (i & 1) ? 1 : 2047;
        bits[i] = (uint8_t)(rand() & 1);
    }
    roundtrip("extreme-probs", bits, probs, N);

    /* skewed source coded with matching skewed model: entropy check */
    srand(97);
    for (i = 0; i < N; i++) {
        probs[i] = 1966; /* p0 = 0.96 */
        bits[i] = (uint8_t)((rand() % 100) >= 96);
    }
    {
        size_t sz = roundtrip("skewed-0.96", bits, probs, N);
        /* H(0.04) = 0.2423 bits -> ~30.3 KB ideal */
        double ideal = 0.2423 * N / 8.0;
        if (sz && (double)sz > ideal * 1.10)
            fail("skewed-0.96", "cost >10%% above entropy");
        else if (sz)
            printf("     skewed-0.96 cost: %.4f bits/bit (entropy 0.2423)\n",
                   8.0 * (double)sz / N);
    }

    /* tiny sequences */
    for (size_t len = 1; len <= 16; len++) {
        char name[32];
        snprintf(name, sizeof(name), "tiny-%zu", len);
        srand((unsigned)len);
        for (i = 0; i < len; i++) {
            probs[i] = (uint16_t)(1 + rand() % 2047);
            bits[i] = (uint8_t)(rand() & 1);
        }
        roundtrip(name, bits, probs, len);
    }

    test_direct_bits();

    free(bits);
    free(probs);
    if (failures) {
        fprintf(stderr, "test_range_coder: %d FAILURES\n", failures);
        return 1;
    }
    printf("test_range_coder: all passed\n");
    return 0;
}
