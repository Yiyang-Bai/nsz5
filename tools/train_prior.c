/*
 * train_prior — build a pretrained model prior from real signal.
 *
 * For every chunk of the training corpus the models adapt from a uniform
 * start exactly as the encoder does; the final per-model probabilities are
 * averaged across all chunks and written as prior.bin. Averaging the
 * post-adaptation states approximates the stationary distribution each
 * model converges to, which is what a fresh chunk wants to start from.
 *
 * Usage: train_prior signal.bin prior.bin [chunk_samples] [version] [profile]
 */
#include "../src/context.h"
#include "../src/magnitude.h"
#include "../src/predictor.h"
#include "../src/zigzag.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MODEL_COUNT (sizeof(NszMagModels) / sizeof(NszBitModel))

static void put_u16(uint8_t *p, uint16_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)(v >> 8);
}

static void put_u32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr,
                "usage: %s signal.bin prior.bin [chunk_samples] [version] "
                "[profile]\n",
                argv[0]);
        return 2;
    }
    const size_t chunk = (argc > 3) ? (size_t)strtoul(argv[3], NULL, 10)
                                    : 102400;
    const uint16_t version = (argc > 4) ? (uint16_t)strtoul(argv[4], NULL, 10)
                                        : 1;
    const uint16_t profile = (argc > 5) ? (uint16_t)strtoul(argv[5], NULL, 10)
                                        : 0;
    if (profile >= NSZ_PRED_PROFILES) {
        fprintf(stderr, "invalid profile %u\n", profile);
        return 2;
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

    uint64_t *sums = (uint64_t *)calloc(MODEL_COUNT, sizeof(uint64_t));
    NszMagModels *mm = (NszMagModels *)malloc(sizeof(NszMagModels));
    uint8_t *scratch = (uint8_t *)malloc(chunk * 4 + 64);
    size_t nchunks = 0;

    for (size_t off = 0; off < n; off += chunk) {
        size_t len = (n - off < chunk) ? n - off : chunk;
        if (len < chunk / 2)
            break; /* skip a short trailing chunk: unrepresentative */

        nsz_mag_models_init(mm);
        NszRcEnc rc;
        nsz_rc_enc_init(&rc, scratch, chunk * 4 + 64);
        NszPredictor pred;
        nsz_predictor_init(&pred, NSZ_PRED_LPC8, profile);
        NszContext cx;
        nsz_context_init(&cx);

        for (size_t i = 0; i < len; i++) {
            int32_t p = nsz_predictor_predict(&pred);
            int32_t r = (int32_t)x[off + i] - p;
            uint32_t u = nsz_zigzag_encode(r);
            uint32_t ctx = nsz_context_id(&cx);
            uint32_t grp = ctx >> 6;
            if (nsz_mag_encode(&rc, mm, ctx, grp, u) != 0 || rc.overflow) {
                fprintf(stderr, "encode failure in training at %zu\n", off);
                return 1;
            }
            nsz_context_update(&cx, (uint32_t)(r < 0 ? -r : r));
            nsz_predictor_update(&pred, (int32_t)x[off + i], p);
        }

        const NszBitModel *ms = (const NszBitModel *)mm;
        for (size_t j = 0; j < MODEL_COUNT; j++)
            sums[j] += ms[j].p0;
        nchunks++;
    }

    if (nchunks == 0) {
        fprintf(stderr, "no full chunks in corpus\n");
        return 1;
    }

    size_t fsz = 12 + MODEL_COUNT * 2;
    uint8_t *out = (uint8_t *)malloc(fsz);
    put_u32(out + 0, 0x505A534Eu); /* "NSZP" */
    put_u16(out + 4, version);
    put_u16(out + 6, profile);
    put_u32(out + 8, (uint32_t)MODEL_COUNT);
    for (size_t j = 0; j < MODEL_COUNT; j++) {
        uint32_t avg = (uint32_t)(sums[j] / nchunks);
        if (avg < 31)
            avg = 31;
        if (avg > 2017)
            avg = 2017;
        put_u16(out + 12 + 2 * j, (uint16_t)avg);
    }

    FILE *g = fopen(argv[2], "wb");
    if (!g || fwrite(out, 1, fsz, g) != fsz) {
        fprintf(stderr, "cannot write %s\n", argv[2]);
        return 1;
    }
    fclose(g);

    printf("trained prior v%u (profile %u) from %zu chunks (%zu samples), "
           "%zu models -> %s\n",
           version, profile, nchunks, n, (size_t)MODEL_COUNT, argv[2]);
    free(x);
    free(sums);
    free(mm);
    free(scratch);
    free(out);
    return 0;
}
