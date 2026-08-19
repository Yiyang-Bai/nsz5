/*
 * End-to-end codec round-trip tests (M0: raw + delta modes).
 *
 * Every case asserts decode(encode(x)) == x exactly, plus negative tests
 * for corrupted and truncated streams. The real-nanopore case also reports
 * bits/sample for the delta+varint baseline.
 */
#include "../src/nsz.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures = 0;

static void fail(const char *name, const char *why)
{
    fprintf(stderr, "FAIL %-28s %s\n", name, why);
    failures++;
}

/* Round-trips `x` and returns encoded size (0 on failure). */
static size_t roundtrip(const char *name, const int16_t *x, size_t n)
{
    uint8_t *enc = NULL;
    size_t enc_sz = 0;
    int rc = nsz_compress(x, n, &enc, &enc_sz);
    if (rc != NSZ_OK) {
        fail(name, "compress failed");
        return 0;
    }
    if (enc_sz > nsz_compress_bound(n)) {
        fail(name, "encoded size exceeds bound");
        free(enc);
        return 0;
    }

    uint32_t count = 0;
    if (nsz_peek_sample_count(enc, enc_sz, &count) != NSZ_OK || count != n) {
        fail(name, "peek_sample_count mismatch");
        free(enc);
        return 0;
    }

    int16_t *dec = (int16_t *)malloc(n ? n * sizeof(int16_t) : 1);
    memset(dec, 0x5A, n ? n * sizeof(int16_t) : 1);
    rc = nsz_decompress(enc, enc_sz, dec, n);
    if (rc != NSZ_OK) {
        fail(name, "decompress failed");
        free(enc);
        free(dec);
        return 0;
    }
    if (n && memcmp(x, dec, n * sizeof(int16_t)) != 0) {
        fail(name, "decoded data differs");
        free(enc);
        free(dec);
        return 0;
    }

    printf("PASS %-28s n=%-9zu encoded=%-9zu (%.3f bits/sample)\n",
           name, n, enc_sz, n ? 8.0 * (double)enc_sz / (double)n : 0.0);
    free(enc);
    free(dec);
    return enc_sz;
}

static void test_corruption(void)
{
    enum { N = 4096 };
    int16_t x[N];
    srand(7);
    for (int i = 0; i < N; i++)
        x[i] = (int16_t)(700 + rand() % 64 - 32);

    uint8_t *enc = NULL;
    size_t enc_sz = 0;
    if (nsz_compress(x, N, &enc, &enc_sz) != NSZ_OK) {
        fail("corruption-setup", "compress failed");
        return;
    }
    int16_t dec[N];

    /* Flip one payload byte: the decoder must either report an error or
     * still produce exactly the original data (bytes in the range coder's
     * flush padding are never read and are legitimately inert). It must
     * NEVER silently return wrong data. */
    for (size_t off = NSZ_HEADER_SIZE; off < enc_sz; off += 97) {
        enc[off] ^= 0xFF;
        int rc = nsz_decompress(enc, enc_sz, dec, N);
        enc[off] ^= 0xFF;
        if (rc == NSZ_OK && memcmp(dec, x, sizeof(x)) != 0) {
            fail("corrupt-payload", "corrupted stream returned wrong data");
            free(enc);
            return;
        }
    }

    /* bad magic */
    enc[0] ^= 0xFF;
    if (nsz_decompress(enc, enc_sz, dec, N) != NSZ_EBADMAGIC)
        fail("corrupt-magic", "expected NSZ_EBADMAGIC");
    enc[0] ^= 0xFF;

    /* truncation at every boundary class */
    for (size_t cut = 0; cut < enc_sz; cut += (enc_sz / 13) + 1) {
        if (nsz_decompress(enc, cut, dec, N) == NSZ_OK) {
            fail("truncated", "truncated stream decoded as OK");
            free(enc);
            return;
        }
    }

    /* wrong sample count */
    if (nsz_decompress(enc, enc_sz, dec, N - 1) == NSZ_OK)
        fail("wrong-n", "mismatched n decoded as OK");

    printf("PASS %-28s all corrupt/truncated inputs rejected\n", "malformed-input");
    free(enc);
}

