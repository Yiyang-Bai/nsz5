#!/usr/bin/env python3
"""nsz5 — byte-exact archival compression for POD5 files.

The POD5 file is treated as a byte stream. Signal blobs (VBZ-compressed
Arrow buffers) are located, decoded to int16, and re-compressed with the
nsz codec; every other byte is preserved verbatim in a zstd-compressed
"skeleton". On decompression each VBZ blob is regenerated with the exact
inner recipe (delta -> zigzag -> svb16 -> zstd-1 via lib_pod5) and spliced
back at its original offset.

Byte-exactness is verified PER BLOB at compression time: any blob that
does not regenerate bit-for-bit is stored verbatim instead. The output is
therefore always exactly restorable; SHA-256 of the original is stored in
the container and checked on decompression.

Commands:
    nsz5.py compress   input.pod5  output.nsz5  [--prior prior.bin]
    nsz5.py decompress input.nsz5  rebuilt.pod5
    nsz5.py verify     a.pod5      b.pod5
    nsz5.py bench      input.pod5  [--prior prior.bin]
"""
import argparse
import hashlib
import multiprocessing as mp
import os
import re
import struct
import sys
import time
from pathlib import Path

import numpy as np
import pyarrow as pa
import pyarrow.ipc as ipc
import zstandard
from pod5.signal_tools import vbz_compress_signal, vbz_decompress_signal

sys.path.insert(0, str(Path(__file__).resolve().parent))
import libnsz
import zstd_backends

NSZ5_MAGIC = b"NSZ5POD2"
MODE_VERBATIM = 0
MODE_NSZ = 1        # regenerate blob via lib_pod5's bundled zstd
MODE_NSZ_ALT = 2    # MODE_NSZ_ALT + i: regenerate via alt zstd backend i


# --------------------------------------------------------------------- #
# POD5 structure                                                        #
# --------------------------------------------------------------------- #

def find_embedded_arrow_files(raw: bytes):
    """Locate embedded Arrow file regions [start, end) inside a POD5 file.

    Each embedded Arrow file contributes exactly two "ARROW1" magics (one
    at its start, one at its end), so consecutive magics pair up as
    (start, end). Every region is later validated by actually opening it
    with pyarrow; a false magic inside blob data would make that fail
    loudly rather than corrupt anything.
    """
    magics = [m.start() for m in re.finditer(b"ARROW1", raw)]
    if len(magics) % 2 != 0:
        raise ValueError(f"odd number of ARROW1 magics ({len(magics)})")
    return [(magics[i], magics[i + 1] + 6) for i in range(0, len(magics), 2)]


def find_signal_blobs(raw: bytes):
    """Yield (file_offset, length, sample_count) for every signal blob."""
    blobs = []
    for start, end in find_embedded_arrow_files(raw):
        region = pa.py_buffer(memoryview(raw)[start:end])
        reader = ipc.open_file(pa.BufferReader(region))
        names = reader.schema.names
        if "signal" not in names or "samples" not in names:
            continue
        sig_idx = reader.schema.get_field_index("signal")
        cnt_idx = reader.schema.get_field_index("samples")
        if reader.schema.field(sig_idx).type != pa.large_binary():
            continue
        for bi in range(reader.num_record_batches):
            batch = reader.get_batch(bi)
            col = batch.column(sig_idx)
            counts = batch.column(cnt_idx).to_numpy(zero_copy_only=False)
            bufs = col.buffers()
            offsets = np.frombuffer(bufs[1], dtype=np.int64, count=len(col) + 1)
            data_abs = bufs[2].address - region.address + start
            for i in range(len(col)):
                off = int(data_abs + offsets[i])
                length = int(offsets[i + 1] - offsets[i])
                blobs.append((off, length, int(counts[i])))
    blobs.sort()
    for i in range(1, len(blobs)):
        if blobs[i][0] < blobs[i - 1][0] + blobs[i - 1][1]:
            raise ValueError("overlapping signal blobs")
    return blobs


# --------------------------------------------------------------------- #
# Per-blob compression (shared by the serial and parallel paths)        #
# --------------------------------------------------------------------- #

