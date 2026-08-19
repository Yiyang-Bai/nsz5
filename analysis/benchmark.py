#!/usr/bin/env python3
"""Codec benchmark: every competitor runs on the SAME int16 signal.

nsz and VBZ work per POD5-sized chunk (102400 samples), as they must in a
real file. General-purpose compressors get the whole concatenated stream
(their best case). FLAC runs at max compression (level 8).

Usage:
    python benchmark.py signal.bin [--prior prior.bin] [--max-samples N]
"""
import argparse
import io
import subprocess
import sys
import tempfile
import time
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "python"))
import libnsz
from pod5.signal_tools import vbz_compress_signal

CHUNK = 102400


def bench_nsz(x):
    t0 = time.monotonic()
    total = 0
    for off in range(0, len(x), CHUNK):
        total += len(libnsz.compress(x[off : off + CHUNK]))
    return total, time.monotonic() - t0


def bench_vbz(x):
    t0 = time.monotonic()
    total = 0
    for off in range(0, len(x), CHUNK):
        total += len(vbz_compress_signal(x[off : off + CHUNK]))
    return total, time.monotonic() - t0


def bench_flac(x, level=8):
    import pyflac

    out = io.BytesIO()

    def cb(buf, num_bytes, num_samples, frame):
        out.write(buf[:num_bytes])

    enc = pyflac.StreamEncoder(
        write_callback=cb, sample_rate=8000, compression_level=level,
        blocksize=0, verify=False,
    )
    t0 = time.monotonic()
    enc.process(x.reshape(-1, 1).astype(np.int16))
    enc.finish()
    return out.getbuffer().nbytes, time.monotonic() - t0


def bench_cli(x, cmd_fmt, tmpdir):
    src = Path(tmpdir) / "in.bin"
    x.astype("<i2").tofile(src)
    dst = Path(tmpdir) / "out.z"
    if dst.exists():
        dst.unlink()
    cmd = [c.format(src=src, dst=dst) for c in cmd_fmt]
    t0 = time.monotonic()
    subprocess.run(cmd, check=True, capture_output=True)
    dt = time.monotonic() - t0
    size = dst.stat().st_size
    dst.unlink()
    return size, dt


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("signal_bin")
    ap.add_argument("--prior", default=None)
    ap.add_argument("--max-samples", type=int, default=20_000_000)
    args = ap.parse_args()

    x = np.fromfile(args.signal_bin, dtype="<i2")[: args.max_samples]
    n = len(x)
    if args.prior:
        libnsz.load_prior(Path(args.prior).read_bytes())
    print(f"dataset: {args.signal_bin}  ({n} samples)")
    print(f"prior:   {args.prior or 'none'}\n")

    rows = []

    size, dt = bench_nsz(x)
    rows.append(("nsz (this work)", size, dt))
    size, dt = bench_vbz(x)
    rows.append(("VBZ (POD5 default)", size, dt))
    try:
        size, dt = bench_flac(x, 8)
        rows.append(("FLAC -8", size, dt))
    except Exception as e:  # pyflac missing etc.
        print(f"(FLAC skipped: {e})")

    with tempfile.TemporaryDirectory(dir="work") as td:
        for name, cmd in [
            ("zstd -19", ["zstd", "-19", "-q", "-f", "{src}", "-o", "{dst}"]),
            ("zstd -1", ["zstd", "-1", "-q", "-f", "{src}", "-o", "{dst}"]),
            ("xz -9", ["sh", "-c", "xz -9 -c {src} > {dst}"]),
            ("bzip2 -9", ["sh", "-c", "bzip2 -9 -c {src} > {dst}"]),
        ]:
            if cmd[0] == "sh":
                cmd = ["sh", "-c", cmd[2].format(src="'" + str(Path(td) / "in.bin") + "'",
                                                 dst="'" + str(Path(td) / "out.z") + "'")]
                src = Path(td) / "in.bin"
                x.astype("<i2").tofile(src)
                dst = Path(td) / "out.z"
                t0 = time.monotonic()
                subprocess.run(cmd, check=True, capture_output=True)
                dt = time.monotonic() - t0
                size = dst.stat().st_size
                dst.unlink()
            else:
                size, dt = bench_cli(x, cmd, td)
            rows.append((name, size, dt))

    rows.append(("raw int16", 2 * n, 0.0))

    vbz_bps = next(8.0 * s / n for name, s, _ in rows if name.startswith("VBZ"))
    rows.sort(key=lambda r: r[1])

    print(f"{'codec':<20} {'bits/sample':>12} {'ratio':>8} {'vs VBZ':>8} {'enc speed':>12}")
    for name, size, dt in rows:
        bps = 8.0 * size / n
        ratio = 16.0 / bps
        vs = (1 - bps / vbz_bps) * 100
        speed = f"{n / dt / 1e6:.1f} Ms/s" if dt > 0 else "—"
        print(f"{name:<20} {bps:>12.4f} {ratio:>7.3f}x {vs:>+7.2f}% {speed:>12}")


if __name__ == "__main__":
    main()
