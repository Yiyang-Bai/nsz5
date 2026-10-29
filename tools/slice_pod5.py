#!/usr/bin/env python3
"""Write the first N reads of a POD5 file to a new POD5 file.

Used to create a small but real file for the end-to-end archival
round-trip check (compress -> decompress -> SHA-256 identical)."""
import argparse

import pod5


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("pod5_in")
    ap.add_argument("pod5_out")
    ap.add_argument("--reads", type=int, default=20000)
    args = ap.parse_args()

    n = 0
    with pod5.Reader(args.pod5_in) as reader, \
            pod5.Writer(args.pod5_out) as writer:
        for read in reader.reads():
            writer.add_read(read.to_read())
            n += 1
            if n >= args.reads:
                break
    print(f"wrote {n} reads to {args.pod5_out}")


if __name__ == "__main__":
    main()