def _find_regen_mode(blob, sig, count, preferred, alt_versions, dctx):
    """Return the blob-regeneration mode that reproduces `blob`
    byte-exactly, or None. Tries the last successful mode first."""
    order = ([preferred] if preferred is not None else []) + \
        [m for m in range(MODE_NSZ, MODE_NSZ_ALT + len(alt_versions))
         if m != preferred]
    inner = None
    for mode in order:
        if mode == MODE_NSZ:
            if bytes(vbz_compress_signal(sig)) == blob:
                return MODE_NSZ
        else:
            if inner is None:
                try:
                    inner = dctx.decompress(
                        blob, max_output_size=count * 4 + 1024
                    )
                except zstandard.ZstdError:
                    return None
            ver = alt_versions[mode - MODE_NSZ_ALT]
            if zstd_backends.compress_level1(ver, inner) == blob:
                return mode
    return None


def _compress_one(blob, count, length, preferred, alt_versions, dctx):
    """Compress a single blob. Returns (mode, payload, new_preferred)."""
    if count <= 0 or length <= 0:
        return MODE_VERBATIM, blob, preferred
    sig = vbz_decompress_signal(np.frombuffer(blob, dtype=np.uint8), count)
    regen_mode = _find_regen_mode(blob, sig, count, preferred,
                                  alt_versions, dctx)
    if regen_mode is None:
        return MODE_VERBATIM, blob, preferred
    nsz_blob = libnsz.compress(sig)
    # paranoia: prove we can decode our own blob right now
    if np.array_equal(libnsz.decompress(nsz_blob, count), sig) \
            and len(nsz_blob) < length:
        return regen_mode, nsz_blob, regen_mode
    return MODE_VERBATIM, blob, regen_mode


# worker-process state for the parallel compress path (a process pool
# parallelizes the WHOLE per-blob pipeline: vbz decode, regeneration
# check and nsz encode; measured faster than C-level-only threading)
_wstate = {}


def _worker_init(prior_bytes, alt_versions):
    if prior_bytes:
        libnsz.load_prior(prior_bytes)
    _wstate["alt"] = alt_versions
    _wstate["dctx"] = zstandard.ZstdDecompressor()
    _wstate["preferred"] = None


def _worker_compress(args):
    idx, blob, count = args
    mode, payload, pref = _compress_one(
        blob, count, len(blob), _wstate["preferred"], _wstate["alt"],
        _wstate["dctx"]
    )
    _wstate["preferred"] = pref
    return idx, mode, payload


# --------------------------------------------------------------------- #
# Container I/O                                                         #
# --------------------------------------------------------------------- #

_MODELS_DIR = Path(__file__).resolve().parent.parent / "models"


def auto_prior(pod5_path):
    """Pick the shipped prior matching the file's chemistry: RNA kits use
    the RNA-fitted profile, everything else the R10-DNA (delta) profile."""
    import pod5

    with pod5.Reader(pod5_path) as reader:
        ri = next(reader.reads()).run_info
    kit = (ri.sequencing_kit or "").lower()
    if "rna" in kit:
        choice = _MODELS_DIR / "prior.bin"
    else:
        choice = _MODELS_DIR / "prior_r10.bin"
    print(f"auto prior: kit={kit!r} sample_rate={ri.sample_rate} "
          f"-> {choice.name}")
    return str(choice)


