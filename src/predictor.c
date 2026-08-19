#include "predictor.h"

#include <string.h>

/*
 * Fixed order-8 LPC coefficient profiles, Q14 fixed point. The profile id
 * is recorded in every chunk header, so encoder and decoder always agree.
 *
 * Profile 0 — R9-era RNA (fitted on SG-NEx Hct116 directRNA; also within
 *             0.002 bits/sample of optimal on RNA004). Sum = 0.9888; the
 *             adaptive bias corrector absorbs the small DC leak.
 * Profile 1 — R10.4.1 DNA (5 kHz). Measured: the differenced signal is
 *             already white, every higher-order fit is WORSE than plain
 *             delta, so this profile IS delta (sum exactly 1, no DC leak).
 *
 * See analysis/analyze_signal.py for the fitting/measurement procedure.
 */
static const int32_t NSZ_LPC_PROFILES[NSZ_PRED_PROFILES][NSZ_PRED_HIST] = {
    { 7147, 3460, 1963, 1101, 663, 580, 538, 748 },
    { 16384, 0, 0, 0, 0, 0, 0, 0 },
};

#define NSZ_Q 14

/* EWMA bias corrector parameters (must match analysis/analyze_signal.py):
 * B is a Q8 estimate of the mean residual; correction = B >> 8;
 * update: B += ((r << 8) - B) >> 6. */
#define NSZ_BIAS_Q 8
#define NSZ_BIAS_RATE 6

/* Predictions are clamped so the residual is always bounded:
 * |residual| <= 32768 + NSZ_PRED_CLAMP < 2^18. */
#define NSZ_PRED_CLAMP 131072

/*
 * Arithmetic shift right with floor semantics for negative values.
 * (">>" on negative signed ints is implementation-defined in C99; this
 * form is portable and compiles to a single sar instruction on x86.)
 */
static inline int32_t asr32(int32_t x, int k)
{
    return (x < 0) ? -1 - ((-1 - x) >> k) : x >> k;
}

static inline int64_t asr64(int64_t x, int k)
{
    return (x < 0) ? -1 - ((-1 - x) >> k) : x >> k;
}

void nsz_predictor_init(NszPredictor *p, NszPredMode mode, uint16_t profile)
{
    p->mode = mode;
    p->coef = NSZ_LPC_PROFILES[profile < NSZ_PRED_PROFILES ? profile : 0];
    p->bias = 0;
    memset(p->hist, 0, sizeof(p->hist));
}

int32_t nsz_predictor_predict(const NszPredictor *p)
{
    switch (p->mode) {
    case NSZ_PRED_LPC8: {
        int64_t acc = 0;
        for (int k = 0; k < NSZ_PRED_HIST; k++)
            acc += (int64_t)p->coef[k] * p->hist[k];
        int64_t pred = asr64(acc, NSZ_Q) + asr32(p->bias, NSZ_BIAS_Q);
        if (pred > NSZ_PRED_CLAMP)
            pred = NSZ_PRED_CLAMP;
        else if (pred < -NSZ_PRED_CLAMP)
            pred = -NSZ_PRED_CLAMP;
        return (int32_t)pred;
    }
    case NSZ_PRED_DELTA:
        return p->hist[0];
    case NSZ_PRED_ZERO:
    default:
        return 0;
    }
}

void nsz_predictor_update(NszPredictor *p, int32_t sample, int32_t pred)
{
    if (p->mode == NSZ_PRED_LPC8) {
        /* The bias state depends on the residual of THIS sample, so it must
         * be updated before the history shifts. The caller passes in the
         * prediction it just used, which is bit-identical to recomputing. */
        int32_t r = sample - pred;
        /* |r| < 2^18, so r * 2^NSZ_BIAS_Q fits in int32. Multiplication is
         * used instead of << because left-shifting negative values is UB. */
        p->bias += asr32(r * (1 << NSZ_BIAS_Q) - p->bias, NSZ_BIAS_RATE);
    }
    memmove(&p->hist[1], &p->hist[0], (NSZ_PRED_HIST - 1) * sizeof(int32_t));
    p->hist[0] = sample;
}