/* Chunks must decode independently: any subset, any order. */
static void test_chunk_independence(void)
{
    enum { NCHUNKS = 16, LEN = 4096 };
    int16_t x[NCHUNKS][LEN];
    uint8_t *enc[NCHUNKS];
    size_t enc_sz[NCHUNKS];

    srand(4242);
    for (int c = 0; c < NCHUNKS; c++) {
        int16_t v = (int16_t)(rand() % 2000 - 1000);
        for (int i = 0; i < LEN; i++) {
            v = (int16_t)(v + rand() % 41 - 20);
            x[c][i] = v;
        }
        if (nsz_compress(x[c], LEN, &enc[c], &enc_sz[c]) != NSZ_OK) {
            fail("chunk-independence", "compress failed");
            return;
        }
    }

    /* decode in reverse order, then only odd chunks, with fresh buffers */
    int16_t dec[LEN];
    for (int c = NCHUNKS - 1; c >= 0; c--) {
        if (nsz_decompress(enc[c], enc_sz[c], dec, LEN) != NSZ_OK ||
            memcmp(dec, x[c], sizeof(dec)) != 0) {
            fail("chunk-independence", "reverse-order decode failed");
            return;
        }
    }
    for (int c = 1; c < NCHUNKS; c += 2) {
        if (nsz_decompress(enc[c], enc_sz[c], dec, LEN) != NSZ_OK ||
            memcmp(dec, x[c], sizeof(dec)) != 0) {
            fail("chunk-independence", "subset decode failed");
            return;
        }
    }
    for (int c = 0; c < NCHUNKS; c++)
        free(enc[c]);
    printf("PASS %-28s any order, any subset\n", "chunk-independence");
}

/* The decoder must survive arbitrary garbage: random bytes and random bit
 * flips anywhere (header included) must produce an error or correct data,
 * never a crash, hang, out-of-bounds access, or silent wrong data. */
static void test_decoder_fuzz(void)
{
    enum { N = 4096, ITERS = 2000 };
    int16_t x[N], dec[N];
    srand(31337);
    for (int i = 0; i < N; i++)
        x[i] = (int16_t)(600 + rand() % 200);

    uint8_t *enc = NULL;
    size_t enc_sz = 0;
    if (nsz_compress(x, N, &enc, &enc_sz) != NSZ_OK) {
        fail("decoder-fuzz", "compress failed");
        return;
    }

    /* random single/multi bit flips anywhere including the header */
    uint8_t *mut = (uint8_t *)malloc(enc_sz);
    for (int it = 0; it < ITERS; it++) {
        memcpy(mut, enc, enc_sz);
        int flips = 1 + rand() % 8;
        for (int f = 0; f < flips; f++)
            mut[(size_t)rand() % enc_sz] ^= (uint8_t)(1u << (rand() % 8));
        int rc = nsz_decompress(mut, enc_sz, dec, N);
        if (rc == NSZ_OK && memcmp(dec, x, sizeof(x)) != 0) {
            fail("decoder-fuzz", "bit flips produced silent wrong data");
            free(enc);
            free(mut);
            return;
        }
    }

    /* pure random garbage with a valid-looking size */
    for (int it = 0; it < ITERS; it++) {
        size_t sz = 1 + (size_t)rand() % enc_sz;
        for (size_t i = 0; i < sz; i++)
            mut[i] = (uint8_t)rand();
        (void)nsz_decompress(mut, sz, dec, N); /* must simply not crash */
    }

    free(enc);
    free(mut);
    printf("PASS %-28s %d bit-flip + %d garbage inputs survived\n",
           "decoder-fuzz", ITERS, ITERS);
}

/* The batch API must produce bit-identical outputs to sequential calls,
 * regardless of thread count. */