def compress_file(pod5_path, nsz5_path, prior_path="auto", threads=1):
    t0 = time.monotonic()
    raw = Path(pod5_path).read_bytes()
    sha = hashlib.sha256(raw).digest()

    if prior_path == "auto":
        prior_path = auto_prior(pod5_path)
    prior_bytes = b""
    if prior_path and prior_path != "none":
        prior_bytes = Path(prior_path).read_bytes()
        libnsz.load_prior(prior_bytes)

    blobs = find_signal_blobs(raw)
    if threads < 1:
        threads = min(32, os.cpu_count() or 1)
    print(f"{len(raw)} bytes, {len(blobs)} signal blobs, "
          f"{threads} worker(s)")

    alt_versions = zstd_backends.available_versions()

    total_samples = sum(c for _, _, c in blobs)
    results = [None] * len(blobs)

    if threads > 1:
        items = ((k, raw[off : off + length], count)
                 for k, (off, length, count) in enumerate(blobs))
        done = 0
        with mp.Pool(threads, initializer=_worker_init,
                     initargs=(prior_bytes, alt_versions)) as pool:
            for idx, mode, payload in pool.imap_unordered(
                    _worker_compress, items, chunksize=32):
                results[idx] = (mode, payload)
                done += 1
                if done % 50000 == 0:
                    print(f"  {done}/{len(blobs)} blobs...")
    else:
        dctx = zstandard.ZstdDecompressor()
        preferred = None
        for k, (off, length, count) in enumerate(blobs):
            blob = raw[off : off + length]
            mode, payload, preferred = _compress_one(
                blob, count, length, preferred, alt_versions, dctx)
            results[k] = (mode, payload)
            if (k + 1) % 50000 == 0:
                print(f"  {k + 1}/{len(blobs)} blobs...")

    n_verbatim = sum(1 for m, _ in results if m == MODE_VERBATIM)
    records = [
        (off, length, count, results[k][0], results[k][1])
        for k, (off, length, count) in enumerate(blobs)
    ]

    # skeleton: the file with all blob ranges excised
    parts = []
    pos = 0
    for off, length, _, _, _ in records:
        parts.append(raw[pos:off])
        pos = off + length
    parts.append(raw[pos:])
    skeleton = b"".join(parts)
    skeleton_z = zstandard.ZstdCompressor(level=19).compress(skeleton)

    with open(nsz5_path, "wb") as f:
        f.write(NSZ5_MAGIC)
        f.write(struct.pack("<QI", len(raw), len(prior_bytes)))
        f.write(sha)
        f.write(prior_bytes)
        # alt zstd backend table: decompression needs exactly these versions
        f.write(struct.pack("<B", len(alt_versions)))
        for v in alt_versions:
            vb = v.encode("ascii")
            f.write(struct.pack("<B", len(vb)))
            f.write(vb)
        f.write(struct.pack("<QQ", len(skeleton), len(skeleton_z)))
        f.write(skeleton_z)
        f.write(struct.pack("<I", len(records)))
        for off, length, count, mode, payload in records:
            f.write(struct.pack("<QIIBI", off, length, count, mode,
                                len(payload)))
            f.write(payload)

    out_size = Path(nsz5_path).stat().st_size
    dt = time.monotonic() - t0
    print(f"compressed: {len(raw)} -> {out_size} bytes "
          f"({100 * (1 - out_size / len(raw)):.2f}% smaller)")
    print(f"signal: {total_samples} samples, "
          f"verbatim fallbacks: {n_verbatim}/{len(blobs)}")
    print(f"time: {dt:.1f} s ({total_samples / dt / 1e6:.2f} Msamples/s)")
    return 0


def _parse_nsz5_header(mm):
    """Parse NSZ5 header; return metadata and the mmap offset where
    per-blob records begin."""

    def take(fmt_or_len, pos):
        if isinstance(fmt_or_len, int):
            end = pos + fmt_or_len
            if end > len(mm):
                raise ValueError("truncated NSZ5 container")
            return mm[pos:end], end
        size = struct.calcsize(fmt_or_len)
        if pos + size > len(mm):
            raise ValueError("truncated NSZ5 container")
        return struct.unpack_from(fmt_or_len, mm, pos), pos + size

    if mm[:8] != NSZ5_MAGIC:
        raise ValueError("not an NSZ5 file")
    pos = 8
    (orig_size, prior_len), pos = take("<QI", pos)
    sha_stored, pos = take(32, pos)
    prior_bytes, pos = take(prior_len, pos)
    (n_alt,), pos = take("<B", pos)
    alt_versions = []
    for _ in range(n_alt):
        (vlen,), pos = take("<B", pos)
        vb, pos = take(vlen, pos)
        alt_versions.append(vb.decode("ascii"))
    (skel_raw_len, skel_z_len), pos = take("<QQ", pos)
    if skel_raw_len > orig_size:
        raise ValueError("skeleton larger than original file")
    skel_z, pos = take(skel_z_len, pos)
    skeleton = zstandard.ZstdDecompressor().decompress(
        skel_z, max_output_size=skel_raw_len
    )
    (blob_count,), pos = take("<I", pos)
    return {
        "container_size": len(mm),
        "orig_size": orig_size,
        "sha_stored": sha_stored,
        "prior_bytes": prior_bytes,
        "alt_versions": alt_versions,
        "skeleton": skeleton,
        "blob_count": blob_count,
        "records_pos": pos,
    }


