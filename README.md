# nsz5 — byte-exact lossless compression for POD5 nanopore files

A domain-specific lossless codec for Oxford Nanopore raw signal, wrapped in
an archival container that reconstructs the original POD5 file
**byte-for-byte** (verified by SHA-256) and provably leaves basecalling
results unchanged (verified by identical Dorado outputs).

```
POD5 raw int16 signal
    → fixed order-8 LPC prediction (Q14 integer, per-chemistry profiles)
    → adaptive bias correction
    → residual → zigzag → magnitude class + mantissa tree
    → volatility/surprise context modeling (256 contexts)
    → adaptive binary range coding, seeded from a pretrained prior
    → compressed bitstream
```

## Results

### Codec level — identical 20 M-sample signal per dataset, same chunking

| codec | R9/RNA002 | RNA004 | R10.4.1 DNA | encode speed |
|---|---|---|---|---|
| **nsz (this work)** | **6.432 (+4.0%)** | **6.570 (+5.9%)** | **6.729 (+2.8%)** | ~11–12 Ms/s |
| FLAC ‑8 | 6.514 (+2.8%) | 6.643 (+4.9%) | 6.855 (+1.0%) | 5–26 Ms/s |
| VBZ (POD5 default) | 6.702 (baseline) | 6.983 (baseline) | 6.923 (baseline) | ~500 Ms/s |
| bzip2 ‑9 | 7.146 (−6.6%) | 7.256 (−3.9%) | 7.246 (−4.7%) | 7 Ms/s |
| xz ‑9 | 7.427 (−10.8%) | 7.635 (−9.3%) | 7.635 (−10.3%) | <1 Ms/s |
| zstd ‑19 | 8.470 (−26.4%) | 8.465 (−21.2%) | 8.381 (−21.1%) | ~1 Ms/s |
| raw int16 | 16.000 | 16.000 | 16.000 | — |

Values are bits/sample; percentages are savings relative to VBZ on the same
data. Reproduce with `analysis/benchmark.py`. Two conclusions replicate the
published nsz reference across all three chemistries:
every general-purpose compressor loses to VBZ, and FLAC (a stock audio
codec) beats VBZ but not nsz.

### Head-to-head with ex-zd on its own published benchmark

