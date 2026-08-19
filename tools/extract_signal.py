#!/usr/bin/env python3
"""Dump raw int16 signal from a POD5 file to a flat binary file (little-endian).

Used to feed real nanopore signal into the C unit tests.

Usage:
    python extract_signal.py input.pod5 output.bin [--max-samples N]
"""
import argparse
import sys

import numpy as np
import pod5


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("pod5_in")
    ap.add_argument("bin_out")
    ap.add_argument("--max-samples", type=int, default=4_000_000)
    args = ap.parse_args()

    chunks = []
    total = 0
    with pod5.Reader(args.pod5_in) as reader:
        for read in reader.reads():
            sig = read.signal.astype(np.int16)
            chunks.append(sig)
            total += len(sig)
            if total >= args.max_samples:
                break

    signal = np.concatenate(chunks)[: args.max_samples]
    signal.astype("<i2").tofile(args.bin_out)
    print(f"wrote {len(signal)} samples ({signal.nbytes} bytes) to {args.bin_out}")
    print(f"min={signal.min()} max={signal.max()} mean={signal.mean():.1f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
