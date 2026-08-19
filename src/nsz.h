/*
 * nsz.h — public API of the nsz lossless int16 signal codec.
 *
 * The codec knows nothing about POD5; it operates on raw int16 arrays.
 *
 * Chunk binary format v1 (all fields little-endian):
 *
 *   offset size field
 *   0      4    magic          "NSZ1" (0x315A534E LE)
 *   4      2    version        1
 *   6      2    flags          NSZ_F_RAW or NSZ_F_DELTA
 *   8      4    sample_count
 *   12     4    payload_size   bytes following the header
 *   16     4    raw_size       always 2 * sample_count
 *   20     2    prior_version  0 = no model prior (M0)
 *   22     2    reserved       0
 *   24     4    checksum       CRC32 of the original int16 samples (LE bytes)
 *   28     ...  payload
 *
 * Guarantee: the encoded chunk never exceeds NSZ_HEADER_SIZE + 2*n bytes,
 * because encoding falls back to verbatim raw storage (NSZ_F_RAW) whenever
 * the compressed payload would be >= the raw payload.
 */
#ifndef NSZ_H
#define NSZ_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define NSZ_MAGIC 0x315A534Eu /* "NSZ1" */
/* v2: count-adaptive model rates (bitstream-incompatible with v1) */
#define NSZ_VERSION 2u
#define NSZ_HEADER_SIZE 28u

/* chunk flags (exactly one must be set) */
#define NSZ_F_RAW 0x0001u   /* payload = verbatim int16 little-endian */
#define NSZ_F_DELTA 0x0002u /* payload = LEB128 varint of zigzag(delta residual) */
#define NSZ_F_LPC8 0x0004u  /* payload = LEB128 varint of zigzag(LPC8 residual) */
#define NSZ_F_RC 0x0008u    /* payload = range-coded magnitude/mantissa of
                               zigzag(LPC8 residual) */

enum {
    NSZ_OK = 0,
    NSZ_EARG = -1,      /* invalid argument */
    NSZ_ENOMEM = -2,    /* allocation failure */
    NSZ_EBADMAGIC = -3, /* wrong magic */
    NSZ_EVERSION = -4,  /* unsupported version */
    NSZ_ECORRUPT = -5,  /* structurally invalid stream */
    NSZ_ECHECKSUM = -6, /* decoded data does not match stored checksum */
    NSZ_EBOUNDS = -7    /* output buffer too small / size mismatch */
};

/* Maximum encoded size for n samples (header + raw fallback payload). */
size_t nsz_compress_bound(size_t n);

/*
 * Compress into a caller-provided buffer.
 * On input *out_size is the buffer capacity (>= nsz_compress_bound(n)),
 * on success it holds the number of bytes written.
 */
int nsz_compress_chunk(const int16_t *samples, size_t n,
                       uint8_t *out, size_t *out_size);

/*
 * Decompress a chunk. `n` must equal the encoded sample_count
 * (use nsz_peek_sample_count to discover it).
 */
int nsz_decompress_chunk(const uint8_t *in, size_t in_size,
                         int16_t *samples, size_t n);

/* Convenience variant: allocates *output with malloc (caller frees,
 * e.g. via nsz_free when calling across a language boundary). */
int nsz_compress(const int16_t *samples, size_t n,
                 uint8_t **output, size_t *output_size);
void nsz_free(void *ptr);

/*
 * Batch API: process many independent chunks in one call, parallelized
 * with OpenMP (threads <= 0 selects the OpenMP default). Chunks share no
 * state, so results are bit-identical to sequential calls.
 *
 * nsz_compress_batch: chunk i holds samples[offsets[i] .. offsets[i+1]).
 * Each output is malloc'd (free with nsz_free) and internally VERIFIED
 * (decoded and compared) before being returned. Returns NSZ_OK only if
 * every chunk compressed and verified.
 *
 * nsz_decompress_batch: input i is inputs[i][0 .. input_sizes[i]), decoded
 * into samples[offsets[i] .. offsets[i+1]).
 */
int nsz_compress_batch(const int16_t *samples, const uint64_t *offsets,
                       size_t n_chunks, int threads,
                       uint8_t **outputs, size_t *output_sizes);
int nsz_decompress_batch(const uint8_t *const *inputs,
                         const size_t *input_sizes, size_t n_chunks,
                         int threads, int16_t *samples,
                         const uint64_t *offsets);
int nsz_decompress(const uint8_t *input, size_t input_size,
                   int16_t *samples, size_t n);

/* Read sample_count from a chunk header without decoding. */
int nsz_peek_sample_count(const uint8_t *in, size_t in_size, uint32_t *count);

/*
 * Model prior. When loaded, every chunk's probability models start from
 * the pretrained state instead of uniform, avoiding the per-chunk learning
 * cost. The prior's version number is recorded in each chunk header;
 * decoding requires the SAME prior to be loaded (NSZ_EVERSION otherwise).
 *
 * File format (little-endian):
 *   "NSZP" u32 | version u16 (nonzero) | reserved u16 |
 *   model_count u32 | model_count x u16 probabilities
 */
int nsz_load_prior(const uint8_t *data, size_t size);
int nsz_load_prior_file(const char *path);
void nsz_clear_prior(void);
uint16_t nsz_prior_version(void);

/*
 * Coefficient profile (0 = R9/RNA, 1 = R10 DNA). The encoder records the
 * active profile in every chunk header; the decoder always follows the
 * header, so mixed-profile archives decode correctly. Loading a prior
 * whose file carries a profile id switches the active profile too.
 */
int nsz_set_profile(uint16_t profile);
uint16_t nsz_get_profile(void);

#ifdef __cplusplus
}
#endif

#endif /* NSZ_H */