static void test_batch(void)
{
    enum { NCHUNKS = 64, LEN = 8192 };
    static int16_t x[NCHUNKS * LEN];
    uint64_t off[NCHUNKS + 1];
    srand(777);
    int16_t v = 700;
    for (size_t i = 0; i < (size_t)NCHUNKS * LEN; i++) {
        v = (int16_t)(v + rand() % 31 - 15);
        x[i] = v;
    }
    for (int i = 0; i <= NCHUNKS; i++)
        off[i] = (uint64_t)i * LEN;

    /* sequential reference */
    uint8_t *ref[NCHUNKS];
    size_t ref_sz[NCHUNKS];
    for (int i = 0; i < NCHUNKS; i++) {
        if (nsz_compress(x + off[i], LEN, &ref[i], &ref_sz[i]) != NSZ_OK) {
            fail("batch", "sequential compress failed");
            return;
        }
    }

    /* parallel batch with several thread counts */
    for (int threads = 1; threads <= 8; threads *= 2) {
        uint8_t *outs[NCHUNKS];
        size_t sizes[NCHUNKS];
        if (nsz_compress_batch(x, off, NCHUNKS, threads, outs, sizes)
                != NSZ_OK) {
            fail("batch", "batch compress failed");
            return;
        }
        for (int i = 0; i < NCHUNKS; i++) {
            if (sizes[i] != ref_sz[i] ||
                memcmp(outs[i], ref[i], sizes[i]) != 0) {
                fail("batch", "batch output differs from sequential");
                return;
            }
        }

        /* batch decompress back */
        static int16_t dec[NCHUNKS * LEN];
        const uint8_t *ins[NCHUNKS];
        for (int i = 0; i < NCHUNKS; i++)
            ins[i] = outs[i];
        if (nsz_decompress_batch(ins, sizes, NCHUNKS, threads, dec, off)
                != NSZ_OK ||
            memcmp(dec, x, sizeof(x)) != 0) {
            fail("batch", "batch decompress mismatch");
            return;
        }
        for (int i = 0; i < NCHUNKS; i++)
            nsz_free(outs[i]);
    }
    for (int i = 0; i < NCHUNKS; i++)
        free(ref[i]);
    printf("PASS %-28s identical to sequential, 1-8 threads\n", "batch-api");
}

static void test_prior(void)
{
    if (nsz_load_prior_file("models/prior.bin") != NSZ_OK) {
        printf("SKIP %-28s models/prior.bin not present\n", "prior");
        return;
    }

    enum { N = 8192 };
    int16_t x[N];
    srand(21);
    int16_t v = 700;
    for (int i = 0; i < N; i++) {
        v = (int16_t)(v + rand() % 21 - 10);
        x[i] = v;
    }

    uint8_t *enc = NULL;
    size_t enc_sz = 0;
    int16_t dec[N];
    if (nsz_compress(x, N, &enc, &enc_sz) != NSZ_OK) {
        fail("prior-roundtrip", "compress failed");
        nsz_clear_prior();
        return;
    }

    /* with the prior loaded: exact roundtrip */
    if (nsz_decompress(enc, enc_sz, dec, N) != NSZ_OK ||
        memcmp(x, dec, sizeof(x)) != 0) {
        fail("prior-roundtrip", "mismatch with prior loaded");
        free(enc);
        nsz_clear_prior();
        return;
    }

    /* without the prior: must refuse with NSZ_EVERSION, never wrong data */
    nsz_clear_prior();
    if (nsz_decompress(enc, enc_sz, dec, N) != NSZ_EVERSION) {
        fail("prior-mismatch", "expected NSZ_EVERSION without prior");
        free(enc);
        return;
    }

    /* reloaded: works again */
    if (nsz_load_prior_file("models/prior.bin") != NSZ_OK ||
        nsz_decompress(enc, enc_sz, dec, N) != NSZ_OK ||
        memcmp(x, dec, sizeof(x)) != 0) {
        fail("prior-reload", "roundtrip after reload failed");
        free(enc);
        nsz_clear_prior();
        return;
    }

    printf("PASS %-28s roundtrip + version enforcement\n", "prior");
    free(enc);
    nsz_clear_prior(); /* leave global state clean for the other tests */
}

static void test_real_data(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f) {
        fail("real-nanopore", "cannot open tests/data/real_signal.bin "
                              "(run tools/extract_signal.py first)");
        return;
    }
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t n = (size_t)bytes / 2;
    int16_t *x = (int16_t *)malloc((size_t)bytes);
    if (fread(x, 1, (size_t)bytes, f) != (size_t)bytes) {
        fail("real-nanopore", "short read");
        fclose(f);
        free(x);
        return;
    }
    fclose(f);

    /* whole-buffer roundtrip */
    roundtrip("real-nanopore-4M", x, n);

    /* POD5-like chunking (VBZ chunks are ~100k samples) */
    const size_t chunk = 102400;
    size_t total_enc = 0;
    int ok = 1;
    for (size_t off = 0; off < n; off += chunk) {
        size_t len = (n - off < chunk) ? n - off : chunk;
        uint8_t *enc = NULL;
        size_t enc_sz = 0;
        if (nsz_compress(x + off, len, &enc, &enc_sz) != NSZ_OK) {
            ok = 0;
            break;
        }
        int16_t *dec = (int16_t *)malloc(len * sizeof(int16_t));
        int rc = nsz_decompress(enc, enc_sz, dec, len);
        if (rc != NSZ_OK || memcmp(x + off, dec, len * sizeof(int16_t))) {
            ok = 0;
            free(enc);
            free(dec);
            break;
        }
        total_enc += enc_sz;
        free(enc);
        free(dec);
    }
    if (!ok) {
        fail("real-nanopore-chunked", "chunked roundtrip failed");
    } else {
        printf("PASS %-28s n=%-9zu encoded=%-9zu (%.3f bits/sample)\n",
               "real-nanopore-chunked", n, total_enc,
               8.0 * (double)total_enc / (double)n);
    }
    free(x);
}

