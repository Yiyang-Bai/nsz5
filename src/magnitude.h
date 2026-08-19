/*
 * magnitude.h — magnitude-class + mantissa representation of residuals.
 *
 * A zigzag-mapped residual u >= 0 is shifted to w = u + 1 >= 1 and split:
 *
 *   class    c = bit_length(w) - 1     (how big, roughly)
 *   mantissa m = w - 2^c               (exactly which value, c bits)
 *
 * The class is coded as an adaptive unary sequence: one "continue" bit per
 * level through per-level models, terminated by a "stop" bit. Small
 * residuals (the common case) therefore cost only a few highly-predictable
 * bits.
 *
 * The mantissa's leading NSZ_MANT_TREE_BITS bits go through a per-class
 * binary tree of adaptive models (the tree context is the bit path walked
 * so far); remaining low bits are nearly uniform noise and are coded as
 * raw direct bits.
 *
 * Class 19 covers w in [2^19, 2^20-1], so the supported input range is
 * u in [0, 2^20-2]. The predictor's clamp guarantees |residual| < 2^18,
 * i.e. u < 2^19 — comfortably inside.
 */
#ifndef NSZ_MAGNITUDE_H
#define NSZ_MAGNITUDE_H

#include "context.h"
#include "model.h"
#include "range_coder.h"

#include <stdint.h>

#define NSZ_MAG_MAX_CLASS 19
#define NSZ_MANT_TREE_BITS 6

/*
 * Model bank:
 *  - the magnitude class (unary) models are fully context-selected
 *    (256 volatility×surprise contexts), because the class distribution
 *    depends strongly on the local noise level;
 *  - the mantissa tree uses only 4 coarse volatility groups, because
 *    splitting its statistics across 256 contexts would dilute them
 *    (each tree node would see too few samples to adapt).
 *
 * Total state: (256*20+1... exactly 256*21 + 4*21*64) u16 = 21504 bytes.
 * This entire struct is the "model prior" that can be snapshotted/loaded.
 */
typedef struct {
    NszBitModel unary[NSZ_CTX_COUNT][NSZ_MAG_MAX_CLASS + 1];
    NszBitModel tree[NSZ_CTX_GROUPS][NSZ_MAG_MAX_CLASS + 1]
                    [1 << NSZ_MANT_TREE_BITS];
} NszMagModels;

static inline void nsz_mag_models_init(NszMagModels *mm)
{
    for (int x = 0; x < NSZ_CTX_COUNT; x++)
        for (int k = 0; k <= NSZ_MAG_MAX_CLASS; k++)
            nsz_model_init(&mm->unary[x][k]);
    for (int g = 0; g < NSZ_CTX_GROUPS; g++)
        for (int c = 0; c <= NSZ_MAG_MAX_CLASS; c++)
            for (int i = 0; i < (1 << NSZ_MANT_TREE_BITS); i++)
                nsz_model_init(&mm->tree[g][c][i]);
}

/* Encode zigzag value u under context `ctx` (< 256) / group `grp` (< 4).
 * Returns 0 on success, -1 if u is out of the supported range. */
static inline int nsz_mag_encode(NszRcEnc *e, NszMagModels *mm,
                                 uint32_t ctx, uint32_t grp, uint32_t u)
{
    uint32_t w = u + 1;
    int c = 0;
    while ((w >> (c + 1)) != 0)
        c++;
    if (c > NSZ_MAG_MAX_CLASS)
        return -1;

    /* adaptive unary: c "continue" bits then one "stop" bit */
    for (int k = 0; k < c; k++)
        nsz_model_encode(e, &mm->unary[ctx][k], 1);
    nsz_model_encode(e, &mm->unary[ctx][c], 0);

    /* mantissa: c bits, MSB first; leading bits through the tree */
    uint32_t mant = w - (1u << c);
    int tb = (c < NSZ_MANT_TREE_BITS) ? c : NSZ_MANT_TREE_BITS;
    uint32_t node = 1;
    for (int i = c - 1; i >= c - tb; i--) {
        int bit = (int)((mant >> i) & 1u);
        nsz_model_encode(e, &mm->tree[grp][c][node], bit);
        node = (node << 1) | (uint32_t)bit;
    }
    for (int i = c - tb - 1; i >= 0; i--)
        nsz_rc_encode_direct(e, (int)((mant >> i) & 1u));
    return 0;
}

/* Decode one zigzag value under context `ctx` / group `grp`.
 * Returns 0 on success, -1 on malformed input. */
static inline int nsz_mag_decode(NszRcDec *d, NszMagModels *mm,
                                 uint32_t ctx, uint32_t grp, uint32_t *u)
{
    int c = 0;
    while (nsz_model_decode(d, &mm->unary[ctx][c])) {
        c++;
        if (c > NSZ_MAG_MAX_CLASS)
            return -1;
    }

    int tb = (c < NSZ_MANT_TREE_BITS) ? c : NSZ_MANT_TREE_BITS;
    uint32_t node = 1;
    uint32_t mant = 0;
    for (int i = 0; i < tb; i++) {
        int bit = nsz_model_decode(d, &mm->tree[grp][c][node]);
        node = (node << 1) | (uint32_t)bit;
        mant = (mant << 1) | (uint32_t)bit;
    }
    for (int i = 0; i < c - tb; i++)
        mant = (mant << 1) | (uint32_t)nsz_rc_decode_direct(d);

    *u = (1u << c) + mant - 1u;
    return 0;
}

#endif /* NSZ_MAGNITUDE_H */
