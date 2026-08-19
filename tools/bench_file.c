/*
 * bench_file — compress + decompress + verify a flat int16 LE signal file.
 *
 * Splits the file into POD5-sized chunks (default 102400 samples), encodes
 * and decodes every chunk, verifies exact reconstruction with memcmp, and
 * reports bits/sample and throughput.
 *
 * Usage: bench_file signal.bin [chunk_samples] [prior.bin]
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
        fprintf(stderr, "usage: %s signal.bin [chunk_samples] [prior.bin]\n",
                argv[0]);
        return 2;
    }
    const size_t chunk = (argc > 2) ? (size_t)strtoul(argv[2], NULL, 10)
                                    : 102400;
    if (argc > 3) {
        int rc = nsz_load_prior_file(argv[3]);
        if (rc != NSZ_OK) {
            fprintf(stderr, "cannot load prior %s (rc=%d)\n", argv[3], rc);
            return 2;
        }
        printf("prior:          v%u (%s)\n", nsz_prior_version(), argv[3]);
    }

    FILE *f = fopen(argv[1], "rb");
    if (!f) {
        fprintf(stderr, "cannot open %s\n", argv[1]);
        return 2;
    }
    fseek(f, 0, SEEK_END);
    long bytes = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t n = (size_t)bytes / 2;
    int16_t *x = (int16_t *)malloc((size_t)bytes);
    if (!x || fread(x, 1, (size_t)bytes, f) != (size_t)bytes) {
        fprintf(stderr, "read failed\n");
        return 2;
    }
    fclose(f);

    uint8_t *enc = (uint8_t *)malloc(nsz_compress_bound(chunk));
    int16_t *dec = (int16_t *)malloc(chunk * sizeof(int16_t));

    size_t total_enc = 0, raw_chunks = 0, nchunks = 0;
    double t_enc = 0.0, t_dec = 0.0;

    for (size_t off = 0; off < n; off += chunk) {
        size_t len = (n - off < chunk) ? n - off : chunk;

        size_t enc_sz = nsz_compress_bound(len);
        double t0 = now_sec();
        int rc = nsz_compress_chunk(x + off, len, enc, &enc_sz);
        t_enc += now_sec() - t0;
        if (rc != NSZ_OK) {
            fprintf(stderr, "compress failed at offset %zu (rc=%d)\n", off, rc);
            return 1;
        }

        t0 = now_sec();
        rc = nsz_decompress_chunk(enc, enc_sz, dec, len);
        t_dec += now_sec() - t0;
        if (rc != NSZ_OK) {
            fprintf(stderr, "decompress failed at offset %zu (rc=%d)\n", off, rc);
            return 1;
        }
        if (memcmp(x + off, dec, len * sizeof(int16_t)) != 0) {
            fprintf(stderr, "MISMATCH at offset %zu\n", off);
            return 1;
        }

        /* flags live at header offset 6 */
        if ((enc[6] | ((uint16_t)enc[7] << 8)) == NSZ_F_RAW)
            raw_chunks++;
        total_enc += enc_sz;
        nchunks++;
    }

    printf("samples:        %zu\n", n);
    printf("chunks:         %zu (%zu raw fallback)\n", nchunks, raw_chunks);
    printf("raw bytes:      %zu\n", n * 2);
    printf("encoded bytes:  %zu\n", total_enc);
    printf("bits/sample:    %.4f\n", 8.0 * (double)total_enc / (double)n);
    printf("ratio vs raw:   %.3fx\n", (double)(n * 2) / (double)total_enc);
    printf("encode:         %.2f s  (%.1f Msamples/s)\n",
           t_enc, (double)n / t_enc / 1e6);
    printf("decode:         %.2f s  (%.1f Msamples/s)\n",
           t_dec, (double)n / t_dec / 1e6);
    printf("VERIFIED: all %zu chunks reconstructed exactly\n", nchunks);

    free(x);
    free(enc);
    free(dec);
    return 0;
}
