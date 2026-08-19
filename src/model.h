/*
 * model.h — count-adaptive binary probability model.
 *
 * Each model holds an 11-bit estimate of P(bit == 0). After every coded
 * bit the estimate moves toward the observed bit by 1/2^rate of the gap,
 * where the rate depends on how many observations the model has seen:
 * young models take big steps (fast convergence from ignorance), mature
 * models take small steps (low estimation noise around the true rate).
 * Models seeded from a pretrained prior start "mature". The estimate is
 * intrinsically clamped away from 0 and 1, so every symbol stays
 * decodable.
 *
 * Encoder and decoder run identical update code on identical bit
 * sequences, keeping their probability state bit-exact.
 */
#ifndef NSZ_MODEL_H
#define NSZ_MODEL_H

#include "range_coder.h"

#include <stdint.h>

/* observation-count thresholds for the adaptation schedule */
#define NSZ_MODEL_N_YOUNG 16  /* below this: rate 4 (1/16 per step) */
#define NSZ_MODEL_N_ADULT 64  /* below this: rate 5; above: rate 6 */
#define NSZ_MODEL_N_CAP 1024
/* models seeded from a prior start with this many virtual observations */
#define NSZ_MODEL_PRIOR_SEED NSZ_MODEL_N_ADULT

typedef struct {
    uint16_t p0; /* P(bit==0) * 2048 */
    uint16_t n;  /* saturating observation count */
} NszBitModel;

static inline void nsz_model_init(NszBitModel *m)
{
    m->p0 = NSZ_RC_PROB_ONE / 2; /* 1024: no prior knowledge */
    m->n = 0;
}

static inline void nsz_model_update(NszBitModel *m, int bit)
{
    int rate = (m->n < NSZ_MODEL_N_YOUNG) ? 4
             : (m->n < NSZ_MODEL_N_ADULT) ? 5 : 6;
    if (m->n < NSZ_MODEL_N_CAP)
        m->n++;
    if (bit)
        m->p0 -= (uint16_t)(m->p0 >> rate);
    else
        m->p0 += (uint16_t)((NSZ_RC_PROB_ONE - m->p0) >> rate);
}

static inline void nsz_model_encode(NszRcEnc *e, NszBitModel *m, int bit)
{
    nsz_rc_encode_bit(e, m->p0, bit);
    nsz_model_update(m, bit);
}

static inline int nsz_model_decode(NszRcDec *d, NszBitModel *m)
{
    int bit = nsz_rc_decode_bit(d, m->p0);
    nsz_model_update(m, bit);
    return bit;
}

#endif /* NSZ_MODEL_H */
