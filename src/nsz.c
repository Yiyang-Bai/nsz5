/*
 * nsz.c — M0 implementation: chunk container, CRC32, varint payloads.
 *
 * Two payload encodings exist at this stage:
 *   NSZ_F_DELTA — order-1 delta residuals, zigzag-mapped, LEB128 varints
 *   NSZ_F_RAW   — verbatim int16 little-endian (fallback)
 *
 * The encoder always tries DELTA first; if the payload would reach the raw
 * size (2*n bytes) it falls back to RAW, so the output payload is never
 * larger than the input. The decoder validates every field and never reads
 * or writes out of bounds on malformed input.
 */
#include "nsz.h"
#include "magnitude.h"
#include "predictor.h"
#include "zigzag.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* CRC32 (IEEE, polynomial 0xEDB88320), table built on first use.      */
/* ------------------------------------------------------------------ */

static uint32_t crc_table[256];
static int crc_table_ready = 0;

static void crc32_init(void)
{
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++)
            c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
        crc_table[i] = c;
    }
    crc_table_ready = 1;
}

/* CRC over samples serialized as little-endian int16, independent of the
 * host byte order, so checksums are portable. */
static uint32_t crc32_samples(const int16_t *samples, size_t n)
{
    if (!crc_table_ready)
        crc32_init();
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        uint16_t v = (uint16_t)samples[i];
        uint8_t lo = (uint8_t)(v & 0xFFu);
        uint8_t hi = (uint8_t)(v >> 8);
        c = crc_table[(c ^ lo) & 0xFFu] ^ (c >> 8);
        c = crc_table[(c ^ hi) & 0xFFu] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* Little-endian field access                                          */
/* ------------------------------------------------------------------ */

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

static uint16_t get_u16(const uint8_t *p)
{
    return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

static uint32_t get_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* ------------------------------------------------------------------ */
/* LEB128 varint for uint32 (max 5 bytes)                              */
/* ------------------------------------------------------------------ */

/* Returns bytes consumed, or 0 on malformed/out-of-bounds input.
 * (The varint encoder was removed when the range coder became the default;
 * decoding stays for compatibility with M0/M1 streams.) */
static size_t varint_get(const uint8_t *src, size_t avail, uint32_t *out)
{
    uint32_t v = 0;
    size_t i = 0;
    for (;;) {
        if (i >= avail || i >= 5)
            return 0;
        uint8_t b = src[i];
        /* the 5th byte may only carry the top 4 bits of a uint32 */
        if (i == 4 && (b & 0xF0u))
            return 0;
        v |= (uint32_t)(b & 0x7Fu) << (7 * i);
        i++;
        if (!(b & 0x80u))
            break;
    }
    *out = v;
    return i;
}

/* ------------------------------------------------------------------ */
/* Model prior                                                         */
/* ------------------------------------------------------------------ */

#define NSZ_PRIOR_MAGIC 0x505A534Eu /* "NSZP" */
#define NSZ_PRIOR_MODEL_COUNT (sizeof(NszMagModels) / sizeof(NszBitModel))

static NszMagModels nsz_prior_models;
static uint16_t nsz_prior_ver = 0; /* 0 = no prior loaded */
static uint16_t nsz_profile = 0;   /* active coefficient profile */

int nsz_set_profile(uint16_t profile)
{
    if (profile >= NSZ_PRED_PROFILES)
        return NSZ_EARG;
    nsz_profile = profile;
    return NSZ_OK;
}

uint16_t nsz_get_profile(void)
{
    return nsz_profile;
}

/* Initialize a chunk's model bank: from the prior if loaded, else uniform. */
static void mag_models_setup(NszMagModels *mm)
{
    if (nsz_prior_ver)
        *mm = nsz_prior_models;
    else
        nsz_mag_models_init(mm);
}

int nsz_load_prior(const uint8_t *data, size_t size)
{
    if (!data || size < 12)
        return NSZ_ECORRUPT;
    if (get_u32(data) != NSZ_PRIOR_MAGIC)
        return NSZ_EBADMAGIC;
    uint16_t ver = get_u16(data + 4);
    uint16_t profile = get_u16(data + 6);
    uint32_t count = get_u32(data + 8);
    if (ver == 0 || count != NSZ_PRIOR_MODEL_COUNT ||
        profile >= NSZ_PRED_PROFILES)
        return NSZ_EVERSION;
    if (size < 12 + (size_t)count * 2)
        return NSZ_ECORRUPT;

    NszBitModel *dst = (NszBitModel *)&nsz_prior_models;
    for (uint32_t i = 0; i < count; i++) {
        uint16_t p = get_u16(data + 12 + 2 * i);
        if (p < 1 || p > NSZ_RC_PROB_ONE - 1)
            return NSZ_ECORRUPT;
        dst[i].p0 = p;
        /* prior-seeded models start mature: small adaptation steps */
        dst[i].n = NSZ_MODEL_PRIOR_SEED;
    }
    nsz_prior_ver = ver;
    nsz_profile = profile;
    return NSZ_OK;
}

int nsz_load_prior_file(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NSZ_EARG;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) {
        fclose(f);
        return NSZ_ECORRUPT;
    }
    uint8_t *buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
        fclose(f);
        return NSZ_ENOMEM;
    }
    int rc = NSZ_ECORRUPT;
    if (fread(buf, 1, (size_t)sz, f) == (size_t)sz)
        rc = nsz_load_prior(buf, (size_t)sz);
    fclose(f);
    free(buf);
    return rc;
}