Reproduction of Fig. 1 of the ex-zd paper (Gamaarachchi et al., *Genome Res*
2025) on the **same dataset** (HG002-Prom5K Chr22 subset, flowcell
PGXXSX240041, 32.3 B samples, from the authors' gtgseq S3 bucket), same
slow5tools v1.3.0, same metric (`du` bytes; ratio vs uncompressed BLOW5):

| method | bytes | ratio |
|---|---|---|
| **nsz5 (this work)** | **27,339,081,585** | **2.366** |
| ex-zd zstd (paper's best) | 27,503,933,718 | 2.352 |
| ex-zd zlib | 27,740,315,084 | 2.332 |
| POD5 native (VBZ) | 28,154,437,312 | 2.298 |
| zstd + svb-zd | 28,232,068,018 | 2.291 |
| zlib + svb-zd (BLOW5 default) | 28,470,503,899 | 2.272 |
| uncompressed BLOW5 | 64,685,036,199 | 1.000 |

The measured ex-zd ratio (2.352) matches the published value (2.35),
validating the pipeline. nsz5 is 0.60% smaller than ex-zd and 2.90%
smaller than VBZ on this data, with byte-exact reconstruction verified
(SHA-256 identical; 971/515,679 blobs fell back to verbatim). The paper's
prior was trained on a different HG002 run (held-out). Figures:
`work/figures/fig6*.png`, `fig7*.png`, `fig8*.png`.

### File level — full POD5 → .nsz5 → POD5 round trips

| file | chemistry | size | saving | verbatim fallbacks | SHA-256 |
|---|---|---|---|---|---|
| SG-NEx Hct116 directRNA (blue-crab) | R9.4.1 / RNA002 | 92 MB | 4.22% | 0 / 7,897 | identical |
| ONT rna-modbase control (native MinKNOW) | RNA004 | 364 MB | 7.79% | 0 / 60,053 | identical |
| ONT GIAB HG002 (native MinKNOW) | R10.4.1 DNA 5 kHz | 1.88 GB | 2.83% | 49 / 26,300 | identical |

### Parallel throughput

Chunks are independent, so both directions parallelize with bit-identical
output (`--threads` flag; compression uses a process pool over blobs,
decompression uses the OpenMP batch API in C). Measured on the 32.3
B-sample chr22 dataset (32 threads): compress 5,071 s → 442 s (11.5×,
73 Ms/s), decompress 2,648 s → 393 s (6.7×, 84 Ms/s), SHA-256 verified.
Small files: 88 MB compresses in 1.9 s (was 24.6 s).

### Basecalling equivalence (Dorado 2.1.1, A100, rna004_130bps_hac@v5.2.0)

Three runs — original POD5 twice (nondeterminism control) and the rebuilt
POD5 once — produced **identical canonicalized FASTQ output**
(59,995 reads each, same md5). Byte-identity of the file makes this
expected; the double run proves it empirically.

## How it works

**Predictor.** A fixed order-8 linear predictor in Q14 integer arithmetic
(bit-exact across encoder/decoder), plus an adaptive EWMA bias corrector
that absorbs the DC leak of quantized coefficients. Two coefficient
*profiles* ship with the codec and are recorded per chunk header:

- profile 0 — R9-era RNA (fitted on SG-NEx directRNA; measured within
  0.002 bits/sample of optimal on RNA004 as well);
- profile 1 — R10.4.1 DNA: measured to be pure delta. The 5 kHz DNA signal
  is white after first differencing; every higher-order fit was *worse*.

**Residual mapping.** Residual → zigzag → `w = u+1` split into a magnitude
class (adaptive unary code) and a mantissa (leading bits through a binary
tree of adaptive models, tail bits raw).

**Entropy coder.** An LZMA-style adaptive binary range coder (fractional
bits per symbol; measured 0.2420 bits/bit against a 0.2423-bit source).

**Contexts.** Every model is selected by a causal context: an EWMA of
|residual| log-quantized at 4 levels/octave (64 levels) × a 4-level
"surprise" term = 256 contexts for the class models; 4 coarse volatility
groups for the mantissa tree (finer splits dilute its statistics).

**Prior.** Chunks are independently decodable, so models reset per chunk.
To avoid paying the learning cost of 10,240 context models on every chunk,
a pretrained prior (`models/prior*.bin`, ~21 KB) seeds each chunk. Priors
are versioned and coupled to a profile; the chunk header records both and
the decoder refuses to guess (NSZ_EVERSION on mismatch). Measured on
held-out data (trained on one run, tested on another): −0.047 bits/sample.
Without a prior, the 256-context model is a net LOSS on real chunk sizes —
context modeling and the prior only work as a pair.

**Raw fallback.** If a chunk would not shrink, it is stored verbatim
(header + 2n bytes max). Uniform-random input costs 28 bytes of overhead,
never more.

## Byte-exactness (the container)

A POD5 file is treated as a byte stream: Arrow metadata is preserved
verbatim (zstd-19 "skeleton"); each VBZ signal blob is located via
zero-copy pyarrow buffer addresses, decoded to int16, and nsz-compressed.
On decompression every blob is regenerated as
`zstd-1(svb16(zigzag(delta(x))))` and spliced back at its original offset.

The outer zstd frame depends on the zstd version that wrote the file, so:

- regeneration is verified **per blob at compression time**; a blob that
  does not reproduce bit-for-bit is stored verbatim instead;
- the compressor auto-detects the writing zstd version by trying multiple
  libzstd builds (`work/zstd_env_*`, currently 1.5.6/1.5.5/1.5.2/1.5.0/1.4.9)
  and records the choice per blob. The GIAB 2025 file (zstd-1.5.0-era
  MinKNOW) went from 25,590 fallbacks to 49 this way;
- SHA-256 of the original is stored in the container and re-verified on
  every decompression. The output is *always* byte-identical; the format
  degrades in size, never in correctness.

## Usage

```bash
# build codec + library + tools
make lib bench train          # work/libnsz.so, bench_file, train_prior
make test                     # full test suite under ASan/UBSan

# archive a POD5 file (prior optional but recommended)
venv/bin/python python/nsz5.py compress   in.pod5  out.nsz5 --prior models/prior.bin
venv/bin/python python/nsz5.py decompress out.nsz5 rebuilt.pod5
venv/bin/python python/nsz5.py verify     in.pod5  rebuilt.pod5

# signal-only comparison against VBZ on the same file
venv/bin/python python/nsz5.py bench in.pod5 --prior models/prior.bin

# codec-level benchmark vs FLAC/zstd/xz/bzip2 on a raw signal dump
venv/bin/python tools/extract_signal.py in.pod5 sig.bin --max-samples 20000000
venv/bin/python analysis/benchmark.py sig.bin --prior models/prior.bin

# fit coefficients / train a prior for a new chemistry
venv/bin/python analysis/analyze_signal.py sig.bin --order 8
./work/train_prior train_sig.bin models/prior_new.bin 102400 <version> <profile>
```

Python side needs `venv/` with: `pod5`, `pyarrow`, `numpy`, `zstandard`
(plus `blue-crab` for blow5 conversion, `pyflac` for benchmarks).

## Layout

```
src/nsz.{c,h}            public API, chunk format, CRC32, prior, raw fallback
src/predictor.{c,h}      order-8 LPC (Q14) + bias corrector, profiles
src/zigzag.h             signed<->unsigned mapping
src/magnitude.h          class + mantissa-tree coding
src/context.h            EWMA volatility + surprise contexts
src/model.h              count-adaptive binary probability models
src/range_coder.h        adaptive binary range coder
tools/bench_file.c       full-file roundtrip verification + throughput
tools/train_prior.c      prior training
tools/extract_signal.py  POD5 -> flat int16 dump
python/nsz5.py           archival container CLI (compress/decompress/verify/bench)
python/libnsz.py         ctypes bindings
python/zstd_backends.py  multi-version zstd for blob regeneration
analysis/                coefficient fitting, entropy analysis, benchmarks
models/prior.bin         pretrained prior, profile 0 (R9/RNA)
models/prior_r10.bin     pretrained prior, profile 1 (R10 DNA)
tests/                   unit + fuzz + adversarial tests (make test)
```

## Testing

`make test` runs everything under `-fsanitize=address,undefined`:

- zigzag: exhaustive ±2^17 window, int32 extremes, 10 M random values
- predictor: encoder/decoder state-sync on 6 signal shapes × 3 modes ×
  2 profiles, including real nanopore signal
- range coder: random probability streams, extreme skews, entropy-optimality
  check (within 0.15% of the source entropy)
- magnitude: exhaustive round-trip of all 2^19 values, both directions
- context: determinism, bounds under adversarial input, quantizer
  monotonicity (exhaustive)
- roundtrip: synthetic edge cases (int16 extremes, impulses, 1/2/3-sample
  chunks), 3,000-chunk fuzz, 4 M samples of real signal, chunk-independence
  (any order/subset), decoder fuzz (2,000 bit-flip + 2,000 garbage inputs,
  crash-free, no silent wrong data), prior version enforcement
- container: truncated/corrupted .nsz5 rejected with clear errors
- full-file: `bench_file` round-trips 108 M real samples on every change

## Limitations / further work

- Compression throughput of the Python container driver is ~5 Msamples/s
  (codec core alone: ~14 Ms/s single-threaded). Chunks are independent, so
  parallelizing across cores is straightforward and not yet done.
- The R10 profile is plain delta; its gain over VBZ (~2.8%) is honest but
  smaller than on RNA (noise is whiter). Count-adaptive model rates and
  logit-domain mixing are the known remaining levers (~0.03–0.1 b/s).
- `.nsz5` is an archival format: decompress before use. Registering nsz as
  a POD5 signal codec would allow direct reading with random access.
- Blob regeneration currently covers zstd 1.4.9–1.5.7 era writers; a file
  from an unknown writer falls back to verbatim blobs (correct, just less
  compression) and prints the fallback count.
