/*
 * range_coder.h — adaptive binary range coder (LZMA-style).
 *
 * Encodes a sequence of binary decisions, each with an 11-bit probability
 * estimate P(bit = 0) in [1, 2047] out of 2048. Average cost per bit
 * approaches the true entropy, i.e. fractional bits per symbol — this is
 * what byte-aligned varints fundamentally cannot do.
 *
 * How it works: the coder maintains an interval [low, low + range). Each
 * bit narrows the interval to the sub-interval proportional to its
 * probability. When `range` drops below 2^24 the top byte of `low` is
 * settled and emitted (normalization). The decoder mirrors the interval
 * arithmetic exactly, so both sides stay bit-synchronized.
 *
 * Carry handling: `low` is 33+ bits; an addition can overflow into bit 32.
 * The encoder therefore buffers one byte (`cache`) plus a run of 0xFF bytes
 * (`cache_size`) that a carry would increment before they can be written.
 *
 * All arithmetic is integer and deterministic. No floating point.
 *
 * Bounds safety: the encoder never writes past its buffer (sets `overflow`
 * instead); the decoder never reads past its buffer (sets `error` and
 * returns zero bytes, which the caller must check after decoding).
 */
#ifndef NSZ_RANGE_CODER_H
#define NSZ_RANGE_CODER_H

#include <stddef.h>
#include <stdint.h>

#define NSZ_RC_PROB_BITS 11
#define NSZ_RC_PROB_ONE (1u << NSZ_RC_PROB_BITS) /* 2048 */
#define NSZ_RC_TOP (1u << 24)

/* ---------------- encoder ---------------- */

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t pos;
    uint64_t low;
    uint32_t range;
    uint8_t cache;
    uint64_t cache_size;
    int overflow; /* set when output exceeds cap; output is then invalid */
} NszRcEnc;

static inline void nsz_rc_enc_init(NszRcEnc *e, uint8_t *buf, size_t cap)
{
    e->buf = buf;
    e->cap = cap;
    e->pos = 0;
    e->low = 0;
    e->range = 0xFFFFFFFFu;
    e->cache = 0;
    e->cache_size = 1;
    e->overflow = 0;
}

static inline void nsz_rc_enc_out(NszRcEnc *e, uint8_t byte)
{
    if (e->pos < e->cap)
        e->buf[e->pos++] = byte;
    else
        e->overflow = 1;
}

static inline void nsz_rc_enc_shift_low(NszRcEnc *e)
{
    if ((uint32_t)e->low < 0xFF000000u || (e->low >> 32) != 0) {
        uint8_t carry = (uint8_t)(e->low >> 32);
        do {
            nsz_rc_enc_out(e, (uint8_t)(e->cache + carry));
            e->cache = 0xFF;
        } while (--e->cache_size);
        e->cache = (uint8_t)(e->low >> 24);
    }
    e->cache_size++;
    /* The shift must happen in 32-bit arithmetic so the top byte (already
     * moved to cache, or accounted as a pending 0xFF) is discarded. */
    e->low = (uint32_t)(((uint32_t)e->low) << 8);
}

/* Encode one bit given P(bit==0) = p0/2048, p0 in [1, 2047]. */
static inline void nsz_rc_encode_bit(NszRcEnc *e, uint32_t p0, int bit)
{
    uint32_t bound = (e->range >> NSZ_RC_PROB_BITS) * p0;
    if (!bit) {
        e->range = bound;
    } else {
        e->low += bound;
        e->range -= bound;
    }
    while (e->range < NSZ_RC_TOP) {
        nsz_rc_enc_shift_low(e);
        e->range <<= 8;
    }
}

/* Encode one bit with implicit probability 1/2 (no model). */
static inline void nsz_rc_encode_direct(NszRcEnc *e, int bit)
{
    e->range >>= 1;
    if (bit)
        e->low += e->range;
    while (e->range < NSZ_RC_TOP) {
        nsz_rc_enc_shift_low(e);
        e->range <<= 8;
    }
}

/* Flush remaining state; encoded size is e->pos afterwards. */
static inline void nsz_rc_enc_flush(NszRcEnc *e)
{
    for (int i = 0; i < 5; i++)
        nsz_rc_enc_shift_low(e);
}

/* ---------------- decoder ---------------- */

typedef struct {
    const uint8_t *buf;
    size_t size;
    size_t pos;
    uint32_t range;
    uint32_t code;
    int error; /* set when input is exhausted early */
} NszRcDec;

static inline uint8_t nsz_rc_dec_byte(NszRcDec *d)
{
    if (d->pos < d->size)
        return d->buf[d->pos++];
    d->error = 1;
    return 0;
}

static inline void nsz_rc_dec_init(NszRcDec *d, const uint8_t *buf, size_t size)
{
    d->buf = buf;
    d->size = size;
    d->pos = 0;
    d->range = 0xFFFFFFFFu;
    d->code = 0;
    d->error = 0;
    (void)nsz_rc_dec_byte(d); /* first byte is always 0 (encoder cache) */
    for (int i = 0; i < 4; i++)
        d->code = (d->code << 8) | nsz_rc_dec_byte(d);
}

static inline int nsz_rc_decode_bit(NszRcDec *d, uint32_t p0)
{
    uint32_t bound = (d->range >> NSZ_RC_PROB_BITS) * p0;
    int bit;
    if (d->code < bound) {
        bit = 0;
        d->range = bound;
    } else {
        bit = 1;
        d->code -= bound;
        d->range -= bound;
    }
    while (d->range < NSZ_RC_TOP) {
        d->range <<= 8;
        d->code = (d->code << 8) | nsz_rc_dec_byte(d);
    }
    return bit;
}

static inline int nsz_rc_decode_direct(NszRcDec *d)
{
    d->range >>= 1;
    int bit = (d->code >= d->range);
    if (bit)
        d->code -= d->range;
    while (d->range < NSZ_RC_TOP) {
        d->range <<= 8;
        d->code = (d->code << 8) | nsz_rc_dec_byte(d);
    }
    return bit;
}

#endif /* NSZ_RANGE_CODER_H */
