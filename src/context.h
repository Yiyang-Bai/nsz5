/*
 * context.h — causal volatility / surprise context for residual coding.
 *
 * The local noise level of nanopore signal varies over time, so a single
 * global residual distribution is suboptimal. Every probability model is
 * instead selected by a context id computed from CAUSAL state only:
 *
 *   volatility V — EWMA of |residual|, kept in Q4 fixed point:
 *       V += (|r|*16 - V) >> 3
 *   log-quantized at 4 levels per octave into 64 levels.
 *
 *   surprise s — how large the PREVIOUS residual was relative to V:
 *       s = 0:  |r_prev| <= V/2      (quiet)
 *       s = 1:  |r_prev| <= V        (normal)
 *       s = 2:  |r_prev| <= 2V       (elevated)
 *       s = 3:  otherwise            (shock)
 *
 *   context id = level*4 + s   in [0, 255]
 *
 * The mantissa tree uses a coarser grouping (level >> 4, 4 groups) to
 * avoid diluting its statistics across 256 contexts.
 *
 * CRITICAL: the context for sample i must be computed BEFORE updating the
 * state with sample i's residual (never uses current/future information),
 * and encoder and decoder must interleave nsz_context_id/_update in exactly
 * the same order. All arithmetic is deterministic integer.
 */
#ifndef NSZ_CONTEXT_H
#define NSZ_CONTEXT_H

#include <stdint.h>

#define NSZ_CTX_VOL_LEVELS 64
#define NSZ_CTX_SURPRISE 4
#define NSZ_CTX_COUNT (NSZ_CTX_VOL_LEVELS * NSZ_CTX_SURPRISE) /* 256 */
#define NSZ_CTX_GROUPS 4

#define NSZ_CTX_VOL_Q 4    /* Q4 fixed point for the EWMA */
#define NSZ_CTX_VOL_RATE 3 /* EWMA adaptation: 1/8 per step */

typedef struct {
    uint32_t vol;      /* Q4 EWMA of |residual| */
    uint32_t prev_abs; /* |residual| of the previous sample */
} NszContext;

static inline void nsz_context_init(NszContext *c)
{
    /* Start at a typical quiet-signal volatility (|r| ~ 20); any value
     * works as long as encoder and decoder use the same one. */
    c->vol = 20u << NSZ_CTX_VOL_Q;
    c->prev_abs = 0;
}

/* Log-quantize V (Q4) at 4 sub-levels per octave into [0, 63]. */
static inline uint32_t nsz_ctx_vol_level(uint32_t v)
{
    if (v < 4)
        return v;
#if defined(__GNUC__) || defined(__clang__)
    int m = 31 - __builtin_clz(v); /* v >= 4, so defined and m >= 2 */
#else
    int m = 2;
    while ((v >> (m + 1)) != 0)
        m++;
#endif
    /* m = index of the most significant bit (>= 2) */
    uint32_t sub = (v >> (m - 2)) & 3u;
    uint32_t lvl = ((uint32_t)(m - 1) << 2) | sub;
    return (lvl > NSZ_CTX_VOL_LEVELS - 1) ? NSZ_CTX_VOL_LEVELS - 1 : lvl;
}

static inline uint32_t nsz_context_id(const NszContext *c)
{
    uint32_t pq = c->prev_abs << NSZ_CTX_VOL_Q; /* Q4, < 2^22: no overflow */
    uint32_t s;
    if (pq * 2 <= c->vol)
        s = 0;
    else if (pq <= c->vol)
        s = 1;
    else if (pq <= c->vol * 2)
        s = 2;
    else
        s = 3;
    return nsz_ctx_vol_level(c->vol) * NSZ_CTX_SURPRISE + s;
}

/* Coarse volatility group for the mantissa tree models. */
static inline uint32_t nsz_context_group(const NszContext *c)
{
    return nsz_ctx_vol_level(c->vol) >> 4; /* 64 levels -> 4 groups */
}

/* Feed the |residual| of the sample that was just coded. */
static inline void nsz_context_update(NszContext *c, uint32_t abs_r)
{
    int32_t diff = (int32_t)(abs_r << NSZ_CTX_VOL_Q) - (int32_t)c->vol;
    int32_t step = (diff < 0) ? -1 - ((-1 - diff) >> NSZ_CTX_VOL_RATE)
                              : diff >> NSZ_CTX_VOL_RATE;
    c->vol = (uint32_t)((int32_t)c->vol + step);
    c->prev_abs = abs_r;
}

#endif /* NSZ_CONTEXT_H */