_REC_HDR = struct.Struct("<QIIBI")


def _read_record_group(mm, pos, n, alt_versions, orig_size, prev_end):
    """Read up to *n* blob records starting at *pos* in the mmap."""
    records = []
    end_pos = pos
    hdr_len = _REC_HDR.size
    for _ in range(n):
        if end_pos + hdr_len > len(mm):
            raise ValueError("truncated NSZ5 container")
        off, length, count, mode, payload_len = _REC_HDR.unpack_from(mm, end_pos)
        end_pos += hdr_len
        payload_off = end_pos
        end_pos += payload_len
        if off < prev_end or off + length > orig_size:
            raise ValueError("blob offset out of bounds")
        if mode >= MODE_NSZ_ALT + len(alt_versions):
            raise ValueError("unknown blob mode")
        prev_end = off + length
        records.append((off, length, count, mode, payload_off, payload_len))
    return records, end_pos, prev_end


def decompress_file(nsz5_path, pod5_path, threads=1):
    """Stream-decompress an NSZ5 archive without holding the full POD5 or
    all decoded signals in memory."""
    t0 = time.monotonic()
    if threads < 1:
        threads = min(32, os.cpu_count() or 1)

    with open(nsz5_path, "rb") as f:
        with mmap_mod(f) as mm:
            meta = _parse_nsz5_header(mm)
            if meta["prior_bytes"]:
                libnsz.load_prior(meta["prior_bytes"])
            else:
                libnsz.clear_prior()

            skeleton = meta["skeleton"]
            alt_versions = meta["alt_versions"]
            orig_size = meta["orig_size"]
            container_size = meta["container_size"]
            blob_count = meta["blob_count"]

            hasher = hashlib.sha256()
            skel_pos = 0
            out_pos = 0
            total_samples = 0
            GROUP = 2048
            dctx = zstandard.ZstdDecompressor()
            rec_pos = meta["records_pos"]
            prev_end = 0
            done = 0

            with open(pod5_path, "wb") as outf:
                while done < blob_count:
                    grp, rec_pos, prev_end = _read_record_group(
                        mm, rec_pos, min(GROUP, blob_count - done),
                        alt_versions, orig_size, prev_end,
                    )
                    nsz_in_grp = [
                        (j, r) for j, r in enumerate(grp) if r[3] != MODE_VERBATIM
                    ]
                    sigs = {}
                    if nsz_in_grp:
                        if threads > 1 and len(nsz_in_grp) > 1:
                            payloads = [
                                mm[r[4] : r[4] + r[5]] for _, r in nsz_in_grp
                            ]
                            counts = [r[2] for _, r in nsz_in_grp]
                            concat = libnsz.decompress_batch(
                                payloads, counts, threads
                            )
                            cum = 0
                            for (j, r), c in zip(nsz_in_grp, counts):
                                sigs[j] = concat[cum : cum + c]
                                cum += c
                        else:
                            for j, r in nsz_in_grp:
                                payload = mm[r[4] : r[4] + r[5]]
                                sigs[j] = libnsz.decompress(payload, r[2])

                    for j, record in enumerate(grp):
                        off, length, count, mode, payload_off, payload_len = record
                        gap = off - out_pos
                        if gap:
                            chunk = skeleton[skel_pos : skel_pos + gap]
                            outf.write(chunk)
                            hasher.update(chunk)
                            skel_pos += gap
                            out_pos += gap

                        if mode == MODE_VERBATIM:
                            blob = mm[payload_off : payload_off + payload_len]
                        else:
                            sig = sigs[j]
                            blob = bytes(vbz_compress_signal(sig))
                            if mode >= MODE_NSZ_ALT:
                                inner = dctx.decompress(
                                    blob, max_output_size=count * 4 + 1024
                                )
                                blob = zstd_backends.compress_level1(
                                    alt_versions[mode - MODE_NSZ_ALT], inner
                                )
                            if len(blob) != length:
                                raise ValueError(
                                    "regenerated blob length mismatch"
                                )
                        outf.write(blob)
                        hasher.update(blob)
                        out_pos += length
                        total_samples += count

                    done += len(grp)
                    if done % 50000 == 0 or done == blob_count:
                        print(f"  {done}/{blob_count} blobs...", flush=True)

                if rec_pos != len(mm):
                    raise ValueError("trailing bytes in NSZ5 container")

                tail = len(skeleton) - skel_pos
                if tail:
                    chunk = skeleton[skel_pos:]
                    outf.write(chunk)
                    hasher.update(chunk)
                    out_pos += tail

            if out_pos != orig_size:
                raise ValueError("reconstructed size mismatch")
            if hasher.digest() != meta["sha_stored"]:
                Path(pod5_path).unlink(missing_ok=True)
                raise ValueError(
                    "SHA-256 mismatch: reconstruction is NOT identical"
                )

    dt = time.monotonic() - t0
    print(f"decompressed: {container_size} -> {orig_size} bytes")
    print("SHA-256 verified: reconstruction is byte-identical")
    print(f"time: {dt:.1f} s ({total_samples / dt / 1e6:.2f} Msamples/s)")
    return 0