int main(void)
{
    enum { BIG = 1000000 };
    int16_t *buf = (int16_t *)malloc(BIG * sizeof(int16_t));
    size_t i;

    /* tiny lengths */
    {
        int16_t one[1] = { 123 };
        int16_t two[2] = { -32768, 32767 };
        int16_t three[3] = { 0, -1, 1 };
        roundtrip("empty", NULL, 0);
        roundtrip("one-sample", one, 1);
        roundtrip("two-extremes", two, 2);
        roundtrip("three-samples", three, 3);
    }

    /* constants at the limits */
    for (i = 0; i < BIG; i++) buf[i] = 0;
    roundtrip("constant-zero", buf, BIG);
    for (i = 0; i < BIG; i++) buf[i] = INT16_MIN;
    roundtrip("constant-int16min", buf, BIG);
    for (i = 0; i < BIG; i++) buf[i] = INT16_MAX;
    roundtrip("constant-int16max", buf, BIG);

    /* alternating extremes: maximal residuals every step */
    for (i = 0; i < BIG; i++) buf[i] = (i & 1) ? INT16_MAX : INT16_MIN;
    roundtrip("alternating-extremes", buf, BIG);

    /* ramp (wraps within int16) */
    for (i = 0; i < BIG; i++) buf[i] = (int16_t)((i * 3) & 0xFFFF);
    roundtrip("ramp", buf, BIG);

    /* impulses on a flat baseline */
    for (i = 0; i < BIG; i++) buf[i] = (int16_t)((i % 997 == 0) ? 30000 : 500);
    roundtrip("impulses", buf, BIG);

    /* uniform random: must trigger raw fallback and stay <= bound */
    srand(42);
    for (i = 0; i < BIG; i++) buf[i] = (int16_t)(rand() & 0xFFFF);
    roundtrip("uniform-random", buf, BIG);

    /* fuzz: random lengths, ranges and structures */
    srand(1337);
    for (int iter = 0; iter < 3000; iter++) {
        size_t n = (size_t)(rand() % 3000) + 1;
        int span = 1 << (rand() % 16);
        int base = rand() % 65536 - 32768;
        for (i = 0; i < n; i++) {
            long v = base + rand() % (2 * span + 1) - span;
            if (v < INT16_MIN) v = INT16_MIN;
            if (v > INT16_MAX) v = INT16_MAX;
            buf[i] = (int16_t)v;
        }
        uint8_t *enc = NULL;
        size_t enc_sz = 0;
        if (nsz_compress(buf, n, &enc, &enc_sz) != NSZ_OK) {
            fail("fuzz", "compress failed");
            break;
        }
        int16_t *dec = (int16_t *)malloc(n * sizeof(int16_t));
        int rc = nsz_decompress(enc, enc_sz, dec, n);
        int bad = (rc != NSZ_OK) || memcmp(buf, dec, n * sizeof(int16_t));
        free(enc);
        free(dec);
        if (bad) {
            fprintf(stderr, "fuzz iter %d: n=%zu span=%d base=%d\n",
                    iter, n, span, base);
            fail("fuzz", "roundtrip mismatch");
            break;
        }
    }
    if (!failures)
        printf("PASS %-28s 3000 random chunks\n", "fuzz");

    test_corruption();
    test_chunk_independence();
    test_decoder_fuzz();
    test_batch();
    test_prior();
    test_real_data("tests/data/real_signal.bin");

    free(buf);
    if (failures) {
        fprintf(stderr, "test_roundtrip: %d FAILURES\n", failures);
        return 1;
    }
    printf("test_roundtrip: all passed\n");
    return 0;
}
