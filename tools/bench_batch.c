/*
 * bench_batch — OpenMP scaling benchmark for the batch API.
 * Statically linked (does not touch libnsz.so).
 *
 * Usage: bench_batch signal.bin [chunk_samples] [prior.bin]
 */
#define _POSIX_C_SOURCE 199309L

#include "../src/nsz.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_sec(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s signal.bin [chunk] [prior.bin]\n", argv[0]);
        return 2;
    }
    size_t chunk = (argc > 2) ? strtoul(argv[2], NULL, 10) : 102400;
    if (argc > 3 && nsz_load_prior_file(argv[3]) != NSZ_OK) {
        fprintf(stderr, "cannot load prior\n");
        return 2;
    }

    FILE *f = fopen(argv[1], "rb");
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t n = (size_t)bytes / 2;
    int16_t *x = malloc((size_t)bytes);
    if (fread(x, 1, (size_t)bytes, f) != (size_t)bytes)
        return 2;
    fclose(f);

    size_t n_chunks = (n + chunk - 1) / chunk;
    uint64_t *off = malloc((n_chunks + 1) * sizeof(uint64_t));
    for (size_t i = 0; i <= n_chunks; i++)
        off[i] = (i * chunk < n) ? i * chunk : n;

    uint8_t **outs = malloc(n_chunks * sizeof(uint8_t *));
    size_t *sizes = malloc(n_chunks * sizeof(size_t));

    int thread_counts[] = { 1, 2, 4, 8, 16, 32 };
    for (size_t t = 0; t < sizeof(thread_counts) / sizeof(int); t++) {
        int th = thread_counts[t];
        double t0 = now_sec();
        int rc = nsz_compress_batch(x, off, n_chunks, th, outs, sizes);
        double dt = now_sec() - t0;
        if (rc != NSZ_OK) {
            fprintf(stderr, "batch failed (threads=%d)\n", th);
            return 1;
        }
        size_t total = 0;
        for (size_t i = 0; i < n_chunks; i++)
            total += sizes[i];
        printf("threads=%-3d %6.2f s  %7.1f Msamples/s  (%zu bytes)\n",
               th, dt, (double)n / dt / 1e6, total);
        for (size_t i = 0; i < n_chunks; i++)
            nsz_free(outs[i]);
    }
    free(x);
    free(off);
    free(outs);
    free(sizes);
    return 0;
}
