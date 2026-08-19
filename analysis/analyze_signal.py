#!/usr/bin/env python3
"""Fit fixed order-N LPC coefficients on real nanopore signal and measure
residual statistics for candidate predictors.

Outputs:
  - Levinson-Durbin LPC coefficients (float and Q14-quantized)
  - order-0 entropy of the residual for: raw, delta, LPC (float), LPC (Q14),
    LPC (Q14 + adaptive bias corrector, exactly as implemented in C)
  - residual autocorrelation after LPC
  - a ready-to-paste C array of the Q14 coefficients

Usage:
    python analyze_signal.py tests/data/real_signal.bin [--order 8]
"""
import argparse
import sys

import numpy as np

Q = 14  # fixed-point fractional bits


def entropy_bits(residual: np.ndarray) -> float:
    """Order-0 entropy in bits/sample."""
    _, counts = np.unique(residual, return_counts=True)
    p = counts / counts.sum()
    return float(-(p * np.log2(p)).sum())


def levinson_durbin(r: np.ndarray, order: int) -> np.ndarray:
    """Solve the Toeplitz system for LPC coefficients a[0..order-1] such that
    pred[n] = sum_k a[k] * x[n-1-k]."""
    a = np.zeros(order)
    e = r[0]
    for i in range(order):
        k = (r[i + 1] - np.dot(a[:i], r[1 : i + 1][::-1])) / e
        a[:i] = a[:i] - k * a[:i][::-1]
        a[i] = k
        e *= 1.0 - k * k
    return a


def lpc_residual_float(x: np.ndarray, a: np.ndarray) -> np.ndarray:
    order = len(a)
    pred = np.zeros(len(x))
    for k in range(order):
        pred[order:] += a[k] * x[order - 1 - k : len(x) - 1 - k]
    r = x[order:] - np.round(pred[order:])
    return r.astype(np.int64)


def lpc_residual_q14(x: np.ndarray, aq: np.ndarray, bias: bool) -> np.ndarray:
    """Bit-exact simulation of the C predictor (Q14, optional EWMA bias)."""
    order = len(aq)
    n = len(x)
    xi = x.astype(np.int64)
    # vectorized Q14 prediction (history = original samples; lossless codec
    # reconstructs them exactly, so this matches the C decoder)
    acc = np.zeros(n, dtype=np.int64)
    for k in range(order):
        acc[order:] += aq[k] * xi[order - 1 - k : n - 1 - k]
    pred = acc >> Q  # floor shift, matches C asr()

    if not bias:
        r = xi[order:] - pred[order:]
        return r

    # bias corrector must run sequentially
    res = np.empty(n - order, dtype=np.int64)
    B = 0  # Q8 EWMA of residual
    for i in range(order, n):
        p = pred[i] + (B >> 8)
        r = xi[i] - p
        res[i - order] = r
        B += ((r << 8) - B) >> 6
    return res


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("signal_bin")
    ap.add_argument("--order", type=int, default=8)
    ap.add_argument("--max-samples", type=int, default=4_000_000)
    args = ap.parse_args()

    x = np.fromfile(args.signal_bin, dtype="<i2")[: args.max_samples].astype(
        np.float64
    )
    n = len(x)
    print(f"samples: {n}")
    print(f"raw entropy:              {entropy_bits(x.astype(np.int64)):8.4f} bits/sample")

    delta = np.diff(x).astype(np.int64)
    print(f"delta residual entropy:   {entropy_bits(delta):8.4f} bits/sample")

    # autocorrelation of the zero-mean signal
    xc = x - x.mean()
    r = np.array(
        [np.dot(xc[: n - k], xc[k:]) / (n - k) for k in range(args.order + 1)]
    )
    a = levinson_durbin(r, args.order)
    print(f"\nLPC-{args.order} coefficients (float): {np.round(a, 6).tolist()}")
    print(f"coefficient sum: {a.sum():.6f}")

    aq = np.round(a * (1 << Q)).astype(np.int64)
    print(f"Q14 coefficients: {aq.tolist()}")
    print(f"Q14 sum: {aq.sum()} / {1 << Q} = {aq.sum() / (1 << Q):.6f}")

    rf = lpc_residual_float(x, a)
    print(f"\nLPC float residual entropy:      {entropy_bits(rf):8.4f} bits/sample")

    rq = lpc_residual_q14(x, aq, bias=False)
    print(f"LPC Q14 residual entropy:        {entropy_bits(rq):8.4f} bits/sample")
    print(f"LPC Q14 residual mean (bias):    {rq.mean():8.4f}")

    rqb = lpc_residual_q14(x, aq, bias=True)
    print(f"LPC Q14+bias residual entropy:   {entropy_bits(rqb):8.4f} bits/sample")
    print(f"LPC Q14+bias residual mean:      {rqb.mean():8.4f}")
    print(f"residual std: {rqb.std():.2f}, kurtosis: "
          f"{((rqb - rqb.mean()) ** 4).mean() / rqb.var() ** 2:.2f}")

    # residual autocorrelation (should be ~0 at all lags)
    rr = rqb - rqb.mean()
    denom = np.dot(rr, rr)
    ac = [float(np.dot(rr[: len(rr) - k], rr[k:]) / denom) for k in range(1, 9)]
    print(f"residual autocorr lags 1-8: {[round(v, 4) for v in ac]}")

    print("\n/* paste into predictor.c */")
    print(f"static const int32_t NSZ_LPC8_Q14[{args.order}] = "
          f"{{{', '.join(str(v) for v in aq)}}};")
    return 0


if __name__ == "__main__":
    sys.exit(main())
