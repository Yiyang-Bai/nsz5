/*
 * predictor.h — sample predictor shared by encoder and decoder.
 *
 * CRITICAL INVARIANT: the encoder and the decoder must call
 * nsz_predictor_predict / nsz_predictor_update in exactly the same order
 * with exactly the same reconstructed sample values, so that both sides
 * hold bit-identical state at every step. All arithmetic is integer and
 * deterministic.
 *
 * Modes:
 *   NSZ_PRED_ZERO  — predicts 0 (residual = sample itself; raw-equivalent)
 *   NSZ_PRED_DELTA — order-1: predicts the previous sample
 *   NSZ_PRED_LPC8  — fixed order-8 LPC (Q14) + adaptive EWMA bias corrector
 */
#ifndef NSZ_PREDICTOR_H
#define NSZ_PREDICTOR_H

#include <stdint.h>

#define NSZ_PRED_HIST 8
#define NSZ_PRED_PROFILES 2 /* 0 = R9/RNA, 1 = R10 DNA (see predictor.c) */

typedef enum {
    NSZ_PRED_ZERO = 0,
    NSZ_PRED_DELTA = 1,
    NSZ_PRED_LPC8 = 2
} NszPredMode;

typedef struct {
    NszPredMode mode;
    const int32_t *coef; /* active LPC coefficient profile (Q14) */
    /* hist[0] is the most recent sample, hist[NSZ_PRED_HIST-1] the oldest.
     * Initialized to zero: the first prediction is always 0. */
    int32_t hist[NSZ_PRED_HIST];
    /* Q8 EWMA estimate of the mean residual (LPC8 mode only). */
    int32_t bias;
} NszPredictor;

void nsz_predictor_init(NszPredictor *p, NszPredMode mode, uint16_t profile);
int32_t nsz_predictor_predict(const NszPredictor *p);
/* `pred` must be the value nsz_predictor_predict() just returned for this
 * sample (passing it in avoids recomputing the prediction internally). */
void nsz_predictor_update(NszPredictor *p, int32_t sample, int32_t pred);

#endif /* NSZ_PREDICTOR_H */