def mmap_mod(f):
    import mmap
    return mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)


def verify_files(a_path, b_path):
    def stream_hash(path, chunk=1 << 20):
        h = hashlib.sha256()
        with open(path, "rb") as f:
            while True:
                block = f.read(chunk)
                if not block:
                    break
                h.update(block)
        return h.hexdigest()

    ha = stream_hash(a_path)
    hb = stream_hash(b_path)
    print(f"{ha}  {a_path}")
    print(f"{hb}  {b_path}")
    if ha == hb:
        print("IDENTICAL")
        return 0
    print("DIFFERENT")
    return 1


def bench_file(pod5_path, prior_path=None):
    raw = Path(pod5_path).read_bytes()
    if prior_path:
        libnsz.load_prior(Path(prior_path).read_bytes())
    blobs = find_signal_blobs(raw)
    total_samples = 0
    vbz_bytes = 0
    nsz_bytes = 0
    t_enc = 0.0
    for off, length, count in blobs:
        if count == 0 or length == 0:
            continue
        sig = vbz_decompress_signal(
            np.frombuffer(raw[off : off + length], dtype=np.uint8), count
        )
        t0 = time.monotonic()
        nsz_blob = libnsz.compress(sig)
        t_enc += time.monotonic() - t0
        total_samples += count
        vbz_bytes += length
        nsz_bytes += len(nsz_blob)
    print(f"samples:      {total_samples}")
    print(f"raw int16:    16.0000 bits/sample")
    print(f"VBZ payload:  {8 * vbz_bytes / total_samples:.4f} bits/sample")
    print(f"nsz payload:  {8 * nsz_bytes / total_samples:.4f} bits/sample "
          f"({100 * (1 - nsz_bytes / vbz_bytes):.2f}% below VBZ)")
    print(f"encode:       {total_samples / t_enc / 1e6:.2f} Msamples/s")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    sub = ap.add_subparsers(dest="cmd", required=True)

    c = sub.add_parser("compress")
    c.add_argument("input")
    c.add_argument("output")
    c.add_argument("--prior", default="auto",
                   help="prior file, 'auto' (default: pick by chemistry) "
                        "or 'none'")
    c.add_argument("--threads", type=int, default=1,
                   help="worker processes (0 = auto, default 1)")

    d = sub.add_parser("decompress")
    d.add_argument("input")
    d.add_argument("output")
    d.add_argument("--threads", type=int, default=1,
                   help="decode threads (0 = auto, default 1)")

    v = sub.add_parser("verify")
    v.add_argument("a")
    v.add_argument("b")

    b = sub.add_parser("bench")
    b.add_argument("input")
    b.add_argument("--prior", default=None)

    args = ap.parse_args()
    if args.cmd == "compress":
        return compress_file(args.input, args.output, args.prior,
                             threads=args.threads)
    if args.cmd == "decompress":
        threads = args.threads if args.threads >= 1 else \
            min(32, os.cpu_count() or 1)
        return decompress_file(args.input, args.output, threads=threads)
    if args.cmd == "verify":
        return verify_files(args.a, args.b)
    if args.cmd == "bench":
        return bench_file(args.input, args.prior)
    return 2


if __name__ == "__main__":
    sys.exit(main())