void nsz_clear_prior(void)
{
    nsz_prior_ver = 0;
}

uint16_t nsz_prior_version(void)
{
    return nsz_prior_ver;
}

/* ------------------------------------------------------------------ */
/* Header                                                              */
/* ------------------------------------------------------------------ */

static void header_write(uint8_t *out, uint16_t flags, uint32_t sample_count,
                         uint32_t payload_size, uint32_t checksum)
{
    put_u32(out + 0, NSZ_MAGIC);
    put_u16(out + 4, (uint16_t)NSZ_VERSION);
    put_u16(out + 6, flags);
    put_u32(out + 8, sample_count);
    put_u32(out + 12, payload_size);
    put_u32(out + 16, sample_count * 2u);
    /* prior_version and profile matter only for range-coded chunks */
    put_u16(out + 20, (flags == NSZ_F_RC) ? nsz_prior_ver : 0);
    put_u16(out + 22, (flags == NSZ_F_RC) ? nsz_profile : 0);
    put_u32(out + 24, checksum);
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

size_t nsz_compress_bound(size_t n)
{
    return (size_t)NSZ_HEADER_SIZE + 2u * n;
}

int nsz_compress_chunk(const int16_t *samples, size_t n,
                       uint8_t *out, size_t *out_size)
{
    if ((!samples && n > 0) || !out || !out_size)
        return NSZ_EARG;
    if (n > 0x7FFFFFFFu) /* sample_count must fit the u32 header field */
        return NSZ_EARG;
    if (*out_size < nsz_compress_bound(n))
        return NSZ_EBOUNDS;

    uint8_t *payload = out + NSZ_HEADER_SIZE;
    const size_t raw_size = 2u * n;

    /* Range-coded LPC8 residuals, capped at raw_size: if the coder would
     * write past the raw size we abandon compression and store raw. */
    int use_rc = 1;
    NszRcEnc rc;
    NszMagModels mm;
    NszPredictor pred;
    NszContext cx;
    nsz_rc_enc_init(&rc, payload, raw_size);
    mag_models_setup(&mm);
    nsz_predictor_init(&pred, NSZ_PRED_LPC8, nsz_profile);
    nsz_context_init(&cx);
    for (size_t i = 0; i < n; i++) {
        int32_t p = nsz_predictor_predict(&pred);
        int32_t r = (int32_t)samples[i] - p; /* |r| < 2^18 by pred clamp */
        uint32_t u = nsz_zigzag_encode(r);
        /* context comes from PAST residuals only, then is advanced */
        uint32_t ctx = nsz_context_id(&cx);
        uint32_t grp = ctx >> 6; /* == vol_level >> 4 */
        if (nsz_mag_encode(&rc, &mm, ctx, grp, u) != 0 || rc.overflow) {
            use_rc = 0;
            break;
        }
        nsz_context_update(&cx, (uint32_t)(r < 0 ? -r : r));
        nsz_predictor_update(&pred, (int32_t)samples[i], p);
    }
    if (use_rc) {
        nsz_rc_enc_flush(&rc);
        if (rc.overflow)
            use_rc = 0;
    }

    uint16_t flags;
    size_t payload_size;
    if (use_rc && rc.pos < raw_size) {
        flags = (uint16_t)NSZ_F_RC;
        payload_size = rc.pos;
    } else {
        flags = (uint16_t)NSZ_F_RAW;
        payload_size = raw_size;
        for (size_t i = 0; i < n; i++) {
            uint16_t v = (uint16_t)samples[i];
            payload[2 * i] = (uint8_t)(v & 0xFFu);
            payload[2 * i + 1] = (uint8_t)(v >> 8);
        }
    }

    header_write(out, flags, (uint32_t)n, (uint32_t)payload_size,
                 crc32_samples(samples, n));
    *out_size = NSZ_HEADER_SIZE + payload_size;
    return NSZ_OK;
}

int nsz_peek_sample_count(const uint8_t *in, size_t in_size, uint32_t *count)
{
    if (!in || !count || in_size < NSZ_HEADER_SIZE)
        return NSZ_ECORRUPT;
    if (get_u32(in + 0) != NSZ_MAGIC)
        return NSZ_EBADMAGIC;
    if (get_u16(in + 4) != NSZ_VERSION)
        return NSZ_EVERSION;
    *count = get_u32(in + 8);
    return NSZ_OK;
}

int nsz_decompress_chunk(const uint8_t *in, size_t in_size,
                         int16_t *samples, size_t n)
{
    if (!in || (!samples && n > 0))
        return NSZ_EARG;
    if (in_size < NSZ_HEADER_SIZE)
        return NSZ_ECORRUPT;

    if (get_u32(in + 0) != NSZ_MAGIC)
        return NSZ_EBADMAGIC;
    if (get_u16(in + 4) != NSZ_VERSION)
        return NSZ_EVERSION;

    const uint16_t flags = get_u16(in + 6);
    const uint32_t sample_count = get_u32(in + 8);
    const uint32_t payload_size = get_u32(in + 12);
    const uint32_t raw_size = get_u32(in + 16);
    const uint32_t checksum = get_u32(in + 24);

    if (sample_count != n)
        return NSZ_EBOUNDS;
    if (raw_size != sample_count * 2u)
        return NSZ_ECORRUPT;
    if ((size_t)payload_size != in_size - NSZ_HEADER_SIZE)
        return NSZ_ECORRUPT;

    const uint8_t *payload = in + NSZ_HEADER_SIZE;

    if (flags == NSZ_F_RAW) {
        if (payload_size != raw_size)
            return NSZ_ECORRUPT;
        for (size_t i = 0; i < n; i++) {
            uint16_t v = (uint16_t)payload[2 * i] |
                         ((uint16_t)payload[2 * i + 1] << 8);
            samples[i] = (int16_t)v;
        }
    } else if (flags == NSZ_F_RC) {
        /* the chunk must have been encoded with the currently loaded prior */
        if (get_u16(in + 20) != nsz_prior_ver)
            return NSZ_EVERSION;
        /* the coefficient profile travels in the header */
        const uint16_t profile = get_u16(in + 22);
        if (profile >= NSZ_PRED_PROFILES)
            return NSZ_EVERSION;
        NszRcDec rc;
        NszMagModels mm;
        NszPredictor pred;
        NszContext cx;
        nsz_rc_dec_init(&rc, payload, payload_size);
        mag_models_setup(&mm);
        nsz_predictor_init(&pred, NSZ_PRED_LPC8, profile);
        nsz_context_init(&cx);
        for (size_t i = 0; i < n; i++) {
            uint32_t ctx = nsz_context_id(&cx);
            uint32_t grp = ctx >> 6; /* == vol_level >> 4 */
            uint32_t u;
            if (nsz_mag_decode(&rc, &mm, ctx, grp, &u) != 0)
                return NSZ_ECORRUPT;
            int32_t res = nsz_zigzag_decode(u);
            int32_t x = nsz_predictor_predict(&pred) + res;
            if (x < INT16_MIN || x > INT16_MAX)
                return NSZ_ECORRUPT;
            samples[i] = (int16_t)x;
            nsz_context_update(&cx, (uint32_t)(res < 0 ? -res : res));
            nsz_predictor_update(&pred, x, x - res);
        }
        if (rc.error)
            return NSZ_ECORRUPT;
    } else if (flags == NSZ_F_DELTA || flags == NSZ_F_LPC8) {
        size_t pos = 0;
        NszPredictor pred;
        nsz_predictor_init(&pred,
                           flags == NSZ_F_LPC8 ? NSZ_PRED_LPC8
                                               : NSZ_PRED_DELTA,
                           0);
        for (size_t i = 0; i < n; i++) {
            uint32_t u;
            size_t r = varint_get(payload + pos, payload_size - pos, &u);
            if (r == 0)
                return NSZ_ECORRUPT;
            pos += r;
            int32_t res = nsz_zigzag_decode(u);
            int32_t x = nsz_predictor_predict(&pred) + res;
            if (x < INT16_MIN || x > INT16_MAX)
                return NSZ_ECORRUPT;
            samples[i] = (int16_t)x;
            nsz_predictor_update(&pred, x, x - res);
        }
        if (pos != payload_size)
            return NSZ_ECORRUPT;
    } else {
        return NSZ_ECORRUPT;
    }

    if (crc32_samples(samples, n) != checksum)
        return NSZ_ECHECKSUM;
    return NSZ_OK;
}

int nsz_compress(const int16_t *samples, size_t n,
                 uint8_t **output, size_t *output_size)
{
    if (!output || !output_size)
        return NSZ_EARG;
    size_t cap = nsz_compress_bound(n);
    uint8_t *buf = (uint8_t *)malloc(cap ? cap : 1);
    if (!buf)
        return NSZ_ENOMEM;
    size_t sz = cap;
    int rc = nsz_compress_chunk(samples, n, buf, &sz);
    if (rc != NSZ_OK) {
        free(buf);
        return rc;
    }
    *output = buf;
    *output_size = sz;
    return NSZ_OK;
}

int nsz_decompress(const uint8_t *input, size_t input_size,
                   int16_t *samples, size_t n)
{
    return nsz_decompress_chunk(input, input_size, samples, n);
}

void nsz_free(void *ptr)
{
    free(ptr);
}

#ifdef _OPENMP
#include <omp.h>
#endif

int nsz_compress_batch(const int16_t *samples, const uint64_t *offsets,
                       size_t n_chunks, int threads,
                       uint8_t **outputs, size_t *output_sizes)
{
    if (!samples || !offsets || !outputs || !output_sizes)
        return NSZ_EARG;

    int failed = 0;
#ifdef _OPENMP
    if (threads > 0)
        omp_set_num_threads(threads);
#else
    (void)threads;
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
#endif
    for (size_t i = 0; i < n_chunks; i++) {
        outputs[i] = NULL;
        output_sizes[i] = 0;
        size_t len = (size_t)(offsets[i + 1] - offsets[i]);
        uint8_t *out = NULL;
        size_t out_sz = 0;
        int rc = nsz_compress(samples + offsets[i], len, &out, &out_sz);
        if (rc == NSZ_OK) {
            /* verify: decode and compare before handing the blob out */
            int16_t *check = (int16_t *)malloc(len ? len * 2 : 1);
            if (!check ||
                nsz_decompress(out, out_sz, check, len) != NSZ_OK ||
                (len && memcmp(check, samples + offsets[i], len * 2) != 0))
                rc = NSZ_ECORRUPT;
            free(check);
        }
        if (rc != NSZ_OK) {
            free(out);
#ifdef _OPENMP
#pragma omp atomic write
#endif
            failed = 1;
        } else {
            outputs[i] = out;
            output_sizes[i] = out_sz;
        }
    }

    if (failed) {
        for (size_t i = 0; i < n_chunks; i++) {
            free(outputs[i]);
            outputs[i] = NULL;
        }
        return NSZ_ECORRUPT;
    }
    return NSZ_OK;
}

int nsz_decompress_batch(const uint8_t *const *inputs,
                         const size_t *input_sizes, size_t n_chunks,
                         int threads, int16_t *samples,
                         const uint64_t *offsets)
{
    if (!inputs || !input_sizes || !samples || !offsets)
        return NSZ_EARG;

    int failed = 0;
#ifdef _OPENMP
    if (threads > 0)
        omp_set_num_threads(threads);
#else
    (void)threads;
#endif
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic, 8)
#endif
    for (size_t i = 0; i < n_chunks; i++) {
        size_t len = (size_t)(offsets[i + 1] - offsets[i]);
        if (nsz_decompress(inputs[i], input_sizes[i],
                           samples + offsets[i], len) != NSZ_OK) {
#ifdef _OPENMP
#pragma omp atomic write
#endif
            failed = 1;
        }
    }
    return failed ? NSZ_ECORRUPT : NSZ_OK;
}
