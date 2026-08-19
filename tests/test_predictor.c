/*
 * Predictor unit tests.
 *
 * The key property is encoder/decoder state synchronization: reconstructing
 * each sample as prediction + residual, while feeding the reconstructed
 * sample back into a second predictor instance, must reproduce the input
 * exactly for every mode and every input shape.
 */
#include "../src/predictor.h"

#include <limits.h>
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

/* Simulates encoder (residual from `enc`) and decoder (reconstruction into
 * `dec`), with two independent predictor instances that must stay in sync. */
static void check_sync_p(const char *name, NszPredMode mode, uint16_t prof,
                         const int16_t *x, size_t n)
{
    NszPredictor enc, dec;
    nsz_predictor_init(&enc, mode, prof);
    nsz_predictor_init(&dec, mode, prof);
    for (size_t i = 0; i < n; i++) {
        int32_t pe = nsz_predictor_predict(&enc);
        int32_t r = (int32_t)x[i] - pe;

        int32_t pd = nsz_predictor_predict(&dec);
        if (pd != pe) {
            fail(name, "encoder and decoder predictions diverged");
            return;
        }
        int32_t rec = pd + r;
        if (rec != (int32_t)x[i]) {
            fail(name, "reconstruction mismatch");
            return;
        }
        nsz_predictor_update(&enc, (int32_t)x[i], pe);
        nsz_predictor_update(&dec, rec, pd);
    }
    printf("PASS %-24s mode=%d n=%zu\n", name, (int)mode, n);
}

static void check_sync(const char *name, NszPredMode mode,
                       const int16_t *x, size_t n)
{
    check_sync_p(name, mode, 0, x, n);
}

/* Mean |residual| for LPC8 on the given signal (sanity metric). */
static double mean_abs_residual(const int16_t *x, size_t n)
{
    NszPredictor p;
    nsz_predictor_init(&p, NSZ_PRED_LPC8, 0);
    double acc = 0;
    for (size_t i = 0; i < n; i++) {
        int32_t pr = nsz_predictor_predict(&p);
        int32_t r = (int32_t)x[i] - pr;
        acc += (r < 0) ? -r : r;
        nsz_predictor_update(&p, (int32_t)x[i], pr);
    }
    return acc / (double)n;
}

int main(void)
{
    enum { N = 200000 };
    int16_t *buf = (int16_t *)malloc(N * sizeof(int16_t));
    size_t i;

    NszPredMode modes[3] = { NSZ_PRED_ZERO, NSZ_PRED_DELTA, NSZ_PRED_LPC8 };
    const char *base[3] = { "zero", "delta", "lpc8" };

    for (int m = 0; m < 3; m++) {
        char name[64];

        for (i = 0; i < N; i++) buf[i] = 700;
        snprintf(name, sizeof(name), "%s-constant", base[m]);
        check_sync(name, modes[m], buf, N);

        for (i = 0; i < N; i++) buf[i] = (int16_t)((i * 7) & 0xFFFF);
        snprintf(name, sizeof(name), "%s-ramp", base[m]);
        check_sync(name, modes[m], buf, N);

        for (i = 0; i < N; i++)
            buf[i] = (int16_t)(500.0 * sin((double)i * 0.01) +
                               200.0 * sin((double)i * 0.37));
        snprintf(name, sizeof(name), "%s-sinusoid", base[m]);
        check_sync(name, modes[m], buf, N);

        for (i = 0; i < N; i++)
            buf[i] = (i & 1) ? INT16_MAX : INT16_MIN;
        snprintf(name, sizeof(name), "%s-extremes", base[m]);
        check_sync(name, modes[m], buf, N);

        srand(99);
        for (i = 0; i < N; i++) buf[i] = (int16_t)(rand() & 0xFFFF);
        snprintf(name, sizeof(name), "%s-random", base[m]);
        check_sync(name, modes[m], buf, N);
    }

    /* profile 1 (R10 delta) must stay in sync too */
    srand(77);
    for (i = 0; i < N; i++) buf[i] = (int16_t)(rand() & 0xFFFF);
    check_sync_p("lpc8-p1-random", NSZ_PRED_LPC8, 1, buf, N);
    for (i = 0; i < N; i++) buf[i] = (i & 1) ? INT16_MAX : INT16_MIN;
    check_sync_p("lpc8-p1-extremes", NSZ_PRED_LPC8, 1, buf, N);

    /* real signal: sync + LPC8 must beat delta on |residual| */
    {
        FILE *f = fopen("tests/data/real_signal.bin", "rb");
        if (!f) {
            fail("real-signal", "cannot open tests/data/real_signal.bin");
        } else {
            size_t n = fread(buf, sizeof(int16_t), N, f);
            fclose(f);
            check_sync("zero-real", NSZ_PRED_ZERO, buf, n);
            check_sync("delta-real", NSZ_PRED_DELTA, buf, n);
            check_sync("lpc8-real", NSZ_PRED_LPC8, buf, n);

            NszPredictor d;
            nsz_predictor_init(&d, NSZ_PRED_DELTA, 0);
            double dacc = 0;
            for (i = 0; i < n; i++) {
                int32_t pr = nsz_predictor_predict(&d);
                int32_t r = (int32_t)buf[i] - pr;
                dacc += (r < 0) ? -r : r;
                nsz_predictor_update(&d, (int32_t)buf[i], pr);
            }
            double delta_mar = dacc / (double)n;
            double lpc_mar = mean_abs_residual(buf, n);
            printf("     mean|residual|: delta=%.2f lpc8=%.2f\n",
                   delta_mar, lpc_mar);
            if (lpc_mar >= delta_mar)
                fail("lpc8-gain", "LPC8 not better than delta on real data");
            else
                printf("PASS %-24s lpc8 beats delta\n", "lpc8-gain");
        }
    }

    free(buf);
    if (failures) {
        fprintf(stderr, "test_predictor: %d FAILURES\n", failures);
        return 1;
    }
    printf("test_predictor: all passed\n");
    return 0;
}
